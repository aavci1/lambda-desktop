#include "Compositor/Presenter.hpp"
#include "Compositor/CompositorPresentation.hpp"
#include "Compositor/Wayland/PresentationState.hpp"
#include "presentation-time-server-protocol.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <stdexcept>
#include <unistd.h>

namespace lambdaui::compositor {
namespace {

class MockPresenter final : public Presenter {
public:
  [[nodiscard]] PresenterKind kind() const noexcept override { return PresenterKind::VulkanDisplay; }
  [[nodiscard]] Canvas& canvas() override { throw std::logic_error("MockPresenter has no canvas"); }
  void updateOutputGeometry(float, std::int32_t, std::int32_t) override {}

  [[nodiscard]] std::uint32_t lastVulkanPresentId() const override { return lastPresentId_; }
  [[nodiscard]] std::vector<lambdaui::VulkanPastPresentationTiming> pollVulkanPresentationTimings() override {
    return timings_;
  }
  void enqueueTiming(std::uint32_t presentId, std::uint64_t actualPresentTime) {
    timings_.push_back({.presentId = presentId, .actualPresentTime = actualPresentTime});
    lastPresentId_ = presentId;
  }

private:
  std::uint32_t lastPresentId_ = 0;
  std::vector<lambdaui::VulkanPastPresentationTiming> timings_;
};

} // namespace

TEST_CASE("mock presenter tracks Vulkan presentation feedback ids") {
  MockPresenter presenter;
  presenter.enqueueTiming(3, 1000);
  presenter.enqueueTiming(4, 2000);
  auto const timings = presenter.pollVulkanPresentationTimings();
  REQUIRE(timings.size() == 2);
  CHECK(timings[0].presentId == 3);
  CHECK(timings[1].actualPresentTime == 2000);
  CHECK(presenter.lastVulkanPresentId() == 4);
}

TEST_CASE("presentation feedback resources use the bound manager version") {
  CHECK(presentationResourceVersion(1) == 1);
  CHECK(presentationResourceVersion(2) == 2);
  CHECK(presentationResourceVersion(3) == kPresentationVersion);
}

TEST_CASE("presentation fallback timeout tracks two refresh periods") {
  CHECK(presentation::presentationCompletionFallbackMilliseconds(60'000) == 34);
  CHECK(presentation::presentationCompletionFallbackMilliseconds(120'000) == 17);
  CHECK(presentation::presentationCompletionFallbackMilliseconds(0) == 34);
  CHECK(presentation::monotonicMillisecondsFromNsec(1'234'567'890ull) == 1234);
}

TEST_CASE("presentation completion resolves flip data and discard fallbacks") {
  PresentationTiming const scheduled{
      .monotonicNsec = 111'000'000ull,
      .sequence = 3,
      .backendPresentId = 9,
      .refreshNsec = 16'666'666u,
      .flags = static_cast<std::uint32_t>(WP_PRESENTATION_FEEDBACK_KIND_VSYNC),
  };
  PresentationCompletion const completion{
      .backendPresentId = 9,
      .monotonicNsec = 1'234'567'890ull,
      .sequence = (std::uint64_t{1} << 32u) | 5u,
      .flags = static_cast<std::uint32_t>(WP_PRESENTATION_FEEDBACK_KIND_HW_CLOCK),
  };

  auto const presented = presentation::resolvePresentationFeedbackCompletion(
      scheduled,
      true,
      completion,
      100,
      101,
      60'000,
      static_cast<std::uint32_t>(WP_PRESENTATION_FEEDBACK_KIND_HW_COMPLETION));
  REQUIRE(presented.ready);
  CHECK(presented.presented);
  CHECK(presented.timing.monotonicNsec == completion.monotonicNsec);
  CHECK(presented.timing.sequence == completion.sequence);
  CHECK(presented.timing.backendPresentId == scheduled.backendPresentId);
  CHECK(presented.timing.refreshNsec == scheduled.refreshNsec);
  CHECK((presented.timing.flags & WP_PRESENTATION_FEEDBACK_KIND_VSYNC) != 0);
  CHECK((presented.timing.flags & WP_PRESENTATION_FEEDBACK_KIND_HW_CLOCK) != 0);
  CHECK((presented.timing.flags & WP_PRESENTATION_FEEDBACK_KIND_HW_COMPLETION) != 0);
  auto const fields = presentation::presentedFeedbackFields(presented.timing);
  CHECK(fields.tvSecHi == 0);
  CHECK(fields.tvSecLo == 1);
  CHECK(fields.tvNsec == 234'567'890u);
  CHECK(fields.refresh == scheduled.refreshNsec);
  CHECK(fields.seqHi == 1);
  CHECK(fields.seqLo == 5);
  CHECK(fields.flags == presented.timing.flags);

  PresentationCompletion softwareCompletion = completion;
  softwareCompletion.flags = static_cast<std::uint32_t>(WP_PRESENTATION_FEEDBACK_KIND_VSYNC);
  auto const software = presentation::resolvePresentationFeedbackCompletion(
      scheduled,
      true,
      softwareCompletion,
      100,
      101,
      60'000,
      static_cast<std::uint32_t>(WP_PRESENTATION_FEEDBACK_KIND_HW_COMPLETION));
  REQUIRE(software.ready);
  CHECK(software.presented);
  CHECK((software.timing.flags & WP_PRESENTATION_FEEDBACK_KIND_VSYNC) != 0);
  CHECK((software.timing.flags & WP_PRESENTATION_FEEDBACK_KIND_HW_CLOCK) == 0);
  CHECK((software.timing.flags & WP_PRESENTATION_FEEDBACK_KIND_HW_COMPLETION) == 0);

  auto const pending = presentation::resolvePresentationFeedbackCompletion(
      scheduled,
      false,
      PresentationCompletion{},
      100,
      120,
      60'000,
      static_cast<std::uint32_t>(WP_PRESENTATION_FEEDBACK_KIND_HW_COMPLETION));
  CHECK_FALSE(pending.ready);

  auto const expired = presentation::resolvePresentationFeedbackCompletion(
      scheduled,
      false,
      PresentationCompletion{},
      100,
      134,
      60'000,
      static_cast<std::uint32_t>(WP_PRESENTATION_FEEDBACK_KIND_HW_COMPLETION));
  CHECK(expired.ready);
  CHECK_FALSE(expired.presented);

  PresentationCompletion zeroTimestamp = completion;
  zeroTimestamp.monotonicNsec = 0;
  auto const zero = presentation::resolvePresentationFeedbackCompletion(
      scheduled,
      true,
      zeroTimestamp,
      100,
      101,
      60'000,
      static_cast<std::uint32_t>(WP_PRESENTATION_FEEDBACK_KIND_HW_COMPLETION));
  CHECK(zero.ready);
  CHECK_FALSE(zero.presented);
}

TEST_CASE("Vulkan display frame callbacks wait for present timing or short fallback") {
  auto const pending = presentation::resolveFrameCallbackCompletion(false, 0, 200, 220, 60'000);
  CHECK_FALSE(pending.ready);

  auto const completed = presentation::resolveFrameCallbackCompletion(true, 1'234'567'890ull, 200, 220, 60'000);
  REQUIRE(completed.ready);
  CHECK(completed.callbackMs == 1234);

  auto const completedWithoutTimestamp = presentation::resolveFrameCallbackCompletion(true, 0, 200, 220, 60'000);
  REQUIRE(completedWithoutTimestamp.ready);
  CHECK(completedWithoutTimestamp.callbackMs == 220);

  auto const expired = presentation::resolveFrameCallbackCompletion(false, 0, 200, 234, 60'000);
  REQUIRE(expired.ready);
  CHECK(expired.callbackMs == 234);
}

TEST_CASE("forceVulkanDisplayPresenter respects environment flag") {
  unsetenv("LAMBDA_WINDOW_MANAGER_PRESENT");
  CHECK_FALSE(forceVulkanDisplayPresenter());
  setenv("LAMBDA_WINDOW_MANAGER_PRESENT", "vulkan-display", 1);
  CHECK(forceVulkanDisplayPresenter());
  unsetenv("LAMBDA_WINDOW_MANAGER_PRESENT");
}

} // namespace lambdaui::compositor
