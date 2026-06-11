#include "Compositor/Wayland/WaylandServerImpl.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

TEST_CASE("frame callbacks are limited to surfaces in the presented frame") {
  using lambda::compositor::WaylandServer;
  using lambda::compositor::surfaceParticipatesInPresentedFrame;

  WaylandServer::Impl::Surface visible{};
  visible.id = 10;
  WaylandServer::Impl::Surface notPresented{};
  notPresented.id = 20;
  WaylandServer::Impl::Surface zeroId{};

  std::vector<std::uint64_t> const presentedSurfaceIds{10, 30};

  CHECK(surfaceParticipatesInPresentedFrame(&visible, presentedSurfaceIds));
  CHECK_FALSE(surfaceParticipatesInPresentedFrame(&notPresented, presentedSurfaceIds));
  CHECK_FALSE(surfaceParticipatesInPresentedFrame(&zeroId, presentedSurfaceIds));
  CHECK_FALSE(surfaceParticipatesInPresentedFrame(nullptr, presentedSurfaceIds));
}

TEST_CASE("resize pacing grace tracks recent activity for three refreshes") {
  using lambda::compositor::resizePacingGraceActive;
  using lambda::compositor::resizePacingGraceNanoseconds;

  CHECK(resizePacingGraceNanoseconds(60'000) == 49'999'998);
  CHECK(resizePacingGraceNanoseconds(144'000) == 33'000'000);
  CHECK(resizePacingGraceNanoseconds(30'000) == 75'000'000);

  std::uint64_t const activity = 1'000'000'000ull;
  CHECK(resizePacingGraceActive(activity, activity + 49'000'000ull, 60'000));
  CHECK_FALSE(resizePacingGraceActive(activity, activity + 51'000'000ull, 60'000));
  CHECK_FALSE(resizePacingGraceActive(0, activity, 60'000));
  CHECK_FALSE(resizePacingGraceActive(activity, activity - 1ull, 60'000));
}
