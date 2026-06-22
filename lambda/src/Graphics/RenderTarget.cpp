#include <Lambda/Graphics/RenderTarget.hpp>

#include "Graphics/Platform/RenderTarget.hpp"

#include <stdexcept>
#include <utility>

namespace lambda {

namespace {

platform::RenderTarget& checkedTarget(std::unique_ptr<platform::RenderTarget> const& impl) {
  if (!impl) {
    throw std::runtime_error("Lambda RenderTarget backend is unavailable");
  }
  return *impl;
}

} // namespace

#if LAMBDA_VULKAN
RenderTarget::RenderTarget(VulkanRenderTargetSpec const& spec)
    : impl_(platform::createRenderTarget(spec)) {
  checkedTarget(impl_);
}
#endif

#if LAMBDA_METAL
RenderTarget::RenderTarget(MetalRenderTargetSpec const& spec)
    : impl_(platform::createRenderTarget(spec)) {
  checkedTarget(impl_);
}
#endif

RenderTarget::~RenderTarget() = default;
RenderTarget::RenderTarget(RenderTarget&&) noexcept = default;
RenderTarget& RenderTarget::operator=(RenderTarget&&) noexcept = default;

Canvas& RenderTarget::canvas() {
  return checkedTarget(impl_).canvas();
}

void RenderTarget::beginFrame() {
  checkedTarget(impl_).beginFrame();
}

void RenderTarget::endFrame() {
  checkedTarget(impl_).endFrame();
}

} // namespace lambda
