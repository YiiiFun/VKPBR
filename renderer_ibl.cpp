// IBL (image-based lighting): HDR equirect loading + GPU precomputation of
// environment / irradiance / prefiltered-specular cubemaps + BRDF integration LUT.
//
// Design notes (lessons from a previous failed attempt):
//  * Each compute dispatch gets its OWN descriptor set. Sharing one set across
//    multiple dispatches recorded in the same command buffer means the GPU only
//    ever observes the LAST host-side vkUpdateDescriptorSets write.
//  * Per-dispatch parameters travel in PUSH CONSTANTS (recorded into the command
//    buffer), never in a shared host-coherent UBO (same last-write-wins race).
//  * Shaders decide "IBL vs fallback ambient" via a dedicated int `iblEnabled`
//    UBO field — never by thresholding the float intensity.

#include "renderer.h"
#include "platform.h"

#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>

#include "external/stb/stb_image.h"

namespace {

// Record a full-image (all mips / all layers) layout transition using sync2 barriers.
void recordIBLImageBarrier(vk::raii::CommandBuffer& cmd,
                           vk::Image image,
                           vk::ImageLayout oldLayout,
                           vk::ImageLayout newLayout,
                           vk::PipelineStageFlags2 srcStage,
                           vk::AccessFlags2 srcAccess,
                           vk::PipelineStageFlags2 dstStage,
                           vk::AccessFlags2 dstAccess,
                           uint32_t mipLevels,
                           uint32_t layerCount) {
  vk::ImageMemoryBarrier2 barrier{
    .srcStageMask = srcStage,
    .srcAccessMask = srcAccess,
    .dstStageMask = dstStage,
    .dstAccessMask = dstAccess,
    .oldLayout = oldLayout,
    .newLayout = newLayout,
    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    .image = image,
    .subresourceRange = {
      .aspectMask = vk::ImageAspectFlagBits::eColor,
      .baseMipLevel = 0,
      .levelCount = mipLevels,
      .baseArrayLayer = 0,
      .layerCount = layerCount
    }
  };
  vk::DependencyInfo depInfo{
    .imageMemoryBarrierCount = 1,
    .pImageMemoryBarriers = &barrier
  };
  cmd.pipelineBarrier2(depInfo);
}

} // namespace

// Record + submit + wait for a small one-time command buffer on the graphics queue.
// Used for the fallback-cube transition (needs layerCount=6, which the shared
// transitionImageLayout helper does not support).
void Renderer::submitIBLOneTimeCommands(const std::function<void(vk::raii::CommandBuffer&)>& recorder) {
  vk::CommandPoolCreateInfo poolInfo{
    .flags = vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
    .queueFamilyIndex = queueFamilyIndices.graphicsFamily.value()
  };
  vk::raii::CommandPool tempPool(device, poolInfo);
  vk::CommandBufferAllocateInfo allocInfo{.commandPool = *tempPool,
                                          .level = vk::CommandBufferLevel::ePrimary,
                                          .commandBufferCount = 1};
  vk::raii::CommandBuffers cmds(device, allocInfo);
  vk::raii::CommandBuffer& cmd = cmds[0];
  cmd.begin(vk::CommandBufferBeginInfo{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
  recorder(cmd);
  cmd.end();
  vk::raii::Fence fence(device, vk::FenceCreateInfo{}); {
    std::lock_guard<std::mutex> lock(queueMutex);
    vk::SubmitInfo submitInfo{};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &(*cmd);
    graphicsQueue.submit(submitInfo, *fence);
  }
  (void) waitForFencesSafe(*fence, VK_TRUE, 30'000'000'000ULL);
}

// Create the 4 IBL compute pipelines sharing one layout:
//   binding 0 = combined image sampler (input), binding 1 = storage image (output),
//   16-byte push constant range (per-dispatch params).
bool Renderer::createIBLComputePipelines() {
  try {
    if (*iblComputePipelineLayout)
      return true;

    std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = vk::ShaderStageFlagBits::eCompute;
    bindings[1].binding = 1;
    bindings[1].descriptorType = vk::DescriptorType::eStorageImage;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = vk::ShaderStageFlagBits::eCompute;

    vk::DescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    iblComputeDescriptorSetLayout = vk::raii::DescriptorSetLayout(device, layoutInfo);

    vk::PushConstantRange pcRange{vk::ShaderStageFlagBits::eCompute, 0, sizeof(float) * 4};
    vk::PipelineLayoutCreateInfo plInfo{};
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &(*iblComputeDescriptorSetLayout);
    plInfo.pushConstantRangeCount = 1;
    plInfo.pPushConstantRanges = &pcRange;
    iblComputePipelineLayout = vk::raii::PipelineLayout(device, plInfo);

    auto makePipeline = [&](const char* path, vk::raii::Pipeline& out, const char* label) -> bool {
      auto code = readFile(path);
      if (code.empty()) {
        std::cerr << "IBL: failed to read " << path << std::endl;
        return false;
      }
      vk::ShaderModuleCreateInfo modInfo{};
      modInfo.codeSize = code.size();
      modInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
      vk::raii::ShaderModule mod(device, modInfo);
      vk::PipelineShaderStageCreateInfo stage{};
      stage.stage = vk::ShaderStageFlagBits::eCompute;
      stage.module = *mod;
      stage.pName = "main";
      vk::ComputePipelineCreateInfo ci{};
      ci.stage = stage;
      ci.layout = *iblComputePipelineLayout;
      try {
        out = vk::raii::Pipeline(device, nullptr, ci);
        return true;
      } catch (const std::exception& e) {
        std::cerr << "IBL: failed to create " << label << " pipeline: " << e.what() << std::endl;
        return false;
      }
    };

    if (!makePipeline("shaders/ibl_equirect_to_cubemap.spv", iblEquirectToCubemapPipeline, "equirect-to-cubemap"))
      return false;
    if (!makePipeline("shaders/ibl_irradiance.spv", iblIrradiancePipeline, "irradiance"))
      return false;
    if (!makePipeline("shaders/ibl_prefilter.spv", iblPrefilterPipeline, "prefilter"))
      return false;
    if (!makePipeline("shaders/ibl_brdf_lut.spv", iblBrdfLutPipeline, "brdf-lut"))
      return false;
    return true;
  } catch (const std::exception& e) {
    std::cerr << "IBL: createIBLComputePipelines failed: " << e.what() << std::endl;
    return false;
  }
}

// Load the HDR equirect map and generate all IBL textures with 8 compute dispatches
// (1 env cube + 1 irradiance + 5 prefilter mips + 1 BRDF LUT), each with its own
// descriptor set and push-constant parameters. Synchronous: submits and waits once.
bool Renderer::loadAndGenerateIBL() {
  LOGI("IBL: initializing (HDR: %s)", iblHdrPath.c_str());
  try {
    // ---------- 0. Shared sampler + 1x1 fallback cube (needed even on failure) ----------
    // The sampler wraps in U for the equirect seam and supports mipmaps for the
    // prefiltered specular cubemap.
    if (!*iblSampler) {
      vk::SamplerCreateInfo samplerInfo{};
      samplerInfo.magFilter = vk::Filter::eLinear;
      samplerInfo.minFilter = vk::Filter::eLinear;
      samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
      samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
      samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
      samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
      samplerInfo.mipLodBias = 0.0f;
      samplerInfo.minLod = 0.0f;
      samplerInfo.maxLod = static_cast<float>(IBL_PREFILTER_MIPS - 1);
      samplerInfo.anisotropyEnable = vk::False;
      iblSampler = vk::raii::Sampler(device, samplerInfo);
    }
    if (!*iblFallbackCubeImage) {
      std::tie(iblFallbackCubeImage, iblFallbackCubeImageAllocation) = createImagePooled(
        1, 1, vk::Format::eR8G8B8A8Unorm, vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal,
        1, vk::SharingMode::eExclusive, {}, 6, vk::ImageCreateFlagBits::eCubeCompatible);
      iblFallbackCubeView = createImageView(iblFallbackCubeImage, vk::Format::eR8G8B8A8Unorm,
                                            vk::ImageAspectFlagBits::eColor, 1, vk::ImageViewType::eCube, 0, 6);
      // Transition ALL 6 layers (transitionImageLayout only handles layerCount=1).
      submitIBLOneTimeCommands([&](vk::raii::CommandBuffer& cmd) {
        recordIBLImageBarrier(cmd, *iblFallbackCubeImage, vk::ImageLayout::eUndefined,
                              vk::ImageLayout::eShaderReadOnlyOptimal,
                              vk::PipelineStageFlagBits2::eTopOfPipe, vk::AccessFlagBits2::eNone,
                              vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eComputeShader,
                              vk::AccessFlagBits2::eShaderRead, 1, 6);
      });
    }

    // ---------- 1. Load HDR equirectangular image (Radiance RGBE) ----------
    // Do NOT flip vertically: the equirect projection already matches Vulkan UV space.
    stbi_set_flip_vertically_on_load_thread(0);
    int eqW = 0, eqH = 0, eqChannels = 0;
    float* eqPixels = stbi_loadf(iblHdrPath.c_str(), &eqW, &eqH, &eqChannels, STBI_rgb_alpha);
    if (!eqPixels || eqW <= 0 || eqH <= 0) {
      std::cerr << "IBL: failed to load HDR file '" << iblHdrPath << "': "
                << (stbi_failure_reason() ? stbi_failure_reason() : "unknown error") << std::endl;
      if (eqPixels)
        stbi_image_free(eqPixels);
      return false;
    }
    LOGI("IBL: loaded %dx%d HDR equirect (%d channels)", eqW, eqH, eqChannels);
    const vk::DeviceSize eqSize = static_cast<vk::DeviceSize>(eqW) * static_cast<vk::DeviceSize>(eqH) * 4 * sizeof(float);

    // ---------- 2. Pick storage-capable formats ----------
    const auto supportsStorage = [&](vk::Format f) {
      return !!(physicalDevice.getFormatProperties(f).optimalTilingFeatures & vk::FormatFeatureFlagBits::eStorageImage);
    };
    iblCubeFormat = supportsStorage(vk::Format::eR16G16B16A16Sfloat)
      ? vk::Format::eR16G16B16A16Sfloat : vk::Format::eR32G32B32A32Sfloat;
    const vk::Format lutFormat = supportsStorage(vk::Format::eR16G16Sfloat)
      ? vk::Format::eR16G16Sfloat : vk::Format::eR32G32Sfloat;

    // ---------- 3. GPU images ----------
    // Transient equirect 2D source (destroyed at the end of this function).
    auto [eqImage, eqAlloc] = createImagePooled(
      static_cast<uint32_t>(eqW), static_cast<uint32_t>(eqH), vk::Format::eR32G32B32A32Sfloat,
      vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
      vk::MemoryPropertyFlagBits::eDeviceLocal);
    auto eqView = createImageView(eqImage, vk::Format::eR32G32B32A32Sfloat, vk::ImageAspectFlagBits::eColor);

    const auto cubeUsage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled;
    std::tie(iblEnvCubeImage, iblEnvCubeImageAllocation) = createImagePooled(
      IBL_ENV_CUBE_SIZE, IBL_ENV_CUBE_SIZE, iblCubeFormat, vk::ImageTiling::eOptimal, cubeUsage,
      vk::MemoryPropertyFlagBits::eDeviceLocal, 1, vk::SharingMode::eExclusive, {}, 6,
      vk::ImageCreateFlagBits::eCubeCompatible);
    std::tie(iblIrradianceImage, iblIrradianceImageAllocation) = createImagePooled(
      IBL_IRRADIANCE_SIZE, IBL_IRRADIANCE_SIZE, iblCubeFormat, vk::ImageTiling::eOptimal, cubeUsage,
      vk::MemoryPropertyFlagBits::eDeviceLocal, 1, vk::SharingMode::eExclusive, {}, 6,
      vk::ImageCreateFlagBits::eCubeCompatible);
    std::tie(iblPrefilterImage, iblPrefilterImageAllocation) = createImagePooled(
      IBL_PREFILTER_SIZE, IBL_PREFILTER_SIZE, iblCubeFormat, vk::ImageTiling::eOptimal, cubeUsage,
      vk::MemoryPropertyFlagBits::eDeviceLocal, IBL_PREFILTER_MIPS, vk::SharingMode::eExclusive, {}, 6,
      vk::ImageCreateFlagBits::eCubeCompatible);
    std::tie(iblBrdfLutImage, iblBrdfLutImageAllocation) = createImagePooled(
      IBL_LUT_SIZE, IBL_LUT_SIZE, lutFormat, vk::ImageTiling::eOptimal, cubeUsage,
      vk::MemoryPropertyFlagBits::eDeviceLocal);

    // ---------- 4. Views ----------
    // Storage views are transient (only needed during generation); cube/2D views persist.
    auto envStorageView = createImageView(iblEnvCubeImage, iblCubeFormat, vk::ImageAspectFlagBits::eColor,
                                          1, vk::ImageViewType::e2DArray, 0, 6);
    iblEnvCubeView = createImageView(iblEnvCubeImage, iblCubeFormat, vk::ImageAspectFlagBits::eColor,
                                     1, vk::ImageViewType::eCube, 0, 6);
    auto irrStorageView = createImageView(iblIrradianceImage, iblCubeFormat, vk::ImageAspectFlagBits::eColor,
                                          1, vk::ImageViewType::e2DArray, 0, 6);
    iblIrradianceCubeView = createImageView(iblIrradianceImage, iblCubeFormat, vk::ImageAspectFlagBits::eColor,
                                            1, vk::ImageViewType::eCube, 0, 6);
    std::vector<vk::raii::ImageView> prefilterStorageViews;
    prefilterStorageViews.reserve(IBL_PREFILTER_MIPS);
    for (uint32_t mip = 0; mip < IBL_PREFILTER_MIPS; ++mip) {
      prefilterStorageViews.push_back(createImageView(iblPrefilterImage, iblCubeFormat, vk::ImageAspectFlagBits::eColor,
                                                      1, vk::ImageViewType::e2DArray, mip, 6));
    }
    iblPrefilterCubeView = createImageView(iblPrefilterImage, iblCubeFormat, vk::ImageAspectFlagBits::eColor,
                                           IBL_PREFILTER_MIPS, vk::ImageViewType::eCube, 0, 6);
    auto lutStorageView = createImageView(iblBrdfLutImage, lutFormat, vk::ImageAspectFlagBits::eColor);
    iblBrdfLutView = createImageView(iblBrdfLutImage, lutFormat, vk::ImageAspectFlagBits::eColor);

    // ---------- 5. Staging upload of the equirect pixels ----------
    auto [stagingBuf, stagingAlloc] = createBufferPooled(
      eqSize, vk::BufferUsageFlagBits::eTransferSrc,
      vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    if (!stagingAlloc || !stagingAlloc->mappedPtr) {
      std::cerr << "IBL: staging buffer not mappable" << std::endl;
      return false;
    }
    std::memcpy(stagingAlloc->mappedPtr, eqPixels, eqSize);
    stbi_image_free(eqPixels);
    eqPixels = nullptr;

    // ---------- 6. Pipelines ----------
    if (!createIBLComputePipelines())
      return false;

    // ---------- 7. Per-dispatch descriptor sets (8 independent sets!) ----------
    constexpr uint32_t IBL_DISPATCH_COUNT = 8; // 1 env + 1 irradiance + 5 prefilter + 1 LUT
    std::array<vk::DescriptorPoolSize, 2> poolSizes = {
      vk::DescriptorPoolSize{.type = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = IBL_DISPATCH_COUNT},
      vk::DescriptorPoolSize{.type = vk::DescriptorType::eStorageImage, .descriptorCount = IBL_DISPATCH_COUNT}
    };
    vk::DescriptorPoolCreateInfo poolInfo{.maxSets = IBL_DISPATCH_COUNT,
                                          .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
                                          .pPoolSizes = poolSizes.data()};
    vk::raii::DescriptorPool genPool(device, poolInfo);
    std::vector<vk::DescriptorSetLayout> setLayouts(IBL_DISPATCH_COUNT, *iblComputeDescriptorSetLayout);
    vk::DescriptorSetAllocateInfo setAllocInfo{.descriptorPool = *genPool,
                                               .descriptorSetCount = IBL_DISPATCH_COUNT,
                                               .pSetLayouts = setLayouts.data()};
    vk::raii::DescriptorSets genSets(device, setAllocInfo);

    auto writeSet = [&](uint32_t idx, vk::ImageView inView, vk::Sampler inSampler,
                        vk::ImageView outView) {
      vk::DescriptorImageInfo inInfo{.sampler = inSampler,
                                     .imageView = inView,
                                     .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
      vk::DescriptorImageInfo outInfo{.sampler = nullptr,
                                      .imageView = outView,
                                      .imageLayout = vk::ImageLayout::eGeneral};
      std::array<vk::WriteDescriptorSet, 2> writes = {
        vk::WriteDescriptorSet{.dstSet = *genSets[idx], .dstBinding = 0, .descriptorCount = 1,
                               .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &inInfo},
        vk::WriteDescriptorSet{.dstSet = *genSets[idx], .dstBinding = 1, .descriptorCount = 1,
                               .descriptorType = vk::DescriptorType::eStorageImage, .pImageInfo = &outInfo}
      };
      device.updateDescriptorSets(writes, {});
    };

    writeSet(0, *eqView, *iblSampler, *envStorageView);
    writeSet(1, *iblEnvCubeView, *iblSampler, *irrStorageView);
    for (uint32_t mip = 0; mip < IBL_PREFILTER_MIPS; ++mip) {
      writeSet(2 + mip, *iblEnvCubeView, *iblSampler, *prefilterStorageViews[mip]);
    }
    // BRDF LUT: shader has no input sampler; bind the default 2D texture as a harmless dummy.
    writeSet(7, *defaultTextureResources.textureImageView, *defaultTextureResources.textureSampler, *lutStorageView);

    // ---------- 8. Record the generation command buffer ----------
    vk::CommandPoolCreateInfo cmdPoolInfo{
      .flags = vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
      .queueFamilyIndex = queueFamilyIndices.graphicsFamily.value()
    };
    vk::raii::CommandPool tempPool(device, cmdPoolInfo);
    vk::CommandBufferAllocateInfo cmdAllocInfo{.commandPool = *tempPool,
                                               .level = vk::CommandBufferLevel::ePrimary,
                                               .commandBufferCount = 1};
    vk::raii::CommandBuffers cmds(device, cmdAllocInfo);
    vk::raii::CommandBuffer& cmd = cmds[0];
    cmd.begin(vk::CommandBufferBeginInfo{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

    // 8a. Equirect: UNDEFINED -> TRANSFER_DST, copy, -> SHADER_READ_ONLY (compute read).
    recordIBLImageBarrier(cmd, *eqImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
                          vk::PipelineStageFlagBits2::eTopOfPipe, vk::AccessFlagBits2::eNone,
                          vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite, 1, 1);
    vk::BufferImageCopy copyRegion{
      .bufferOffset = 0,
      .bufferRowLength = 0,
      .bufferImageHeight = 0,
      .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1},
      .imageOffset = {0, 0, 0},
      .imageExtent = {static_cast<uint32_t>(eqW), static_cast<uint32_t>(eqH), 1}
    };
    cmd.copyBufferToImage(*stagingBuf, *eqImage, vk::ImageLayout::eTransferDstOptimal, copyRegion);
    recordIBLImageBarrier(cmd, *eqImage, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                          vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite,
                          vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead, 1, 1);

    // 8b. Targets: UNDEFINED -> GENERAL (compute storage write).
    recordIBLImageBarrier(cmd, *iblEnvCubeImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral,
                          vk::PipelineStageFlagBits2::eTopOfPipe, vk::AccessFlagBits2::eNone,
                          vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite, 1, 6);
    recordIBLImageBarrier(cmd, *iblIrradianceImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral,
                          vk::PipelineStageFlagBits2::eTopOfPipe, vk::AccessFlagBits2::eNone,
                          vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite, 1, 6);
    recordIBLImageBarrier(cmd, *iblPrefilterImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral,
                          vk::PipelineStageFlagBits2::eTopOfPipe, vk::AccessFlagBits2::eNone,
                          vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite,
                          IBL_PREFILTER_MIPS, 6);
    recordIBLImageBarrier(cmd, *iblBrdfLutImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral,
                          vk::PipelineStageFlagBits2::eTopOfPipe, vk::AccessFlagBits2::eNone,
                          vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite, 1, 1);

    auto dispatchIBL = [&](vk::raii::Pipeline& pipe, uint32_t setIdx, float pc0, float pc1,
                           uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ) {
      cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *pipe);
      cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *iblComputePipelineLayout, 0,
                             *genSets[setIdx], nullptr);
      const float pc[4] = {pc0, pc1, 0.0f, 0.0f};
      cmd.pushConstants(*iblComputePipelineLayout, vk::ShaderStageFlagBits::eCompute, 0u,
                        vk::ArrayProxy<const float>(4, pc));
      cmd.dispatch(groupsX, groupsY, groupsZ);
    };
    const auto groupsFor = [](uint32_t size) { return (size + 7u) / 8u; };

    // 8c. Dispatch 0: equirect -> environment cubemap.
    dispatchIBL(iblEquirectToCubemapPipeline, 0, static_cast<float>(IBL_ENV_CUBE_SIZE),
                1.0f / static_cast<float>(IBL_ENV_CUBE_SIZE), groupsFor(IBL_ENV_CUBE_SIZE), groupsFor(IBL_ENV_CUBE_SIZE), 6);

    // 8d. Env cube: GENERAL -> SHADER_READ_ONLY so irradiance/prefilter can sample it.
    recordIBLImageBarrier(cmd, *iblEnvCubeImage, vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal,
                          vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite,
                          vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead, 1, 6);

    // 8e. Dispatch 1: diffuse irradiance cubemap (hemisphere cosine convolution).
    dispatchIBL(iblIrradiancePipeline, 1, static_cast<float>(IBL_IRRADIANCE_SIZE), 0.0f,
                groupsFor(IBL_IRRADIANCE_SIZE), groupsFor(IBL_IRRADIANCE_SIZE), 6);

    // 8f. Dispatches 2..6: GGX-prefiltered specular cubemap, one mip per roughness.
    for (uint32_t mip = 0; mip < IBL_PREFILTER_MIPS; ++mip) {
      const uint32_t mipSize = IBL_PREFILTER_SIZE >> mip;
      const float roughness = static_cast<float>(mip) / static_cast<float>(IBL_PREFILTER_MIPS - 1);
      dispatchIBL(iblPrefilterPipeline, 2 + mip, static_cast<float>(mipSize), roughness,
                  groupsFor(mipSize), groupsFor(mipSize), 6);
    }

    // 8g. Dispatch 7: BRDF integration LUT (no input).
    dispatchIBL(iblBrdfLutPipeline, 7, 0.0f, 0.0f, groupsFor(IBL_LUT_SIZE), groupsFor(IBL_LUT_SIZE), 1);

    // 8h. Final: GENERAL -> SHADER_READ_ONLY for fragment (raster) + compute (ray query) reads.
    recordIBLImageBarrier(cmd, *iblIrradianceImage, vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal,
                          vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite,
                          vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eComputeShader,
                          vk::AccessFlagBits2::eShaderRead, 1, 6);
    recordIBLImageBarrier(cmd, *iblPrefilterImage, vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal,
                          vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite,
                          vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eComputeShader,
                          vk::AccessFlagBits2::eShaderRead, IBL_PREFILTER_MIPS, 6);
    recordIBLImageBarrier(cmd, *iblBrdfLutImage, vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal,
                          vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite,
                          vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eComputeShader,
                          vk::AccessFlagBits2::eShaderRead, 1, 1);
    cmd.end();

    // ---------- 9. Submit and wait ----------
    vk::raii::Fence fence(device, vk::FenceCreateInfo{}); {
      std::lock_guard<std::mutex> lock(queueMutex);
      vk::SubmitInfo submitInfo{};
      submitInfo.commandBufferCount = 1;
      submitInfo.pCommandBuffers = &(*cmd);
      graphicsQueue.submit(submitInfo, *fence);
    }
    (void) waitForFencesSafe(*fence, VK_TRUE, 120'000'000'000ULL); // generous: first-time prefilter

    // Transient resources (staging, equirect image/view, storage views, gen pool/sets)
    // are RAII locals and release here, after the fence wait.

    iblInitialized = true;
    LOGI("IBL: generated env cube %u, irradiance %u, prefilter %ux%u mips, LUT %u (format %d)",
         IBL_ENV_CUBE_SIZE, IBL_IRRADIANCE_SIZE, IBL_PREFILTER_SIZE, IBL_PREFILTER_MIPS, IBL_LUT_SIZE,
         static_cast<int>(iblCubeFormat));

    // Rebind into PBR entity sets + Ray Query set (no-op when no entities exist yet).
    bindIBLToPBRDescriptorSets();
    return true;
  } catch (const std::exception& e) {
    std::cerr << "IBL: loadAndGenerateIBL failed: " << e.what() << std::endl;
    return false;
  }
}

// Invalidate per-frame fixed-binding tracking so bindings 14/15/16 (PBR) and 8/9/10
// (Ray Query) are rewritten with the current IBL (or fallback) views at the next
// descriptor safe point.
void Renderer::bindIBLToPBRDescriptorSets() {
  for (auto& kv : entityResources) {
    kv.second.pbrFixedBindingsWritten.assign(MAX_FRAMES_IN_FLIGHT, false);
  }
  const uint32_t allFramesMask = (MAX_FRAMES_IN_FLIGHT >= 32u) ? 0xFFFFFFFFu : ((1u << MAX_FRAMES_IN_FLIGHT) - 1u);
  rayQueryDescriptorsDirtyMask.fetch_or(allFramesMask, std::memory_order_relaxed);
}
