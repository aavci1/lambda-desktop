#include "Compositor/Window/WindowManagerInternal.hpp"

#include "Compositor/Wayland/DataDeviceDndState.hpp"
#include "Compositor/Wayland/PointerButtonGrabState.hpp"
#include "Compositor/Wayland/PointerConstraintState.hpp"
#include "Compositor/Wayland/WaylandServerImpl.hpp"
#include "Compositor/Wayland/XdgPopupState.hpp"
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

namespace lambda::compositor {

using wm::activeDragSnapTarget;
using wm::clearSnapPreview;
using wm::closeFocusedToplevel;
using wm::dismissPopupGrab;
using wm::dismissTopPopup;
using wm::dismissTopPopupOutside;
using wm::handleCompositorShortcut;
using wm::handleScreenshotSelectionKey;
using wm::handleScreenshotSelectionPointerButton;
using wm::handleScreenshotSelectionPointerMotion;
using wm::handleScreenshotSelectionPointerPosition;
using wm::interactiveFrameDisplaySize;
using wm::isManagedToplevel;
using wm::kSnapDwellMs;
using wm::kSnapPreviewAnimationMs;
using wm::modalTransientChildFor;
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
using wm::popupTrace;
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

wl_client* surfaceClient(WaylandServer::Impl::Surface const* surface) {
  return surface && surface->resource ? wl_resource_get_client(surface->resource) : nullptr;
}

bool resourceBelongsToClient(wl_resource* resource, wl_client* client) {
  return resource && client && wl_resource_get_client(resource) == client;
}

wl_client* popupGrabClient(WaylandServer::Impl* server) {
  if (!server || !server->popupGrabsEnabled_) return nullptr;
  auto* popup = xdgPopupGrabSyncTop(server->popupGrab_, server->grabPopup_);
  if (!popup) return nullptr;
  if (server->popupGrab_.client) return server->popupGrab_.client;
  return popup->resource ? wl_resource_get_client(popup->resource) : nullptr;
}

std::uint64_t surfaceId(WaylandServer::Impl::Surface const* surface) {
  return surface ? surface->id : 0;
}

WaylandServer::Impl::Surface* popupGrabSurface(WaylandServer::Impl* server) {
  auto* popup = server ? xdgPopupGrabSyncTop(server->popupGrab_, server->grabPopup_) : nullptr;
  return popup && popup->xdgSurface ? popup->xdgSurface->surface : nullptr;
}

bool surfaceIsPopupTreeMember(WaylandServer::Impl::Surface const* surface) {
  for (auto const* current = surface; current;) {
    if (surfaceIsXdgPopup(current)) return true;
    if (!surfaceIsSubsurface(current) || !current->subsurfaceRole) return false;
    current = current->subsurfaceRole->parent;
  }
  return false;
}

void clearStalePointerButtonGrab(WaylandServer::Impl* server) {
  if (!server) return;
  std::uint32_t const previousCount = server->pointerButtonCount_;
  wl_client* const previousClient = server->pointerButtonGrabClient_;
  if (!pointerButtonGrabClearStale({
          .grabSurface = &server->pointerButtonGrabSurface_,
          .grabClient = &server->pointerButtonGrabClient_,
          .buttonCount = &server->pointerButtonCount_,
      })) {
    return;
  }
  popupTrace("lambda-window-manager: pointer button grab cleared stale client=%p count=%u\n",
             static_cast<void*>(previousClient),
             previousCount);
}

WaylandServer::Impl::Surface* pointerButtonDeliverySurface(WaylandServer::Impl* server) {
  if (!server) return nullptr;
  clearStalePointerButtonGrab(server);
  return pointerButtonGrabDeliverySurface({
      .grabSurface = &server->pointerButtonGrabSurface_,
      .grabClient = &server->pointerButtonGrabClient_,
      .buttonCount = &server->pointerButtonCount_,
  }, server->pointerFocus_);
}

WaylandServer::Impl::Surface* pointerButtonMotionFocusSurface(WaylandServer::Impl* server,
                                                              WaylandServer::Impl::Surface* hitSurface) {
  if (!server) return hitSurface;
  clearStalePointerButtonGrab(server);
  return pointerButtonGrabMotionFocusSurface({
      .grabSurface = &server->pointerButtonGrabSurface_,
      .grabClient = &server->pointerButtonGrabClient_,
      .buttonCount = &server->pointerButtonCount_,
  }, hitSurface);
}

wl_client* pointerButtonDeliveryClient(WaylandServer::Impl* server,
                                       WaylandServer::Impl::Surface const* deliverySurface) {
  if (!server) return nullptr;
  clearStalePointerButtonGrab(server);
  return pointerButtonGrabDeliveryClient({
      .grabSurface = &server->pointerButtonGrabSurface_,
      .grabClient = &server->pointerButtonGrabClient_,
      .buttonCount = &server->pointerButtonCount_,
  }, surfaceClient(deliverySurface ? deliverySurface : server->pointerFocus_));
}

bool sendPointerButtonToFocus(WaylandServer::Impl* server,
                              std::uint32_t button,
                              bool pressed,
                              std::uint32_t timeMs) {
  WaylandServer::Impl::Surface* focus = pointerButtonDeliverySurface(server);
  wl_client* const client = pointerButtonDeliveryClient(server, focus);
  if (!server || !client) {
    popupTrace("lambda-window-manager: pointer button drop button=%u state=%s focus=%llu button_grab=%llu "
               "client=%p count=%u\n",
               button,
               pressed ? "pressed" : "released",
               static_cast<unsigned long long>(surfaceId(focus)),
               static_cast<unsigned long long>(surfaceId(server ? server->pointerButtonGrabSurface_ : nullptr)),
               static_cast<void*>(server ? server->pointerButtonGrabClient_ : nullptr),
               server ? server->pointerButtonCount_ : 0);
    return false;
  }
  wl_client* const previousClient = server->pointerButtonGrabClient_;
  std::uint32_t const previousCount = server->pointerButtonCount_;
  std::uint32_t serial = issueSeatSerial(server,
                                         pressed ? SeatSerialKind::PointerButtonPress
                                                 : SeatSerialKind::PointerButtonRelease,
                                         client,
                                         focus);
  std::size_t sent = 0;
  for (wl_resource* pointer : server->pointerResources_) {
    if (!resourceBelongsToClient(pointer, client)) continue;
    wl_pointer_send_button(pointer,
                           serial,
                           timeMs,
                           button,
                           pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
    if (wl_resource_get_version(pointer) >= WL_POINTER_FRAME_SINCE_VERSION) wl_pointer_send_frame(pointer);
    ++sent;
  }
  PointerButtonGrabTransition const grabTransition = pointerButtonGrabUpdateForButton({
      .grabSurface = &server->pointerButtonGrabSurface_,
      .grabClient = &server->pointerButtonGrabClient_,
      .buttonCount = &server->pointerButtonCount_,
  }, focus, client, pressed);
  popupTrace("lambda-window-manager: pointer button send button=%u state=%s focus=%llu pointer_focus=%llu "
             "button_grab=%llu client=%p->%p count=%u->%u sent=%zu serial=%u\n",
             button,
             pressed ? "pressed" : "released",
             static_cast<unsigned long long>(surfaceId(focus)),
             static_cast<unsigned long long>(surfaceId(server->pointerFocus_)),
             static_cast<unsigned long long>(surfaceId(server->pointerButtonGrabSurface_)),
             static_cast<void*>(previousClient),
             static_cast<void*>(grabTransition.nextClient),
             previousCount,
             grabTransition.nextCount,
             sent,
             serial);
  return true;
}

bool handlePopupGrabPointerButton(WaylandServer::Impl* server,
                                  std::uint32_t button,
                                  bool pressed,
                                  std::uint32_t timeMs) {
  wl_client* const grabClient = popupGrabClient(server);
  if (!grabClient) return false;
  WaylandServer::Impl::Surface* focus = pointerButtonDeliverySurface(server);
  if (pointerButtonDeliveryClient(server, focus) == grabClient) {
    return sendPointerButtonToFocus(server, button, pressed, timeMs);
  }
  popupTrace("lambda-window-manager: xdg_popup grab dismiss button=%u state=%s focus=%llu pointer_focus=%llu "
             "button_grab=%llu client=%p grab_popup=%llu\n",
             button,
             pressed ? "pressed" : "released",
             static_cast<unsigned long long>(surfaceId(focus)),
             static_cast<unsigned long long>(surfaceId(server->pointerFocus_)),
             static_cast<unsigned long long>(surfaceId(server->pointerButtonGrabSurface_)),
             static_cast<void*>(server->pointerButtonGrabClient_),
             static_cast<unsigned long long>(surfaceId(popupGrabSurface(server))));
  dismissPopupGrab(server);
  return true;
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
    clampPointerConstraintGlobalPoint(constraint, pointerX_, pointerY_);
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
  Surface* hitSurface = surfaceAt(this, pointerX_, pointerY_);
  sendPointerFocus(this, pointerButtonMotionFocusSurface(this, hitSurface), timeMs);
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
    clampPointerConstraintGlobalPoint(constraint, pointerX_, pointerY_);
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
  Surface* hitSurface = surfaceAt(this, pointerX_, pointerY_);
  sendPointerFocus(this, pointerButtonMotionFocusSurface(this, hitSurface), timeMs);
  updateCompositorCursorForPointer(this);
  notePointerVisualStateChanged(this, previousVisualState);
}
void WaylandServer::Impl::handlePointerButton(std::uint32_t button, bool pressed, std::uint32_t timeMs) {
  if (handleScreenshotSelectionPointerButton(this, button, pressed, timeMs)) return;
  Surface* target = surfaceAt(this, pointerX_, pointerY_);
  Surface* deliverySurface = pointerButtonDeliverySurface(this);
  wl_client* deliveryClient = pointerButtonDeliveryClient(this, deliverySurface);
  bool const implicitButtonGrabActiveBeforeButton = !pressed && pointerButtonCount_ > 0 && pointerButtonGrabSurface_;
  popupTrace("lambda-window-manager: pointer button input button=%u state=%s pointer=%.1f,%.1f "
             "target=%llu focus=%llu delivery=%llu button_grab=%llu client=%p count=%u grab_popup=%llu "
             "popup_grabs=%u\n",
             button,
             pressed ? "pressed" : "released",
             pointerX_,
             pointerY_,
             static_cast<unsigned long long>(surfaceId(target)),
             static_cast<unsigned long long>(surfaceId(pointerFocus_)),
             static_cast<unsigned long long>(surfaceId(deliverySurface)),
             static_cast<unsigned long long>(surfaceId(pointerButtonGrabSurface_)),
             static_cast<void*>(deliveryClient),
             pointerButtonCount_,
             static_cast<unsigned long long>(surfaceId(popupGrabSurface(this))),
             popupGrabsEnabled_ ? 1u : 0u);
  if (button == BTN_LEFT && !pressed && dndSource_) {
    updateDndTarget(this, target, timeMs);
    bool completedDrop = false;
    if (dndTarget_) {
      if (auto* device = dataDeviceForClient(this, wl_resource_get_client(dndTarget_->resource))) {
        if (!dndOffer_ || dndOffer_->selectedAction == WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE) {
          DndClearPlan const clearPlan = dndClearPlanAfterDrop(false);
          clearDnd(this, clearPlan.destroyOffer, clearPlan.sendLeave, clearPlan.cancelSource);
          return;
        }
        wl_data_device_send_drop(device->resource);
        completedDrop = true;
        dndOffer_->dropPerformed = true;
      }
      if (dndSourceShouldReceiveDropPerformed(completedDrop) &&
          dndSource_->resource &&
          wl_resource_get_version(dndSource_->resource) >= WL_DATA_SOURCE_DND_DROP_PERFORMED_SINCE_VERSION) {
        wl_data_source_send_dnd_drop_performed(dndSource_->resource);
      }
    }
    DndClearPlan const clearPlan = dndClearPlanAfterDrop(completedDrop);
    clearDnd(this, clearPlan.destroyOffer, clearPlan.sendLeave, clearPlan.cancelSource);
    return;
  }
  if (handlePopupGrabPointerButton(this, button, pressed, timeMs)) return;
  if (pressed && dismissTopPopupOutside(this, target)) return;
  if (pressed) {
    Surface* activationTarget = target;
    if (!activationTarget) {
      if (auto context = topChromeHitContext(this, pointerX_, pointerY_)) activationTarget = context->surface;
    }
    if (Surface* modalChild = modalTransientChildFor(this, activationTarget)) {
      raiseSurface(this, modalChild);
      setKeyboardFocus(this, modalChild);
      sendPointerFocus(this, nullptr, timeMs);
      updateCompositorCursorForPointer(this);
      flushClients();
      return;
    }
  }
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
        wm::FrameDisplaySize const frameSize = interactiveFrameDisplaySize(chromeControlTarget);
        resizeStartWidth_ = frameSize.width;
        resizeStartHeight_ = frameSize.height;
        resizeLastX_ = resizeStartWindowX_;
        resizeLastY_ = resizeStartWindowY_;
        resizeLastWidth_ = resizeStartWidth_;
        resizeLastHeight_ = resizeStartHeight_;
        resizeEdges_ = resizeEdges;
        updateCompositorCursorForPointer(this);
        LAMBDA_RESIZE_TRACE("compositor",
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
      std::int32_t const finalX = resizeLastX_;
      std::int32_t const finalY = resizeLastY_;
      std::int32_t const finalWidth = resizeLastWidth_;
      std::int32_t const finalHeight = resizeLastHeight_;
      resizeSurface_ = nullptr;
      resizeEdges_ = XDG_TOPLEVEL_RESIZE_EDGE_NONE;
      if (finalWidth > 0 && finalHeight > 0) {
        if (requestToplevelResizeConfigure(this, resizedSurface, finalX, finalY, finalWidth, finalHeight)) {
          flushClients();
        }
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
    if (!surfaceIsPopupTreeMember(target)) {
      raiseSurface(this, target);
      setKeyboardFocus(this, target);
    }
    sendPointerFocus(this, target, timeMs);
    updateCompositorCursorForPointer(this);
  }
  sendPointerButtonToFocus(this, button, pressed, timeMs);
  if (implicitButtonGrabActiveBeforeButton && pointerButtonCount_ == 0) {
    sendPointerFocus(this, surfaceAt(this, pointerX_, pointerY_), timeMs);
    updateCompositorCursorForPointer(this);
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
  std::uint32_t serial = issueSeatSerialForSurface(this, SeatSerialKind::KeyboardKey, keyboardFocus_);
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
  wm::clearFocusCycle(this);
  wm::sendKeyboardModifiers(this);
}

namespace {

std::optional<std::string> commandForAppId(std::string const& appId) {
  auto const registry = lambda_shell::buildDefaultAppRegistry(
      lambda_shell::defaultLocalLambdaAppDirs(),
      lambda_shell::defaultXdgApplicationDirs(),
      lambda_shell::executableInPath);
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

} // namespace lambda::compositor
