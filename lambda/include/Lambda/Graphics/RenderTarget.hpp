#pragma once

/// \file Lambda/Graphics/RenderTarget.hpp
///
/// Public render destination abstraction for window-backed, offscreen, and externally-owned GPU targets.

#include <Lambda/Graphics/Canvas.hpp>

#include <cstdint>
#include <memory>

#if LAMBDA_VULKAN
#include <vulkan/vulkan.h>
#endif

#if LAMBDA_METAL && defined(__OBJC__)
@class MTLTexture;
@class MTLCommandBuffer;
@class MTLSharedEvent;
#endif

namespace lambda {

namespace scenegraph {
class SceneGraph;
}

#if LAMBDA_VULKAN
struct VulkanRenderTargetSpec {
  VkImage image = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkFormat format = VK_FORMAT_UNDEFINED;
  std::uint32_t width = 0;
  std::uint32_t height = 0;

  VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkImageLayout finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  bool preserveContents = false;

  VkSemaphore waitSemaphore = VK_NULL_HANDLE;
  VkSemaphore signalSemaphore = VK_NULL_HANDLE;

  /// Optional command buffer. When null, Lambda records, submits, and synchronizes internally.
  /// When non-null, Lambda records commands into it and the caller owns submission/synchronization.
  VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

  /// Optional fence signaled when external command-buffer work has completed.
  /// Readback users such as compositor screenshots wait on this before mapping staging buffers.
  VkFence completionFence = VK_NULL_HANDLE;
};
#endif

#if LAMBDA_METAL
struct MetalRenderTargetSpec {
  void* texture = nullptr;       ///< id<MTLTexture>; required.
  int format = 0;                ///< MTLPixelFormat; 0 means match the texture.
  std::uint32_t width = 0;
  std::uint32_t height = 0;

  void* commandBuffer = nullptr; ///< id<MTLCommandBuffer>; optional.
  void* sharedEvent = nullptr;   ///< id<MTLSharedEvent>; optional.
  std::uint64_t signalValue = 0;
};
#endif

namespace platform {
class RenderTarget;
}

class RenderTarget {
public:
#if LAMBDA_VULKAN
  explicit RenderTarget(VulkanRenderTargetSpec const& spec);
#endif
#if LAMBDA_METAL
  explicit RenderTarget(MetalRenderTargetSpec const& spec);
#endif

  ~RenderTarget();

  RenderTarget(RenderTarget const&) = delete;
  RenderTarget& operator=(RenderTarget const&) = delete;
  RenderTarget(RenderTarget&&) noexcept;
  RenderTarget& operator=(RenderTarget&&) noexcept;

  [[nodiscard]] Canvas& canvas();

  void beginFrame();
  void endFrame();

  void renderScene(scenegraph::SceneGraph const& scene);

private:
  std::unique_ptr<platform::RenderTarget> impl_;
};

} // namespace lambda
