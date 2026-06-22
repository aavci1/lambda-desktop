#pragma once

#include <Lambda/Graphics/RenderTarget.hpp>

#include <memory>

namespace lambda::platform {

class RenderTarget {
public:
  virtual ~RenderTarget() = default;

  virtual Canvas& canvas() = 0;
  virtual void beginFrame() = 0;
  virtual void endFrame() = 0;
};

#if LAMBDA_VULKAN
std::unique_ptr<RenderTarget> createRenderTarget(VulkanRenderTargetSpec const& spec);
#endif

#if LAMBDA_METAL
std::unique_ptr<RenderTarget> createRenderTarget(MetalRenderTargetSpec const& spec);
#endif

} // namespace lambda::platform
