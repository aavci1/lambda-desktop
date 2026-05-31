#include "Compositor/Presenter.hpp"
#include "Compositor/Wayland/PresentationState.hpp"

#include <doctest/doctest.h>

#include <stdexcept>
#include <unistd.h>

namespace lambda::compositor {
namespace {

class MockPresenter final : public Presenter {
public:
  [[nodiscard]] PresenterKind kind() const noexcept override { return PresenterKind::VulkanDisplay; }
  [[nodiscard]] Canvas& canvas() override { throw std::logic_error("MockPresenter has no canvas"); }
  void updateOutputGeometry(float, std::int32_t, std::int32_t) override {}

  [[nodiscard]] std::uint32_t lastVulkanPresentId() const override { return lastPresentId_; }
  [[nodiscard]] std::vector<lambda::VulkanPastPresentationTiming> pollVulkanPresentationTimings() override {
    return timings_;
  }
  void enqueueTiming(std::uint32_t presentId, std::uint64_t actualPresentTime) {
    timings_.push_back({.presentId = presentId, .actualPresentTime = actualPresentTime});
    lastPresentId_ = presentId;
  }

private:
  std::uint32_t lastPresentId_ = 0;
  std::vector<lambda::VulkanPastPresentationTiming> timings_;
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

TEST_CASE("forceVulkanDisplayPresenter respects environment flag") {
  unsetenv("LAMBDA_WINDOW_MANAGER_PRESENT");
  CHECK_FALSE(forceVulkanDisplayPresenter());
  setenv("LAMBDA_WINDOW_MANAGER_PRESENT", "vulkan-display", 1);
  CHECK(forceVulkanDisplayPresenter());
  unsetenv("LAMBDA_WINDOW_MANAGER_PRESENT");
}

} // namespace lambda::compositor
