#pragma once

#include <Lambda/Platform/Linux/KmsOutput.hpp>

#include <Lambda/Graphics/Canvas.hpp>
#include "Graphics/Vulkan/VulkanCanvas.hpp"

#include <cstdint>
#include <memory>
#include <vector>

struct VkInstance_T;
using VkInstance = VkInstance_T*;

namespace lambda {
class TextSystem;
}

namespace lambda::compositor {

enum class PresenterKind {
  AtomicKms,
  VulkanDisplay,
};

struct PresenterContext {
  platform::KmsOutput const& output;
  TextSystem& textSystem;
  VkInstance vulkanInstance = nullptr;
  float dpiScale = 1.f;
  std::int32_t logicalWidth = 0;
  std::int32_t logicalHeight = 0;
};

/// Abstracts GBM/atomic-KMS vs legacy Vulkan-display presentation paths.
class Presenter {
public:
  virtual ~Presenter() = default;

  [[nodiscard]] virtual PresenterKind kind() const noexcept = 0;
  [[nodiscard]] virtual Canvas& canvas() = 0;

  virtual void updateOutputGeometry(float dpiScale, std::int32_t logicalWidth, std::int32_t logicalHeight) = 0;

  virtual void prepareFrame() {}
  virtual void markFrameRendered() {}

  [[nodiscard]] virtual bool isAtomic() const noexcept { return false; }
  [[nodiscard]] virtual platform::KmsAtomicPresenter* atomicPresenter() { return nullptr; }

  [[nodiscard]] virtual bool vulkanDisplayTimingAvailable() const { return false; }
  [[nodiscard]] virtual std::uint32_t lastVulkanPresentId() const { return 0; }
  [[nodiscard]] virtual std::vector<VulkanPastPresentationTiming> pollVulkanPresentationTimings() {
    return {};
  }
};

[[nodiscard]] bool forceVulkanDisplayPresenter();
[[nodiscard]] std::unique_ptr<Presenter> createPresenter(PresenterContext const& context);

} // namespace lambda::compositor
