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

WindowGeometry frameGeometryFor(WaylandServer::Impl* server, WaylandServer::Impl::Surface const* surface) {
  std::int32_t const titleBarHeight = externalTitleBarHeight(server, surface);
  WindowGeometry const content = windowGeometryFor(surface);
  return {
      .x = content.x,
      .y = content.y - titleBarHeight,
      .width = content.width,
      .height = content.height + titleBarHeight,
  };
}

WindowGeometry snapPreviewFrameGeometry(WaylandServer::Impl* server,
                                        WaylandServer::Impl::Surface const* surface,
                                        SnapTarget target) {
  std::int32_t const topInset = topInsetForSurface(server, surface);
  WindowGeometry const content = snapTargetGeometry(snapOutputGeometryFor(server), target, topInset);
  std::int32_t const titleBarHeight = externalTitleBarHeight(server, surface);
  return {
      .x = content.x,
      .y = content.y - titleBarHeight,
      .width = content.width,
      .height = content.height + titleBarHeight,
  };
}

WindowGeometry snapPreviewCurrentWindow(WaylandServer::Impl* server, std::uint32_t nowMs) {
  if (!server->snapPreviewVisible_ || server->snapPreviewStartedAtMs_ == 0) {
    return server->snapPreviewTargetWindow_;
  }
  std::uint32_t const elapsed = nowMs - server->snapPreviewStartedAtMs_;
  if (elapsed >= kSnapPreviewAnimationMs) {
    server->snapPreviewStartedAtMs_ = 0;
    server->snapPreviewStartWindow_ = server->snapPreviewTargetWindow_;
    return server->snapPreviewTargetWindow_;
  }
  float const progress = easeOutCubic(static_cast<float>(elapsed) /
                                      static_cast<float>(kSnapPreviewAnimationMs));
  WindowGeometry const start = server->snapPreviewStartWindow_;
  WindowGeometry const end = server->snapPreviewTargetWindow_;
  return {
      .x = lerpInt(start.x, end.x, progress),
      .y = lerpInt(start.y, end.y, progress),
      .width = std::max(1, lerpInt(start.width, end.width, progress)),
      .height = std::max(1, lerpInt(start.height, end.height, progress)),
  };
}

void resetDragSnapState(WaylandServer::Impl* server) {
  server->dragSnapTarget_.reset();
  server->dragSnapTargetStartedAtMs_ = 0;
}

} // namespace flux::compositor::wm

namespace flux::compositor::wm {

void clearSnapPreview(WaylandServer::Impl* server) {
  resetDragSnapState(server);
  server->snapPreviewVisible_ = false;
  server->snapPreviewDropPending_ = false;
  server->snapPreviewSurfaceId_ = 0;
  server->snapPreviewStartedAtMs_ = 0;
  server->snapPreviewStartWindow_ = {};
  server->snapPreviewTargetWindow_ = {};
}

} // namespace flux::compositor::wm

namespace flux::compositor::wm {

void animateSnapPreviewTo(WaylandServer::Impl* server,
                          WaylandServer::Impl::Surface const* surface,
                          WindowGeometry target,
                          std::uint32_t nowMs) {
  WindowGeometry const current =
      server->snapPreviewVisible_ ? snapPreviewCurrentWindow(server, nowMs) : frameGeometryFor(server, surface);
  if (server->snapPreviewVisible_ &&
      server->snapPreviewTargetWindow_.x == target.x &&
      server->snapPreviewTargetWindow_.y == target.y &&
      server->snapPreviewTargetWindow_.width == target.width &&
      server->snapPreviewTargetWindow_.height == target.height) {
    return;
  }
  server->snapPreviewVisible_ = true;
  server->snapPreviewSurfaceId_ = surface ? surface->id : 0;
  server->snapPreviewStartedAtMs_ = nowMs;
  server->snapPreviewStartWindow_ = current;
  server->snapPreviewTargetWindow_ = target;
}

} // namespace flux::compositor::wm

namespace flux::compositor::wm {

std::optional<SnapTarget> activeDragSnapTarget(WaylandServer::Impl* server,
                                               WaylandServer::Impl::Surface const* surface,
                                               std::uint32_t nowMs) {
  if (!surface) {
    clearSnapPreview(server);
    return std::nullopt;
  }
  auto const target =
      snapTargetForWindow(windowGeometryFor(surface),
                          snapOutputGeometryFor(server),
                          topInsetForSurface(server, surface));
  if (!target) {
    clearSnapPreview(server);
    return std::nullopt;
  }
  if (!server->dragSnapTarget_ || *server->dragSnapTarget_ != *target) {
    WindowGeometry const targetFrame = snapPreviewFrameGeometry(server, surface, *target);
    if (server->snapPreviewVisible_) {
      server->dragSnapTarget_ = target;
      server->dragSnapTargetStartedAtMs_ = nowMs;
      animateSnapPreviewTo(server, surface, targetFrame, nowMs);
      return target;
    }
    server->dragSnapTarget_ = target;
    server->dragSnapTargetStartedAtMs_ = nowMs;
    server->snapPreviewTargetWindow_ = targetFrame;
    return std::nullopt;
  }
  if (nowMs - server->dragSnapTargetStartedAtMs_ < kSnapDwellMs) {
    return std::nullopt;
  }
  animateSnapPreviewTo(server, surface, snapPreviewFrameGeometry(server, surface, *target), nowMs);
  return target;
}

} // namespace flux::compositor::wm

namespace flux::compositor {

using wm::activeDragSnapTarget;
using wm::clearSnapPreview;
using wm::frameGeometryFor;
using wm::kMinWindowHeight;
using wm::kMinWindowWidth;
using wm::monotonicMilliseconds;
using wm::snapOutputGeometryFor;
using wm::snapPreviewCurrentWindow;
using wm::startGeometryAnimation;
using wm::surfaceById;
using wm::topInsetForSurface;

std::optional<SnapPreviewSnapshot> snapPreviewForDrag(WaylandServer::Impl const* server) {
  auto* mutableServer = const_cast<WaylandServer::Impl*>(server);
  std::uint32_t const now = monotonicMilliseconds();
  if (WaylandServer::Impl::Surface const* surface = server->dragSurface_) {
    activeDragSnapTarget(mutableServer, surface, now);
  }
  if (!server->snapPreviewVisible_) return std::nullopt;
  if (server->snapPreviewDropPending_) {
    WaylandServer::Impl::Surface* surface = surfaceById(mutableServer, server->snapPreviewSurfaceId_);
    if (!surface) {
      clearSnapPreview(mutableServer);
      return std::nullopt;
    }
    WindowGeometry const currentFrame = frameGeometryFor(mutableServer, surface);
    WindowGeometry const target = server->snapPreviewTargetWindow_;
    if (!surface->geometryAnimationActive &&
        currentFrame.x == target.x &&
        currentFrame.y == target.y &&
        currentFrame.width == target.width &&
        currentFrame.height == target.height) {
      clearSnapPreview(mutableServer);
      return std::nullopt;
    }
  }
  WindowGeometry const current = snapPreviewCurrentWindow(mutableServer, now);
  WindowGeometry const end = server->snapPreviewTargetWindow_;
  OutputGeometry const output = snapOutputGeometryFor(mutableServer);
  return SnapPreviewSnapshot{
      .surfaceId = server->snapPreviewSurfaceId_,
      .x = current.x,
      .y = current.y,
      .width = current.width,
      .height = current.height,
      .targetX = end.x,
      .targetY = end.y,
      .targetWidth = end.width,
      .targetHeight = end.height,
      .cacheX = 0,
      .cacheY = 0,
      .cacheWidth = output.width,
      .cacheHeight = output.height,
  };
}

} // namespace flux::compositor

namespace flux::compositor::wm {

void startGeometryAnimation(WaylandServer::Impl* server,
                            WaylandServer::Impl::Surface* surface,
                            std::int32_t targetX,
                            std::int32_t targetY,
                            std::int32_t targetWidth,
                            std::int32_t targetHeight) {
  if (!surface) return;
  surface->geometryAnimationStartX = surface->windowX;
  surface->geometryAnimationStartY = surface->windowY;
  surface->geometryAnimationStartWidth = displayWidth(surface);
  surface->geometryAnimationStartHeight = displayHeight(surface);
  surface->geometryAnimationTargetX = targetX;
  surface->geometryAnimationTargetY = targetY;
  surface->geometryAnimationTargetWidth = targetWidth;
  surface->geometryAnimationTargetHeight = targetHeight;
  surface->geometryAnimationConfigureSent = false;
  surface->geometryAnimationStartedAtMs = monotonicMilliseconds();
  surface->lastResizeInputNsec = flux::detail::resizeTraceTimestampNanoseconds();
  surface->awaitingConfigureCommit = false;
  surface->awaitingConfigureWidth = 0;
  surface->awaitingConfigureHeight = 0;
  surface->geometryAnimationActive = true;
  if (surface->geometryAnimationStartX == targetX && surface->geometryAnimationStartY == targetY &&
      surface->geometryAnimationStartWidth == targetWidth &&
      surface->geometryAnimationStartHeight == targetHeight) {
    surface->geometryAnimationActive = false;
    return;
  }
  bool const grows = targetWidth > surface->geometryAnimationStartWidth ||
                     targetHeight > surface->geometryAnimationStartHeight;
  if (grows) {
    sendToplevelConfigure(server, toplevelForSurface(server, surface), targetWidth, targetHeight);
    surface->geometryAnimationConfigureSent = true;
  }
  ++server->contentSerial_;
  server->flushClients();
}

} // namespace flux::compositor::wm

namespace flux::compositor::wm {

void snapToplevel(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface, SnapTarget target) {
  if (!isManagedToplevel(surface)) return;
  if (!surface->snapped && !surface->maximized && !surface->fullscreen) {
    surface->restoreX = surface->windowX;
    surface->restoreY = surface->windowY;
    surface->restoreWidth = displayWidth(surface);
    surface->restoreHeight = displayHeight(surface);
  }
  WindowGeometry const geometry =
      snapTargetGeometry(snapOutputGeometryFor(server), target, topInsetForSurface(server, surface));
  surface->snapped = true;
  surface->maximized = false;
  surface->fullscreen = false;
  startGeometryAnimation(server,
                         surface,
                         geometry.x,
                         geometry.y,
                         geometry.width,
                         geometry.height);
}

} // namespace flux::compositor::wm

namespace flux::compositor::wm {

void snapToplevel(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface, bool leftHalf) {
  snapToplevel(server, surface, leftHalf ? SnapTarget::LeftHalf : SnapTarget::RightHalf);
}

} // namespace flux::compositor::wm

namespace flux::compositor {

using wm::isManagedToplevel;
using wm::kMinWindowHeight;
using wm::kMinWindowWidth;
using wm::snapOutputGeometryFor;
using wm::startGeometryAnimation;
using wm::topInsetForSurface;

bool restoreToplevel(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface) {
  if (!isManagedToplevel(surface) || (!surface->snapped && !surface->maximized && !surface->fullscreen)) return false;
  std::int32_t const restoreWidth =
      std::max(kMinWindowWidth, surface->restoreWidth > 0 ? surface->restoreWidth : surface->width);
  std::int32_t const restoreHeight =
      std::max(kMinWindowHeight, surface->restoreHeight > 0 ? surface->restoreHeight : surface->height);
  std::int32_t const restoreX =
      std::clamp(surface->restoreX, 0, std::max(0, server->logicalOutputWidth() - restoreWidth));
  OutputGeometry const restoreOutput = snapOutputGeometryFor(server);
  std::int32_t const restoreY =
      std::clamp(surface->restoreY,
                 topInsetForSurface(server, surface),
                 std::max(topInsetForSurface(server, surface), restoreOutput.height - restoreHeight));
  surface->maximized = false;
  surface->snapped = false;
  surface->fullscreen = false;
  startGeometryAnimation(server, surface, restoreX, restoreY, restoreWidth, restoreHeight);
  return true;
}

} // namespace flux::compositor

namespace flux::compositor {

using wm::isManagedToplevel;
using wm::snapOutputGeometryFor;
using wm::startGeometryAnimation;
using wm::topInsetForSurface;

void maximizeToplevel(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface) {
  if (!isManagedToplevel(surface)) return;
  if (surface->maximized || surface->fullscreen) return;
  if (!surface->snapped) {
    surface->restoreX = surface->windowX;
    surface->restoreY = surface->windowY;
    surface->restoreWidth = displayWidth(surface);
    surface->restoreHeight = displayHeight(surface);
  }
  WindowGeometry const target = maximizedWindowGeometry(snapOutputGeometryFor(server),
                                                        topInsetForSurface(server, surface));
  surface->maximized = true;
  surface->snapped = false;
  surface->fullscreen = false;
  startGeometryAnimation(server, surface, target.x, target.y, target.width, target.height);
}

} // namespace flux::compositor

namespace flux::compositor::wm {

void snapFocusedToplevel(WaylandServer::Impl* server, bool leftHalf) {
  snapToplevel(server, server->keyboardFocus_, leftHalf);
}

} // namespace flux::compositor::wm

namespace flux::compositor::wm {

void maximizeFocusedToplevel(WaylandServer::Impl* server) {
  flux::compositor::maximizeToplevel(server, server->keyboardFocus_);
}

} // namespace flux::compositor::wm

namespace flux::compositor::wm {

bool restoreFocusedToplevel(WaylandServer::Impl* server) {
  return flux::compositor::restoreToplevel(server, server->keyboardFocus_);
}

} // namespace flux::compositor::wm

namespace flux::compositor::wm {

void fullscreenToplevel(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface) {
  if (!isManagedToplevel(surface) || surface->fullscreen) return;
  if (!surface->snapped && !surface->maximized) {
    surface->restoreX = surface->windowX;
    surface->restoreY = surface->windowY;
    surface->restoreWidth = displayWidth(surface);
    surface->restoreHeight = displayHeight(surface);
  }
  std::int32_t const topInset = topInsetForSurface(server, surface);
  std::int32_t const targetHeight = std::max(kMinWindowHeight, server->logicalOutputHeight() - topInset);
  surface->fullscreen = true;
  surface->maximized = false;
  surface->snapped = false;
  surface->minimized = false;
  startGeometryAnimation(server,
                         surface,
                         0,
                         topInset,
                         std::max(kMinWindowWidth, server->logicalOutputWidth()),
                         targetHeight);
}

} // namespace flux::compositor::wm

namespace flux::compositor::wm {

void restoreSnappedForDrag(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface) {
  if (!surface || (!surface->snapped && !surface->maximized && !surface->fullscreen)) return;
  WindowGeometry const restored = restoredDragGeometry({
      .pointerX = server->pointerX_,
      .pointerY = server->pointerY_,
      .dragOffsetY = server->dragOffsetY_,
      .snappedWindow = windowGeometryFor(surface),
      .restoreWindow = {
          .x = surface->restoreX,
          .y = surface->restoreY,
          .width = surface->restoreWidth > 0 ? surface->restoreWidth : surface->width,
          .height = surface->restoreHeight > 0 ? surface->restoreHeight : surface->height,
      },
      .output = snapOutputGeometryFor(server),
      .topInset = topInsetForSurface(server, surface),
  });
  surface->windowX = restored.x;
  surface->windowY = restored.y;
  setConfiguredFrameSize(surface, restored.width, restored.height);
  surface->snapped = false;
  surface->maximized = false;
  surface->fullscreen = false;
  surface->geometryAnimationActive = false;
  clearSnapPreview(server);
  server->dragOffsetX_ = server->pointerX_ - static_cast<float>(surface->windowX);
  server->dragOffsetY_ = server->pointerY_ - static_cast<float>(surface->windowY);
  sendToplevelConfigure(server, toplevelForSurface(server, surface), restored.width, restored.height);
}

} // namespace flux::compositor::wm

namespace flux::compositor::wm {

void toggleMaximizedToplevel(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface) {
  if (!isManagedToplevel(surface)) return;
  if (surface->maximized) {
    flux::compositor::restoreToplevel(server, surface);
    return;
  }
  flux::compositor::maximizeToplevel(server, surface);
}

} // namespace flux::compositor::wm

namespace flux::compositor::wm {

void updateDrag(WaylandServer::Impl* server) {
  if (!server->dragSurface_) return;
  WaylandServer::Impl::Surface* surface = server->dragSurface_;
  restoreSnappedForDrag(server, surface);
  std::int32_t const oldX = surface->windowX;
  std::int32_t const oldY = surface->windowY;
  std::int32_t const topInset = topInsetForSurface(server, surface);
  OutputGeometry const output = snapOutputGeometryFor(server);
  WindowGeometry const next = centerSnappedWindowGeometry({
      .x = static_cast<std::int32_t>(server->pointerX_ - server->dragOffsetX_),
      .y = static_cast<std::int32_t>(server->pointerY_ - server->dragOffsetY_),
      .width = displayWidth(surface),
      .height = displayHeight(surface),
  }, output, topInset);
  surface->windowX = next.x;
  surface->windowY = next.y;
  if (surface->windowX != oldX || surface->windowY != oldY) {
    ++server->contentSerial_;
  }
  activeDragSnapTarget(server, surface, monotonicMilliseconds());
}

} // namespace flux::compositor::wm

namespace flux::compositor::wm {

void updateResize(WaylandServer::Impl* server) {
  WaylandServer::Impl::Surface* surface = server->resizeSurface_;
  if (!surface) return;
  surface->geometryAnimationActive = false;
  std::int32_t const oldX = surface->windowX;
  std::int32_t const oldY = surface->windowY;
  std::int32_t const oldWidth = displayWidth(surface);
  std::int32_t const oldHeight = displayHeight(surface);

  float const dx = server->pointerX_ - server->resizeStartX_;
  float const dy = server->pointerY_ - server->resizeStartY_;
  ResizeEdge const edges = resizeEdgesFromXdg(server->resizeEdges_);
  bool const left = hasResizeEdge(edges, ResizeEdge::Left);
  bool const top = hasResizeEdge(edges, ResizeEdge::Top);
  WindowGeometry next = resizedWindowGeometry({
      .startPointerX = server->resizeStartX_,
      .startPointerY = server->resizeStartY_,
      .pointerX = server->pointerX_,
      .pointerY = server->pointerY_,
      .startWindow = {
          .x = server->resizeStartWindowX_,
          .y = server->resizeStartWindowY_,
          .width = server->resizeStartWidth_,
          .height = server->resizeStartHeight_,
      },
      .edges = edges,
      .output = outputGeometryFor(server),
      .topInset = topInsetForSurface(server, surface),
  });
  next = clampToplevelGeometryToSizeHints(next, committedSizeHints(toplevelForSurface(server, surface)), edges);

  if (left) surface->windowX = next.x;
  if (top) surface->windowY = next.y;
  if (next.width == server->resizeLastWidth_ && next.height == server->resizeLastHeight_) {
    if (surface->windowX != oldX || surface->windowY != oldY) {
      ++server->contentSerial_;
    }
    return;
  }
  surface->lastResizeInputNsec = flux::detail::resizeTraceTimestampNanoseconds();
  server->resizeLastWidth_ = next.width;
  server->resizeLastHeight_ = next.height;
  if (surface->windowX != oldX || surface->windowY != oldY ||
      displayWidth(surface) != oldWidth || displayHeight(surface) != oldHeight) {
    ++server->contentSerial_;
  }
  flux::detail::resizeTrace("compositor",
                            "update-resize surface=%llu pointer=%.1f,%.1f window=%d,%d size=%dx%d "
                            "delta=%.1f,%.1f edges=%u\n",
                            static_cast<unsigned long long>(surface->id),
                            server->pointerX_,
                            server->pointerY_,
                            surface->windowX,
                            surface->windowY,
                            next.width,
                            next.height,
                            dx,
                            dy,
                            server->resizeEdges_);
  if (!surface->awaitingConfigureCommit) {
    sendToplevelConfigure(server, toplevelForSurface(server, surface), next.width, next.height);
  } else {
    flux::detail::resizeTrace("compositor",
                              "coalesce-resize-configure surface=%llu pending=%dx%d awaiting=%dx%d\n",
                              static_cast<unsigned long long>(surface->id),
                              next.width,
                              next.height,
                              surface->awaitingConfigureWidth,
                              surface->awaitingConfigureHeight);
  }
}

} // namespace flux::compositor::wm
