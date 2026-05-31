#pragma once

#include "Compositor/Wayland/WaylandServerImpl.hpp"

namespace lambda::compositor {

enum class XdgSurfaceBufferCommitReadiness : std::uint8_t {
  Ready,
  MissingRoleObject,
  Unconfigured,
};

[[nodiscard]] inline bool xdgSurfaceHasConstructedRoleObject(WaylandServer::Impl::XdgSurface const* xdgSurface) {
  if (!xdgSurface || !xdgSurface->surface) return false;
  return surfaceIsXdgToplevel(xdgSurface->surface) || surfaceIsXdgPopup(xdgSurface->surface);
}

[[nodiscard]] inline bool xdgSurfaceCreationHasExistingBuffer(WaylandServer::Impl::Surface const* surface) {
  return surface && surface->bufferState.buffer != nullptr;
}

[[nodiscard]] inline WaylandServer::Impl::XdgSurface* xdgSurfaceForSurface(WaylandServer::Impl* server,
                                                                           WaylandServer::Impl::Surface const* surface) {
  if (!server || !surface) return nullptr;
  for (auto const& xdgSurface : server->xdgSurfaces_) {
    if (xdgSurface && xdgSurface->surface == surface) return xdgSurface.get();
  }
  return nullptr;
}

[[nodiscard]] inline XdgSurfaceBufferCommitReadiness xdgSurfaceBufferCommitReadiness(
    WaylandServer::Impl::XdgSurface const* xdgSurface) {
  if (!xdgSurface) return XdgSurfaceBufferCommitReadiness::Ready;
  if (!xdgSurfaceHasConstructedRoleObject(xdgSurface)) {
    return XdgSurfaceBufferCommitReadiness::MissingRoleObject;
  }
  if (!xdgSurface->configured) {
    return XdgSurfaceBufferCommitReadiness::Unconfigured;
  }
  return XdgSurfaceBufferCommitReadiness::Ready;
}

} // namespace lambda::compositor
