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
#include "engine.h"
#include "mesh_component.h"
#include "scene_loading.h"
#include "imgui/imgui.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <thread>

// This implementation corresponds to the Engine_Architecture chapter in the tutorial:
// @see en/Building_a_Simple_Engine/Engine_Architecture/02_architectural_patterns.adoc

namespace {
glm::vec3 TransformAabbCenterExtents(const glm::mat4& transform,
                                     const glm::vec3& localMin,
                                     const glm::vec3& localMax,
                                     glm::vec3& outExtents)
{
  const glm::vec3 localCenter = 0.5f * (localMin + localMax);
  const glm::vec3 localExtents = 0.5f * (localMax - localMin);
  const glm::vec3 worldCenter = glm::vec3(transform * glm::vec4(localCenter, 1.0f));
  const glm::mat3 linear = glm::mat3(transform);
  const glm::mat3 absLinear = glm::mat3(glm::abs(linear[0]), glm::abs(linear[1]), glm::abs(linear[2]));
  outExtents = absLinear * localExtents;
  return worldCenter;
}

bool SphereIntersectsAabb(const glm::vec3& center, float cameraRadius, const glm::vec3& boxMin, const glm::vec3& boxMax) {
  const glm::vec3 closest = glm::clamp(center, boxMin, boxMax);
  const glm::vec3 delta = center - closest;
  return glm::dot(delta, delta) < cameraRadius * cameraRadius;
}
} // namespace

Engine::Engine() : resourceManager(std::make_unique<ResourceManager>()) {
}

bool Engine::IsMainThread() const {
  return std::this_thread::get_id() == mainThreadId;
}

void Engine::ProcessPendingEntityRemovals() {
  std::vector<std::string> names; {
    std::lock_guard<std::mutex> lk(pendingEntityRemovalsMutex);
    if (pendingEntityRemovalNames.empty())
      return;
    names.swap(pendingEntityRemovalNames);
  }

  // Process on the main thread only (safety)
  if (!IsMainThread()) {
    // Put them back; we'll retry next main-thread tick
    std::lock_guard<std::mutex> lk(pendingEntityRemovalsMutex);
    pendingEntityRemovalNames.insert(pendingEntityRemovalNames.end(), names.begin(), names.end());
    return;
  }

  // Apply removals using the normal API (which takes the appropriate locks).
  for (const auto& name : names) {
    (void) RemoveEntity(name);
  }
}

Engine::~Engine() {
  Cleanup();
}

bool Engine::Initialize(const std::string& appName, int width, int height, bool enableValidationLayers) {
  // Create platform
  // Record main thread identity for deferring destructive operations from background threads
  mainThreadId = std::this_thread::get_id();

  platform = CreatePlatform();
  if (!platform->Initialize(appName, width, height)) {
    return false;
  }

  // Set resize callback
  platform->SetResizeCallback([this](int width, int height) {
    HandleResize(width, height);
  });

  // Set mouse callback
  platform->SetMouseCallback([this](float x, float y, uint32_t buttons) {
    handleMouseInput(x, y, buttons);
  });

  platform->SetScrollCallback([this](float yOffset) {
    handleScrollInput(yOffset);
  });

  // Set keyboard callback
  platform->SetKeyboardCallback([this](uint32_t key, bool pressed) {
    handleKeyInput(key, pressed);
  });

  // Set char callback
  platform->SetCharCallback([this](uint32_t c) {
    if (imguiSystem) {
      imguiSystem->HandleChar(c);
    }
  });

  // Create renderer
  renderer = std::make_unique<Renderer>(platform.get());
  if (!renderer->Initialize(appName, enableValidationLayers)) {
    return false;
  }

  try {
    // Model loader via constructor; also wire into renderer
    modelLoader = std::make_unique<ModelLoader>(renderer.get());
    renderer->SetModelLoader(modelLoader.get());

#ifdef ENABLE_COURSE_OPACITY_MICROMAPS
    // OMM integration via constructor
    ommIntegration = std::make_unique<OmmIntegration>();
    ommIntegration->init(*renderer, *modelLoader);
#endif

    // ImGui integration
    imguiSystem = std::make_unique<ImGuiSystem>(renderer.get(), width, height);
  } catch (const std::exception& e) {
    std::cerr << "Subsystem initialization failed: " << e.what() << std::endl;
    return false;
  }

  initialized = true;
  return true;
}

void Engine::Run() {
  if (!initialized) {
    throw std::runtime_error("Engine not initialized");
  }

  running = true;

  // Main loop
  while (running) {
    const auto frameStart = std::chrono::steady_clock::now();

    // Process platform events
    if (!platform->ProcessEvents()) {
      running = false;
      break;
    }

    // Calculate delta time
    deltaTimeMs = CalculateDeltaTimeMs();

    // Update frame counter and FPS
    frameCount++;
    fpsUpdateTimer += deltaTimeMs.count() * 0.001f;

    // Update window title with FPS and frame time every second
    if (fpsUpdateTimer >= 1.0f) {
      uint64_t framesSinceLastUpdate = frameCount - lastFPSUpdateFrame;
      double avgMs = 0.0;
      if (framesSinceLastUpdate > 0 && fpsUpdateTimer > 0.0f) {
        currentFPS = static_cast<float>(static_cast<double>(framesSinceLastUpdate) / static_cast<double>(fpsUpdateTimer));
        avgMs = (fpsUpdateTimer / static_cast<double>(framesSinceLastUpdate)) * 1000.0;
      } else {
        // Avoid divide-by-zero; keep previous FPS and estimate avgMs from last delta
        currentFPS = std::max(currentFPS, 1.0f);
        avgMs = static_cast<double>(deltaTimeMs.count());
      }

      // Update window title with frame count, FPS, and frame time
      std::string title = "Simple Engine - Frame: " + std::to_string(frameCount) +
          " | FPS: " + std::to_string(static_cast<int>(currentFPS)) +
          " | ms: " + std::to_string(static_cast<int>(avgMs));
      platform->SetWindowTitle(title);

      // Reset timer and frame counter for next update
      fpsUpdateTimer = 0.0f;
      lastFPSUpdateFrame = frameCount;
    }

    // Update
    Update(deltaTimeMs);

    // Render
    Render();

    constexpr auto targetFrameTime = std::chrono::microseconds(16667);
    const auto frameElapsed = std::chrono::steady_clock::now() - frameStart;
    if (frameElapsed < targetFrameTime) {
      std::this_thread::sleep_for(targetFrameTime - frameElapsed);
    }
  }
}

void Engine::Cleanup() {
  if (initialized) {
    // Wait for the device to be idle before cleaning up
    if (renderer) {
      renderer->WaitIdle();
    }

    // Clear entities
    {
      std::unique_lock<std::shared_mutex> lk(entitiesMutex);
      entities.clear();
      entityMap.clear();
    }

    // Clean up subsystems in reverse order of creation
    imguiSystem.reset();
#ifdef ENABLE_COURSE_OPACITY_MICROMAPS
    ommIntegration.reset();
#endif
    modelLoader.reset();
    renderer.reset();
    platform.reset();

    initialized = false;
  }
}

Entity* Engine::CreateEntity(const std::string& name) {
  std::unique_lock<std::shared_mutex> lk(entitiesMutex);
  // Always allow duplicate names; map stores a representative entity
  // Create the entity
  auto entity = std::make_unique<Entity>(name);
  // Add to the vector and map
  entities.push_back(std::move(entity));
  Entity* rawPtr = entities.back().get();
  // Update the map to point to the most recently created entity with this name
  entityMap[name] = rawPtr;

  return rawPtr;
}

Entity* Engine::GetEntity(const std::string& name) {
  std::shared_lock<std::shared_mutex> lk(entitiesMutex);
  auto it = entityMap.find(name);
  if (it != entityMap.end()) {
    return it->second;
  }
  return nullptr;
}

bool Engine::RemoveEntity(Entity* entity) {
  if (!entity) {
    return false;
  }

  // If called from a background thread, defer removal to avoid deleting entities
  // while the render thread may be iterating a snapshot.
  if (!IsMainThread()) {
    std::lock_guard<std::mutex> lk(pendingEntityRemovalsMutex);
    pendingEntityRemovalNames.push_back(entity->GetName());
    return true;
  }

  std::unique_lock<std::shared_mutex> lk(entitiesMutex);

  // Remember the name before erasing ownership
  std::string name = entity->GetName();

  // Find the entity in the vector
  auto it = std::ranges::find_if(entities,
                                 [entity](const std::unique_ptr<Entity>& e) {
                                   return e.get() == entity;
                                 });

  if (it != entities.end()) {
    // Remove from the vector (ownership)
    entities.erase(it);

    // Update the map: point to another entity with the same name if one exists
    auto remainingIt = std::ranges::find_if(entities,
                                            [&name](const std::unique_ptr<Entity>& e) {
                                              return e->GetName() == name;
                                            });

    if (remainingIt != entities.end()) {
      entityMap[name] = remainingIt->get();
    } else {
      entityMap.erase(name);
    }

    return true;
  }

  return false;
}

bool Engine::RemoveEntity(const std::string& name) {
  // If called from a background thread, defer removal to avoid deleting entities
  // while the render thread may be iterating a snapshot.
  if (!IsMainThread()) {
    std::lock_guard<std::mutex> lk(pendingEntityRemovalsMutex);
    pendingEntityRemovalNames.push_back(name);
    return true;
  }

  std::unique_lock<std::shared_mutex> lk(entitiesMutex);
  auto it = entityMap.find(name);
  if (it == entityMap.end())
    return false;
  Entity* entity = it->second;
  if (!entity)
    return false;

  // Find the entity in the vector
  auto vecIt = std::ranges::find_if(entities,
                                    [entity](const std::unique_ptr<Entity>& e) {
                                      return e.get() == entity;
                                    });
  if (vecIt == entities.end()) {
    entityMap.erase(name);
    return false;
  }

  entities.erase(vecIt);

  // Update the map: point to another entity with the same name if one exists
  auto remainingIt = std::ranges::find_if(entities,
                                          [&name](const std::unique_ptr<Entity>& e) {
                                            return e && e->GetName() == name;
                                          });
  if (remainingIt != entities.end()) {
    entityMap[name] = remainingIt->get();
  } else {
    entityMap.erase(name);
  }
  return true;
}

void Engine::SetActiveCamera(CameraComponent* cameraComponent) {
  activeCamera = cameraComponent;
}

const CameraComponent* Engine::GetActiveCamera() const {
  return activeCamera;
}

const ResourceManager* Engine::GetResourceManager() const {
  return resourceManager.get();
}

const Platform* Engine::GetPlatform() const {
  return platform.get();
}

Renderer* Engine::GetRenderer() {
  return renderer.get();
}

ModelLoader* Engine::GetModelLoader() {
  return modelLoader.get();
}

#ifdef ENABLE_COURSE_OPACITY_MICROMAPS
OmmIntegration* Engine::GetOmmIntegration() {
  return ommIntegration.get();
}
#endif

const ImGuiSystem* Engine::GetImGuiSystem() const {
  return imguiSystem.get();
}

void Engine::handleMouseInput(float x, float y, uint32_t buttons) {
  // Update ImGui system with current mouse state immediately.
  // This pushes events to the ImGui IO queue for processing in NewFrame().
  if (imguiSystem) {
    imguiSystem->HandleMouse(x, y, buttons);
  }

  if (cameraControl.cameraFixed) {
    cameraControl.mouseLeftPressed = false;
    cameraControl.mouseRightPressed = false;
    cameraControl.firstMouse = true;
    cameraControl.firstRightMouse = true;
    cameraControl.pendingXOffset = 0.0f;
    cameraControl.pendingYOffset = 0.0f;
    cameraControl.pendingPanX = 0.0f;
    cameraControl.pendingPanY = 0.0f;
    HandleMouseHover(x, y);
    return;
  }

  // Handle LEFT button (Touch DOWN/MOVE/UP)
  if (buttons & 1) {
    if (!cameraControl.mouseLeftPressed) {
      // Finger just went down
      cameraControl.mouseLeftPressed = true;
      cameraControl.firstMouse = true;
    }

    if (cameraControl.firstMouse) {
      cameraControl.lastMouseX = x;
      cameraControl.lastMouseY = y;
      cameraControl.firstMouse = false;
    }

    // Accumulate movement deltas. These will be applied in UpdateCameraControls
    // AFTER ImGui has updated its capture state (post-NewFrame).
    float dx = (x - cameraControl.lastMouseX);
    float dy = (y - cameraControl.lastMouseY);
    cameraControl.pendingXOffset += dx;
    cameraControl.pendingYOffset += dy;

    cameraControl.lastMouseX = x;
    cameraControl.lastMouseY = y;
  } else {
    // Finger lifted
    cameraControl.mouseLeftPressed = false;
  }

  // Right-button drag pans the camera in its local right/up plane.
  if (buttons & 2) {
    if (!cameraControl.mouseRightPressed) {
      cameraControl.mouseRightPressed = true;
      cameraControl.firstRightMouse = true;
    }

    if (cameraControl.firstRightMouse) {
      cameraControl.lastRightMouseX = x;
      cameraControl.lastRightMouseY = y;
      cameraControl.firstRightMouse = false;
    }

    cameraControl.pendingPanX += x - cameraControl.lastRightMouseX;
    cameraControl.pendingPanY += y - cameraControl.lastRightMouseY;
    cameraControl.lastRightMouseX = x;
    cameraControl.lastRightMouseY = y;
  } else {
    cameraControl.mouseRightPressed = false;
  }

  // Update hover detection
  HandleMouseHover(x, y);
}

void Engine::handleScrollInput(float yOffset) {
  if (imguiSystem) {
    imguiSystem->HandleScroll(yOffset);
  }
  if (cameraControl.cameraFixed) {
    cameraControl.pendingScrollDelta = 0.0f;
    return;
  }
  cameraControl.pendingScrollDelta += yOffset;
}

void Engine::handleKeyInput(uint32_t key, bool pressed) {
  if (key == GLFW_KEY_U) {
    if (pressed && !uiToggleKeyDown && imguiSystem) {
      imguiSystem->ToggleVisible();
    }
    uiToggleKeyDown = pressed;
    return;
  }

  switch (key) {
    case GLFW_KEY_W:
    case GLFW_KEY_UP:
      cameraControl.moveForward = cameraControl.cameraFixed ? false : pressed;
      break;
    case GLFW_KEY_S:
    case GLFW_KEY_DOWN:
      cameraControl.moveBackward = cameraControl.cameraFixed ? false : pressed;
      break;
    case GLFW_KEY_A:
    case GLFW_KEY_LEFT:
      cameraControl.moveLeft = cameraControl.cameraFixed ? false : pressed;
      break;
    case GLFW_KEY_D:
    case GLFW_KEY_RIGHT:
      cameraControl.moveRight = cameraControl.cameraFixed ? false : pressed;
      break;
    case GLFW_KEY_Q:
    case GLFW_KEY_PAGE_UP:
    case GLFW_KEY_SPACE:
      cameraControl.moveUp = cameraControl.cameraFixed ? false : pressed;
      break;
    case GLFW_KEY_E:
    case GLFW_KEY_PAGE_DOWN:
    case GLFW_KEY_LEFT_SHIFT:
      cameraControl.moveDown = cameraControl.cameraFixed ? false : pressed;
      break;
    default:
      break;
  }

  if (imguiSystem) {
    imguiSystem->HandleKeyboard(key, pressed);
  }
}

void Engine::Update(TimeDelta deltaTime) {
  // Apply any entity removals requested by background threads.
  ProcessPendingEntityRemovals();

  // During background scene loading we avoid touching the live entity
  // list from the main thread. This lets the loading thread construct
  // entities/components safely while the main thread only drives the
  // UI/loading overlay.
  if (renderer && renderer->IsLoading()) {
    if (imguiSystem) {
      uint32_t rw, rh;
      renderer->GetSwapChainExtent(&rw, &rh);
      if (rw > 0 && rh > 0) {
        imguiSystem->HandleResize(rw, rh);
      }
      imguiSystem->NewFrame();
    }
    return;
  }

  // Update ImGui system
  if (imguiSystem) {
    uint32_t rw, rh;
    renderer->GetSwapChainExtent(&rw, &rh);
    if (rw > 0 && rh > 0) {
      imguiSystem->HandleResize(rw, rh);
    }
    imguiSystem->NewFrame();
  }

  if (!imguiSystem || imguiSystem->IsVisible()) {
    DrawCameraControlPanel();
  }

  // Update camera controls
  if (activeCamera) {
    UpdateCameraControls(deltaTime);
  }

  // Update all entities.
  // Do not hold `entitiesMutex` while calling `Entity::Update()`.
  // Background threads may need the unique lock to add entities during loading,
  // and holding a shared lock for a long time can starve them.
  std::vector<Entity *> snapshot; {
    std::shared_lock<std::shared_mutex> lk(entitiesMutex);
    snapshot.reserve(entities.size());
    for (auto& uptr : entities) {
      snapshot.push_back(uptr.get());
    }
  }
  for (Entity* entity : snapshot) {
    if (!entity || !entity->IsActive())
      continue;
    entity->Update(deltaTime);
  }
}

void Engine::Render() {
  // Ensure renderer is ready
  if (!renderer || !renderer->IsInitialized()) {
    return;
  }

  // Check if we have an active camera
  if (!activeCamera) {
    return;
  }

  // Apply any entity removals requested by background threads before taking a snapshot.
  ProcessPendingEntityRemovals();

  // Snapshot entity pointers under a short shared lock, then release the lock
  // before rendering. This prevents starving the background loader thread
  // that need the unique lock to create entities/components.
  std::vector<Entity *> snapshot; {
    std::shared_lock<std::shared_mutex> lk(entitiesMutex);
    snapshot.reserve(entities.size());
    for (auto& uptr : entities) {
      snapshot.push_back(uptr.get());
    }
  }

  // Render the scene (ImGui will be rendered within the render pass)
  renderer->Render(snapshot, activeCamera, imguiSystem.get());
}

std::chrono::milliseconds Engine::CalculateDeltaTimeMs() {
  // Get current time using a steady clock to avoid system time jumps
  uint64_t currentTime = static_cast<uint64_t>(
    std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch())
    .count());

  // Initialize lastFrameTimeMs on first call
  if (lastFrameTimeMs == 0) {
    lastFrameTimeMs = currentTime;
    return std::chrono::milliseconds(16); // ~16ms as a sane initial guess
  }

  // Calculate delta time in milliseconds
  uint64_t delta = currentTime - lastFrameTimeMs;

  // Update last frame time
  lastFrameTimeMs = currentTime;

  return std::chrono::milliseconds(static_cast<long long>(delta));
}

void Engine::HandleResize(int width, int height) const {
  if (height <= 0 || width <= 0) {
    return;
  }
  LOGI("Engine: HandleResize %dx%d", width, height);

  // Update the active camera's aspect ratio
  if (activeCamera) {
    activeCamera->SetAspectRatio(static_cast<float>(width) / static_cast<float>(height));
  }

  // Notify the renderer that the framebuffer has been resized
  if (renderer) {
    renderer->SetFramebufferResized();
  }

  // Notify ImGui system about the resize
  if (imguiSystem) {
    imguiSystem->HandleResize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
  }
}

void Engine::UpdateCameraControls(TimeDelta deltaTime) {
  if (!activeCamera)
    return;

  // Get a camera transform component
  auto* cameraTransform = activeCamera->GetOwner()->GetComponent<TransformComponent>();
  if (!cameraTransform)
    return;

  if (cameraControl.cameraFixed) {
    cameraControl.moveForward = false;
    cameraControl.moveBackward = false;
    cameraControl.moveLeft = false;
    cameraControl.moveRight = false;
    cameraControl.moveUp = false;
    cameraControl.moveDown = false;
    cameraControl.pendingXOffset = 0.0f;
    cameraControl.pendingYOffset = 0.0f;
    cameraControl.pendingPanX = 0.0f;
    cameraControl.pendingPanY = 0.0f;
    cameraControl.pendingScrollDelta = 0.0f;
    cameraControl.mouseLeftPressed = false;
    cameraControl.mouseRightPressed = false;
    cameraControl.firstMouse = true;
    cameraControl.firstRightMouse = true;
    return;
  }

  // Calculate movement speed
  float velocity = cameraControl.cameraSpeed * deltaTime.count() * .001f;

  // Check if ImGui wants to capture mouse input (updated in NewFrame)
  bool imguiWantsMouse = imguiSystem && imguiSystem->WantCaptureMouse();

  // INTERACTION LOCKING LOGIC:
  // If a touch began, we wait until ImGui has processed the first DOWN event (in NewFrame)
  // before deciding whether this drag belongs to the GUI or the 3D Scene.
  if (cameraControl.mouseLeftPressed) {
    if (cameraControl.isFirstFrameOfInteraction) {
      // This is the first frame (Update call) where the finger is DOWN.
      // ImGui's WantCaptureMouse now accurately reflects if the tap was on a window.
      cameraControl.startedOnImGui = imguiWantsMouse;
      cameraControl.isFirstFrameOfInteraction = false;
    }

    // Only apply rotation if the interaction started on the scene background
    if (!cameraControl.startedOnImGui) {
      float xOffset = cameraControl.pendingXOffset * cameraControl.mouseSensitivity;
      float yOffset = cameraControl.pendingYOffset * cameraControl.mouseSensitivity;

      cameraControl.yaw -= xOffset;
      cameraControl.pitch -= yOffset;
    }
  } else {
    // Reset locking state when finger is lifted
    cameraControl.isFirstFrameOfInteraction = true;
    cameraControl.startedOnImGui = false;
  }

  if (cameraControl.mouseRightPressed) {
    if (cameraControl.isFirstFrameOfRightInteraction) {
      cameraControl.rightStartedOnImGui = imguiWantsMouse;
      cameraControl.isFirstFrameOfRightInteraction = false;
    }
  } else {
    cameraControl.isFirstFrameOfRightInteraction = true;
    cameraControl.rightStartedOnImGui = false;
  }

  // Constrain pitch to avoid gimbal lock
  if (cameraControl.pitch > 89.0f)
    cameraControl.pitch = 89.0f;
  if (cameraControl.pitch < -89.0f)
    cameraControl.pitch = -89.0f;

  // Clear accumulated offsets after processing
  cameraControl.pendingXOffset = 0.0f;
  cameraControl.pendingYOffset = 0.0f;

  // Capture base orientation from GLTF camera once and then apply mouse deltas relative to it
  if (!cameraControl.baseOrientationCaptured) {
    // TransformComponent stores Euler in radians; convert to quaternion
    glm::vec3 baseEuler = cameraTransform->GetRotation();
    const glm::quat qx = glm::angleAxis(baseEuler.x, glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::quat qy = glm::angleAxis(baseEuler.y, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::quat qz = glm::angleAxis(baseEuler.z, glm::vec3(0.0f, 0.0f, 1.0f));
    // Match CameraComponent::UpdateViewMatrix composition (q = qz * qy * qx)
    cameraControl.baseOrientation = qz * qy * qx;
    cameraControl.baseOrientationCaptured = true;
  }

  // Build delta orientation from yaw/pitch mouse deltas (degrees -> radians)
  const float yawRad = glm::radians(cameraControl.yaw);
  const float pitchRad = glm::radians(cameraControl.pitch);
  const glm::quat qDeltaY = glm::angleAxis(yawRad, glm::vec3(0.0f, 1.0f, 0.0f));
  const glm::quat qDeltaX = glm::angleAxis(pitchRad, glm::vec3(1.0f, 0.0f, 0.0f));
  // Apply yaw then pitch in the same convention as CameraComponent (ZYX overall), so delta = Ry * Rx
  glm::quat qDelta = qDeltaY * qDeltaX;
  glm::quat qFinal = cameraControl.baseOrientation * qDelta;

  // Derive camera basis directly from rotated axes to avoid ambiguity
  glm::vec3 right = glm::normalize(qFinal * glm::vec3(1.0f, 0.0f, 0.0f));
  glm::vec3 up = glm::normalize(qFinal * glm::vec3(0.0f, 1.0f, 0.0f));
  // Camera forward in world space.
  // Our view/projection conventions assume the camera looks down -Z in its local space.
  glm::vec3 front = glm::normalize(qFinal * glm::vec3(0.0f, 0.0f, -1.0f));

  // Get the current camera position
  glm::vec3 position = cameraTransform->GetPosition();

  // Apply movement based on input
  if (cameraControl.moveForward) {
    position += front * velocity;
  }
  if (cameraControl.moveBackward) {
    position -= front * velocity;
  }
  if (cameraControl.moveLeft) {
    position -= right * velocity;
  }
  if (cameraControl.moveRight) {
    position += right * velocity;
  }
  if (cameraControl.moveUp) {
    position += up * velocity;
  }
  if (cameraControl.moveDown) {
    position -= up * velocity;
  }

  // Wheel up/down performs a discrete forward/backward dolly.
  if (!imguiWantsMouse && cameraControl.pendingScrollDelta != 0.0f) {
    position += front * cameraControl.pendingScrollDelta * cameraControl.scrollStep;
  }

  // Right-button drag pans without changing the camera orientation.
  if (cameraControl.mouseRightPressed && !cameraControl.rightStartedOnImGui) {
    position += right * cameraControl.pendingPanX * cameraControl.panSensitivity;
    position -= up * cameraControl.pendingPanY * cameraControl.panSensitivity;
  }

  cameraControl.pendingPanX = 0.0f;
  cameraControl.pendingPanY = 0.0f;
  cameraControl.pendingScrollDelta = 0.0f;

  position = ResolveCameraCollision(cameraTransform->GetPosition(), position);

  // Update camera position
  cameraTransform->SetPosition(position);
  // Apply rotation to the camera transform based on GLTF base orientation plus mouse deltas
  // TransformComponent expects radians Euler (ZYX order in our CameraComponent).
  cameraTransform->SetRotation(glm::eulerAngles(qFinal));

  // Update camera target based on a direction
  glm::vec3 target = position + front;
  activeCamera->SetTarget(target);

  // Ensure the camera view matrix reflects the new transform immediately this frame
  activeCamera->ForceViewMatrixUpdate();
}

void Engine::DrawCameraControlPanel() {
  if (!imguiSystem || !activeCamera)
    return;

  auto* cameraTransform = activeCamera->GetOwner()->GetComponent<TransformComponent>();
  if (!cameraTransform)
    return;

  cameraControl.uiFontScale = glm::clamp(cameraControl.uiFontScale, 0.75f, 2.5f);
  ImGui::GetIO().FontGlobalScale = cameraControl.uiFontScale;

  ImGui::Begin("Camera");

  glm::vec3 position = cameraTransform->GetPosition();
  ImGui::Text("Position: X %.3f  Y %.3f  Z %.3f", position.x, position.y, position.z);

  float editablePosition[3] = {position.x, position.y, position.z};
  if (ImGui::InputFloat3("Set Position", editablePosition, "%.3f")) {
    const glm::vec3 requestedPosition(editablePosition[0], editablePosition[1], editablePosition[2]);
    if (!cameraControl.collisionEnabled || !IsCameraPositionBlocked(requestedPosition)) {
      cameraTransform->SetPosition(requestedPosition);
      cameraControl.lastCollisionBlocked = false;
      if (activeCamera) {
        activeCamera->ForceViewMatrixUpdate();
      }
    } else {
      cameraControl.lastCollisionBlocked = true;
    }
  }

  ImGui::Separator();
  if (activeCamera->GetProjectionType() == CameraComponent::ProjectionType::Perspective) {
    float fov = activeCamera->GetFieldOfView();
    if (ImGui::SliderFloat("FOV (degrees)", &fov, 1.0f, 179.0f, "%.1f")) {
      activeCamera->SetFieldOfView(glm::clamp(fov, 1.0f, 179.0f));
    }
  } else {
    ImGui::TextUnformatted("FOV: unavailable in orthographic projection");
  }

  ImGui::Separator();
  ImGui::SliderFloat("Move Speed", &cameraControl.cameraSpeed, 0.1f, 50.0f, "%.2f");
  ImGui::SliderFloat("Mouse Sensitivity", &cameraControl.mouseSensitivity, 0.01f, 1.0f, "%.2f");
  ImGui::SliderFloat("Wheel Step", &cameraControl.scrollStep, 0.05f, 5.0f, "%.2f");
  ImGui::SliderFloat("Right-drag Pan", &cameraControl.panSensitivity, 0.0005f, 0.05f, "%.4f");

  ImGui::Separator();
  if (ImGui::SliderFloat("UI Font Scale", &cameraControl.uiFontScale, 0.75f, 2.5f, "%.2f")) {
    cameraControl.uiFontScale = glm::clamp(cameraControl.uiFontScale, 0.75f, 2.5f);
    ImGui::GetIO().FontGlobalScale = cameraControl.uiFontScale;
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset")) {
    cameraControl.uiFontScale = 1.3f;
    ImGui::GetIO().FontGlobalScale = cameraControl.uiFontScale;
  }

  ImGui::Separator();
  if (ImGui::Checkbox("Fixed Camera", &cameraControl.cameraFixed) && cameraControl.cameraFixed) {
    cameraControl.moveForward = false;
    cameraControl.moveBackward = false;
    cameraControl.moveLeft = false;
    cameraControl.moveRight = false;
    cameraControl.moveUp = false;
    cameraControl.moveDown = false;
    cameraControl.pendingXOffset = 0.0f;
    cameraControl.pendingYOffset = 0.0f;
    cameraControl.pendingPanX = 0.0f;
    cameraControl.pendingPanY = 0.0f;
    cameraControl.pendingScrollDelta = 0.0f;
    cameraControl.mouseLeftPressed = false;
    cameraControl.mouseRightPressed = false;
  }
  ImGui::Separator();
  ImGui::Checkbox("Enable Camera Collision", &cameraControl.collisionEnabled);
  ImGui::BeginDisabled(!cameraControl.collisionEnabled);
  ImGui::SliderFloat("Collision Radius (scene units)", &cameraControl.collisionRadius, 0.05f, 1.0f, "%.2f");
  ImGui::EndDisabled();
  ImGui::Text("Collision Status: %s",
              cameraControl.collisionEnabled
                ? (cameraControl.lastCollisionBlocked ? "blocked" : "clear")
                : "disabled");

  ImGui::Separator();
  ImGui::TextUnformatted("Controls:");
  ImGui::BulletText("W/S or Up/Down: forward/backward");
  ImGui::BulletText("A/D or Left/Right: strafe left/right");
  ImGui::BulletText("Space/Q/PageUp: move up");
  ImGui::BulletText("Left Shift/E/PageDown: move down");
  ImGui::BulletText("Left mouse drag: rotate camera");
  ImGui::BulletText("Right mouse drag: pan camera");
  ImGui::BulletText("Mouse wheel: forward/backward dolly");
  ImGui::BulletText("U: show/hide UI");

  ImGui::End();
}

glm::vec3 Engine::ResolveCameraCollision(const glm::vec3& currentPosition, const glm::vec3& desiredPosition) {
  cameraControl.lastCollisionBlocked = false;
  if (!cameraControl.collisionEnabled)
    return desiredPosition;

  if (!IsCameraPositionBlocked(desiredPosition))
    return desiredPosition;

  cameraControl.lastCollisionBlocked = true;

  // Try axis-separated movement so the camera can slide along walls/objects
  // instead of stopping completely when only one component is blocked.
  glm::vec3 resolved = currentPosition;
  for (int axis = 0; axis < 3; ++axis) {
    glm::vec3 candidate = resolved;
    candidate[axis] = desiredPosition[axis];
    if (!IsCameraPositionBlocked(candidate)) {
      resolved = candidate;
    }
  }

  return resolved;
}

bool Engine::IsCameraPositionBlocked(const glm::vec3& position) const {
  const float radius = std::max(cameraControl.collisionRadius, 0.05f);

  std::shared_lock<std::shared_mutex> lk(entitiesMutex);
  for (const auto& entityPtr : entities) {
    const Entity* entity = entityPtr.get();
    if (!entity || !entity->IsActive())
      continue;
    if (activeCamera && entity == activeCamera->GetOwner())
      continue;

    auto* mesh = entity->GetComponent<MeshComponent>();
    auto* transform = entity->GetComponent<TransformComponent>();
    if (!mesh || !transform || !mesh->HasLocalAABB())
      continue;

    glm::vec3 worldExtents(0.0f);
    const glm::vec3 worldCenter = TransformAabbCenterExtents(transform->GetModelMatrix(),
                                                            mesh->GetLocalAABBMin(),
                                                            mesh->GetLocalAABBMax(),
                                                            worldExtents);
    const glm::vec3 boxMin = worldCenter - worldExtents;
    const glm::vec3 boxMax = worldCenter + worldExtents;
    if (SphereIntersectsAabb(position, radius, boxMin, boxMax)) {
      return true;
    }
  }

  return false;
}

void Engine::HandleMouseHover(float mouseX, float mouseY) {
  // Update current mouse position for any systems that might need it
  currentMouseX = mouseX;
  currentMouseY = mouseY;
}
