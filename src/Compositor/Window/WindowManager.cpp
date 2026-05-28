#include "Compositor/Window/WindowManagerInternal.hpp"

#include "Compositor/Wayland/WaylandServerImpl.hpp"
#include "Compositor/Chrome/ChromeMetrics.hpp"
#include "Compositor/Window/WindowGeometry.hpp"
#include "Detail/ResizeTrace.hpp"
#include "Shell/ShellAppRegistry.hpp"
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
#include <string>
#include <vector>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <xkbcommon/xkbcommon.h>

namespace flux::compositor {

using wm::activeDragSnapTarget;
using wm::clearSnapPreview;
using wm::closeFocusedToplevel;
using wm::dismissTopPopup;
using wm::dismissTopPopupOutside;
using wm::handleCompositorShortcut;
using wm::handleScreenshotSelectionKey;
using wm::handleScreenshotSelectionPointerButton;
using wm::handleScreenshotSelectionPointerMotion;
using wm::handleScreenshotSelectionPointerPosition;
using wm::isManagedToplevel;
using wm::kSnapDwellMs;
using wm::kSnapPreviewAnimationMs;
using wm::monotonicMilliseconds;
using wm::raiseSurface;
using wm::resetDragSnapState;
using wm::restoreSurfaceForShellFocus;
using wm::resourceBelongsToSurfaceClient;
using wm::sendPointerFocus;
using wm::sendRelativePointerMotion;
using wm::setKeyboardFocus;
using wm::shellAppIdMatches;
using wm::snapToplevel;
using wm::titlebarAt;
using wm::closeButtonAt;
using wm::minimizeButtonAt;
using wm::maximizeButtonAt;
using wm::resizeOrCloseChromeAt;
using wm::topChromeHitContext;
using wm::toggleMaximizedToplevel;
using wm::updateCompositorCursorForPointer;
using wm::updateDrag;
using wm::updateResize;
using wm::updateShortcutModifier;

namespace {

struct PointerVisualState {
  WaylandServer::Impl::Surface* closeButton = nullptr;
  WaylandServer::Impl::Surface* minimizeButton = nullptr;
  WaylandServer::Impl::Surface* maximizeButton = nullptr;
  bool compositorCursorOverride = false;
  CursorShape compositorCursorShape = CursorShape::Arrow;
};

PointerVisualState pointerVisualState(WaylandServer::Impl* server) {
  if (!server) return {};
  return {
      .closeButton = closeButtonAt(server, server->pointerX_, server->pointerY_),
      .minimizeButton = minimizeButtonAt(server, server->pointerX_, server->pointerY_),
      .maximizeButton = maximizeButtonAt(server, server->pointerX_, server->pointerY_),
      .compositorCursorOverride = server->compositorCursorOverride_,
      .compositorCursorShape = server->compositorCursorShape_,
  };
}

void notePointerVisualStateChanged(WaylandServer::Impl* server, PointerVisualState const& previous) {
  PointerVisualState const current = pointerVisualState(server);
  if (current.closeButton != previous.closeButton ||
      current.minimizeButton != previous.minimizeButton ||
      current.maximizeButton != previous.maximizeButton ||
      current.compositorCursorOverride != previous.compositorCursorOverride ||
      current.compositorCursorShape != previous.compositorCursorShape) {
    ++server->contentSerial_;
  }
}

} // namespace

std::optional<int> WaylandServer::Impl::snapPreviewWakeDelayMs() const {
  if (!dragSurface_ && !snapPreviewVisible_) return std::nullopt;
  auto* server = const_cast<WaylandServer::Impl*>(this);
  std::uint32_t const now = monotonicMilliseconds();
  if (!dragSurface_) {
    if (snapPreviewStartedAtMs_ > 0) return 0;
    return std::nullopt;
  }
  auto const activeTarget = activeDragSnapTarget(server, dragSurface_, now);
  if (activeTarget) {
    if (snapPreviewStartedAtMs_ > 0) return 0;
    return std::nullopt;
  }
  if (!dragSnapTarget_) return std::nullopt;
  std::uint32_t const elapsed = now - dragSnapTargetStartedAtMs_;
  if (elapsed >= kSnapDwellMs) return 0;
  return static_cast<int>(kSnapDwellMs - elapsed);
}
void WaylandServer::Impl::handlePointerMotion(double dx, double dy, std::uint32_t timeMs) {
  if (handleScreenshotSelectionPointerMotion(this, dx, dy, timeMs)) return;
  if (auto* constraint = activePointerConstraint(this);
      constraint && constraint->kind == PointerConstraint::Kind::Lock) {
    sendRelativePointerMotion(this, dx, dy, timeMs);
    return;
  }
  PointerVisualState const previousVisualState = pointerVisualState(this);
  float const scale = std::max(0.5f, preferredScale_);
  pointerX_ = std::clamp(pointerX_ + static_cast<float>(dx) / scale,
                         0.f,
                         std::max(0.f, static_cast<float>(logicalOutputWidth() - 1)));
  pointerY_ = std::clamp(pointerY_ + static_cast<float>(dy) / scale,
                         0.f,
                         std::max(0.f, static_cast<float>(logicalOutputHeight() - 1)));
  if (auto* constraint = activePointerConstraint(this);
      constraint && constraint->kind == PointerConstraint::Kind::Confine && constraint->surface) {
    pointerX_ = std::clamp(pointerX_,
                           static_cast<float>(constraint->surface->windowX),
                           static_cast<float>(constraint->surface->windowX + std::max(0, displayWidth(constraint->surface) - 1)));
    pointerY_ = std::clamp(pointerY_,
                           static_cast<float>(constraint->surface->windowY),
                           static_cast<float>(constraint->surface->windowY + std::max(0, displayHeight(constraint->surface) - 1)));
  }
  if (resizeSurface_) {
    updateResize(this);
    updateCompositorCursorForPointer(this);
    return;
  }
  if (dragSurface_) {
    updateDrag(this);
    updateCompositorCursorForPointer(this);
    return;
  }
  if (dndSource_) {
    updateDndTarget(this, surfaceAt(this, pointerX_, pointerY_), timeMs);
    updateCompositorCursorForPointer(this);
    return;
  }
  sendPointerFocus(this, surfaceAt(this, pointerX_, pointerY_), timeMs);
  updateCompositorCursorForPointer(this);
  notePointerVisualStateChanged(this, previousVisualState);
  sendRelativePointerMotion(this, dx, dy, timeMs);
}
void WaylandServer::Impl::handlePointerPosition(double x, double y, std::uint32_t timeMs) {
  if (handleScreenshotSelectionPointerPosition(this, x, y, timeMs)) return;
  if (auto* constraint = activePointerConstraint(this);
      constraint && constraint->kind == PointerConstraint::Kind::Lock) {
    return;
  }
  PointerVisualState const previousVisualState = pointerVisualState(this);
  float const scale = std::max(0.5f, preferredScale_);
  pointerX_ = std::clamp(static_cast<float>(x) / scale,
                         0.f,
                         std::max(0.f, static_cast<float>(logicalOutputWidth() - 1)));
  pointerY_ = std::clamp(static_cast<float>(y) / scale,
                         0.f,
                         std::max(0.f, static_cast<float>(logicalOutputHeight() - 1)));
  if (auto* constraint = activePointerConstraint(this);
      constraint && constraint->kind == PointerConstraint::Kind::Confine && constraint->surface) {
    pointerX_ = std::clamp(pointerX_,
                           static_cast<float>(constraint->surface->windowX),
                           static_cast<float>(constraint->surface->windowX + std::max(0, displayWidth(constraint->surface) - 1)));
    pointerY_ = std::clamp(pointerY_,
                           static_cast<float>(constraint->surface->windowY),
                           static_cast<float>(constraint->surface->windowY + std::max(0, displayHeight(constraint->surface) - 1)));
  }
  if (resizeSurface_) {
    updateResize(this);
    updateCompositorCursorForPointer(this);
    return;
  }
  if (dragSurface_) {
    updateDrag(this);
    updateCompositorCursorForPointer(this);
    return;
  }
  if (dndSource_) {
    updateDndTarget(this, surfaceAt(this, pointerX_, pointerY_), timeMs);
    updateCompositorCursorForPointer(this);
    return;
  }
  sendPointerFocus(this, surfaceAt(this, pointerX_, pointerY_), timeMs);
  updateCompositorCursorForPointer(this);
  notePointerVisualStateChanged(this, previousVisualState);
}
void WaylandServer::Impl::handlePointerButton(std::uint32_t button, bool pressed, std::uint32_t timeMs) {
  if (handleScreenshotSelectionPointerButton(this, button, pressed, timeMs)) return;
  Surface* target = surfaceAt(this, pointerX_, pointerY_);
  if (button == BTN_LEFT && !pressed && dndSource_) {
    updateDndTarget(this, target, timeMs);
    bool completedDrop = false;
    if (dndTarget_) {
      if (auto* device = dataDeviceForClient(this, wl_resource_get_client(dndTarget_->resource))) {
        if (!dndOffer_ || dndOffer_->selectedAction == WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE) {
          if (dndSource_->resource) wl_data_source_send_cancelled(dndSource_->resource);
          clearDnd(this);
          return;
        }
        wl_data_device_send_drop(device->resource);
        completedDrop = true;
      }
      if (dndSource_->resource && wl_resource_get_version(dndSource_->resource) >= WL_DATA_SOURCE_DND_DROP_PERFORMED_SINCE_VERSION) {
        wl_data_source_send_dnd_drop_performed(dndSource_->resource);
      }
    } else if (dndSource_->resource) {
      wl_data_source_send_cancelled(dndSource_->resource);
    }
    clearDnd(this, !completedDrop);
    return;
  }
  if (pressed && dismissTopPopupOutside(this, target)) return;
  if (button == BTN_LEFT) {
    if (pressed) {
      if (altDown_) {
        Surface* moveTarget = target;
        if (!moveTarget) {
          if (auto context = topChromeHitContext(this, pointerX_, pointerY_)) moveTarget = context->surface;
        }
        if (moveTarget && isManagedToplevel(moveTarget)) {
          raiseSurface(this, moveTarget);
          setKeyboardFocus(this, moveTarget);
          sendPointerFocus(this, nullptr, timeMs);
          dragSurface_ = moveTarget;
          dragOffsetX_ = pointerX_ - static_cast<float>(moveTarget->windowX);
          dragOffsetY_ = pointerY_ - static_cast<float>(moveTarget->windowY);
          clearSnapPreview(this);
          return;
        }
      }

      std::uint32_t resizeEdges = XDG_TOPLEVEL_RESIZE_EDGE_NONE;
      bool closeButton = false;
      Surface* chromeControlTarget = resizeOrCloseChromeAt(this, pointerX_, pointerY_, closeButton, resizeEdges);
      if (chromeControlTarget && closeButton) {
        raiseSurface(this, chromeControlTarget);
        setKeyboardFocus(this, chromeControlTarget);
        closePressSurface_ = chromeControlTarget;
        ++contentSerial_;
        return;
      }
      if (Surface* minimizeTarget = minimizeButtonAt(this, pointerX_, pointerY_)) {
        raiseSurface(this, minimizeTarget);
        setKeyboardFocus(this, minimizeTarget);
        minimizePressSurface_ = minimizeTarget;
        ++contentSerial_;
        return;
      }
      if (Surface* maximizeTarget = maximizeButtonAt(this, pointerX_, pointerY_)) {
        raiseSurface(this, maximizeTarget);
        setKeyboardFocus(this, maximizeTarget);
        maximizePressSurface_ = maximizeTarget;
        ++contentSerial_;
        return;
      }
      if (chromeControlTarget && resizeEdges != XDG_TOPLEVEL_RESIZE_EDGE_NONE) {
        raiseSurface(this, chromeControlTarget);
        setKeyboardFocus(this, chromeControlTarget);
        sendPointerFocus(this, nullptr, timeMs);
        chromeControlTarget->snapped = false;
        chromeControlTarget->maximized = false;
        chromeControlTarget->fullscreen = false;
        chromeControlTarget->geometryAnimationActive = false;
        clearSnapPreview(this);
        resizeSurface_ = chromeControlTarget;
        resizeStartX_ = pointerX_;
        resizeStartY_ = pointerY_;
        resizeStartWindowX_ = chromeControlTarget->windowX;
        resizeStartWindowY_ = chromeControlTarget->windowY;
        resizeStartWidth_ = displayWidth(chromeControlTarget);
        resizeStartHeight_ = displayHeight(chromeControlTarget);
        resizeLastWidth_ = resizeStartWidth_;
        resizeLastHeight_ = resizeStartHeight_;
        resizeEdges_ = resizeEdges;
        updateCompositorCursorForPointer(this);
        flux::detail::resizeTrace("compositor",
                                  "begin-resize surface=%llu pointer=%.1f,%.1f edges=%u startWindow=%d,%d "
                                  "startSize=%dx%d\n",
                                  static_cast<unsigned long long>(chromeControlTarget->id),
                                  pointerX_,
                                  pointerY_,
                                  resizeEdges_,
                                  resizeStartWindowX_,
                                  resizeStartWindowY_,
                                  resizeStartWidth_,
                                  resizeStartHeight_);
        return;
      }
      Surface* chromeTarget = titlebarAt(this, pointerX_, pointerY_);
      if (chromeTarget) {
        raiseSurface(this, chromeTarget);
        setKeyboardFocus(this, chromeTarget);
        sendPointerFocus(this, nullptr, timeMs);
        bool const doubleClick = lastTitleClickSurface_ == chromeTarget &&
                                 timeMs - lastTitleClickTimeMs_ <= 400u;
        lastTitleClickSurface_ = chromeTarget;
        lastTitleClickTimeMs_ = timeMs;
        if (doubleClick) {
          dragSurface_ = nullptr;
          clearSnapPreview(this);
          toggleMaximizedToplevel(this, chromeTarget);
          return;
        }
        dragSurface_ = chromeTarget;
        dragOffsetX_ = pointerX_ - static_cast<float>(chromeTarget->windowX);
        dragOffsetY_ = pointerY_ - static_cast<float>(chromeTarget->windowY);
        clearSnapPreview(this);
        return;
      }
    } else if (closePressSurface_) {
      Surface* closeTarget = closeButtonAt(this, pointerX_, pointerY_);
      if (closeTarget && closeTarget == closePressSurface_) {
        setKeyboardFocus(this, closePressSurface_);
        closeFocusedToplevel(this);
        flushClients();
      }
      closePressSurface_ = nullptr;
      ++contentSerial_;
      sendPointerFocus(this, surfaceAt(this, pointerX_, pointerY_), timeMs);
      updateCompositorCursorForPointer(this);
      return;
    } else if (minimizePressSurface_) {
      Surface* minimizeTarget = minimizeButtonAt(this, pointerX_, pointerY_);
      if (minimizeTarget && minimizeTarget == minimizePressSurface_) {
        minimizeToplevel(this, minimizePressSurface_, timeMs);
        flushClients();
      }
      minimizePressSurface_ = nullptr;
      ++contentSerial_;
      sendPointerFocus(this, surfaceAt(this, pointerX_, pointerY_), timeMs);
      updateCompositorCursorForPointer(this);
      return;
    } else if (maximizePressSurface_) {
      Surface* maximizeTarget = maximizeButtonAt(this, pointerX_, pointerY_);
      if (maximizeTarget && maximizeTarget == maximizePressSurface_) {
        toggleMaximizedToplevel(this, maximizePressSurface_);
        flushClients();
      }
      maximizePressSurface_ = nullptr;
      ++contentSerial_;
      sendPointerFocus(this, surfaceAt(this, pointerX_, pointerY_), timeMs);
      updateCompositorCursorForPointer(this);
      return;
    } else if (resizeSurface_) {
      updateResize(this);
      traceResizeSurface("end-resize", resizeSurface_);
      Surface* resizedSurface = resizeSurface_;
      std::int32_t const finalWidth = resizeLastWidth_;
      std::int32_t const finalHeight = resizeLastHeight_;
      resizeSurface_ = nullptr;
      resizeEdges_ = XDG_TOPLEVEL_RESIZE_EDGE_NONE;
      if (finalWidth > 0 && finalHeight > 0) {
        sendToplevelConfigure(this, toplevelForSurface(this, resizedSurface), finalWidth, finalHeight);
      } else {
        sendToplevelStateConfigure(this, toplevelForSurface(this, resizedSurface));
      }
      sendPointerFocus(this, surfaceAt(this, pointerX_, pointerY_), timeMs);
      updateCompositorCursorForPointer(this);
      return;
    } else if (dragSurface_) {
      if (auto target = activeDragSnapTarget(this, dragSurface_, monotonicMilliseconds())) {
        if (*target == SnapTarget::Maximized) {
          maximizeToplevel(this, dragSurface_);
        } else {
          snapToplevel(this, dragSurface_, *target);
        }
        snapPreviewDropPending_ = true;
      }
      dragSurface_ = nullptr;
      resetDragSnapState(this);
      sendPointerFocus(this, surfaceAt(this, pointerX_, pointerY_), timeMs);
      updateCompositorCursorForPointer(this);
      return;
    }
  }

  if (pressed && target) {
    raiseSurface(this, target);
    setKeyboardFocus(this, target);
    sendPointerFocus(this, target, timeMs);
    updateCompositorCursorForPointer(this);
  }
  if (!pointerFocus_) return;
  std::uint32_t serial = nextInputSerial_++;
  lastPointerButtonSerial_ = pressed ? serial : 0;
  lastPointerButtonSurface_ = pressed ? pointerFocus_ : nullptr;
  for (wl_resource* pointer : pointerResources_) {
    if (!resourceBelongsToSurfaceClient(pointer, pointerFocus_)) continue;
    wl_pointer_send_button(pointer,
                           serial,
                           timeMs,
                           button,
                           pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
    if (wl_resource_get_version(pointer) >= WL_POINTER_FRAME_SINCE_VERSION) wl_pointer_send_frame(pointer);
  }
}
void WaylandServer::Impl::handlePointerAxis(double dx, double dy, std::uint32_t timeMs) {
  if (!pointerFocus_) return;
  for (wl_resource* pointer : pointerResources_) {
    if (!resourceBelongsToSurfaceClient(pointer, pointerFocus_)) continue;
    if (dx != 0.0) {
      wl_pointer_send_axis(pointer, timeMs, WL_POINTER_AXIS_HORIZONTAL_SCROLL, wl_fixed_from_double(dx));
    }
    if (dy != 0.0) {
      wl_pointer_send_axis(pointer, timeMs, WL_POINTER_AXIS_VERTICAL_SCROLL, wl_fixed_from_double(dy));
    }
    if (wl_resource_get_version(pointer) >= WL_POINTER_FRAME_SINCE_VERSION) wl_pointer_send_frame(pointer);
  }
}
void WaylandServer::Impl::handleKeyboardKey(std::uint32_t key, bool pressed, std::uint32_t timeMs) {
  xkb_state_component changed = static_cast<xkb_state_component>(0);
  if (xkbState_) {
    changed = xkb_state_update_key(xkbState_,
                                   key + 8u,
                                   pressed ? XKB_KEY_DOWN : XKB_KEY_UP);
  }
  bool const consumeModifier = updateShortcutModifier(this, key, pressed);
  bool const modifiersChanged =
      (changed & (XKB_STATE_MODS_DEPRESSED | XKB_STATE_MODS_LATCHED | XKB_STATE_MODS_LOCKED |
                  XKB_STATE_LAYOUT_EFFECTIVE)) != 0;
  if (modifiersChanged) wm::sendKeyboardModifiers(this);
  if (consumeModifier) return;
  if (handleScreenshotSelectionKey(this, key, pressed, timeMs)) return;
  if (pressed && key == KEY_ESC && dismissTopPopup(this)) return;
  if (handleCompositorShortcut(this, key, pressed, timeMs)) return;
  if (!keyboardFocus_) return;
  std::uint32_t serial = nextInputSerial_++;
  for (wl_resource* keyboard : keyboardResources_) {
    if (!resourceBelongsToSurfaceClient(keyboard, keyboardFocus_)) continue;
    wl_keyboard_send_key(keyboard,
                         serial,
                         timeMs,
                         key,
                         pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
  }
}

void WaylandServer::Impl::resetKeyboardState(std::uint32_t) {
  if (xkbKeymap_) {
    xkb_state* state = xkb_state_new(xkbKeymap_);
    if (state) {
      if (xkbState_) xkb_state_unref(xkbState_);
      xkbState_ = state;
    }
  }
  metaDown_ = false;
  ctrlDown_ = false;
  altDown_ = false;
  shiftDown_ = false;
  wm::sendKeyboardModifiers(this);
}

namespace {

std::optional<std::string> commandForAppId(std::string const& appId) {
  auto const registry = lambda_shell::buildDefaultAppRegistry(
      "examples", lambda_shell::defaultXdgApplicationDirs(), lambda_shell::executableInPath);
  return lambda_shell::resolveAppLaunchCommand(appId, registry);
}

bool spawnShellCommand(std::string const& command, std::string const& waylandDisplay) {
  if (command.empty()) return false;
  pid_t const child = fork();
  if (child < 0) {
    std::fprintf(stderr, "lambda-window-manager: fork failed while launching %s\n", command.c_str());
    return false;
  }
  if (child == 0) {
    if (setsid() < 0) _exit(126);
    pid_t const grandchild = fork();
    if (grandchild < 0) _exit(126);
    if (grandchild > 0) _exit(0);
    setenv("WAYLAND_DISPLAY", waylandDisplay.c_str(), 1);
    execl("/bin/sh", "sh", "-lc", command.c_str(), static_cast<char*>(nullptr));
    _exit(127);
  }
  int status = 0;
  while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
  return true;
}

} // namespace
bool WaylandServer::Impl::launchShellApp(std::string const& appId) {
  std::optional<std::string> const command = commandForAppId(appId);
  if (!command) {
    std::fprintf(stderr, "lambda-window-manager: refusing unknown shell app id %s\n", appId.c_str());
    return false;
  }
  return spawnShellCommand(*command, socketName_);
}
bool WaylandServer::Impl::focusShellApp(std::string const& appId, std::uint32_t timeMs) {
  for (auto it = surfaces_.rbegin(); it != surfaces_.rend(); ++it) {
    Surface* surface = it->get();
    if (!surface || !surfaceIsXdgToplevel(surface)) continue;
    XdgToplevel* toplevel = toplevelForSurface(this, surface);
    if (!toplevel || !shellAppIdMatches(appId, toplevel->appId)) continue;
    focusSurface(this, surface, timeMs);
    return true;
  }
  return false;
}
bool WaylandServer::Impl::focusShellWindow(std::uint64_t windowId, std::uint32_t timeMs) {
  auto found = std::find_if(surfaces_.begin(), surfaces_.end(), [windowId](auto const& surface) {
    return surface && surface->id == windowId && surfaceIsXdgToplevel(surface.get());
  });
  if (found == surfaces_.end() || !restoreSurfaceForShellFocus(found->get())) return false;
  Surface* surface = found->get();
  focusSurface(this, surface, timeMs);
  return true;
}

bool WaylandServer::Impl::quitShellApp(std::string const& appId) {
  bool found = false;
  for (auto const& surfacePtr : surfaces_) {
    Surface* surface = surfacePtr.get();
    if (!surface || !surfaceIsXdgToplevel(surface)) continue;
    XdgToplevel* toplevel = toplevelForSurface(this, surface);
    if (!toplevel || !toplevel->resource || !shellAppIdMatches(appId, toplevel->appId)) continue;
    xdg_toplevel_send_close(toplevel->resource);
    found = true;
  }
  if (found) flushClients();
  return found;
}

} // namespace flux::compositor
