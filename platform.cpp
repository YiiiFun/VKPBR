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
#include "platform.h"

#include <stdexcept>

// Desktop platform implementation

bool DesktopPlatform::Initialize(const std::string& appName, int requestedWidth, int requestedHeight) {
  if (!glfwInit()) {
    throw std::runtime_error("Failed to initialize GLFW");
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

  window = glfwCreateWindow(requestedWidth, requestedHeight, appName.c_str(), nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    throw std::runtime_error("Failed to create GLFW window");
  }

  glfwSetWindowUserPointer(window, this);

  glfwSetFramebufferSizeCallback(window, WindowResizeCallback);
  glfwSetCursorPosCallback(window, MousePositionCallback);
  glfwSetMouseButtonCallback(window, MouseButtonCallback);
  glfwSetScrollCallback(window, ScrollCallback);
  glfwSetKeyCallback(window, KeyCallback);
  glfwSetCharCallback(window, CharCallback);

  glfwGetFramebufferSize(window, &width, &height);

  return true;
}

void DesktopPlatform::Cleanup() {
  if (window) {
    glfwDestroyWindow(window);
    window = nullptr;
  }
  glfwTerminate();
}

bool DesktopPlatform::ProcessEvents() {
  glfwPollEvents();
  return !glfwWindowShouldClose(window);
}

bool DesktopPlatform::HasWindowResized() {
  bool resized = windowResized;
  windowResized = false;
  return resized;
}

bool DesktopPlatform::CreateVulkanSurface(VkInstance instance, VkSurfaceKHR* surface) {
  if (glfwCreateWindowSurface(instance, window, nullptr, surface) != VK_SUCCESS) {
    return false;
  }
  return true;
}

void DesktopPlatform::SetResizeCallback(std::function<void(int, int)> callback) {
  resizeCallback = std::move(callback);
}

void DesktopPlatform::SetMouseCallback(std::function < void(float, float, uint32_t) > callback) {
  mouseCallback = std::move(callback);
}

void DesktopPlatform::SetScrollCallback(std::function<void(float)> callback) {
  scrollCallback = std::move(callback);
}

void DesktopPlatform::SetKeyboardCallback(std::function < void(uint32_t, bool) > callback) {
  keyboardCallback = std::move(callback);
}

void DesktopPlatform::SetCharCallback(std::function<void(uint32_t)> callback) {
  charCallback = std::move(callback);
}

void DesktopPlatform::SetWindowTitle(const std::string& title) {
  if (window) {
    glfwSetWindowTitle(window, title.c_str());
  }
}

void DesktopPlatform::WindowResizeCallback(GLFWwindow* window, int width, int height) {
  auto* platform = static_cast<DesktopPlatform *>(glfwGetWindowUserPointer(window));
  platform->width = width;
  platform->height = height;
  platform->windowResized = true;

  if (platform->resizeCallback) {
    platform->resizeCallback(width, height);
  }
}

void DesktopPlatform::MousePositionCallback(GLFWwindow* window, double xpos, double ypos) {
  auto* platform = static_cast<DesktopPlatform *>(glfwGetWindowUserPointer(window));
  if (platform->mouseCallback) {
    uint32_t buttons = 0;
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
      buttons |= 0x01;
    }
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
      buttons |= 0x02;
    }
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS) {
      buttons |= 0x04;
    }
    platform->mouseCallback(static_cast<float>(xpos), static_cast<float>(ypos), buttons);
  }
}

void DesktopPlatform::MouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
  auto* platform = static_cast<DesktopPlatform *>(glfwGetWindowUserPointer(window));
  if (platform->mouseCallback) {
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);
    uint32_t buttons = 0;
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
      buttons |= 0x01;
    }
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
      buttons |= 0x02;
    }
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS) {
      buttons |= 0x04;
    }
    platform->mouseCallback(static_cast<float>(xpos), static_cast<float>(ypos), buttons);
  }
}

void DesktopPlatform::ScrollCallback(GLFWwindow* window, double, double yoffset) {
  auto* platform = static_cast<DesktopPlatform *>(glfwGetWindowUserPointer(window));
  if (platform->scrollCallback) {
    platform->scrollCallback(static_cast<float>(yoffset));
  }
}

void DesktopPlatform::KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
  auto* platform = static_cast<DesktopPlatform *>(glfwGetWindowUserPointer(window));
  if (platform->keyboardCallback) {
    platform->keyboardCallback(key, action != GLFW_RELEASE);
  }
}

void DesktopPlatform::CharCallback(GLFWwindow* window, unsigned int codepoint) {
  auto* platform = static_cast<DesktopPlatform *>(glfwGetWindowUserPointer(window));
  if (platform->charCallback) {
    platform->charCallback(codepoint);
  }
}
