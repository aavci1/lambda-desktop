#include <Lambda/Graphics/VulkanContext.hpp>

#if LAMBDA_VULKAN

#include "Graphics/Vulkan/VulkanContextPrivate.hpp"

namespace lambda {

VulkanContext& VulkanContext::instance() {
  static VulkanContext context;
  return context;
}

VkInstance VulkanContext::vkInstance() const noexcept {
  return detail::vulkanContextInstance();
}

VkPhysicalDevice VulkanContext::physicalDevice() const noexcept {
  return detail::vulkanContextPhysicalDevice();
}

VkDevice VulkanContext::device() const noexcept {
  return detail::vulkanContextDevice();
}

std::uint32_t VulkanContext::queueFamily() const noexcept {
  return detail::vulkanContextQueueFamily();
}

VkQueue VulkanContext::queue() const noexcept {
  return detail::vulkanContextQueue();
}

VmaAllocator VulkanContext::allocator() const noexcept {
  return detail::vulkanContextAllocator();
}

VkFormat VulkanContext::preferredColorFormat() const noexcept {
  return detail::vulkanContextPreferredColorFormat();
}

void VulkanContext::addRequiredDeviceExtension(char const* name) {
  detail::vulkanContextAddRequiredDeviceExtension(name);
}

void VulkanContext::addRequiredInstanceExtension(char const* name) {
  detail::vulkanContextAddRequiredInstanceExtension(name);
}

void VulkanContext::ensureInitialized() {
  detail::vulkanContextEnsureInitialized();
}

} // namespace lambda

#endif // LAMBDA_VULKAN
