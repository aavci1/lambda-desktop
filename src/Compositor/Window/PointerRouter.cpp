#include "Compositor/Window/WindowManagerInternal.hpp"

#include "Compositor/Wayland/WaylandServerImpl.hpp"
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

namespace flux::compositor::wm {

WaylandServer::Impl::Surface* subsurfaceAt(WaylandServer::Impl* server,
                                           WaylandServer::Impl::Surface* parent,
                                           float parentX,
                                           float parentY,
                                           float x,
                                           float y) {
  if (!server || !parent) return nullptr;
  for (auto it = server->subsurfaces_.rbegin(); it != server->subsurfaces_.rend(); ++it) {
    auto const& subsurface = *it;
    if (!subsurface || subsurface->parent != parent || !subsurface->surface) continue;
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
}

} // namespace flux::compositor::wm

namespace flux::compositor {

using wm::aboveWindowLayerAt;
using wm::ChromeHitContext;
using wm::controlsRegionContains;
using wm::inputRegionContains;
using wm::popupAt;
using wm::subsurfaceAt;
using wm::surfaceUsesCutouts;

WaylandServer::Impl::Surface* surfaceAt(WaylandServer::Impl* server, float x, float y) {
  if (auto* layer = aboveWindowLayerAt(server, x, y)) return layer;
  if (auto* popup = popupAt(server, x, y)) return popup;
  if (server->popupGrabsEnabled_ && server->grabPopup_) return nullptr;

  for (auto it = server->surfaces_.rbegin(); it != server->surfaces_.rend(); ++it) {
    WaylandServer::Impl::Surface* surface = it->get();
    std::int32_t const width = displayWidth(surface);
    std::int32_t const height = displayHeight(surface);
    if (!surface || !surfaceIsTopLevelRenderable(surface) || surfaceIsXdgPopup(surface) ||
        surface->minimized || width <= 0 || height <= 0) {
      continue;
    }
    float const left = static_cast<float>(surface->windowX);
    float const top = static_cast<float>(surface->windowY);
    float const right = left + static_cast<float>(width);
    float const bottom = top + static_cast<float>(height);
    if (x >= left && x < right && y >= top && y < bottom) {
      if (!inputRegionContains(surface, x - left, y - top)) continue;
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
      if (WaylandServer::Impl::Surface* subsurface = subsurfaceAt(server, surface, left, top, x, y)) {
        return subsurface;
      }
      return surface;
    }
  }
  return nullptr;
}

} // namespace flux::compositor

namespace flux::compositor::wm {

WaylandServer::Impl::Surface* titlebarAt(WaylandServer::Impl* server, float x, float y) {
  auto context = topChromeHitContext(server, x, y);
  if (!context) return nullptr;
  if (!context->serverSideDecorated || context->cutouts) return nullptr;
  return containsPoint(x, y, context->left, context->top, context->right, context->contentTop)
             ? context->surface
             : nullptr;
}

} // namespace flux::compositor::wm

namespace flux::compositor::wm {

WaylandServer::Impl::Surface* closeButtonAt(WaylandServer::Impl* server, float x, float y) {
  auto context = topChromeHitContext(server, x, y);
  if (!context) return nullptr;
  return chromeButtonAt(*context, x, y) == ChromeButton::Close ? context->surface : nullptr;
}

} // namespace flux::compositor::wm

namespace flux::compositor::wm {

WaylandServer::Impl::Surface* minimizeButtonAt(WaylandServer::Impl* server, float x, float y) {
  auto context = topChromeHitContext(server, x, y);
  if (!context) return nullptr;
  return chromeButtonAt(*context, x, y) == ChromeButton::Minimize ? context->surface : nullptr;
}

} // namespace flux::compositor::wm

namespace flux::compositor::wm {

WaylandServer::Impl::Surface* maximizeButtonAt(WaylandServer::Impl* server, float x, float y) {
  auto context = topChromeHitContext(server, x, y);
  if (!context) return nullptr;
  return chromeButtonAt(*context, x, y) == ChromeButton::Maximize ? context->surface : nullptr;
}

} // namespace flux::compositor::wm

namespace flux::compositor::wm {

WaylandServer::Impl::Surface* resizeGripAt(WaylandServer::Impl* server, float x, float y, std::uint32_t& edges) {
  edges = XDG_TOPLEVEL_RESIZE_EDGE_NONE;
  auto context = topChromeHitContext(server, x, y);
  if (!context) return nullptr;

  edges = resizeEdgesForContext(*context, x, y);
  if (edges == XDG_TOPLEVEL_RESIZE_EDGE_NONE) return nullptr;
  return context->surface;
}

} // namespace flux::compositor::wm

namespace flux::compositor::wm {

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

} // namespace flux::compositor::wm

namespace flux::compositor::wm {

void setCompositorCursorOverride(WaylandServer::Impl* server, CursorShape shape) {
  if (!server) return;
  server->compositorCursorOverride_ = true;
  server->compositorCursorShape_ = shape;
}

} // namespace flux::compositor::wm

namespace flux::compositor::wm {

void clearCompositorCursorOverride(WaylandServer::Impl* server) {
  if (!server) return;
  server->compositorCursorOverride_ = false;
  server->compositorCursorShape_ = CursorShape::Arrow;
}

} // namespace flux::compositor::wm

namespace flux::compositor::wm {

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

} // namespace flux::compositor::wm

namespace flux::compositor::wm {

bool resourceBelongsToSurfaceClient(wl_resource* resource, WaylandServer::Impl::Surface const* surface) {
  return resource && surface && surface->resource &&
         wl_resource_get_client(resource) == wl_resource_get_client(surface->resource);
}

} // namespace flux::compositor::wm

namespace flux::compositor::wm {

void sendPointerFocus(WaylandServer::Impl* server, WaylandServer::Impl::Surface* next, std::uint32_t timeMs) {
  if (server->pointerFocus_ == next) {
    if (!next) return;
    wl_fixed_t const x = wl_fixed_from_double(server->pointerX_ - static_cast<float>(next->windowX));
    wl_fixed_t const y = wl_fixed_from_double(server->pointerY_ - static_cast<float>(next->windowY));
    for (wl_resource* pointer : server->pointerResources_) {
      if (!resourceBelongsToSurfaceClient(pointer, next)) continue;
      wl_pointer_send_motion(pointer, timeMs, x, y);
      if (wl_resource_get_version(pointer) >= WL_POINTER_FRAME_SINCE_VERSION) wl_pointer_send_frame(pointer);
    }
    return;
  }

  std::uint32_t serial = server->nextInputSerial_++;
  WaylandServer::Impl::Surface* previous = server->pointerFocus_;
  if (previous) {
    for (wl_resource* pointer : server->pointerResources_) {
      if (!resourceBelongsToSurfaceClient(pointer, previous)) continue;
      wl_pointer_send_leave(pointer, serial, previous->resource);
      if (wl_resource_get_version(pointer) >= WL_POINTER_FRAME_SINCE_VERSION) wl_pointer_send_frame(pointer);
    }
  }
  server->pointerFocus_ = next;
  clearCompositorCursorOverride(server);
  server->cursorSurface_ = nullptr;
  server->cursorShape_ = CursorShape::Arrow;
  if (next) {
    wl_fixed_t const x = wl_fixed_from_double(server->pointerX_ - static_cast<float>(next->windowX));
    wl_fixed_t const y = wl_fixed_from_double(server->pointerY_ - static_cast<float>(next->windowY));
    server->pointerEnterSerial_ = serial;
    for (wl_resource* pointer : server->pointerResources_) {
      if (!resourceBelongsToSurfaceClient(pointer, next)) continue;
      wl_pointer_send_enter(pointer, serial, next->resource, x, y);
      if (wl_resource_get_version(pointer) >= WL_POINTER_FRAME_SINCE_VERSION) wl_pointer_send_frame(pointer);
    }
  }
  updatePointerConstraintsForFocus(server);
}

} // namespace flux::compositor::wm

namespace flux::compositor {

using wm::popupForSurface;
using wm::popupIsDescendantOf;

bool surfaceInGrabSubtree(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface) {
  if (!server || !server->popupGrabsEnabled_ || !server->grabPopup_ || !surface) return false;
  if (server->grabPopup_->xdgSurface && surface == server->grabPopup_->xdgSurface->surface) return true;
  WaylandServer::Impl::XdgPopup* popup = popupForSurface(server, surface);
  return popup && popupIsDescendantOf(server, popup, server->grabPopup_);
}

} // namespace flux::compositor

namespace flux::compositor {

using wm::popupForSurface;
using wm::popupTrace;
using wm::sendPointerFocus;
using wm::setKeyboardFocus;
using wm::dismissPopup;

void releasePopupGrab(WaylandServer::Impl* server, WaylandServer::Impl::XdgPopup* popup, std::uint32_t timeMs) {
  if (!server || !popup || server->grabPopup_ != popup) return;
  popupTrace("lambda-window-manager: xdg_popup grab released surface=%llu\n",
             static_cast<unsigned long long>(popup->xdgSurface && popup->xdgSurface->surface
                                                 ? popup->xdgSurface->surface->id
                                                 : 0));
  server->grabPopup_ = nullptr;
  popup->grabbed = false;

  WaylandServer::Impl::Surface* parent = popup->parentSurface;
  if (parent && surfaceIsXdgPopup(parent)) {
    WaylandServer::Impl::XdgPopup* parentPopup = popupForSurface(server, parent);
    if (parentPopup && parentPopup->grabbed && !parentPopup->dismissed) {
      server->grabPopup_ = parentPopup;
      setKeyboardFocus(server, parentPopup->xdgSurface->surface);
      sendPointerFocus(server, surfaceAt(server, server->pointerX_, server->pointerY_), timeMs);
    }
  }
}

} // namespace flux::compositor

namespace flux::compositor {

using wm::dismissPopup;
using wm::popupForSurface;
using wm::popupTrace;
using wm::sendPointerFocus;
using wm::setKeyboardFocus;

void establishPopupGrab(WaylandServer::Impl* server,
                        WaylandServer::Impl::XdgPopup* popup,
                        wl_resource* /*seat*/,
                        std::uint32_t serial) {
  if (!server || !popup || popup->dismissed || !popup->resource || !popup->xdgSurface ||
      !popup->xdgSurface->surface) {
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

  if (serial < server->pointerEnterSerial_ && serial != server->lastPointerButtonSerial_) {
    wl_resource_post_error(popup->resource, XDG_POPUP_ERROR_INVALID_GRAB, "stale grab serial");
    return;
  }

  if (server->grabPopup_ && server->grabPopup_ != popup) {
    server->grabPopup_->grabbed = false;
  }

  popup->grabbed = true;
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

} // namespace flux::compositor

namespace flux::compositor::wm {

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

} // namespace flux::compositor::wm

namespace flux::compositor::wm {

WaylandServer::Impl::XdgPopup* topmostPopup(WaylandServer::Impl* server) {
  for (auto it = server->popups_.rbegin(); it != server->popups_.rend(); ++it) {
    WaylandServer::Impl::XdgPopup* popup = it->get();
    if (!popup || popup->dismissed || !popup->resource || !popup->xdgSurface || !popup->xdgSurface->surface) continue;
    return popup;
  }
  return nullptr;
}

} // namespace flux::compositor::wm

namespace flux::compositor::wm {

bool surfaceBelongsToPopup(WaylandServer::Impl::Surface* surface, WaylandServer::Impl::XdgPopup* popup) {
  return surface && popup && popup->xdgSurface && surface == popup->xdgSurface->surface;
}

} // namespace flux::compositor::wm

namespace flux::compositor::wm {

bool dismissPopup(WaylandServer::Impl::XdgPopup* popup) {
  if (!popup || popup->dismissed) return false;
  popupTrace("lambda-window-manager: xdg_popup dismissed surface=%llu\n",
             static_cast<unsigned long long>(popup->xdgSurface && popup->xdgSurface->surface
                                                 ? popup->xdgSurface->surface->id
                                                 : 0));
  if (popup->server->grabPopup_ == popup) {
    releasePopupGrab(popup->server, popup, 0);
  }
  popup->dismissed = true;
  bool const restoreToplevelFocus = popup->xdgSurface &&
                                    popup->xdgSurface->surface &&
                                    popup->server->keyboardFocus_ == popup->xdgSurface->surface;
  if (popup->xdgSurface && popup->xdgSurface->surface) {
    if (popup->server->pointerFocus_ == popup->xdgSurface->surface) popup->server->pointerFocus_ = nullptr;
    if (popup->server->keyboardFocus_ == popup->xdgSurface->surface) popup->server->keyboardFocus_ = nullptr;
  }
  if (popup->resource) xdg_popup_send_popup_done(popup->resource);
  if (restoreToplevelFocus) activateMostRecentToplevel(popup->server, 0);
  popup->server->flushClients();
  return true;
}

} // namespace flux::compositor::wm

namespace flux::compositor::wm {

bool dismissTopPopup(WaylandServer::Impl* server) {
  return dismissPopup(topmostPopup(server));
}

} // namespace flux::compositor::wm

namespace flux::compositor::wm {

bool dismissTopPopupOutside(WaylandServer::Impl* server, WaylandServer::Impl::Surface* target) {
  if (server->popupGrabsEnabled_ && server->grabPopup_) {
    if (surfaceInGrabSubtree(server, target)) return false;
    WaylandServer::Impl::XdgPopup* popup = topmostPopup(server);
    return popup ? dismissPopup(popup) : false;
  }
  WaylandServer::Impl::XdgPopup* popup = topmostPopup(server);
  if (!popup || surfaceBelongsToPopup(target, popup)) return false;
  return dismissPopup(popup);
}

} // namespace flux::compositor::wm

namespace flux::compositor::wm {

void setKeyboardFocus(WaylandServer::Impl* server, WaylandServer::Impl::Surface* next) {
  if (server->keyboardFocus_ == next) return;
  std::uint32_t serial = server->nextInputSerial_++;
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
    for (wl_resource* keyboard : server->keyboardResources_) {
      if (!resourceBelongsToSurfaceClient(keyboard, next)) continue;
      wl_keyboard_send_enter(keyboard, serial, next->resource, &keys);
      wl_keyboard_send_modifiers(keyboard, server->nextInputSerial_++, modifiers, 0, 0, 0);
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

} // namespace flux::compositor::wm

namespace flux::compositor::wm {

std::uint32_t modifierBit(std::uint32_t index, bool active) {
  if (!active || index == kInvalidModifierIndex || index >= 32u) return 0u;
  return 1u << index;
}

} // namespace flux::compositor::wm

namespace flux::compositor::wm {

std::uint32_t keyboardModifierMask(WaylandServer::Impl* server) {
  return modifierBit(server->shiftModifierIndex_, server->shiftDown_) |
         modifierBit(server->ctrlModifierIndex_, server->ctrlDown_) |
         modifierBit(server->altModifierIndex_, server->altDown_) |
         modifierBit(server->logoModifierIndex_, server->metaDown_);
}

} // namespace flux::compositor::wm
