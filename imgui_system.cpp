/* Copyright (c) 2025 Holochip Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 the "License";
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "imgui_system.h"
#include "renderer.h"

// Include ImGui headers
#include "imgui/imgui.h"

#include <GLFW/glfw3.h>

#include <iostream>

namespace {
ImGuiKey GlfwKeyToImGuiKey(uint32_t key) {
  if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) {
    return static_cast<ImGuiKey>(ImGuiKey_0 + key - GLFW_KEY_0);
  }
  if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) {
    return static_cast<ImGuiKey>(ImGuiKey_A + key - GLFW_KEY_A);
  }
  if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F24) {
    return static_cast<ImGuiKey>(ImGuiKey_F1 + key - GLFW_KEY_F1);
  }
  if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9) {
    return static_cast<ImGuiKey>(ImGuiKey_Keypad0 + key - GLFW_KEY_KP_0);
  }

  switch (key) {
    case GLFW_KEY_TAB: return ImGuiKey_Tab;
    case GLFW_KEY_LEFT: return ImGuiKey_LeftArrow;
    case GLFW_KEY_RIGHT: return ImGuiKey_RightArrow;
    case GLFW_KEY_UP: return ImGuiKey_UpArrow;
    case GLFW_KEY_DOWN: return ImGuiKey_DownArrow;
    case GLFW_KEY_PAGE_UP: return ImGuiKey_PageUp;
    case GLFW_KEY_PAGE_DOWN: return ImGuiKey_PageDown;
    case GLFW_KEY_HOME: return ImGuiKey_Home;
    case GLFW_KEY_END: return ImGuiKey_End;
    case GLFW_KEY_INSERT: return ImGuiKey_Insert;
    case GLFW_KEY_DELETE: return ImGuiKey_Delete;
    case GLFW_KEY_BACKSPACE: return ImGuiKey_Backspace;
    case GLFW_KEY_SPACE: return ImGuiKey_Space;
    case GLFW_KEY_ENTER: return ImGuiKey_Enter;
    case GLFW_KEY_ESCAPE: return ImGuiKey_Escape;
    case GLFW_KEY_APOSTROPHE: return ImGuiKey_Apostrophe;
    case GLFW_KEY_COMMA: return ImGuiKey_Comma;
    case GLFW_KEY_MINUS: return ImGuiKey_Minus;
    case GLFW_KEY_PERIOD: return ImGuiKey_Period;
    case GLFW_KEY_SLASH: return ImGuiKey_Slash;
    case GLFW_KEY_SEMICOLON: return ImGuiKey_Semicolon;
    case GLFW_KEY_EQUAL: return ImGuiKey_Equal;
    case GLFW_KEY_LEFT_BRACKET: return ImGuiKey_LeftBracket;
    case GLFW_KEY_BACKSLASH: return ImGuiKey_Backslash;
    case GLFW_KEY_RIGHT_BRACKET: return ImGuiKey_RightBracket;
    case GLFW_KEY_GRAVE_ACCENT: return ImGuiKey_GraveAccent;
    case GLFW_KEY_CAPS_LOCK: return ImGuiKey_CapsLock;
    case GLFW_KEY_SCROLL_LOCK: return ImGuiKey_ScrollLock;
    case GLFW_KEY_NUM_LOCK: return ImGuiKey_NumLock;
    case GLFW_KEY_PRINT_SCREEN: return ImGuiKey_PrintScreen;
    case GLFW_KEY_PAUSE: return ImGuiKey_Pause;
    case GLFW_KEY_KP_DECIMAL: return ImGuiKey_KeypadDecimal;
    case GLFW_KEY_KP_DIVIDE: return ImGuiKey_KeypadDivide;
    case GLFW_KEY_KP_MULTIPLY: return ImGuiKey_KeypadMultiply;
    case GLFW_KEY_KP_SUBTRACT: return ImGuiKey_KeypadSubtract;
    case GLFW_KEY_KP_ADD: return ImGuiKey_KeypadAdd;
    case GLFW_KEY_KP_ENTER: return ImGuiKey_KeypadEnter;
    case GLFW_KEY_KP_EQUAL: return ImGuiKey_KeypadEqual;
    case GLFW_KEY_LEFT_SHIFT: return ImGuiKey_LeftShift;
    case GLFW_KEY_LEFT_CONTROL: return ImGuiKey_LeftCtrl;
    case GLFW_KEY_LEFT_ALT: return ImGuiKey_LeftAlt;
    case GLFW_KEY_LEFT_SUPER: return ImGuiKey_LeftSuper;
    case GLFW_KEY_RIGHT_SHIFT: return ImGuiKey_RightShift;
    case GLFW_KEY_RIGHT_CONTROL: return ImGuiKey_RightCtrl;
    case GLFW_KEY_RIGHT_ALT: return ImGuiKey_RightAlt;
    case GLFW_KEY_RIGHT_SUPER: return ImGuiKey_RightSuper;
    case GLFW_KEY_MENU: return ImGuiKey_Menu;
    default: return ImGuiKey_None;
  }
}
} // namespace

// This implementation corresponds to the GUI chapter in the tutorial:
// @see en/Building_a_Simple_Engine/GUI/02_imgui_setup.adoc

ImGuiSystem::ImGuiSystem() {
  // Constructor implementation
}

ImGuiSystem::~ImGuiSystem() {
  // Destructor implementation
  Cleanup();
}

bool ImGuiSystem::Initialize(Renderer* renderer, uint32_t width, uint32_t height) {
  if (initialized) {
    return true;
  }

  this->renderer = renderer;
  this->width = width;
  this->height = height;

  // Create ImGui context
  context = ImGui::CreateContext();
  if (!context) {
    std::cerr << "Failed to create ImGui context" << std::endl;
    return false;
  }

  // Configure ImGui
  ImGuiIO& io = ImGui::GetIO();
  // Set display size
  io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
  io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

  // Inform ImGui that we support the new texture update protocol (v1.92+)
  io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

  // Set up ImGui style
  ImGui::StyleColorsDark();

  // Create Vulkan resources
  if (!createResources()) {
    std::cerr << "Failed to create ImGui Vulkan resources" << std::endl;
    Cleanup();
    return false;
  }

  // Initialize per-frame buffers containers
  if (renderer) {
    uint32_t frames = renderer->GetMaxFramesInFlight();
    vertexBuffers.clear();
    vertexBuffers.reserve(frames);
    vertexBufferMemories.clear();
    vertexBufferMemories.reserve(frames);
    indexBuffers.clear();
    indexBuffers.reserve(frames);
    indexBufferMemories.clear();
    indexBufferMemories.reserve(frames);
    for (uint32_t i = 0; i < frames; ++i) {
      vertexBuffers.emplace_back(nullptr);
      vertexBufferMemories.emplace_back(nullptr);
      indexBuffers.emplace_back(nullptr);
      indexBufferMemories.emplace_back(nullptr);
    }
    vertexCounts.assign(frames, 0);
    indexCounts.assign(frames, 0);
  }

  initialized = true;
  return true;
}

void ImGuiSystem::Cleanup() {
  if (!initialized) {
    return;
  }

  // Wait for the device to be idle before cleaning up
  if (renderer) {
    renderer->WaitIdle();
  }
  // Destroy ImGui context
  if (context) {
    ImGui::DestroyContext(context);
    context = nullptr;
  }

  initialized = false;
}

void ImGuiSystem::NewFrame() {
  if (!initialized) {
    return;
  }

  // Reset the flag at the start of each frame
  frameAlreadyRendered = false;

  ImGui::NewFrame();

  // Loading overlay: show a fullscreen progress bar while the initial scene is loading.
  // The bar resets between phases (Textures -> Scene -> AS -> Finalizing) so users
  // don't stare at a 100% bar while the engine is still doing work.
  if (renderer) {
    const bool modelLoading = renderer->IsLoading();
    if (modelLoading) {
      ImGuiIO& io = ImGui::GetIO();
      // Suppress right-click while loading (v1.87+ event API)
      io.AddMouseButtonEvent(1, false);

      const ImVec2 dispSize = io.DisplaySize;

      ImGui::SetNextWindowPos(ImVec2(0, 0));
      ImGui::SetNextWindowSize(dispSize);

      // Override style for loading overlay to ensure visibility and contrast
      ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.05f, 0.05f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
      ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

      ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
          ImGuiWindowFlags_NoResize |
          ImGuiWindowFlags_NoMove |
          ImGuiWindowFlags_NoScrollbar |
          ImGuiWindowFlags_NoCollapse |
          ImGuiWindowFlags_NoSavedSettings |
          ImGuiWindowFlags_NoBringToFrontOnFocus |
          ImGuiWindowFlags_NoNav;

      if (ImGui::Begin("##LoadingOverlay", nullptr, flags)) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        // Center the progress elements
        const float barWidth = dispSize.x * 0.8f;
        const float barX = (dispSize.x - barWidth) * 0.5f;
        const float barY = dispSize.y * 0.45f;
        ImGui::SetCursorPos(ImVec2(barX, barY));
        ImGui::BeginGroup();

        // Phase-aware progress (resets between phases).
        float frac = 0.0f;
        auto phase = renderer->GetLoadingPhase();
        if (phase == Renderer::LoadingPhase::Textures) {
          const uint32_t scheduled = renderer->GetTextureTasksScheduled();
          const uint32_t completed = renderer->GetTextureTasksCompleted();
          frac = (scheduled > 0) ? (static_cast<float>(completed) / static_cast<float>(scheduled)) : 0.0f;
        } else if (phase == Renderer::LoadingPhase::AccelerationStructures) {
          frac = renderer->GetASBuildProgress();
        } else {
          frac = renderer->GetLoadingPhaseProgress();
        }
        ImGui::ProgressBar(frac, ImVec2(barWidth, 0.0f));
        ImGui::Dummy(ImVec2(0.0f, 10.0f));
        ImGui::SetCursorPosX(barX);
        ImGui::Text("Loading: %s", renderer->GetLoadingPhaseName());
        if (phase == Renderer::LoadingPhase::Textures) {
          const uint32_t scheduled = renderer->GetTextureTasksScheduled();
          const uint32_t completed = renderer->GetTextureTasksCompleted();
          ImGui::Text("Textures: %u/%u", completed, scheduled);
        } else if (phase == Renderer::LoadingPhase::AccelerationStructures) {
          const uint32_t done = renderer->GetASBuildItemsDone();
          const uint32_t total = renderer->GetASBuildItemsTotal();
          ImGui::Text("%s (%u/%u, %.1fs)", renderer->GetASBuildStage(), done, total, renderer->GetASBuildElapsedSeconds());
        }
        ImGui::EndGroup();
        ImGui::PopStyleVar(); // WindowPadding
      }
      ImGui::End();
      ImGui::PopStyleVar(); // WindowBorderSize
      ImGui::PopStyleColor(2); // WindowBg, Text
      return;
    }
  }

  // --- Streaming status: small progress indicator in the upper-right ---
  // Once the scene is visible, textures may continue streaming to the GPU.
  // Show a compact progress bar in the top-right while there are still
  // outstanding texture tasks, and hide it once everything is fully loaded.
  if (renderer) {
    const uint32_t uploadTotal = renderer->GetUploadJobsTotal();
    const uint32_t uploadDone = renderer->GetUploadJobsCompleted();
    const bool modelLoading = renderer->IsLoading();
    const bool showASBuild = renderer->ShouldShowASBuildProgressInUI();

    // Acceleration structure build can happen after initial load completes.
    // If it takes a long time, show a compact progress window.
    if (!modelLoading && showASBuild) {
      ImGuiIO& io = ImGui::GetIO();
      const ImVec2 dispSize = io.DisplaySize;

      const float windowWidth = std::min(320.0f, dispSize.x * 0.42f);
      const float windowHeight = 90.0f;
      const ImVec2 winPos(dispSize.x - windowWidth - 10.0f, 10.0f);

      ImGui::SetNextWindowPos(winPos, ImGuiCond_Always);
      ImGui::SetNextWindowSize(ImVec2(windowWidth, windowHeight));
      ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize |
          ImGuiWindowFlags_NoMove |
          ImGuiWindowFlags_NoCollapse |
          ImGuiWindowFlags_NoSavedSettings;

      if (ImGui::Begin("##ASBuildStatus", nullptr, flags)) {
        ImGui::Text("Building acceleration structures...");
        const float asFrac = renderer->GetASBuildProgress();
        ImGui::ProgressBar(asFrac, ImVec2(-1.0f, 0.0f));
        const uint32_t done = renderer->GetASBuildItemsDone();
        const uint32_t total = renderer->GetASBuildItemsTotal();
        ImGui::Text("%s (%u/%u, %.1fs)",
                    renderer->GetASBuildStage(),
                    done,
                    total,
                    renderer->GetASBuildElapsedSeconds());
      }
      ImGui::End();
    }

    if (!modelLoading && uploadTotal > 0 && uploadDone < uploadTotal) {
      ImGuiIO& io = ImGui::GetIO();
      const ImVec2 dispSize = io.DisplaySize;

      const float windowWidth = std::min(260.0f, dispSize.x * 0.35f);
      const float windowHeight = 120.0f;
      // If the AS build status window is visible, offset streaming window below it.
      const float yBase = 10.0f + (showASBuild ? (90.0f + 10.0f) : 0.0f);
      const ImVec2 winPos(dispSize.x - windowWidth - 10.0f, yBase);

      ImGui::SetNextWindowPos(winPos, ImGuiCond_Always);
      ImGui::SetNextWindowSize(ImVec2(windowWidth, windowHeight));
      ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
          ImGuiWindowFlags_NoResize |
          ImGuiWindowFlags_NoMove |
          ImGuiWindowFlags_NoScrollbar |
          ImGuiWindowFlags_NoSavedSettings |
          ImGuiWindowFlags_NoCollapse;

      if (ImGui::Begin("##StreamingTextures", nullptr, flags)) {
        ImGui::TextUnformatted("Streaming textures to GPU");
        float frac = (uploadTotal > 0) ? (float) uploadDone / (float) uploadTotal : 0.0f;
        ImGui::ProgressBar(frac, ImVec2(-1.0f, 0.0f));

        // Perf counters
        const double mbps = renderer->GetUploadThroughputMBps();
        const double avgMs = renderer->GetAverageUploadMs();
        const double totalMB = (double) renderer->GetBytesUploadedTotal() / (1024.0 * 1024.0);
        ImGui::Text("Throughput: %.1f MB/s", mbps);
        ImGui::SameLine();
        ImGui::Text("Avg upload: %.2f ms/tex", avgMs);
        ImGui::Text("Total uploaded: %.1f MB", totalMB);
      }
      ImGui::End();
    }
  }

  ImGui::Begin("Renderer");

  // Texture loading progress
  if (renderer) {
    const uint32_t scheduled = renderer->GetTextureTasksScheduled();
    const uint32_t completed = renderer->GetTextureTasksCompleted();
    if (scheduled > 0 && completed < scheduled) {
      ImGui::Separator();
      float frac = scheduled ? (float) completed / (float) scheduled : 1.0f;
      ImGui::Text("Loading textures: %u / %u", completed, scheduled);
      ImGui::ProgressBar(frac, ImVec2(-FLT_MIN, 0.0f));
      ImGui::Text("You can continue interacting while textures stream in...");
    }
  }

  ImGui::End();
}

void ImGuiSystem::Render(vk::raii::CommandBuffer& commandBuffer, uint32_t frameIndex) {
  if (!initialized) {
    return;
  }

  // End the frame and prepare for rendering
  ImGui::Render();

  // Update vertex and index buffers for this frame
  updateBuffers(frameIndex);

  // Record rendering commands
  ImDrawData* drawData = ImGui::GetDrawData();
  if (!drawData || drawData->CmdListsCount == 0) {
    return;
  }

  // Process dynamic texture updates (v1.92+ RendererHasTextures protocol)
  if (drawData->Textures) {
    for (int n = 0; n < drawData->Textures->Size; n++) {
      ImTextureData* tex = (*drawData->Textures)[n];
      if (tex->Status != ImTextureStatus_OK) {
        UpdateTexture(tex);
      }
    }
  }

  try {
    // Bind the pipeline
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);

    // Set viewport
    vk::Viewport viewport;
    viewport.width = ImGui::GetIO().DisplaySize.x;
    viewport.height = ImGui::GetIO().DisplaySize.y;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    commandBuffer.setViewport(0, {viewport});

    // Set push constants
    struct PushConstBlock {
      float scale[2];
      float translate[2];
    } pushConstBlock{};

    pushConstBlock.scale[0] = 2.0f / ImGui::GetIO().DisplaySize.x;
    pushConstBlock.scale[1] = 2.0f / ImGui::GetIO().DisplaySize.y;
    pushConstBlock.translate[0] = -1.0f;
    pushConstBlock.translate[1] = -1.0f;

    commandBuffer.pushConstants<PushConstBlock>(*pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, pushConstBlock);

    // Bind vertex and index buffers for this frame
    commandBuffer.bindVertexBuffers(0, *vertexBuffers[frameIndex], vk::DeviceSize{0});
    commandBuffer.bindIndexBuffer(*indexBuffers[frameIndex], 0, vk::IndexType::eUint16);

    // Render command lists
    int vertexOffset = 0;
    int indexOffset = 0;

    for (int i = 0; i < drawData->CmdLists.Size; i++) {
      const ImDrawList* cmdList = drawData->CmdLists[i];

      for (int j = 0; j < cmdList->CmdBuffer.Size; j++) {
        const ImDrawCmd* pcmd = &cmdList->CmdBuffer[j];

        // Set scissor rectangle
        vk::Rect2D scissor;
        scissor.offset.x = std::max(static_cast<int32_t>(pcmd->ClipRect.x), 0);
        scissor.offset.y = std::max(static_cast<int32_t>(pcmd->ClipRect.y), 0);
        scissor.extent.width = static_cast<uint32_t>(pcmd->ClipRect.z - pcmd->ClipRect.x);
        scissor.extent.height = static_cast<uint32_t>(pcmd->ClipRect.w - pcmd->ClipRect.y);
        commandBuffer.setScissor(0, {scissor});

        // Bind descriptor set (v1.92+ protocol: TexID stores the descriptor set handle or ID)
        VkDescriptorSet texHandle = (VkDescriptorSet)pcmd->GetTexID();
        if (texHandle) {
          commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0, {vk::DescriptorSet(texHandle)}, {});
        } else {
          commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0, {*descriptorSet}, {});
        }

        // Draw
        commandBuffer.drawIndexed(pcmd->ElemCount, 1, indexOffset, vertexOffset, 0);
        indexOffset += pcmd->ElemCount;
      }

      vertexOffset += cmdList->VtxBuffer.Size;
    }
  } catch (const std::exception& e) {
    std::cerr << "Failed to render ImGui: " << e.what() << std::endl;
  }
}

void ImGuiSystem::HandleMouse(float x, float y, uint32_t buttons) {
  if (!initialized) {
    return;
  }

  ImGuiIO& io = ImGui::GetIO();

  // Update mouse position (v1.87+ event API)
  io.AddMousePosEvent(x, y);

  // Update mouse buttons (v1.87+ event API)
  // We compare with current state to send events only on change
  static uint32_t lastButtons = 0;
  if ((buttons & 0x01) != (lastButtons & 0x01)) {
    io.AddMouseButtonEvent(0, (buttons & 0x01) != 0);
  }
  if ((buttons & 0x02) != (lastButtons & 0x02)) {
    io.AddMouseButtonEvent(1, (buttons & 0x02) != 0);
  }
  if ((buttons & 0x04) != (lastButtons & 0x04)) {
    io.AddMouseButtonEvent(2, (buttons & 0x04) != 0);
  }

  lastButtons = buttons;
}

void ImGuiSystem::HandleScroll(float yOffset) {
  if (!initialized) {
    return;
  }
  ImGui::GetIO().AddMouseWheelEvent(0.0f, yOffset);
}

void ImGuiSystem::HandleKeyboard(uint32_t key, bool pressed) {
  if (!initialized) {
    return;
  }

  ImGuiIO& io = ImGui::GetIO();

  const ImGuiKey imguiKey = GlfwKeyToImGuiKey(key);
  if (imguiKey != ImGuiKey_None) {
    io.AddKeyEvent(imguiKey, pressed);
  }

  switch (key) {
    case GLFW_KEY_LEFT_CONTROL:
    case GLFW_KEY_RIGHT_CONTROL: io.AddKeyEvent(ImGuiMod_Ctrl, pressed); break;
    case GLFW_KEY_LEFT_SHIFT:
    case GLFW_KEY_RIGHT_SHIFT: io.AddKeyEvent(ImGuiMod_Shift, pressed); break;
    case GLFW_KEY_LEFT_ALT:
    case GLFW_KEY_RIGHT_ALT: io.AddKeyEvent(ImGuiMod_Alt, pressed); break;
    case GLFW_KEY_LEFT_SUPER:
    case GLFW_KEY_RIGHT_SUPER: io.AddKeyEvent(ImGuiMod_Super, pressed); break;
    default: break;
  }
}

void ImGuiSystem::HandleChar(uint32_t c) {
  if (!initialized) {
    return;
  }

  ImGuiIO& io = ImGui::GetIO();
  io.AddInputCharacter(c);
}

void ImGuiSystem::HandleResize(uint32_t width, uint32_t height) {
  if (!initialized) {
    return;
  }

  this->width = width;
  this->height = height;

  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
}

bool ImGuiSystem::WantCaptureKeyboard() const {
  if (!initialized) {
    return false;
  }

  return ImGui::GetIO().WantCaptureKeyboard;
}

bool ImGuiSystem::WantCaptureMouse() const {
  if (!initialized) {
    return false;
  }

  return ImGui::GetIO().WantCaptureMouse;
}

bool ImGuiSystem::createResources() {
  // Create all Vulkan resources needed for ImGui rendering
  // (Font texture is now handled dynamically via UpdateTexture during Render)

  if (!createDescriptorSetLayout()) {
    return false;
  }

  if (!createDescriptorPool()) {
    return false;
  }

  if (!createDescriptorSet()) {
    return false;
  }

  if (!createPipelineLayout()) {
    return false;
  }

  if (!createPipeline()) {
    return false;
  }

  return true;
}

void ImGuiSystem::UpdateTexture(ImTextureData* tex) {
  if (tex->Status == ImTextureStatus_WantCreate || tex->Status == ImTextureStatus_WantUpdates) {
    int texWidth = tex->Width;
    int texHeight = tex->Height;
    unsigned char* fontData = (unsigned char*)tex->Pixels;

    if (!fontData) return;

    vk::DeviceSize uploadSize = texWidth * texHeight * tex->BytesPerPixel;

    try {
      const vk::raii::Device& device = renderer->GetRaiiDevice();

      if (tex->Status == ImTextureStatus_WantCreate) {
        // Create the font image
        vk::ImageCreateInfo imageInfo;
        imageInfo.imageType = vk::ImageType::e2D;
        imageInfo.format = (tex->BytesPerPixel == 4) ? vk::Format::eR8G8B8A8Unorm : vk::Format::eR8Unorm;
        imageInfo.extent.width = static_cast<uint32_t>(texWidth);
        imageInfo.extent.height = static_cast<uint32_t>(texHeight);
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = vk::SampleCountFlagBits::e1;
        imageInfo.tiling = vk::ImageTiling::eOptimal;
        imageInfo.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
        imageInfo.sharingMode = vk::SharingMode::eExclusive;
        imageInfo.initialLayout = vk::ImageLayout::eUndefined;

        fontImage = vk::raii::Image(device, imageInfo);

        // Allocate memory for the image
        vk::MemoryRequirements memRequirements = fontImage.getMemoryRequirements();
        vk::MemoryAllocateInfo allocInfo;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = renderer->FindMemoryType(memRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);

        fontMemory = vk::raii::DeviceMemory(device, allocInfo);
        fontImage.bindMemory(*fontMemory, 0);

        // Create image view
        vk::ImageViewCreateInfo viewInfo;
        viewInfo.image = *fontImage;
        viewInfo.viewType = vk::ImageViewType::e2D;
        viewInfo.format = (tex->BytesPerPixel == 4) ? vk::Format::eR8G8B8A8Unorm : vk::Format::eR8Unorm;
        viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        fontView = vk::raii::ImageView(device, viewInfo);

        // Create sampler
        vk::SamplerCreateInfo samplerInfo;
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = vk::CompareOp::eAlways;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;
        samplerInfo.borderColor = vk::BorderColor::eFloatOpaqueWhite;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;

        fontSampler = vk::raii::Sampler(device, samplerInfo);

        // Update descriptor set
        vk::DescriptorImageInfo imageDescriptor;
        imageDescriptor.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        imageDescriptor.imageView = *fontView;
        imageDescriptor.sampler = *fontSampler;

        vk::WriteDescriptorSet writeSet;
        writeSet.dstSet = *descriptorSet;
        writeSet.descriptorCount = 1;
        writeSet.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writeSet.pImageInfo = &imageDescriptor;
        writeSet.dstBinding = 0;

        device.updateDescriptorSets({writeSet}, {});
      }

      // Create a staging buffer for uploading the data
      vk::BufferCreateInfo bufferInfo;
      bufferInfo.size = uploadSize;
      bufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
      bufferInfo.sharingMode = vk::SharingMode::eExclusive;

      vk::raii::Buffer stagingBuffer(device, bufferInfo);
      vk::MemoryRequirements stagingMemRequirements = stagingBuffer.getMemoryRequirements();

      vk::MemoryAllocateInfo stagingAllocInfo;
      stagingAllocInfo.allocationSize = stagingMemRequirements.size;
      stagingAllocInfo.memoryTypeIndex = renderer->FindMemoryType(stagingMemRequirements.memoryTypeBits,
                                                                  vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

      vk::raii::DeviceMemory stagingBufferMemory(device, stagingAllocInfo);
      stagingBuffer.bindMemory(*stagingBufferMemory, 0);

      // Copy data to staging buffer
      void* mappedData = stagingBufferMemory.mapMemory(0, uploadSize);
      memcpy(mappedData, fontData, uploadSize);
      stagingBufferMemory.unmapMemory();

      // Transition image layout and copy data
      vk::Format format = (tex->BytesPerPixel == 4) ? vk::Format::eR8G8B8A8Unorm : vk::Format::eR8Unorm;
      renderer->TransitionImageLayout(*fontImage,
                                      format,
                                      vk::ImageLayout::eUndefined,
                                      vk::ImageLayout::eTransferDstOptimal);
      renderer->CopyBufferToImage(*stagingBuffer,
                                  *fontImage,
                                  static_cast<uint32_t>(texWidth),
                                  static_cast<uint32_t>(texHeight));
      renderer->TransitionImageLayout(*fontImage,
                                      format,
                                      vk::ImageLayout::eTransferDstOptimal,
                                      vk::ImageLayout::eShaderReadOnlyOptimal);

      // Store descriptor set handle as the ImTextureID
      tex->SetTexID((ImTextureID)(intptr_t)(VkDescriptorSet)*descriptorSet);
      tex->SetStatus(ImTextureStatus_OK);

    } catch (const std::exception& e) {
      std::cerr << "Failed to update ImGui texture: " << e.what() << std::endl;
    }
  }
}

bool ImGuiSystem::createDescriptorSetLayout() {
  try {
    vk::DescriptorSetLayoutBinding binding;
    binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    binding.descriptorCount = 1;
    binding.stageFlags = vk::ShaderStageFlagBits::eFragment;
    binding.binding = 0;

    vk::DescriptorSetLayoutCreateInfo layoutInfo;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;

    const vk::raii::Device& device = renderer->GetRaiiDevice();
    descriptorSetLayout = vk::raii::DescriptorSetLayout(device, layoutInfo);

    return true;
  } catch (const std::exception& e) {
    std::cerr << "Failed to create descriptor set layout: " << e.what() << std::endl;
    return false;
  }
}

bool ImGuiSystem::createDescriptorPool() {
  try {
    vk::DescriptorPoolSize poolSize;
    poolSize.type = vk::DescriptorType::eCombinedImageSampler;
    poolSize.descriptorCount = 1;

    vk::DescriptorPoolCreateInfo poolInfo;
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;

    const vk::raii::Device& device = renderer->GetRaiiDevice();
    descriptorPool = vk::raii::DescriptorPool(device, poolInfo);

    return true;
  } catch (const std::exception& e) {
    std::cerr << "Failed to create descriptor pool: " << e.what() << std::endl;
    return false;
  }
}

bool ImGuiSystem::createDescriptorSet() {
  try {
    vk::DescriptorSetAllocateInfo allocInfo;
    allocInfo.descriptorPool = *descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &(*descriptorSetLayout);

    const vk::raii::Device& device = renderer->GetRaiiDevice();
    vk::raii::DescriptorSets descriptorSets(device, allocInfo);
    descriptorSet = std::move(descriptorSets[0]); // Store the first (and only) descriptor set
    std::cout << "ImGui created descriptor set with handle: " << *descriptorSet << std::endl;

    return true;
  } catch (const std::exception& e) {
    std::cerr << "Failed to create descriptor set: " << e.what() << std::endl;
    return false;
  }
}

bool ImGuiSystem::createPipelineLayout() {
  try {
    // Push constant range for the transformation matrix
    vk::PushConstantRange pushConstantRange;
    pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(float) * 4; // 2 floats for scale, 2 floats for translate

    // Create pipeline layout
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &(*descriptorSetLayout);
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    const vk::raii::Device& device = renderer->GetRaiiDevice();
    pipelineLayout = vk::raii::PipelineLayout(device, pipelineLayoutInfo);

    return true;
  } catch (const std::exception& e) {
    std::cerr << "Failed to create pipeline layout: " << e.what() << std::endl;
    return false;
  }
}

bool ImGuiSystem::createPipeline() {
  try {
    // Load shaders
    vk::raii::ShaderModule shaderModule = renderer->CreateShaderModule("shaders/imgui.spv");

    // Shader stage creation
    vk::PipelineShaderStageCreateInfo vertShaderStageInfo;
    vertShaderStageInfo.stage = vk::ShaderStageFlagBits::eVertex;
    vertShaderStageInfo.module = *shaderModule;
    vertShaderStageInfo.pName = "VSMain";

    vk::PipelineShaderStageCreateInfo fragShaderStageInfo;
    fragShaderStageInfo.stage = vk::ShaderStageFlagBits::eFragment;
    fragShaderStageInfo.module = *shaderModule;
    fragShaderStageInfo.pName = "PSMain";

    std::array shaderStages = {vertShaderStageInfo, fragShaderStageInfo};

    // Vertex input
    vk::VertexInputBindingDescription bindingDescription;
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(ImDrawVert);
    bindingDescription.inputRate = vk::VertexInputRate::eVertex;

    std::array<vk::VertexInputAttributeDescription, 3> attributeDescriptions;
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = vk::Format::eR32G32Sfloat;
    attributeDescriptions[0].offset = offsetof(ImDrawVert, pos);

    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = vk::Format::eR32G32Sfloat;
    attributeDescriptions[1].offset = offsetof(ImDrawVert, uv);

    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = vk::Format::eR8G8B8A8Unorm;
    attributeDescriptions[2].offset = offsetof(ImDrawVert, col);

    vk::PipelineVertexInputStateCreateInfo vertexInputInfo;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    // Input assembly
    vk::PipelineInputAssemblyStateCreateInfo inputAssembly;
    inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport and scissor
    vk::PipelineViewportStateCreateInfo viewportState;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    viewportState.pViewports = nullptr; // Dynamic state
    viewportState.pScissors = nullptr; // Dynamic state

    // Rasterization
    vk::PipelineRasterizationStateCreateInfo rasterizer;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = vk::PolygonMode::eFill;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = vk::CullModeFlagBits::eNone;
    rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisampling
    vk::PipelineMultisampleStateCreateInfo multisampling;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

    // Depth and stencil testing
    vk::PipelineDepthStencilStateCreateInfo depthStencil;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = vk::CompareOp::eLessOrEqual;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Color blending
    vk::PipelineColorBlendAttachmentState colorBlendAttachment;
    colorBlendAttachment.colorWriteMask =
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
    colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
    colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;
    colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;

    vk::PipelineColorBlendStateCreateInfo colorBlending;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // Dynamic state
    std::vector<vk::DynamicState> dynamicStates = {
      vk::DynamicState::eViewport,
      vk::DynamicState::eScissor
    };

    vk::PipelineDynamicStateCreateInfo dynamicState;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    vk::Format depthFormat = renderer->findDepthFormat();
    // Create the graphics pipeline with dynamic rendering
    vk::PipelineRenderingCreateInfo renderingInfo;
    renderingInfo.colorAttachmentCount = 1;
    vk::Format colorFormat = renderer->GetSwapChainImageFormat(); // Get the actual swapchain format
    renderingInfo.pColorAttachmentFormats = &colorFormat;
    renderingInfo.depthAttachmentFormat = depthFormat;

    vk::GraphicsPipelineCreateInfo pipelineInfo;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = *pipelineLayout;
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.basePipelineHandle = nullptr;

    const vk::raii::Device& device = renderer->GetRaiiDevice();
    pipeline = vk::raii::Pipeline(device, nullptr, pipelineInfo);
    return true;
  } catch (const std::exception& e) {
    std::cerr << "Failed to create graphics pipeline: " << e.what() << std::endl;
    return false;
  }
}

void ImGuiSystem::updateBuffers(uint32_t frameIndex) {
  ImDrawData* drawData = ImGui::GetDrawData();
  if (!drawData || drawData->CmdListsCount == 0) {
    return;
  }

  try {
    const vk::raii::Device& device = renderer->GetRaiiDevice();

    // Calculate required buffer sizes
    vk::DeviceSize vertexBufferSize = drawData->TotalVtxCount * sizeof(ImDrawVert);
    vk::DeviceSize indexBufferSize = drawData->TotalIdxCount * sizeof(ImDrawIdx);

    // Resize buffers if needed for this frame
    if (frameIndex >= vertexCounts.size())
      return; // Safety

    if (static_cast<uint32_t>(drawData->TotalVtxCount) > vertexCounts[frameIndex]) {
      // Clean up old buffer
      vertexBuffers[frameIndex] = vk::raii::Buffer(nullptr);
      vertexBufferMemories[frameIndex] = vk::raii::DeviceMemory(nullptr);

      // Create new vertex buffer
      vk::BufferCreateInfo bufferInfo;
      bufferInfo.size = vertexBufferSize;
      bufferInfo.usage = vk::BufferUsageFlagBits::eVertexBuffer;
      bufferInfo.sharingMode = vk::SharingMode::eExclusive;

      vertexBuffers[frameIndex] = vk::raii::Buffer(device, bufferInfo);

      vk::MemoryRequirements memRequirements = vertexBuffers[frameIndex].getMemoryRequirements();

      vk::MemoryAllocateInfo allocInfo;
      allocInfo.allocationSize = memRequirements.size;
      allocInfo.memoryTypeIndex = renderer->FindMemoryType(memRequirements.memoryTypeBits,
                                                           vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

      vertexBufferMemories[frameIndex] = vk::raii::DeviceMemory(device, allocInfo);
      vertexBuffers[frameIndex].bindMemory(*vertexBufferMemories[frameIndex], 0);
      vertexCounts[frameIndex] = drawData->TotalVtxCount;
    }

    if (static_cast<uint32_t>(drawData->TotalIdxCount) > indexCounts[frameIndex]) {
      // Clean up old buffer
      indexBuffers[frameIndex] = vk::raii::Buffer(nullptr);
      indexBufferMemories[frameIndex] = vk::raii::DeviceMemory(nullptr);

      // Create new index buffer
      vk::BufferCreateInfo bufferInfo;
      bufferInfo.size = indexBufferSize;
      bufferInfo.usage = vk::BufferUsageFlagBits::eIndexBuffer;
      bufferInfo.sharingMode = vk::SharingMode::eExclusive;

      indexBuffers[frameIndex] = vk::raii::Buffer(device, bufferInfo);

      vk::MemoryRequirements memRequirements = indexBuffers[frameIndex].getMemoryRequirements();

      vk::MemoryAllocateInfo allocInfo;
      allocInfo.allocationSize = memRequirements.size;
      allocInfo.memoryTypeIndex = renderer->FindMemoryType(memRequirements.memoryTypeBits,
                                                           vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

      indexBufferMemories[frameIndex] = vk::raii::DeviceMemory(device, allocInfo);
      indexBuffers[frameIndex].bindMemory(*indexBufferMemories[frameIndex], 0);
      indexCounts[frameIndex] = drawData->TotalIdxCount;
    }

    // Upload data to buffers for this frame (only if we have data to upload)
    if (drawData->TotalVtxCount > 0 && drawData->TotalIdxCount > 0) {
      void* vtxMappedMemory = vertexBufferMemories[frameIndex].mapMemory(0, vertexBufferSize);
      void* idxMappedMemory = indexBufferMemories[frameIndex].mapMemory(0, indexBufferSize);

      ImDrawVert* vtxDst = static_cast<ImDrawVert *>(vtxMappedMemory);
      ImDrawIdx* idxDst = static_cast<ImDrawIdx *>(idxMappedMemory);

      for (int n = 0; n < drawData->CmdListsCount; n++) {
        const ImDrawList* cmdList = drawData->CmdLists[n];
        memcpy(vtxDst, cmdList->VtxBuffer.Data, cmdList->VtxBuffer.Size * sizeof(ImDrawVert));
        memcpy(idxDst, cmdList->IdxBuffer.Data, cmdList->IdxBuffer.Size * sizeof(ImDrawIdx));
        vtxDst += cmdList->VtxBuffer.Size;
        idxDst += cmdList->IdxBuffer.Size;
      }

      vertexBufferMemories[frameIndex].unmapMemory();
      indexBufferMemories[frameIndex].unmapMemory();
    }
  } catch (const std::exception& e) {
    std::cerr << "Failed to update buffers: " << e.what() << std::endl;
  }
}
