/* Copyright (c) 2025 Holochip Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "renderer.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>

bool Renderer::createTAAPipeline() {
  try {
    if (*taaDescriptorSetLayout == nullptr) {
      std::array<vk::DescriptorSetLayoutBinding, 7> bindings{};
      for (uint32_t binding = 0; binding < 4; ++binding) {
        bindings[binding] = vk::DescriptorSetLayoutBinding{
          .binding = binding,
          .descriptorType = vk::DescriptorType::eCombinedImageSampler,
          .descriptorCount = 1,
          .stageFlags = vk::ShaderStageFlagBits::eCompute
        };
      }
      bindings[4] = vk::DescriptorSetLayoutBinding{
        .binding = 4,
        .descriptorType = vk::DescriptorType::eStorageImage,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eCompute
      };
      bindings[5] = vk::DescriptorSetLayoutBinding{
        .binding = 5,
        .descriptorType = vk::DescriptorType::eStorageImage,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eCompute
      };
      bindings[6] = vk::DescriptorSetLayoutBinding{
        .binding = 6,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eCompute
      };

      vk::DescriptorSetLayoutCreateInfo layoutInfo{
        .bindingCount = static_cast<uint32_t>(bindings.size()),
        .pBindings = bindings.data()
      };
      taaDescriptorSetLayout = vk::raii::DescriptorSetLayout(device, layoutInfo);
    }

    vk::DescriptorSetLayout setLayout = *taaDescriptorSetLayout;
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
      .setLayoutCount = 1,
      .pSetLayouts = &setLayout
    };
    taaPipelineLayout = vk::raii::PipelineLayout(device, pipelineLayoutInfo);

    auto shaderCode = readFile("shaders/taa.spv");
    auto shaderModule = createShaderModule(shaderCode);
    vk::PipelineShaderStageCreateInfo stageInfo{
      .stage = vk::ShaderStageFlagBits::eCompute,
      .module = *shaderModule,
      .pName = "main"
    };
    vk::ComputePipelineCreateInfo pipelineInfo{
      .stage = stageInfo,
      .layout = *taaPipelineLayout
    };
    taaPipeline = vk::raii::Pipeline(device, nullptr, pipelineInfo);
    return true;
  } catch (const std::exception& e) {
    std::cerr << "Failed to create TAA pipeline: " << e.what() << '\n';
    return false;
  }
}

void Renderer::destroyTAAResources() {
  taaRasterDescriptorSets.clear();
  taaRayQueryDescriptorSets.clear();
  taaHistoryImageViews.clear();
  taaHistoryImages.clear();
  taaHistoryImageAllocations.clear();
  taaHistoryImageLayouts.clear();
  taaHistoryDepthViews.clear();
  taaHistoryDepthImages.clear();
  taaHistoryDepthAllocations.clear();
  taaHistoryDepthLayouts.clear();
  taaUniformBuffers.clear();
  taaUniformAllocations.clear();
  taaUniformBuffersMapped.clear();
  taaSampler = vk::raii::Sampler(nullptr);
  resetTAAHistory();
}

bool Renderer::createTAAResources() {
  try {
    destroyTAAResources();

    taaColorFormat = vk::Format::eR16G16B16A16Sfloat;
    auto colorProps = physicalDevice.getFormatProperties(taaColorFormat);
    const auto requiredColorFeatures = vk::FormatFeatureFlagBits::eStorageImage |
                                       vk::FormatFeatureFlagBits::eSampledImage;
    if ((colorProps.optimalTilingFeatures & requiredColorFeatures) != requiredColorFeatures) {
      taaColorFormat = vk::Format::eR32G32B32A32Sfloat;
    }

    taaDepthFormat = vk::Format::eR32Sfloat;
    auto depthProps = physicalDevice.getFormatProperties(taaDepthFormat);
    const auto requiredDepthFeatures = vk::FormatFeatureFlagBits::eStorageImage |
                                       vk::FormatFeatureFlagBits::eSampledImage;
    if ((depthProps.optimalTilingFeatures & requiredDepthFeatures) != requiredDepthFeatures) {
      taaDepthFormat = vk::Format::eR16Sfloat;
    }

    taaHistoryImages.reserve(MAX_FRAMES_IN_FLIGHT);
    taaHistoryImageAllocations.reserve(MAX_FRAMES_IN_FLIGHT);
    taaHistoryImageViews.reserve(MAX_FRAMES_IN_FLIGHT);
    taaHistoryImageLayouts.reserve(MAX_FRAMES_IN_FLIGHT);
    taaHistoryDepthImages.reserve(MAX_FRAMES_IN_FLIGHT);
    taaHistoryDepthAllocations.reserve(MAX_FRAMES_IN_FLIGHT);
    taaHistoryDepthViews.reserve(MAX_FRAMES_IN_FLIGHT);
    taaHistoryDepthLayouts.reserve(MAX_FRAMES_IN_FLIGHT);

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
      auto [colorImage, colorAllocation] = createImagePooled(
        swapChainExtent.width,
        swapChainExtent.height,
        taaColorFormat,
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
        vk::MemoryPropertyFlagBits::eDeviceLocal);
      taaHistoryImages.push_back(std::move(colorImage));
      taaHistoryImageAllocations.push_back(std::move(colorAllocation));
      taaHistoryImageViews.push_back(createImageView(
        taaHistoryImages.back(), taaColorFormat, vk::ImageAspectFlagBits::eColor));
      taaHistoryImageLayouts.push_back(vk::ImageLayout::eUndefined);

      auto [depthImage, depthAllocation] = createImagePooled(
        swapChainExtent.width,
        swapChainExtent.height,
        taaDepthFormat,
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
        vk::MemoryPropertyFlagBits::eDeviceLocal);
      taaHistoryDepthImages.push_back(std::move(depthImage));
      taaHistoryDepthAllocations.push_back(std::move(depthAllocation));
      taaHistoryDepthViews.push_back(createImageView(
        taaHistoryDepthImages.back(), taaDepthFormat, vk::ImageAspectFlagBits::eColor));
      taaHistoryDepthLayouts.push_back(vk::ImageLayout::eUndefined);
    }

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
      transitionImageLayout(*taaHistoryImages[i], taaColorFormat,
                            vk::ImageLayout::eUndefined,
                            vk::ImageLayout::eShaderReadOnlyOptimal);
      taaHistoryImageLayouts[i] = vk::ImageLayout::eShaderReadOnlyOptimal;
      transitionImageLayout(*taaHistoryDepthImages[i], taaDepthFormat,
                            vk::ImageLayout::eUndefined,
                            vk::ImageLayout::eShaderReadOnlyOptimal);
      taaHistoryDepthLayouts[i] = vk::ImageLayout::eShaderReadOnlyOptimal;
    }

    vk::SamplerCreateInfo samplerInfo{
      .magFilter = vk::Filter::eLinear,
      .minFilter = vk::Filter::eLinear,
      .mipmapMode = vk::SamplerMipmapMode::eNearest,
      .addressModeU = vk::SamplerAddressMode::eClampToEdge,
      .addressModeV = vk::SamplerAddressMode::eClampToEdge,
      .addressModeW = vk::SamplerAddressMode::eClampToEdge,
      .maxLod = 0.0f
    };
    taaSampler = vk::raii::Sampler(device, samplerInfo);

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
      auto [buffer, allocation] = createBufferPooled(
        sizeof(TAAUniformBufferObject),
        vk::BufferUsageFlagBits::eUniformBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
      taaUniformBuffers.push_back(std::move(buffer));
      taaUniformAllocations.push_back(std::move(allocation));
      taaUniformBuffersMapped.push_back(taaUniformAllocations.back()->mappedPtr);
    }

    createTAADescriptorSets();
    updateCompositeSceneInputs();
    resetTAAHistory();
    return true;
  } catch (const std::exception& e) {
    std::cerr << "Failed to create TAA resources: " << e.what() << '\n';
    destroyTAAResources();
    return false;
  }
}

void Renderer::createTAADescriptorSets() {
  if (!*descriptorPool || !*taaDescriptorSetLayout || !*taaSampler ||
      opaqueSceneColorImageViews.size() < MAX_FRAMES_IN_FLIGHT ||
      taaHistoryImageViews.size() < MAX_FRAMES_IN_FLIGHT ||
      taaHistoryDepthViews.size() < MAX_FRAMES_IN_FLIGHT ||
      taaUniformBuffers.size() < MAX_FRAMES_IN_FLIGHT || !*depthImageView ||
      !*rayQueryOutputImageView || !*rayQueryDepthImageView) {
    return;
  }

  std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, *taaDescriptorSetLayout);
  vk::DescriptorSetAllocateInfo allocInfo{
    .descriptorPool = *descriptorPool,
    .descriptorSetCount = MAX_FRAMES_IN_FLIGHT,
    .pSetLayouts = layouts.data()
  };
  {
    std::lock_guard<std::mutex> lock(descriptorMutex);
    taaRasterDescriptorSets = vk::raii::DescriptorSets(device, allocInfo);
    taaRayQueryDescriptorSets = vk::raii::DescriptorSets(device, allocInfo);
  }

  for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
    const uint32_t previous = (i + MAX_FRAMES_IN_FLIGHT - 1u) % MAX_FRAMES_IN_FLIGHT;
    vk::DescriptorImageInfo rasterColor{
      .sampler = *taaSampler,
      .imageView = *opaqueSceneColorImageViews[i],
      .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
    };
    vk::DescriptorImageInfo rasterDepth{
      .sampler = *taaSampler,
      .imageView = *depthImageView,
      .imageLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal
    };
    vk::DescriptorImageInfo rayColor{
      .sampler = *taaSampler,
      .imageView = *rayQueryOutputImageView,
      .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
    };
    vk::DescriptorImageInfo rayDepth{
      .sampler = *taaSampler,
      .imageView = *rayQueryDepthImageView,
      .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
    };
    vk::DescriptorImageInfo historyColor{
      .sampler = *taaSampler,
      .imageView = *taaHistoryImageViews[previous],
      .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
    };
    vk::DescriptorImageInfo historyDepth{
      .sampler = *taaSampler,
      .imageView = *taaHistoryDepthViews[previous],
      .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
    };
    vk::DescriptorImageInfo outputColor{
      .imageView = *taaHistoryImageViews[i],
      .imageLayout = vk::ImageLayout::eGeneral
    };
    vk::DescriptorImageInfo outputDepth{
      .imageView = *taaHistoryDepthViews[i],
      .imageLayout = vk::ImageLayout::eGeneral
    };
    vk::DescriptorBufferInfo uniformInfo{
      .buffer = *taaUniformBuffers[i],
      .offset = 0,
      .range = sizeof(TAAUniformBufferObject)
    };

    auto writeSet = [&](vk::DescriptorSet set,
                        const vk::DescriptorImageInfo& currentColor,
                        const vk::DescriptorImageInfo& currentDepth) {
      std::array<vk::WriteDescriptorSet, 7> writes = {
        vk::WriteDescriptorSet{.dstSet = set, .dstBinding = 0, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &currentColor},
        vk::WriteDescriptorSet{.dstSet = set, .dstBinding = 1, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &currentDepth},
        vk::WriteDescriptorSet{.dstSet = set, .dstBinding = 2, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &historyColor},
        vk::WriteDescriptorSet{.dstSet = set, .dstBinding = 3, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &historyDepth},
        vk::WriteDescriptorSet{.dstSet = set, .dstBinding = 4, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eStorageImage, .pImageInfo = &outputColor},
        vk::WriteDescriptorSet{.dstSet = set, .dstBinding = 5, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eStorageImage, .pImageInfo = &outputDepth},
        vk::WriteDescriptorSet{.dstSet = set, .dstBinding = 6, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eUniformBuffer, .pBufferInfo = &uniformInfo}
      };
      device.updateDescriptorSets(writes, {});
    };

    std::lock_guard<std::mutex> lock(descriptorMutex);
    writeSet(*taaRasterDescriptorSets[i], rasterColor, rasterDepth);
    writeSet(*taaRayQueryDescriptorSets[i], rayColor, rayDepth);
  }
}

void Renderer::updateCompositeSceneInputs() {
  if (!*taaSampler || taaHistoryImageViews.size() < MAX_FRAMES_IN_FLIGHT) {
    return;
  }

  std::vector<vk::WriteDescriptorSet> writes;
  std::vector<vk::DescriptorImageInfo> imageInfos;
  imageInfos.reserve(compositeDescriptorSets.size() + rqCompositeDescriptorSets.size());
  writes.reserve(compositeDescriptorSets.size() + rqCompositeDescriptorSets.size());

  auto appendWrites = [&](const std::vector<vk::raii::DescriptorSet>& sets) {
    for (uint32_t i = 0; i < sets.size() && i < MAX_FRAMES_IN_FLIGHT; ++i) {
      imageInfos.push_back(vk::DescriptorImageInfo{
        .sampler = *taaSampler,
        .imageView = *taaHistoryImageViews[i],
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
      });
      writes.push_back(vk::WriteDescriptorSet{
        .dstSet = *sets[i],
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .pImageInfo = &imageInfos.back()
      });
    }
  };
  appendWrites(compositeDescriptorSets);
  appendWrites(rqCompositeDescriptorSets);

  if (!writes.empty()) {
    std::lock_guard<std::mutex> lock(descriptorMutex);
    device.updateDescriptorSets(writes, {});
  }
}

void Renderer::updateTAAUniformBuffer(uint32_t frameIndex) {
  if (frameIndex >= taaUniformBuffersMapped.size() || !taaUniformBuffersMapped[frameIndex]) {
    return;
  }

  TAAUniformBufferObject ubo{};
  ubo.invCurrentViewProj = glm::inverse(taaCurrentViewProj);
  ubo.previousViewProj = taaPreviousViewProj;
  ubo.screenHistoryDepth = glm::vec4(
    static_cast<float>(swapChainExtent.width),
    static_cast<float>(swapChainExtent.height),
    std::clamp(taaHistoryWeight, 0.0f, 0.98f),
    std::clamp(taaDepthThreshold, 0.0001f, 0.05f));
  ubo.jitterSharpness = glm::vec4(
    taaCurrentJitter,
    std::clamp(taaSharpness, 0.0f, 0.5f),
    taaHistoryValid ? 1.0f : 0.0f);
  ubo.control = glm::ivec4(taaEnabled ? 1 : 0, taaDebugView, 0, 0);
  std::memcpy(taaUniformBuffersMapped[frameIndex], &ubo, sizeof(ubo));
}

void Renderer::dispatchTAA(vk::raii::CommandBuffer& cmd, bool rayQueryPath) {
  auto& sets = rayQueryPath ? taaRayQueryDescriptorSets : taaRasterDescriptorSets;
  if (!*taaPipeline || currentFrame >= sets.size() ||
      currentFrame >= taaHistoryImages.size() ||
      currentFrame >= taaHistoryDepthImages.size()) {
    return;
  }

  updateTAAUniformBuffer(currentFrame);

  const vk::PipelineStageFlags2 priorColorStages =
    vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eFragmentShader;
  std::array<vk::ImageMemoryBarrier2, 2> toGeneral = {
    vk::ImageMemoryBarrier2{
      .srcStageMask = priorColorStages,
      .srcAccessMask = vk::AccessFlagBits2::eShaderRead,
      .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .dstAccessMask = vk::AccessFlagBits2::eShaderWrite,
      .oldLayout = taaHistoryImageLayouts[currentFrame],
      .newLayout = vk::ImageLayout::eGeneral,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = *taaHistoryImages[currentFrame],
      .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
    },
    vk::ImageMemoryBarrier2{
      .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .srcAccessMask = vk::AccessFlagBits2::eShaderRead,
      .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .dstAccessMask = vk::AccessFlagBits2::eShaderWrite,
      .oldLayout = taaHistoryDepthLayouts[currentFrame],
      .newLayout = vk::ImageLayout::eGeneral,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = *taaHistoryDepthImages[currentFrame],
      .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
    }
  };
  vk::DependencyInfo toGeneralDependency{
    .imageMemoryBarrierCount = static_cast<uint32_t>(toGeneral.size()),
    .pImageMemoryBarriers = toGeneral.data()
  };
  cmd.pipelineBarrier2(toGeneralDependency);
  taaHistoryImageLayouts[currentFrame] = vk::ImageLayout::eGeneral;
  taaHistoryDepthLayouts[currentFrame] = vk::ImageLayout::eGeneral;

  cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *taaPipeline);
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *taaPipelineLayout, 0,
                         {*sets[currentFrame]}, {});
  cmd.dispatch((swapChainExtent.width + 7u) / 8u,
               (swapChainExtent.height + 7u) / 8u, 1);

  std::array<vk::ImageMemoryBarrier2, 2> toRead = {
    vk::ImageMemoryBarrier2{
      .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
      .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eFragmentShader,
      .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
      .oldLayout = vk::ImageLayout::eGeneral,
      .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = *taaHistoryImages[currentFrame],
      .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
    },
    vk::ImageMemoryBarrier2{
      .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
      .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
      .oldLayout = vk::ImageLayout::eGeneral,
      .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = *taaHistoryDepthImages[currentFrame],
      .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
    }
  };
  vk::DependencyInfo toReadDependency{
    .imageMemoryBarrierCount = static_cast<uint32_t>(toRead.size()),
    .pImageMemoryBarriers = toRead.data()
  };
  cmd.pipelineBarrier2(toReadDependency);
  taaHistoryImageLayouts[currentFrame] = vk::ImageLayout::eShaderReadOnlyOptimal;
  taaHistoryDepthLayouts[currentFrame] = vk::ImageLayout::eShaderReadOnlyOptimal;

  taaPreviousViewProj = taaCurrentViewProj;
  taaPreviousRenderMode = rayQueryPath ? RenderMode::RayQuery : RenderMode::Rasterization;
  taaHistoryValid = true;
  ++taaFrameIndex;
}

void Renderer::resetTAAHistory() {
  taaHistoryValid = false;
  taaFrameIndex = 0;
  taaCurrentJitter = glm::vec2(0.0f);
  taaCurrentViewProj = glm::mat4(1.0f);
  taaPreviousViewProj = glm::mat4(1.0f);
  taaPreviousRenderMode = currentRenderMode;
}
