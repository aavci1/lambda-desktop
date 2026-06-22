#include <Lambda/Graphics/RenderTarget.hpp>
#include <Lambda/Graphics/TextSystem.hpp>

#include "Graphics/Linux/FreeTypeTextSystem.hpp"
#include "Graphics/Platform/RenderTarget.hpp"
#include "Graphics/Vulkan/VulkanCanvas.hpp"

#include <memory>
#include <stdexcept>

namespace lambda::platform {

namespace {

TextSystem& renderTargetTextSystem() {
  static FreeTypeTextSystem textSystem;
  return textSystem;
}

class VulkanRenderTarget final : public RenderTarget {
public:
  explicit VulkanRenderTarget(VulkanRenderTargetSpec const& spec)
      : canvas_(createVulkanRenderTargetCanvas(spec, renderTargetTextSystem())) {
    if (!canvas_) {
      throw std::runtime_error("Failed to create Vulkan RenderTarget canvas");
    }
  }

  Canvas& canvas() override { return *canvas_; }
  void beginFrame() override { canvas_->beginFrame(); }
  void endFrame() override { canvas_->present(); }

private:
  std::unique_ptr<Canvas> canvas_;
};

} // namespace

std::unique_ptr<RenderTarget> createRenderTarget(VulkanRenderTargetSpec const& spec) {
  return std::make_unique<VulkanRenderTarget>(spec);
}

} // namespace lambda::platform
