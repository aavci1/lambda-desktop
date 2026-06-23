#pragma once

#include "Compositor/Wayland/WaylandServerImpl.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>

namespace lambdaui::compositor {

enum class XdgSurfaceBufferCommitReadiness : std::uint8_t {
  Ready,
  MissingRoleObject,
  Unconfigured,
};

enum class XdgConfigureAckStatus : std::uint8_t {
  Acked,
  UnknownSerial,
};

struct XdgConfigureAckResult {
  XdgConfigureAckStatus status = XdgConfigureAckStatus::UnknownSerial;
  std::optional<WaylandServer::Impl::XdgConfigure> ackedConfigure;
  std::optional<WaylandServer::Impl::XdgConfigure> ackedResizeConfigure;
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

inline bool resetXdgSurfaceConfigureStateForUnmap(WaylandServer::Impl::XdgSurface* xdgSurface) {
  if (!xdgSurface) return false;
  bool const changed = xdgSurface->configured ||
                       !xdgSurface->configureList.empty() ||
                       xdgSurface->pendingConfigure.has_value() ||
                       xdgSurface->currentConfigure.has_value();
  xdgSurface->configured = false;
  xdgSurface->configureList.clear();
  xdgSurface->pendingConfigure.reset();
  xdgSurface->currentConfigure.reset();
  return changed;
}

inline XdgConfigureAckResult acknowledgeXdgSurfaceConfigure(WaylandServer::Impl::XdgSurface* xdgSurface,
                                                            std::uint32_t serial,
                                                            std::uint32_t resizeSerial = 0) {
  if (!xdgSurface) return {};
  auto configure = std::find_if(xdgSurface->configureList.begin(),
                                xdgSurface->configureList.end(),
                                [serial](WaylandServer::Impl::XdgConfigure const& candidate) {
                                  return candidate.serial == serial;
                                });
  if (configure == xdgSurface->configureList.end()) return {};

  XdgConfigureAckResult result{
      .status = XdgConfigureAckStatus::Acked,
      .ackedConfigure = *configure,
      .ackedResizeConfigure = std::nullopt,
  };
  if (resizeSerial != 0) {
    auto resizeConfigure = std::find_if(xdgSurface->configureList.begin(),
                                        configure + 1,
                                        [resizeSerial](WaylandServer::Impl::XdgConfigure const& candidate) {
                                          return candidate.serial == resizeSerial;
                                        });
    if (resizeConfigure != configure + 1) result.ackedResizeConfigure = *resizeConfigure;
  }

  xdgSurface->configureList.erase(xdgSurface->configureList.begin(), configure + 1);
  xdgSurface->pendingConfigure = result.ackedConfigure;
  xdgSurface->configured = true;
  return result;
}

inline bool commitPendingXdgConfigure(WaylandServer::Impl::XdgSurface* xdgSurface) {
  if (!xdgSurface || !xdgSurface->pendingConfigure) return false;
  xdgSurface->currentConfigure = xdgSurface->pendingConfigure;
  xdgSurface->pendingConfigure.reset();
  return true;
}

} // namespace lambdaui::compositor
