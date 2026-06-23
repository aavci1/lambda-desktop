#pragma once

#include "Compositor/Wayland/WaylandServerImpl.hpp"

#include <algorithm>
#include <cstdint>

namespace lambdaui::compositor {

inline constexpr std::uint32_t kIdleInhibitVersion = 1;

[[nodiscard]] inline std::uint32_t idleInhibitResourceVersion(std::uint32_t boundVersion) {
  return std::min(boundVersion, kIdleInhibitVersion);
}

[[nodiscard]] inline bool idleInhibitorSurfaceActive(WaylandServer::Impl::Surface const* surface) {
  return surface &&
         surface->bufferState.buffer &&
         !surface->minimized &&
         surface->width > 0 &&
         surface->height > 0;
}

} // namespace lambdaui::compositor
