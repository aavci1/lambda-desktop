#pragma once

#include <span>

#include <vulkan/vulkan_core.h>

namespace lambda::platform {

class GpuSurfaceProvider {
public:
  virtual ~GpuSurfaceProvider() = default;

  [[nodiscard]] virtual std::span<char const* const> requiredInstanceExtensions() const = 0;
  virtual VkSurfaceKHR createSurface(VkInstance instance, void* nativeHandle) = 0;
};

} // namespace lambda::platform
