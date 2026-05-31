#pragma once

#include "Compositor/Wayland/WaylandServerImpl.hpp"

namespace lambda::compositor {

[[nodiscard]] inline bool xdgSurfaceHasConstructedRoleObject(WaylandServer::Impl::XdgSurface const* xdgSurface) {
  if (!xdgSurface || !xdgSurface->surface) return false;
  return surfaceIsXdgToplevel(xdgSurface->surface) || surfaceIsXdgPopup(xdgSurface->surface);
}

[[nodiscard]] inline bool xdgSurfaceCreationHasExistingBuffer(WaylandServer::Impl::Surface const* surface) {
  return surface && surface->bufferState.buffer != nullptr;
}

} // namespace lambda::compositor
