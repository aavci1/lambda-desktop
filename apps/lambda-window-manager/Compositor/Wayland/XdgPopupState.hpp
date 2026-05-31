#pragma once

#include "Compositor/Wayland/WaylandServerImpl.hpp"

#include <span>

namespace lambda::compositor {

[[nodiscard]] inline bool xdgPopupParentHasValidRole(WaylandServer::Impl::XdgSurface const* parent) {
  if (!parent) return true;
  return surfaceIsXdgToplevel(parent->surface) || surfaceIsXdgPopup(parent->surface);
}

[[nodiscard]] inline WaylandServer::Impl::Surface const* xdgPopupSurface(
    WaylandServer::Impl::XdgPopup const* popup) {
  return popup && popup->xdgSurface ? popup->xdgSurface->surface : nullptr;
}

[[nodiscard]] inline bool xdgPopupHasLiveChild(std::span<WaylandServer::Impl::XdgPopup const* const> popups,
                                               WaylandServer::Impl::XdgPopup const* popup) {
  auto const* surface = xdgPopupSurface(popup);
  if (!surface) return false;
  for (auto const* candidate : popups) {
    if (!candidate || candidate == popup || candidate->dismissed) continue;
    if (candidate->parentSurface == surface) return true;
  }
  return false;
}

[[nodiscard]] inline bool xdgPopupHasLiveChild(WaylandServer::Impl const* server,
                                               WaylandServer::Impl::XdgPopup const* popup) {
  auto const* surface = xdgPopupSurface(popup);
  if (!server || !surface) return false;
  for (auto const& candidate : server->popups_) {
    if (!candidate || candidate.get() == popup || candidate->dismissed) continue;
    if (candidate->parentSurface == surface) return true;
  }
  return false;
}

} // namespace lambda::compositor
