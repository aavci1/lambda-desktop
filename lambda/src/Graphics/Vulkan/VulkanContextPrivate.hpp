#pragma once

#include <cstdint>

#include <vulkan/vulkan.h>

struct VmaAllocator_T;
using VmaAllocator = VmaAllocator_T*;

namespace lambdaui::detail {

VkInstance vulkanContextInstance() noexcept;
VkPhysicalDevice vulkanContextPhysicalDevice() noexcept;
VkDevice vulkanContextDevice() noexcept;
std::uint32_t vulkanContextQueueFamily() noexcept;
VkQueue vulkanContextQueue() noexcept;
VmaAllocator vulkanContextAllocator() noexcept;
VkFormat vulkanContextPreferredColorFormat() noexcept;

void vulkanContextAddRequiredInstanceExtension(char const* name);
void vulkanContextAddRequiredDeviceExtension(char const* name);
void vulkanContextEnsureInitialized();

} // namespace lambdaui::detail
