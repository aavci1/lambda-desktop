#include "Compositor/Window/WindowManagerInternal.hpp"

#include "Compositor/Wayland/SeatFocusState.hpp"
#include "Compositor/Wayland/WaylandServerImpl.hpp"
#include "Compositor/Wayland/XdgPopupState.hpp"
#include "Compositor/Chrome/ChromeMetrics.hpp"
#include "Compositor/Window/WindowGeometry.hpp"
#include "Detail/ResizeTrace.hpp"
#include "pointer-constraints-unstable-v1-server-protocol.h"
#include "relative-pointer-unstable-v1-server-protocol.h"
#include "wlr-layer-shell-unstable-v1-server-protocol.h"
#include "xdg-shell-server-protocol.h"

#include <linux/input-event-codes.h>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <chrono>
#include <optional>
#include <vector>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <xkbcommon/xkbcommon.h>

namespace lambdaui::compositor::wm {

WaylandServer::Impl::Surface* subsurfaceAt(WaylandServer::Impl* server,
                                           WaylandServer::Impl::Surface* parent,
                                           float parentX,
                                           float parentY,
                                           float x,
                                           float y) {
  if (!server || !parent) return nullptr;
  auto hitLayer = [&](SubsurfaceStackLayer layer) -> WaylandServer::Impl::Surface* {
    auto subsurfaces = orderedSubsurfacesForParent(server, parent, layer);
    for (auto it = subsurfaces.rbegin(); it != subsurfaces.rend(); ++it) {
      auto const* subsurface = *it;
      if (!subsurface || !subsurface->surface) continue;
      WaylandServer::Impl::Surface* surface = subsurface->surface;
      std::int32_t const width = displayWidth(surface);
      std::int32_t const height = displayHeight(surface);
      if (width <= 0 || height <= 0) continue;
      float const left = parentX + static_cast<float>(subsurface->x);
      float const top = parentY + static_cast<float>(subsurface->y);
      float const right = left + static_cast<float>(width);
      float const bottom = top + static_cast<float>(height);
      if (!containsPoint(x, y, left, top, right, bottom)) continue;
      if (!inputRegionContains(surface, x - left, y - top)) continue;
      if (WaylandServer::Impl::Surface* nested = subsurfaceAt(server, surface, left, top, x, y)) return nested;
      return surface;
    }
    return nullptr;
  };
  if (WaylandServer::Impl::Surface* above = hitLayer(SubsurfaceStackLayer::Above)) return above;
  if (WaylandServer::Impl::Surface* below = hitLayer(SubsurfaceStackLayer::Below)) return below;
  return nullptr;
}

} // namespace lambdaui::compositor::wm

namespace lambdaui::compositor {

using wm::aboveWindowLayerAt;
using wm::ChromeHitContext;
using wm::controlsRegionContains;
using wm::inputRegionContains;
using wm::popupAt;
using wm::surfaceBufferOriginX;
using wm::surfaceBufferOriginY;
using wm::surfaceLocalX;
using wm::surfaceLocalY;
using wm::subsurfaceAt;
using wm::surfaceUsesCutouts;

namespace {

wl_client* popupGrabClient(WaylandServer::Impl* server) {
  if (!server || !server->popupGrabsEnabled_) return nullptr;
  auto* popup = xdgPopupGrabSyncTop(server->popupGrab_, server->grabPopup_);
  if (!popup) return nullptr;
  if (server->popupGrab_.client) return server->popupGrab_.client;
  return popup->resource ? wl_resource_get_client(popup->resource) : nullptr;
}

bool surfaceBelongsToClient(WaylandServer::Impl::Surface const* surface, wl_client* client) {
  return surface && surface->resource && client && wl_resource_get_client(surface->resource) == client;
}

} // namespace

WaylandServer::Impl::Surface* surfaceAt(WaylandServer::Impl* server, float x, float y) {
  wl_client* const grabClient = popupGrabClient(server);
  if (auto* layer = aboveWindowLayerAt(server, x, y)) {
    return !grabClient || surfaceBelongsToClient(layer, grabClient) ? layer : nullptr;
  }
  if (auto* popup = popupAt(server, x, y)) return popup;

  for (auto it = server->surfaces_.rbegin(); it != server->surfaces_.rend(); ++it) {
    WaylandServer::Impl::Surface* surface = it->get();
    wm::FrameDisplaySize const frameSize = wm::interactiveFrameDisplaySize(surface);
    std::int32_t const width = frameSize.width;
    std::int32_t const height = frameSize.height;
    if (!surface || !surfaceIsTopLevelRenderable(surface) || surfaceIsXdgPopup(surface) ||
        surface->minimized || width <= 0 || height <= 0) {
      continue;
    }
    float const left = static_cast<float>(surface->windowX);
    float const top = static_cast<float>(surface->windowY);
    float const right = left + static_cast<float>(width);
    float const bottom = top + static_cast<float>(height);
    if (x >= left && x < right && y >= top && y < bottom) {
      if (!inputRegionContains(surface, surfaceLocalX(surface, x), surfaceLocalY(surface, y))) continue;
      if (grabClient && !surfaceBelongsToClient(surface, grabClient)) return nullptr;
      if (surfaceUsesCutouts(server, surface)) {
        ChromeHitContext context{
            .surface = surface,
            .left = left,
            .top = top,
            .right = right,
            .bottom = bottom,
            .contentTop = top,
            .cornerRadius = server ? server->chromeConfig_.windowCornerRadius : CornerRadius{14.f},
            .serverSideDecorated = true,
            .cutouts = true,
        };
        if (controlsRegionContains(context, x, y)) return nullptr;
      }
      if (WaylandServer::Impl::Surface* subsurface = subsurfaceAt(server,
                                                                  surface,
                                                                  surfaceBufferOriginX(surface),
                                                                  surfaceBufferOriginY(surface),
                                                                  x,
                                                                  y)) {
        return subsurface;
      }
      return surface;
    }
  }
  return nullptr;
}

} // namespace lambdaui::compositor

namespace lambdaui::compositor::wm {

WaylandServer::Impl::Surface* titlebarAt(WaylandServer::Impl* server, float x, float y) {
  auto context = topChromeHitContext(server, x, y);
  if (!context) return nullptr;
  if (!context->serverSideDecorated || context->cutouts) return nullptr;
  return containsPoint(x, y, context->left, context->top, context->right, context->contentTop)
             ? context->surface
             : nullptr;
}

} // namespace lambdaui::compositor::wm

namespace lambdaui::compositor::wm {

WaylandServer::Impl::Surface* closeButtonAt(WaylandServer::Impl* server, float x, float y) {
  auto context = topChromeHitContext(server, x, y);
  if (!context) return nullptr;
  return chromeButtonAt(*context, x, y) == ChromeButton::Close ? context->surface : nullptr;
}

} // namespace lambdaui::compositor::wm

namespace lambdaui::compositor::wm {

WaylandServer::Impl::Surface* minimizeButtonAt(WaylandServer::Impl* server, float x, float y) {
  auto context = topChromeHitContext(server, x, y);
  if (!context) return nullptr;
  return chromeButtonAt(*context, x, y) == ChromeButton::Minimize ? context->surface : nullptr;
}

} // namespace lambdaui::compositor::wm

namespace lambdaui::compositor::wm {

WaylandServer::Impl::Surface* maximizeButtonAt(WaylandServer::Impl* server, float x, float y) {
  auto context = topChromeHitContext(server, x, y);
  if (!context) return nullptr;
  return chromeButtonAt(*context, x, y) == ChromeButton::Maximize ? context->surface : nullptr;
}

} // namespace lambdaui::compositor::wm

namespace lambdaui::compositor::wm {

WaylandServer::Impl::Surface* resizeGripAt(WaylandServer::Impl* server, float x, float y, std::uint32_t& edges) {
  edges = XDG_TOPLEVEL_RESIZE_EDGE_NONE;
  auto context = topChromeHitContext(server, x, y);
  if (!context) return nullptr;

  edges = resizeEdgesForContext(*context, x, y);
  if (edges == XDG_TOPLEVEL_RESIZE_EDGE_NONE) return nullptr;
  return context->surface;
}

} // namespace lambdaui::compositor::wm

namespace lambdaui::compositor::wm {

WaylandServer::Impl::Surface* resizeOrCloseChromeAt(WaylandServer::Impl* server,
                                                    float x,
                                                    float y,
                                                    bool& closeButton,
                                                    std::uint32_t& resizeEdges) {
  closeButton = false;
  resizeEdges = XDG_TOPLEVEL_RESIZE_EDGE_NONE;
  auto context = topChromeHitContext(server, x, y);
  if (!context) return nullptr;

  ChromeButton const button = chromeButtonAt(*context, x, y);
  if (button != ChromeButton::None) {
    closeButton = button == ChromeButton::Close;
    return context->surface;
  }

  resizeEdges = resizeEdgesForContext(*context, x, y);
  if (resizeEdges == XDG_TOPLEVEL_RESIZE_EDGE_NONE) return nullptr;
  return context->surface;
}

} // namespace lambdaui::compositor::wm

namespace lambdaui::compositor::wm {

void setCompositorCursorOverride(WaylandServer::Impl* server, CursorShape shape) {
  if (!server) return;
  server->compositorCursorOverride_ = true;
  server->compositorCursorShape_ = shape;
}

} // namespace lambdaui::compositor::wm

namespace lambdaui::compositor::wm {

void clearCompositorCursorOverride(WaylandServer::Impl* server) {
  if (!server) return;
  server->compositorCursorOverride_ = false;
  server->compositorCursorShape_ = CursorShape::Arrow;
}

} // namespace lambdaui::compositor::wm

namespace lambdaui::compositor::wm {

void updateCompositorCursorForPointer(WaylandServer::Impl* server) {
  if (!server) return;
  if (server->dragSurface_ || server->dndSource_) {
    clearCompositorCursorOverride(server);
    return;
  }
  if (server->resizeSurface_) {
    setCompositorCursorOverride(server, cursorShapeForResizeEdges(server->resizeEdges_));
    return;
  }
  bool closeButton = false;
  std::uint32_t edges = XDG_TOPLEVEL_RESIZE_EDGE_NONE;
  if (resizeOrCloseChromeAt(server, server->pointerX_, server->pointerY_, closeButton, edges) && closeButton) {
    clearCompositorCursorOverride(server);
    return;
  }
  if (edges != XDG_TOPLEVEL_RESIZE_EDGE_NONE) {
    setCompositorCursorOverride(server, cursorShapeForResizeEdges(edges));
    return;
  }
  clearCompositorCursorOverride(server);
}

} // namespace lambdaui::compositor::wm

namespace lambdaui::compositor::wm {

bool resourceBelongsToSurfaceClient(wl_resource* resource, WaylandServer::Impl::Surface const* surface) {
  return resource && surface && surface->resource &&
         wl_resource_get_client(resource) == wl_resource_get_client(surface->resource);
}

} // namespace lambdaui::compositor::wm

namespace lambdaui::compositor::wm {

namespace {

std::uint64_t traceSurfaceId(WaylandServer::Impl::Surface const* surface) {
  return surface ? surface->id : 0;
}

std::uint64_t traceGrabPopupSurfaceId(WaylandServer::Impl const* server) {
  auto* popup = server ? xdgPopupGrabTop(server->popupGrab_) : nullptr;
  return popup && popup->xdgSurface && popup->xdgSurface->surface ? popup->xdgSurface->surface->id : 0;
}

} // namespace

void sendPointerFocus(WaylandServer::Impl* server, WaylandServer::Impl::Surface* next, std::uint32_t timeMs) {
  if (server->pointerFocus_ == next) {
    if (!next) return;
    wl_fixed_t const x = wl_fixed_from_double(surfaceLocalX(next, server->pointerX_));
    wl_fixed_t const y = wl_fixed_from_double(surfaceLocalY(next, server->pointerY_));
    popupTrace("lambda-window-manager: pointer focus motion surface=%llu role=%u local=%.1f,%.1f "
               "global=%.1f,%.1f grab_popup=%llu\n",
               static_cast<unsigned long long>(traceSurfaceId(next)),
               static_cast<unsigned int>(next->role),
               wl_fixed_to_double(x),
               wl_fixed_to_double(y),
               server->pointerX_,
               server->pointerY_,
               static_cast<unsigned long long>(traceGrabPopupSurfaceId(server)));
    for (wl_resource* pointer : server->pointerResources_) {
      if (!resourceBelongsToSurfaceClient(pointer, next)) continue;
      wl_pointer_send_motion(pointer, timeMs, x, y);
      if (wl_resource_get_version(pointer) >= WL_POINTER_FRAME_SINCE_VERSION) wl_pointer_send_frame(pointer);
    }
    return;
  }

  std::uint32_t serial = issueSeatSerialForSurface(server, SeatSerialKind::PointerEnter, next);
  WaylandServer::Impl::Surface* previous = server->pointerFocus_;
  if (previous) {
    for (wl_resource* pointer : server->pointerResources_) {
      if (!resourceBelongsToSurfaceClient(pointer, previous)) continue;
      wl_pointer_send_leave(pointer, serial, previous->resource);
      if (wl_resource_get_version(pointer) >= WL_POINTER_FRAME_SINCE_VERSION) wl_pointer_send_frame(pointer);
    }
  }
  server->pointerFocus_ = next;
  popupTrace("lambda-window-manager: pointer focus change previous=%llu next=%llu role=%u global=%.1f,%.1f "
             "grab_popup=%llu\n",
             static_cast<unsigned long long>(traceSurfaceId(previous)),
             static_cast<unsigned long long>(traceSurfaceId(next)),
             static_cast<unsigned int>(next ? next->role : SurfaceRole::None),
             server->pointerX_,
             server->pointerY_,
             static_cast<unsigned long long>(traceGrabPopupSurfaceId(server)));
  clearCompositorCursorOverride(server);
  bool const cursorChanged = server->cursorSurface_ || server->cursorShape_ != CursorShape::Arrow;
  server->cursorSurface_ = nullptr;
  server->cursorShape_ = CursorShape::Arrow;
  if (cursorChanged) ++server->contentSerial_;
  if (next) {
    wl_fixed_t const x = wl_fixed_from_double(surfaceLocalX(next, server->pointerX_));
    wl_fixed_t const y = wl_fixed_from_double(surfaceLocalY(next, server->pointerY_));
    for (wl_resource* pointer : server->pointerResources_) {
      if (!resourceBelongsToSurfaceClient(pointer, next)) continue;
      wl_pointer_send_enter(pointer, serial, next->resource, x, y);
      if (wl_resource_get_version(pointer) >= WL_POINTER_FRAME_SINCE_VERSION) wl_pointer_send_frame(pointer);
    }
  }
  updatePointerConstraintsForFocus(server);
}

} // namespace lambdaui::compositor::wm

namespace lambdaui::compositor {

using wm::popupForSurface;
using wm::popupIsDescendantOf;

bool surfaceInGrabSubtree(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface) {
  if (!server || !server->popupGrabsEnabled_ || !surface) return false;
  auto* grabPopup = xdgPopupGrabSyncTop(server->popupGrab_, server->grabPopup_);
  if (!grabPopup) return false;
  if (surfaceBelongsToClient(surface, popupGrabClient(server))) return true;
  if (grabPopup->xdgSurface && surface == grabPopup->xdgSurface->surface) return true;
  WaylandServer::Impl::XdgPopup* popup = popupForSurface(server, surface);
  return popup && popupIsDescendantOf(server, popup, grabPopup);
}

} // namespace lambdaui::compositor

namespace lambdaui::compositor {

using wm::popupForSurface;
using wm::popupTrace;
using wm::sendPointerFocus;
using wm::setKeyboardFocus;
using wm::dismissPopup;

void releasePopupGrab(WaylandServer::Impl* server, WaylandServer::Impl::XdgPopup* popup, std::uint32_t timeMs) {
  if (!server || !popup || !xdgPopupGrabContains(server->popupGrab_, popup)) return;
  popupTrace("lambda-window-manager: xdg_popup grab released surface=%llu\n",
             static_cast<unsigned long long>(popup->xdgSurface && popup->xdgSurface->surface
                                                 ? popup->xdgSurface->surface->id
                                                 : 0));
  bool const wasTop = server->grabPopup_ == popup || xdgPopupGrabTop(server->popupGrab_) == popup;
  xdgPopupGrabRemove(server->popupGrab_, popup);
  xdgPopupGrabSyncTop(server->popupGrab_, server->grabPopup_);

  if (wasTop && server->grabPopup_ && server->grabPopup_->xdgSurface && !server->grabPopup_->dismissed) {
    setKeyboardFocus(server, server->grabPopup_->xdgSurface->surface);
    sendPointerFocus(server, surfaceAt(server, server->pointerX_, server->pointerY_), timeMs);
  }
}

} // namespace lambdaui::compositor

namespace lambdaui::compositor {

using wm::dismissPopup;
using wm::popupForSurface;
using wm::popupTrace;
using wm::sendPointerFocus;
using wm::setKeyboardFocus;

namespace {

constexpr std::array<SeatSerialKind, 6> kPopupGrabSerialKinds{
    SeatSerialKind::PointerEnter,
    SeatSerialKind::PointerButtonPress,
    SeatSerialKind::PointerButtonRelease,
    SeatSerialKind::KeyboardEnter,
    SeatSerialKind::KeyboardKey,
    SeatSerialKind::KeyboardModifiers,
};

bool validPopupGrabSerial(WaylandServer::Impl* server,
                          WaylandServer::Impl::XdgPopup* popup,
                          WaylandServer::Impl::Surface*,
                          std::uint32_t serial) {
  if (!server || !popup || !popup->resource) return false;
  wl_client* const client = wl_resource_get_client(popup->resource);
  if (seatSerialIsValid(server, serial, client, nullptr, kPopupGrabSerialKinds)) return true;

  // wlroots accepts xdg-popup grab serials broadly; keep Firefox/GTK submenu
  // grabs working even if the client reuses an older serial outside our ledger.
  return serial != 0;
}

} // namespace

void establishPopupGrab(WaylandServer::Impl* server,
                        WaylandServer::Impl::XdgPopup* popup,
                        wl_resource* seat,
                        std::uint32_t serial) {
  if (!server || !popup || popup->dismissed || !popup->resource || !popup->xdgSurface ||
      !popup->xdgSurface->surface) {
    return;
  }

  if (popup->committed) {
    wl_resource_post_error(popup->resource, XDG_POPUP_ERROR_INVALID_GRAB, "xdg_popup is already mapped");
    return;
  }
  if (!xdgPopupGrabRequestAllowed(popup) || xdgPopupGrabContains(server->popupGrab_, popup)) {
    wl_resource_post_error(popup->resource, XDG_POPUP_ERROR_INVALID_GRAB, "xdg_popup already has an active grab");
    return;
  }
  if (xdgPopupHasLiveChild(server, popup)) {
    wl_resource_post_error(popup->resource,
                           XDG_WM_BASE_ERROR_NOT_THE_TOPMOST_POPUP,
                           "xdg_popup was not created on the topmost popup");
    return;
  }

  WaylandServer::Impl::Surface* parent = popup->parentSurface;
  if (parent && surfaceIsXdgPopup(parent)) {
    WaylandServer::Impl::XdgPopup* parentPopup = popupForSurface(server, parent);
    if (!parentPopup || parentPopup->dismissed) {
      dismissPopup(popup);
      return;
    }
    if (!parentPopup->grabbed) {
      wl_resource_post_error(popup->resource, XDG_POPUP_ERROR_INVALID_GRAB,
                             "parent xdg_popup does not have an active grab");
      return;
    }
  }

  if (!validPopupGrabSerial(server, popup, parent, serial)) {
    popupTrace("lambda-window-manager: xdg_popup grab invalid surface=%llu parent=%llu serial=%u\n",
               static_cast<unsigned long long>(popup->xdgSurface && popup->xdgSurface->surface
                                                   ? popup->xdgSurface->surface->id
                                                   : 0),
               static_cast<unsigned long long>(parent ? parent->id : 0),
               serial);
    wl_resource_post_error(popup->resource, XDG_POPUP_ERROR_INVALID_GRAB, "invalid grab serial");
    return;
  }

  wl_client* const client = wl_resource_get_client(popup->resource);
  xdgPopupGrabPush(server->popupGrab_, popup, client, seat);
  server->grabPopup_ = popup;
  setKeyboardFocus(server, popup->xdgSurface->surface);

  std::uint32_t const timeMs = static_cast<std::uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
          .count());
  sendPointerFocus(server, surfaceAt(server, server->pointerX_, server->pointerY_), timeMs);

  popupTrace("lambda-window-manager: xdg_popup grab surface=%llu parent=%llu serial=%u\n",
             static_cast<unsigned long long>(popup->xdgSurface->surface->id),
             static_cast<unsigned long long>(parent ? parent->id : 0),
             serial);
}

} // namespace lambdaui::compositor

namespace lambdaui::compositor::wm {

void sendRelativePointerMotion(WaylandServer::Impl* server, double dx, double dy, std::uint32_t timeMs) {
  if (!server->pointerFocus_ || (dx == 0.0 && dy == 0.0)) return;
  wl_client* focusedClient = wl_resource_get_client(server->pointerFocus_->resource);
  std::uint64_t const timeUsec = static_cast<std::uint64_t>(timeMs) * 1000ull;
  std::uint32_t const timeHi = static_cast<std::uint32_t>(timeUsec >> 32u);
  std::uint32_t const timeLo = static_cast<std::uint32_t>(timeUsec & 0xffffffffu);
  wl_fixed_t const fixedDx = wl_fixed_from_double(dx);
  wl_fixed_t const fixedDy = wl_fixed_from_double(dy);
  for (auto const& relativePointer : server->relativePointers_) {
    if (!relativePointer->resource || !relativePointer->pointer) continue;
    if (wl_resource_get_client(relativePointer->pointer) != focusedClient) continue;
    zwp_relative_pointer_v1_send_relative_motion(relativePointer->resource,
                                                 timeHi,
                                                 timeLo,
                                                 fixedDx,
                                                 fixedDy,
                                                 fixedDx,
                                                 fixedDy);
  }
}

} // namespace lambdaui::compositor::wm

namespace lambdaui::compositor::wm {

WaylandServer::Impl::XdgPopup* topmostPopup(WaylandServer::Impl* server) {
  for (auto it = server->popups_.rbegin(); it != server->popups_.rend(); ++it) {
    WaylandServer::Impl::XdgPopup* popup = it->get();
    if (!popup || popup->dismissed || !popup->resource || !popup->xdgSurface || !popup->xdgSurface->surface) continue;
    return popup;
  }
  return nullptr;
}

} // namespace lambdaui::compositor::wm

namespace lambdaui::compositor::wm {

bool surfaceBelongsToPopup(WaylandServer::Impl::Surface* surface, WaylandServer::Impl::XdgPopup* popup) {
  if (!surface || !popup || !popup->xdgSurface || !popup->xdgSurface->surface) return false;
  WaylandServer::Impl::Surface* root = popup->xdgSurface->surface;
  if (surface == root) return true;
  for (WaylandServer::Impl::Surface* current = surface; surfaceIsSubsurface(current);) {
    WaylandServer::Impl::Subsurface* role = current->subsurfaceRole;
    if (!role || !role->parent) return false;
    if (role->parent == root) return true;
    current = role->parent;
  }
  return false;
}

} // namespace lambdaui::compositor::wm

namespace lambdaui::compositor::wm {

bool dismissPopup(WaylandServer::Impl::XdgPopup* popup) {
  if (!popup || popup->dismissed) return false;
  auto* server = popup->server;
  if (!server) return false;
  popupTrace("lambda-window-manager: xdg_popup dismissed surface=%llu\n",
             static_cast<unsigned long long>(popup->xdgSurface && popup->xdgSurface->surface
                                                 ? popup->xdgSurface->surface->id
                                                 : 0));
  bool const restoreToplevelFocus =
      keyboardFocusShouldRestoreToplevelAfterPopupDismiss(server->keyboardFocus_, popup);
  bool const reset = resetXdgPopupRole(server, popup, true);
  if (restoreToplevelFocus && server->popupGrab_.popups.empty()) activateMostRecentToplevel(server, 0);
  server->flushClients();
  return reset;
}

} // namespace lambdaui::compositor::wm

namespace lambdaui::compositor::wm {

bool dismissPopupGrab(WaylandServer::Impl* server) {
  if (!server || !server->popupGrabsEnabled_ || server->popupGrab_.popups.empty()) return false;
  auto popups = server->popupGrab_.popups;
  bool const restoreToplevelFocus = keyboardFocusShouldRestoreToplevelAfterGrabDismiss(server->keyboardFocus_);
  popupTrace("lambda-window-manager: xdg_popup grab dismiss count=%zu\n", popups.size());
  for (auto it = popups.rbegin(); it != popups.rend(); ++it) {
    if (*it) resetXdgPopupRole(server, *it, true);
  }
  xdgPopupGrabClear(server->popupGrab_);
  server->grabPopup_ = nullptr;
  if (restoreToplevelFocus) activateMostRecentToplevel(server, 0);
  server->flushClients();
  return true;
}

} // namespace lambdaui::compositor::wm

namespace lambdaui::compositor::wm {

bool dismissTopPopup(WaylandServer::Impl* server) {
  if (keyboardDismissShouldClearPopupGrab({
          .popupGrabsEnabled = server && server->popupGrabsEnabled_,
          .popupGrab = server ? &server->popupGrab_ : nullptr,
          .cachedGrabPopup = server ? &server->grabPopup_ : nullptr,
      })) {
    return dismissPopupGrab(server);
  }
  return dismissPopup(topmostPopup(server));
}

} // namespace lambdaui::compositor::wm

namespace lambdaui::compositor::wm {

bool dismissTopPopupOutside(WaylandServer::Impl* server, WaylandServer::Impl::Surface* target) {
  if (server->popupGrabsEnabled_ && xdgPopupGrabSyncTop(server->popupGrab_, server->grabPopup_)) {
    if (surfaceInGrabSubtree(server, target)) return false;
    return dismissPopupGrab(server);
  }
  if (server->popupGrab_.popups.empty()) xdgPopupGrabSyncTop(server->popupGrab_, server->grabPopup_);
  WaylandServer::Impl::XdgPopup* popup = topmostPopup(server);
  if (!popup || surfaceBelongsToPopup(target, popup)) return false;
  return dismissPopup(popup);
}

} // namespace lambdaui::compositor::wm

namespace lambdaui::compositor::wm {

void setKeyboardFocus(WaylandServer::Impl* server, WaylandServer::Impl::Surface* next) {
  if (!server) return;
  next = keyboardFocusTargetForRequest({
      .popupGrabsEnabled = server->popupGrabsEnabled_,
      .popupGrab = &server->popupGrab_,
      .cachedGrabPopup = &server->grabPopup_,
  }, next);
  if (server->keyboardFocus_ == next) return;
  std::uint32_t serial = issueSeatSerialForSurface(server, SeatSerialKind::KeyboardEnter, next);
  WaylandServer::Impl::Surface* previous = server->keyboardFocus_;
  if (previous) {
    for (wl_resource* keyboard : server->keyboardResources_) {
      if (!resourceBelongsToSurfaceClient(keyboard, previous)) continue;
      wl_keyboard_send_leave(keyboard, serial, previous->resource);
    }
  }
  server->keyboardFocus_ = next;
  ++server->contentSerial_;
  server->notifyShellStateChanged();
  noteFocusedToplevel(server, next);
  if (next) {
    wl_array keys;
    wl_array_init(&keys);
    std::uint32_t const modifiers = keyboardModifierMask(server);
    std::uint32_t const latched = keyboardLatchedModifierMask(server);
    std::uint32_t const locked = keyboardLockedModifierMask(server);
    std::uint32_t const group = keyboardLayoutIndex(server);
    std::uint32_t const modifiersSerial =
        issueSeatSerialForSurface(server, SeatSerialKind::KeyboardModifiers, next);
    for (wl_resource* keyboard : server->keyboardResources_) {
      if (!resourceBelongsToSurfaceClient(keyboard, next)) continue;
      wl_keyboard_send_enter(keyboard, serial, next->resource, &keys);
      wl_keyboard_send_modifiers(keyboard, modifiersSerial, modifiers, latched, locked, group);
    }
    wl_array_release(&keys);
  }
  sendSelectionForFocus(server);
  sendPrimarySelectionForFocus(server);
  if (auto* previousToplevel = toplevelForSurface(server, previous)) {
    sendToplevelStateConfigure(server, previousToplevel);
  }
  if (auto* nextToplevel = toplevelForSurface(server, next)) {
    sendToplevelStateConfigure(server, nextToplevel);
  }
}

} // namespace lambdaui::compositor::wm

namespace lambdaui::compositor::wm {

std::uint32_t modifierBit(std::uint32_t index, bool active) {
  if (!active || index == kInvalidModifierIndex || index >= 32u) return 0u;
  return 1u << index;
}

} // namespace lambdaui::compositor::wm

namespace lambdaui::compositor::wm {

std::uint32_t keyboardModifierMask(WaylandServer::Impl* server) {
  if (server && server->xkbState_) {
    return xkb_state_serialize_mods(server->xkbState_, XKB_STATE_MODS_DEPRESSED);
  }
  return modifierBit(server->shiftModifierIndex_, server->shiftDown_) |
         modifierBit(server->ctrlModifierIndex_, server->ctrlDown_) |
         modifierBit(server->altModifierIndex_, server->altDown_) |
         modifierBit(server->logoModifierIndex_, server->metaDown_);
}

std::uint32_t keyboardLatchedModifierMask(WaylandServer::Impl* server) {
  return server && server->xkbState_
             ? xkb_state_serialize_mods(server->xkbState_, XKB_STATE_MODS_LATCHED)
             : 0u;
}

std::uint32_t keyboardLockedModifierMask(WaylandServer::Impl* server) {
  return server && server->xkbState_
             ? xkb_state_serialize_mods(server->xkbState_, XKB_STATE_MODS_LOCKED)
             : 0u;
}

std::uint32_t keyboardLayoutIndex(WaylandServer::Impl* server) {
  return server && server->xkbState_
             ? xkb_state_serialize_layout(server->xkbState_, XKB_STATE_LAYOUT_EFFECTIVE)
             : 0u;
}

} // namespace lambdaui::compositor::wm
