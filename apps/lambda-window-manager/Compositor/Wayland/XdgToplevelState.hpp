#pragma once

#include "Compositor/Wayland/WaylandServerImpl.hpp"

namespace lambda::compositor {

[[nodiscard]] inline bool xdgToplevelSurfaceConfigured(WaylandServer::Impl::XdgToplevel const* toplevel) {
  return toplevel &&
         toplevel->xdgSurface &&
         toplevel->xdgSurface->surface &&
         surfaceIsXdgToplevel(toplevel->xdgSurface->surface) &&
         toplevel->xdgSurface->configured;
}

} // namespace lambda::compositor
