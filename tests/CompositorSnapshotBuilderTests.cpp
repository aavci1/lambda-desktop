#include "Compositor/Wayland/WaylandTypes.hpp"

#include <doctest/doctest.h>

TEST_CASE("committed surface snapshot defaults describe empty buffer state") {
  flux::compositor::CommittedSurfaceSnapshot snapshot{};
  CHECK(snapshot.id == 0);
  CHECK(snapshot.bufferWidth == 0);
  CHECK(snapshot.bufferHeight == 0);
  CHECK(snapshot.bufferTransform == 0);
  CHECK(snapshot.dmabufFormat == 0);
  CHECK(snapshot.opaqueRegionRects.empty());
  CHECK(snapshot.dmabufPlanes.empty());
  CHECK(snapshot.backgroundEffect.tint.a == 0.f);
  CHECK(snapshot.backgroundEffect.borderColor.a == 0.f);
  CHECK_FALSE(snapshot.backgroundEffect.cornerRadiusSet);
}

TEST_CASE("presentation timing carries refresh interval metadata") {
  flux::compositor::PresentationTiming timing{
      .monotonicNsec = 1'000'000'000ull,
      .sequence = 42,
      .refreshNsec = 16'666'666u,
      .flags = 0u,
  };
  timing.backendPresentId = 7;
  CHECK(timing.sequence == 42);
  CHECK(timing.refreshNsec == 16'666'666u);
  CHECK(timing.backendPresentId == 7);
}
