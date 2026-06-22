#pragma once

#include "Graphics/Vulkan/VulkanCanvasTypes.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <vector>

struct VmaAllocator_T;
using VmaAllocator = VmaAllocator_T *;

namespace lambda {

struct VulkanFrameRecorderResources {
  VmaAllocator allocator = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkDescriptorPool descriptorPool = VK_NULL_HANDLE;

  VkBuffer preparedQuadBuffer = VK_NULL_HANDLE;
  VmaAllocation preparedQuadAllocation = VK_NULL_HANDLE;
  VkDescriptorSet preparedQuadDescriptor = VK_NULL_HANDLE;

  VkBuffer preparedRectBuffer = VK_NULL_HANDLE;
  VmaAllocation preparedRectAllocation = VK_NULL_HANDLE;
  VkDescriptorSet preparedRectDescriptor = VK_NULL_HANDLE;

  VkBuffer preparedPathVertexBuffer = VK_NULL_HANDLE;
  VmaAllocation preparedPathVertexAllocation = VK_NULL_HANDLE;
};

void retireVulkanFrameRecorderResources(VulkanFrameRecorderResources resources) noexcept;
void destroyVulkanFrameRecorderResourcesNow(VulkanFrameRecorderResources& resources) noexcept;
bool deferVulkanFrameRecorderResourcesDestroy(VulkanFrameRecorderResources resources) noexcept;

#if defined(LAMBDA_TESTING)
using VulkanFrameRecorderRetireHook =
    std::function<bool(VulkanFrameRecorderResources const& resources)>;
void setVulkanFrameRecorderRetireHookForTesting(VulkanFrameRecorderRetireHook hook);
#endif

/// Per-frame CPU-side Vulkan display list, detachable from a canvas and replayable
/// into later frames.
struct VulkanFrameRecorder {
  std::vector<DrawOp> ops;
  std::vector<QuadInstance> quads;
  std::vector<RectInstance> rects;
  std::vector<VulkanPathVertex> pathVerts;

  mutable VkBuffer preparedQuadBuffer = VK_NULL_HANDLE;
  mutable VmaAllocation preparedQuadAllocation = VK_NULL_HANDLE;
  mutable VkDeviceSize preparedQuadCapacity = 0;
  mutable VkDescriptorSet preparedQuadDescriptor = VK_NULL_HANDLE;
  mutable std::uint64_t preparedQuadUploadSignature = 0;

  mutable VkBuffer preparedRectBuffer = VK_NULL_HANDLE;
  mutable VmaAllocation preparedRectAllocation = VK_NULL_HANDLE;
  mutable VkDeviceSize preparedRectCapacity = 0;
  mutable VkDescriptorSet preparedRectDescriptor = VK_NULL_HANDLE;
  mutable std::uint64_t preparedRectUploadSignature = 0;

  mutable VkBuffer preparedPathVertexBuffer = VK_NULL_HANDLE;
  mutable VmaAllocation preparedPathVertexAllocation = VK_NULL_HANDLE;
  mutable VkDeviceSize preparedPathVertexCapacity = 0;
  mutable std::uint64_t preparedPathVertexUploadSignature = 0;

  mutable VmaAllocator allocator = VK_NULL_HANDLE;
  mutable VkDevice device = VK_NULL_HANDLE;
  mutable VkDescriptorPool descriptorPool = VK_NULL_HANDLE;

  Rect rootClip{};
  std::uint64_t glyphAtlasGeneration = 0;
  bool geometrySignaturesPrepared = false;
  bool replayable = true;

  VulkanFrameRecorder() = default;
  ~VulkanFrameRecorder();
  VulkanFrameRecorder(VulkanFrameRecorder const &) = delete;
  VulkanFrameRecorder &operator=(VulkanFrameRecorder const &) = delete;
  VulkanFrameRecorder(VulkanFrameRecorder &&other) noexcept;
  VulkanFrameRecorder &operator=(VulkanFrameRecorder &&other) noexcept;

  void clear();
};

} // namespace lambda
