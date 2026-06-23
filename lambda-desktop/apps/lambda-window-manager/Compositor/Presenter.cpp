#include "Compositor/Presenter.hpp"

#include "Graphics/Vulkan/VulkanCanvas.hpp"

#include <Lambda/Debug/DebugFlags.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <utility>

namespace lambdaui::compositor {
namespace {

class AtomicKmsPresenterAdapter final : public Presenter {
public:
  AtomicKmsPresenterAdapter(platform::KmsOutput const& output, TextSystem& textSystem)
      : presenter_(output.createAtomicPresenter(textSystem)) {}

  [[nodiscard]] PresenterKind kind() const noexcept override { return PresenterKind::AtomicKms; }
  [[nodiscard]] Canvas& canvas() override { return presenter_->canvas(); }

  void updateOutputGeometry(float dpiScale, std::int32_t logicalWidth, std::int32_t logicalHeight) override {
    presenter_->canvas().updateDpiScale(dpiScale, dpiScale);
    presenter_->canvas().resize(logicalWidth, logicalHeight);
  }

  void prepareFrame() override { (void)presenter_->prepareFrame(); }
  void markFrameRendered() override { (void)presenter_->markFrameRendered(); }

  [[nodiscard]] bool isAtomic() const noexcept override { return true; }
  [[nodiscard]] platform::KmsAtomicPresenter* atomicPresenter() override { return presenter_.get(); }

private:
  std::unique_ptr<platform::KmsAtomicPresenter> presenter_;
};

class VulkanDisplayPresenterAdapter final : public Presenter {
public:
  VulkanDisplayPresenterAdapter(platform::KmsOutput const& output,
                                TextSystem& textSystem,
                                VkInstance instance,
                                float dpiScale,
                                std::int32_t logicalWidth,
                                std::int32_t logicalHeight)
      : canvas_(createVulkanCanvas(output.createVulkanSurface(instance), 1u, textSystem)) {
    canvas_->updateDpiScale(dpiScale, dpiScale);
    canvas_->resize(logicalWidth, logicalHeight);
  }

  [[nodiscard]] PresenterKind kind() const noexcept override { return PresenterKind::VulkanDisplay; }
  [[nodiscard]] Canvas& canvas() override { return *canvas_; }

  void updateOutputGeometry(float dpiScale, std::int32_t logicalWidth, std::int32_t logicalHeight) override {
    canvas_->updateDpiScale(dpiScale, dpiScale);
    canvas_->resize(logicalWidth, logicalHeight);
  }

  [[nodiscard]] bool vulkanDisplayTimingAvailable() const override {
    return vulkanCanvasSupportsDisplayTiming(canvas_.get());
  }

  [[nodiscard]] std::uint32_t lastVulkanPresentId() const override {
    return lastVulkanCanvasPresentId(canvas_.get());
  }

  [[nodiscard]] std::vector<VulkanPastPresentationTiming> pollVulkanPresentationTimings() override {
    return pollVulkanCanvasPastPresentationTimings(canvas_.get());
  }

private:
  std::unique_ptr<Canvas> canvas_;
};

} // namespace

std::unique_ptr<Presenter> createPresenter(PresenterContext const& context) {
  if (forceVulkanDisplayPresenter()) {
    std::fprintf(stderr, "lambda-window-manager: using Vulkan display presenter\n");
    return std::make_unique<VulkanDisplayPresenterAdapter>(context.output,
                                                           context.textSystem,
                                                           context.vulkanInstance,
                                                           context.dpiScale,
                                                           context.logicalWidth,
                                                           context.logicalHeight);
  }
  std::fprintf(stderr, "lambda-window-manager: using GBM/atomic-KMS presenter\n");
  return std::make_unique<AtomicKmsPresenterAdapter>(context.output, context.textSystem);
}

} // namespace lambdaui::compositor
