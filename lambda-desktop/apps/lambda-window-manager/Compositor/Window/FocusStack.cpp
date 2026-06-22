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

namespace lambda::compositor::wm {

void raiseSurface(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface) {
  auto found = std::find_if(server->surfaces_.begin(), server->surfaces_.end(),
                            [surface](auto const& candidate) { return candidate.get() == surface; });
  if (found == server->surfaces_.end() || std::next(found) == server->surfaces_.end()) return;
  auto item = std::move(*found);
  server->surfaces_.erase(found);
  server->surfaces_.push_back(std::move(item));
}

void lowerSurface(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface) {
  auto found = std::find_if(server->surfaces_.begin(), server->surfaces_.end(),
                            [surface](auto const& candidate) { return candidate.get() == surface; });
  if (found == server->surfaces_.end() || found == server->surfaces_.begin()) return;
  auto item = std::move(*found);
  server->surfaces_.erase(found);
  server->surfaces_.insert(server->surfaces_.begin(), std::move(item));
}

WaylandServer::Impl::Surface* surfaceById(WaylandServer::Impl* server, std::uint64_t surfaceId) {
  auto found = std::find_if(server->surfaces_.begin(), server->surfaces_.end(),
                            [surfaceId](auto const& candidate) {
                              return candidate && candidate->id == surfaceId;
                            });
  return found == server->surfaces_.end() ? nullptr : found->get();
}

WaylandServer::Impl::Surface* modalTransientChildFor(WaylandServer::Impl* server,
                                                     WaylandServer::Impl::Surface* surface) {
  if (!server || !surface) return nullptr;
  auto* parent = toplevelForSurface(server, surface);
  if (!parent) return nullptr;

  for (auto it = server->surfaces_.rbegin(); it != server->surfaces_.rend(); ++it) {
    WaylandServer::Impl::Surface* candidateSurface = it->get();
    if (!candidateSurface || candidateSurface == surface || !surfaceIsXdgToplevel(candidateSurface) ||
        candidateSurface->minimized) {
      continue;
    }
    WaylandServer::Impl::XdgToplevel* candidate = toplevelForSurface(server, candidateSurface);
    if (!candidate || !candidate->mapped) continue;
    for (WaylandServer::Impl::XdgToplevel* ancestor = candidate->parent; ancestor; ancestor = ancestor->parent) {
      if (ancestor == parent) return candidateSurface;
    }
  }
  return nullptr;
}

void noteFocusedToplevel(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface) {
  if (!server || !isManagedToplevel(surface)) return;
  removeSurfaceFromFocusOrder(server, surface);
  server->focusOrder_.push_back(surface);
}

WaylandServer::Impl::Surface* mostRecentToplevel(WaylandServer::Impl* server) {
  if (!server) return nullptr;
  return mostRecentToplevelFromOrders(server->focusOrder_, server->surfaces_);
}

void sendKeyboardModifiers(WaylandServer::Impl* server) {
  if (!server->keyboardFocus_) return;
  std::uint32_t const depressed = keyboardModifierMask(server);
  std::uint32_t const latched = keyboardLatchedModifierMask(server);
  std::uint32_t const locked = keyboardLockedModifierMask(server);
  std::uint32_t const group = keyboardLayoutIndex(server);
  std::uint32_t const serial =
      issueSeatSerialForSurface(server, SeatSerialKind::KeyboardModifiers, server->keyboardFocus_);
  for (wl_resource* keyboard : server->keyboardResources_) {
    if (!resourceBelongsToSurfaceClient(keyboard, server->keyboardFocus_)) continue;
    wl_keyboard_send_modifiers(keyboard, serial, depressed, latched, locked, group);
  }
}

WaylandServer::Impl::Surface* previousFocusedToplevel(WaylandServer::Impl* server,
                                                      WaylandServer::Impl::Surface* current) {
  if (!server) return nullptr;
  return previousFocusedToplevelFromOrders(server->focusOrder_, server->surfaces_, current);
}

WaylandServer::Impl::XdgToplevel* focusedToplevel(WaylandServer::Impl* server) {
  if (!server->keyboardFocus_) return nullptr;
  if (auto* toplevel = toplevelForSurface(server, server->keyboardFocus_)) return toplevel;
  if (auto* popup = server->keyboardFocus_->xdgPopup; popup && popup->parentSurface) {
    return toplevelForSurface(server, popup->parentSurface);
  }
  return nullptr;
}

bool closeFocusedToplevel(WaylandServer::Impl* server) {
  if (topmostPopup(server)) return dismissTopPopup(server);
  WaylandServer::Impl::XdgToplevel* toplevel = focusedToplevel(server);
  if (!toplevel || !toplevel->resource) return false;
  xdg_toplevel_send_close(toplevel->resource);
  return true;
}

bool focusCycleActive(WaylandServer::Impl const* server) {
  return server && !server->focusCycleList_.empty();
}

void clearFocusCycle(WaylandServer::Impl* server) {
  if (!server) return;
  bool const wasActive = !server->focusCycleList_.empty();
  server->focusCycleList_.clear();
  server->focusCycleIndex_ = 0;
  server->focusCycleStartedAtMs_ = 0;
  server->focusCycleOverlayShown_ = false;
  if (wasActive) {
    ++server->contentSerial_;
    server->notifyShellStateChanged();
  }
}

bool cycleFocus(WaylandServer::Impl* server, std::uint32_t timeMs, bool forward) {
  if (!server) return false;
  if (server->focusCycleList_.empty()) {
    server->focusCycleList_ =
        focusCycleListFromOrders(server->focusOrder_, server->surfaces_, server->keyboardFocus_);
    server->focusCycleStartedAtMs_ = timeMs;
    server->focusCycleOverlayShown_ = false;
    auto current = std::find(server->focusCycleList_.begin(),
                             server->focusCycleList_.end(),
                             server->keyboardFocus_);
    server->focusCycleIndex_ =
        current == server->focusCycleList_.end()
            ? 0u
            : static_cast<std::size_t>(std::distance(server->focusCycleList_.begin(), current));
  }
  if (server->focusCycleList_.size() < 2u) {
    clearFocusCycle(server);
    return false;
  }

  std::size_t remaining = server->focusCycleList_.size();
  while (remaining-- > 0u) {
    server->focusCycleIndex_ =
        advancedFocusCycleIndex(server->focusCycleIndex_, server->focusCycleList_.size(), forward);
    WaylandServer::Impl::Surface* target = server->focusCycleList_[server->focusCycleIndex_];
    if (!surfaceFocusableInOrder(target)) continue;
    lambda::compositor::focusSurface(server, target, timeMs);
    return true;
  }
  clearFocusCycle(server);
  return false;
}

} // namespace lambda::compositor::wm

namespace lambda::compositor {

std::optional<int> WaylandServer::Impl::windowCyclerWakeDelayMs() const {
  if (focusCycleList_.size() < 2u || focusCycleStartedAtMs_ == 0 || focusCycleOverlayShown_) {
    return std::nullopt;
  }
  std::uint32_t const elapsed = wm::monotonicMilliseconds() - focusCycleStartedAtMs_;
  if (elapsed >= wm::kWindowCyclerOverlayDelayMs) return 0;
  return static_cast<int>(wm::kWindowCyclerOverlayDelayMs - elapsed);
}

using wm::mostRecentToplevel;
using wm::raiseSurface;
using wm::setKeyboardFocus;
using wm::sendPointerFocus;
using wm::isManagedToplevel;
using wm::lowerSurface;
using wm::markToplevelMinimized;
using wm::previousFocusedToplevel;

void removeSurfaceFromFocusOrder(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface) {
  if (!server || !surface) return;
  server->focusOrder_.erase(std::remove(server->focusOrder_.begin(), server->focusOrder_.end(), surface),
                            server->focusOrder_.end());
}

void activateMostRecentToplevel(WaylandServer::Impl* server, std::uint32_t timeMs) {
  WaylandServer::Impl::Surface* target = mostRecentToplevel(server);
  if (!target) return;
  focusSurface(server, target, timeMs);
}

void focusSurface(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface, std::uint32_t timeMs) {
  if (!surface) return;
  if (WaylandServer::Impl::Surface* modalChild = wm::modalTransientChildFor(server, surface)) {
    surface = modalChild;
  }
  surface->minimized = false;
  raiseSurface(server, surface);
  setKeyboardFocus(server, surface);
  sendPointerFocus(server, surfaceAt(server, server->pointerX_, server->pointerY_), timeMs);
}

void minimizeToplevel(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface, std::uint32_t timeMs) {
  if (!markToplevelMinimized(surface)) return;
  lowerSurface(server, surface);
  bool shellStateNotified = false;
  if (server->keyboardFocus_ == surface) {
    setKeyboardFocus(server, previousFocusedToplevel(server, surface));
    shellStateNotified = true;
  }
  if (server->pointerFocus_ == surface) {
    sendPointerFocus(server, surfaceAt(server, server->pointerX_, server->pointerY_), timeMs);
  }
  if (!shellStateNotified) {
    ++server->contentSerial_;
    server->notifyShellStateChanged();
  }
}

} // namespace lambda::compositor
