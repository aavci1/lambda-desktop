#include "Graphics/Vulkan/VulkanFrameRecorder.hpp"

#include "vma/vk_mem_alloc.h"

#include <utility>

namespace lambdaui {

namespace {

bool hasRecorderResources(VulkanFrameRecorderResources const& resources) noexcept {
  return resources.preparedQuadBuffer || resources.preparedRectBuffer ||
         resources.preparedPathVertexBuffer || resources.preparedQuadDescriptor ||
         resources.preparedRectDescriptor;
}

#if defined(LAMBDAUI_TESTING)
VulkanFrameRecorderRetireHook gRetireHookForTesting;
#endif

} // namespace

void destroyVulkanFrameRecorderResourcesNow(VulkanFrameRecorderResources& resources) noexcept {
  if (resources.device && resources.descriptorPool && resources.preparedQuadDescriptor) {
    vkFreeDescriptorSets(resources.device, resources.descriptorPool, 1,
                         &resources.preparedQuadDescriptor);
  }
  resources.preparedQuadDescriptor = VK_NULL_HANDLE;
  if (resources.device && resources.descriptorPool && resources.preparedRectDescriptor) {
    vkFreeDescriptorSets(resources.device, resources.descriptorPool, 1,
                         &resources.preparedRectDescriptor);
  }
  resources.preparedRectDescriptor = VK_NULL_HANDLE;

  if (resources.allocator && resources.preparedQuadBuffer) {
    vmaDestroyBuffer(resources.allocator, resources.preparedQuadBuffer,
                     resources.preparedQuadAllocation);
  }
  resources.preparedQuadBuffer = VK_NULL_HANDLE;
  resources.preparedQuadAllocation = VK_NULL_HANDLE;

  if (resources.allocator && resources.preparedRectBuffer) {
    vmaDestroyBuffer(resources.allocator, resources.preparedRectBuffer,
                     resources.preparedRectAllocation);
  }
  resources.preparedRectBuffer = VK_NULL_HANDLE;
  resources.preparedRectAllocation = VK_NULL_HANDLE;

  if (resources.allocator && resources.preparedPathVertexBuffer) {
    vmaDestroyBuffer(resources.allocator, resources.preparedPathVertexBuffer,
                     resources.preparedPathVertexAllocation);
  }
  resources.preparedPathVertexBuffer = VK_NULL_HANDLE;
  resources.preparedPathVertexAllocation = VK_NULL_HANDLE;
}

void retireVulkanFrameRecorderResources(VulkanFrameRecorderResources resources) noexcept {
  if (!hasRecorderResources(resources)) {
    return;
  }
#if defined(LAMBDAUI_TESTING)
  if (gRetireHookForTesting && gRetireHookForTesting(resources)) {
    return;
  }
#endif
  if (deferVulkanFrameRecorderResourcesDestroy(resources)) {
    return;
  }
  destroyVulkanFrameRecorderResourcesNow(resources);
}

#if defined(LAMBDAUI_TESTING)
void setVulkanFrameRecorderRetireHookForTesting(VulkanFrameRecorderRetireHook hook) {
  gRetireHookForTesting = std::move(hook);
}
#endif

VulkanFrameRecorder::~VulkanFrameRecorder() {
  clear();
}

VulkanFrameRecorder::VulkanFrameRecorder(VulkanFrameRecorder &&other) noexcept
    : ops(std::move(other.ops)),
      quads(std::move(other.quads)),
      rects(std::move(other.rects)),
      pathVerts(std::move(other.pathVerts)),
      preparedQuadBuffer(std::exchange(other.preparedQuadBuffer, VK_NULL_HANDLE)),
      preparedQuadAllocation(std::exchange(other.preparedQuadAllocation, VK_NULL_HANDLE)),
      preparedQuadCapacity(std::exchange(other.preparedQuadCapacity, 0)),
      preparedQuadDescriptor(std::exchange(other.preparedQuadDescriptor, VK_NULL_HANDLE)),
      preparedQuadUploadSignature(std::exchange(other.preparedQuadUploadSignature, 0)),
      preparedRectBuffer(std::exchange(other.preparedRectBuffer, VK_NULL_HANDLE)),
      preparedRectAllocation(std::exchange(other.preparedRectAllocation, VK_NULL_HANDLE)),
      preparedRectCapacity(std::exchange(other.preparedRectCapacity, 0)),
      preparedRectDescriptor(std::exchange(other.preparedRectDescriptor, VK_NULL_HANDLE)),
      preparedRectUploadSignature(std::exchange(other.preparedRectUploadSignature, 0)),
      preparedPathVertexBuffer(std::exchange(other.preparedPathVertexBuffer, VK_NULL_HANDLE)),
      preparedPathVertexAllocation(std::exchange(other.preparedPathVertexAllocation, VK_NULL_HANDLE)),
      preparedPathVertexCapacity(std::exchange(other.preparedPathVertexCapacity, 0)),
      preparedPathVertexUploadSignature(std::exchange(other.preparedPathVertexUploadSignature, 0)),
      allocator(std::exchange(other.allocator, VK_NULL_HANDLE)),
      device(std::exchange(other.device, VK_NULL_HANDLE)),
      descriptorPool(std::exchange(other.descriptorPool, VK_NULL_HANDLE)),
      rootClip(other.rootClip),
      glyphAtlasGeneration(std::exchange(other.glyphAtlasGeneration, 0)),
      geometrySignaturesPrepared(std::exchange(other.geometrySignaturesPrepared, false)),
      replayable(std::exchange(other.replayable, true)) {}

VulkanFrameRecorder &VulkanFrameRecorder::operator=(VulkanFrameRecorder &&other) noexcept {
  if (this == &other) {
    return *this;
  }
  clear();
  ops = std::move(other.ops);
  quads = std::move(other.quads);
  rects = std::move(other.rects);
  pathVerts = std::move(other.pathVerts);
  preparedQuadBuffer = std::exchange(other.preparedQuadBuffer, VK_NULL_HANDLE);
  preparedQuadAllocation = std::exchange(other.preparedQuadAllocation, VK_NULL_HANDLE);
  preparedQuadCapacity = std::exchange(other.preparedQuadCapacity, 0);
  preparedQuadDescriptor = std::exchange(other.preparedQuadDescriptor, VK_NULL_HANDLE);
  preparedQuadUploadSignature = std::exchange(other.preparedQuadUploadSignature, 0);
  preparedRectBuffer = std::exchange(other.preparedRectBuffer, VK_NULL_HANDLE);
  preparedRectAllocation = std::exchange(other.preparedRectAllocation, VK_NULL_HANDLE);
  preparedRectCapacity = std::exchange(other.preparedRectCapacity, 0);
  preparedRectDescriptor = std::exchange(other.preparedRectDescriptor, VK_NULL_HANDLE);
  preparedRectUploadSignature = std::exchange(other.preparedRectUploadSignature, 0);
  preparedPathVertexBuffer = std::exchange(other.preparedPathVertexBuffer, VK_NULL_HANDLE);
  preparedPathVertexAllocation = std::exchange(other.preparedPathVertexAllocation, VK_NULL_HANDLE);
  preparedPathVertexCapacity = std::exchange(other.preparedPathVertexCapacity, 0);
  preparedPathVertexUploadSignature = std::exchange(other.preparedPathVertexUploadSignature, 0);
  allocator = std::exchange(other.allocator, VK_NULL_HANDLE);
  device = std::exchange(other.device, VK_NULL_HANDLE);
  descriptorPool = std::exchange(other.descriptorPool, VK_NULL_HANDLE);
  rootClip = other.rootClip;
  glyphAtlasGeneration = std::exchange(other.glyphAtlasGeneration, 0);
  geometrySignaturesPrepared = std::exchange(other.geometrySignaturesPrepared, false);
  replayable = std::exchange(other.replayable, true);
  return *this;
}

void VulkanFrameRecorder::clear() {
  retireVulkanFrameRecorderResources(VulkanFrameRecorderResources{
      .allocator = allocator,
      .device = device,
      .descriptorPool = descriptorPool,
      .preparedQuadBuffer = preparedQuadBuffer,
      .preparedQuadAllocation = preparedQuadAllocation,
      .preparedQuadDescriptor = preparedQuadDescriptor,
      .preparedRectBuffer = preparedRectBuffer,
      .preparedRectAllocation = preparedRectAllocation,
      .preparedRectDescriptor = preparedRectDescriptor,
      .preparedPathVertexBuffer = preparedPathVertexBuffer,
      .preparedPathVertexAllocation = preparedPathVertexAllocation,
  });
  preparedQuadBuffer = VK_NULL_HANDLE;
  preparedQuadAllocation = VK_NULL_HANDLE;
  preparedQuadCapacity = 0;
  preparedQuadDescriptor = VK_NULL_HANDLE;
  preparedRectBuffer = VK_NULL_HANDLE;
  preparedRectAllocation = VK_NULL_HANDLE;
  preparedRectCapacity = 0;
  preparedRectDescriptor = VK_NULL_HANDLE;
  preparedPathVertexBuffer = VK_NULL_HANDLE;
  preparedPathVertexAllocation = VK_NULL_HANDLE;
  preparedPathVertexCapacity = 0;
  preparedQuadUploadSignature = 0;
  preparedRectUploadSignature = 0;
  preparedPathVertexUploadSignature = 0;
  allocator = VK_NULL_HANDLE;
  device = VK_NULL_HANDLE;
  descriptorPool = VK_NULL_HANDLE;
  ops.clear();
  quads.clear();
  rects.clear();
  pathVerts.clear();
  rootClip = {};
  glyphAtlasGeneration = 0;
  geometrySignaturesPrepared = false;
  replayable = true;
}

} // namespace lambdaui
