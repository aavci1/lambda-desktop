#include "Compositor/Wayland/IdleInhibitState.hpp"

#include <doctest/doctest.h>

#include <cstdint>

namespace lambdaui::compositor {

TEST_CASE("idle inhibit resources use the implemented protocol version") {
  CHECK(kIdleInhibitVersion == 1);
  CHECK(idleInhibitResourceVersion(1) == 1);
  CHECK(idleInhibitResourceVersion(2) == 1);
}

TEST_CASE("idle inhibitor policy requires an active surface") {
  WaylandServer::Impl::Surface surface;

  CHECK_FALSE(idleInhibitorSurfaceActive(nullptr));
  CHECK_FALSE(idleInhibitorSurfaceActive(&surface));

  surface.bufferState.buffer = reinterpret_cast<wl_resource*>(static_cast<std::uintptr_t>(1));
  CHECK_FALSE(idleInhibitorSurfaceActive(&surface));

  surface.width = 64;
  CHECK_FALSE(idleInhibitorSurfaceActive(&surface));

  surface.height = 48;
  CHECK(idleInhibitorSurfaceActive(&surface));

  surface.minimized = true;
  CHECK_FALSE(idleInhibitorSurfaceActive(&surface));

  surface.minimized = false;
  surface.width = 0;
  CHECK_FALSE(idleInhibitorSurfaceActive(&surface));

  surface.width = 64;
  surface.height = 0;
  CHECK_FALSE(idleInhibitorSurfaceActive(&surface));
}

} // namespace lambdaui::compositor
