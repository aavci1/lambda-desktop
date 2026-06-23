#import <Metal/Metal.h>

#include <Lambda/Graphics/RenderTarget.hpp>
#include <Lambda/Graphics/TextSystem.hpp>

#include "Graphics/CoreTextSystem.hpp"
#include "Graphics/Metal/MetalCanvas.hpp"
#include "Graphics/Platform/RenderTarget.hpp"

#include <memory>
#include <stdexcept>

namespace lambdaui::platform {

namespace {

TextSystem& renderTargetTextSystem() {
  static CoreTextSystem textSystem;
  return textSystem;
}

class MetalRenderTarget final : public RenderTarget {
public:
  explicit MetalRenderTarget(MetalRenderTargetSpec const& spec)
      : canvas_(createMetalRenderTargetCanvas(spec, renderTargetTextSystem())) {
    if (!canvas_) {
      throw std::runtime_error("Failed to create Metal RenderTarget canvas");
    }
  }

  Canvas& canvas() override { return *canvas_; }
  void beginFrame() override { canvas_->beginFrame(); }
  void endFrame() override { canvas_->present(); }

private:
  std::unique_ptr<Canvas> canvas_;
};

} // namespace

std::unique_ptr<RenderTarget> createRenderTarget(MetalRenderTargetSpec const& spec) {
  return std::make_unique<MetalRenderTarget>(spec);
}

} // namespace lambdaui::platform
