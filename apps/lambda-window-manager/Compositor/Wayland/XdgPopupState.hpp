#pragma once

#include "Compositor/Wayland/WaylandServerImpl.hpp"

#include <algorithm>
#include <cstdint>
#include <span>

namespace lambda::compositor {

struct XdgPopupReparentInput {
  std::int32_t oldParentX = 0;
  std::int32_t oldParentY = 0;
  std::int32_t newParentX = 0;
  std::int32_t newParentY = 0;
  std::int32_t popupWindowX = 0;
  std::int32_t popupWindowY = 0;
};

struct XdgPopupReparentGeometry {
  std::int32_t popupWindowX = 0;
  std::int32_t popupWindowY = 0;
  std::int32_t configuredX = 0;
  std::int32_t configuredY = 0;
};

[[nodiscard]] inline XdgPopupReparentGeometry xdgPopupReparentGeometry(
    XdgPopupReparentInput const& input) {
  XdgPopupReparentGeometry geometry{
      .popupWindowX = input.popupWindowX + input.newParentX - input.oldParentX,
      .popupWindowY = input.popupWindowY + input.newParentY - input.oldParentY,
  };
  geometry.configuredX = geometry.popupWindowX - input.newParentX;
  geometry.configuredY = geometry.popupWindowY - input.newParentY;
  return geometry;
}

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

[[nodiscard]] inline bool xdgPopupGrabRequestAllowed(WaylandServer::Impl::XdgPopup const* popup) {
  return popup && !popup->dismissed && !popup->committed && !popup->grabbed;
}

[[nodiscard]] inline WaylandServer::Impl::XdgPopup* xdgPopupGrabTop(
    WaylandServer::Impl::XdgPopupGrab const& grab) {
  return grab.popups.empty() ? nullptr : grab.popups.back();
}

inline WaylandServer::Impl::XdgPopup* xdgPopupGrabSyncTop(
    WaylandServer::Impl::XdgPopupGrab const& grab,
    WaylandServer::Impl::XdgPopup*& cachedTop) {
  cachedTop = xdgPopupGrabTop(grab);
  return cachedTop;
}

[[nodiscard]] inline bool xdgPopupGrabContains(WaylandServer::Impl::XdgPopupGrab const& grab,
                                               WaylandServer::Impl::XdgPopup const* popup) {
  return popup && std::find(grab.popups.begin(), grab.popups.end(), popup) != grab.popups.end();
}

inline bool xdgPopupGrabPush(WaylandServer::Impl::XdgPopupGrab& grab,
                             WaylandServer::Impl::XdgPopup* popup,
                             wl_client* client,
                             wl_resource* seatResource) {
  if (!popup) return false;
  std::erase(grab.popups, popup);
  if (grab.popups.empty()) {
    grab.client = client;
    grab.seatResource = seatResource;
  } else if (client) {
    grab.client = client;
  }
  grab.popups.push_back(popup);
  popup->grabbed = true;
  popup->grabSeatResource = seatResource;
  return true;
}

inline bool xdgPopupGrabRemove(WaylandServer::Impl::XdgPopupGrab& grab,
                               WaylandServer::Impl::XdgPopup* popup) {
  if (!popup) return false;
  auto const before = grab.popups.size();
  std::erase(grab.popups, popup);
  if (before == grab.popups.size()) return false;
  popup->grabbed = false;
  popup->grabSeatResource = nullptr;
  if (grab.popups.empty()) {
    grab.client = nullptr;
    grab.seatResource = nullptr;
  }
  return true;
}

inline void xdgPopupGrabClear(WaylandServer::Impl::XdgPopupGrab& grab) {
  for (auto* popup : grab.popups) {
    if (!popup) continue;
    popup->grabbed = false;
    popup->grabSeatResource = nullptr;
  }
  grab.popups.clear();
  grab.client = nullptr;
  grab.seatResource = nullptr;
}

} // namespace lambda::compositor
