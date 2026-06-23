#pragma once

/// \file Lambda/Graphics/VulkanContext.hpp
///
/// Public access to Lambda's shared Vulkan device/core.

#include <cstdint>

#if LAMBDAUI_VULKAN

#include <vulkan/vulkan.h>

struct VmaAllocator_T;
using VmaAllocator = VmaAllocator_T*;

namespace lambdaui {

class VulkanContext {
public:
  static VulkanContext& instance();

  [[nodiscard]] VkInstance vkInstance() const noexcept;
  [[nodiscard]] VkInstance instanceHandle() const noexcept { return vkInstance(); }
  [[nodiscard]] VkPhysicalDevice physicalDevice() const noexcept;
  [[nodiscard]] VkDevice device() const noexcept;
  [[nodiscard]] std::uint32_t queueFamily() const noexcept;
  [[nodiscard]] VkQueue queue() const noexcept;
  [[nodiscard]] VmaAllocator allocator() const noexcept;

  [[nodiscard]] VkFormat preferredColorFormat() const noexcept;

  /// Add a required device extension before the shared device is created.
  /// Throws if the device already exists.
  void addRequiredDeviceExtension(char const* name);

  /// Add a required instance extension before the shared instance is created.
  /// Throws if the instance already exists.
  void addRequiredInstanceExtension(char const* name);

  /// Explicitly initialize the shared core. Useful before any Canvas creation.
  void ensureInitialized();

private:
  VulkanContext() = default;
};

} // namespace lambdaui

#endif // LAMBDAUI_VULKAN
