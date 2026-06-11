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

WindowGeometry frameGeometryFor(WaylandServer::Impl* server, WaylandServer::Impl::Surface const* surface) {
  std::int32_t const titleBarHeight = externalTitleBarHeight(server, surface);
  std::int32_t const frameOutset = frameOutsetForSurface(server, surface);
  WindowGeometry const content = windowGeometryFor(surface);
  return windowFrameGeometryForContent(content, titleBarHeight, frameOutset);
}

WindowGeometry snapPreviewFrameGeometry(WaylandServer::Impl* server,
                                        WaylandServer::Impl::Surface const* surface,
                                        SnapTarget target) {
  std::int32_t const topInset = topInsetForSurface(server, surface);
  std::int32_t const frameOutset = frameOutsetForSurface(server, surface);
  WindowGeometry const content =
      snapTargetGeometry(snapOutputGeometryFor(server), target, topInset, frameOutset);
  std::int32_t const titleBarHeight = externalTitleBarHeight(server, surface);
  return windowFrameGeometryForContent(content, titleBarHeight, frameOutset);
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

WindowGeometry fallbackFloatingGeometry(WaylandServer::Impl* server, WaylandServer::Impl::Surface const* surface) {
  OutputGeometry const output = snapOutputGeometryFor(server);
  std::int32_t const topInset = topInsetForSurface(server, surface);
  std::int32_t const frameOutset = frameOutsetForSurface(server, surface);
  std::int32_t const availableWidth = std::max(kMinWindowWidth, output.width);
  std::int32_t const availableHeight = std::max(kMinWindowHeight, output.height - topInset);
  std::int32_t const width = std::clamp(availableWidth * 2 / 3,
                                        kMinWindowWidth,
                                        std::max(kMinWindowWidth, std::min(availableWidth, 1280)));
  std::int32_t const height = std::clamp(availableHeight * 2 / 3,
                                         kMinWindowHeight,
                                         std::max(kMinWindowHeight, std::min(availableHeight, 900)));
  std::int32_t const preferredX = surface ? surface->windowX : 80;
  std::int32_t const preferredY = surface ? surface->windowY : topInset;
  std::int32_t const minX = std::min(frameOutset, std::max(0, output.width - width));
  std::int32_t const maxX = std::max(minX, output.width - width - frameOutset);
  std::int32_t const maxY = std::max(topInset, output.height - height - frameOutset);
  return {
      .x = std::clamp(preferredX, minX, maxX),
      .y = std::clamp(preferredY, topInset, maxY),
      .width = width,
      .height = height,
  };
}

void resetDragSnapState(WaylandServer::Impl* server) {
  server->dragSnapTarget_.reset();
  server->dragSnapTargetStartedAtMs_ = 0;
}

} // namespace lambda::compositor::wm

namespace lambda::compositor::wm {

void clearSnapPreview(WaylandServer::Impl* server) {
  resetDragSnapState(server);
  server->snapPreviewVisible_ = false;
  server->snapPreviewDropPending_ = false;
  server->snapPreviewSurfaceId_ = 0;
  server->snapPreviewStartedAtMs_ = 0;
  server->snapPreviewStartWindow_ = {};
  server->snapPreviewTargetWindow_ = {};
}

} // namespace lambda::compositor::wm

namespace lambda::compositor::wm {

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

} // namespace lambda::compositor::wm

namespace lambda::compositor::wm {

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
                          topInsetForSurface(server, surface),
                          frameOutsetForSurface(server, surface));
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

} // namespace lambda::compositor::wm

namespace lambda::compositor {

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

} // namespace lambda::compositor

namespace lambda::compositor::wm {

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
  if (surface->geometryAnimationStartWidth <= 0 && targetWidth > 0) {
    surface->geometryAnimationStartWidth = targetWidth;
  }
  if (surface->geometryAnimationStartHeight <= 0 && targetHeight > 0) {
    surface->geometryAnimationStartHeight = targetHeight;
  }
  surface->geometryAnimationTargetX = targetX;
  surface->geometryAnimationTargetY = targetY;
  surface->geometryAnimationTargetWidth = targetWidth;
  surface->geometryAnimationTargetHeight = targetHeight;
  surface->geometryAnimationStartedAtMs = monotonicMilliseconds();
  if (lambda::detail::resizeTraceMetadataEnabled()) {
    surface->lastResizeInputNsec = lambda::detail::resizeTraceTimestampNanoseconds();
  }
  if (server) server->noteResizePacingActivity();
  surface->geometryAnimationActive = true;
  LAMBDA_RESIZE_TRACE("compositor",
                              "animation-start surface=%llu start=%d,%d %dx%d target=%d,%d %dx%d\n",
                              static_cast<unsigned long long>(surface->id),
                              surface->geometryAnimationStartX,
                              surface->geometryAnimationStartY,
                              surface->geometryAnimationStartWidth,
                              surface->geometryAnimationStartHeight,
                              targetX,
                              targetY,
                              targetWidth,
                              targetHeight);
  if (surface->geometryAnimationStartX == targetX && surface->geometryAnimationStartY == targetY &&
      surface->geometryAnimationStartWidth == targetWidth &&
      surface->geometryAnimationStartHeight == targetHeight) {
    surface->geometryAnimationActive = false;
    return;
  }
  ++server->contentSerial_;
  server->flushClients();
}

void clearPreFullscreenState(WaylandServer::Impl::Surface* surface) {
  if (!surface) return;
  surface->preFullscreenSnapped = false;
  surface->preFullscreenMaximized = false;
  surface->preFullscreenX = 0;
  surface->preFullscreenY = 0;
  surface->preFullscreenWidth = 0;
  surface->preFullscreenHeight = 0;
}

} // namespace lambda::compositor::wm

namespace lambda::compositor::wm {

void snapToplevel(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface, SnapTarget target) {
  if (!isManagedToplevel(surface)) return;
  if (!surface->snapped && !surface->maximized && !surface->fullscreen) {
    WindowGeometry const fallback = fallbackFloatingGeometry(server, surface);
    std::int32_t const currentWidth = displayWidth(surface);
    std::int32_t const currentHeight = displayHeight(surface);
    surface->restoreX = surface->windowX;
    surface->restoreY = surface->windowY;
    surface->restoreWidth = currentWidth > 0 ? currentWidth : fallback.width;
    surface->restoreHeight = currentHeight > 0 ? currentHeight : fallback.height;
  }
  WindowGeometry const geometry =
      snapTargetGeometry(snapOutputGeometryFor(server),
                         target,
                         topInsetForSurface(server, surface),
                         frameOutsetForSurface(server, surface));
  surface->snapped = true;
  surface->maximized = false;
  surface->fullscreen = false;
  clearPreFullscreenState(surface);
  startGeometryAnimation(server,
                         surface,
                         geometry.x,
                         geometry.y,
                         geometry.width,
                         geometry.height);
}

} // namespace lambda::compositor::wm

namespace lambda::compositor::wm {

void snapToplevel(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface, bool leftHalf) {
  snapToplevel(server, surface, leftHalf ? SnapTarget::LeftHalf : SnapTarget::RightHalf);
}

} // namespace lambda::compositor::wm

namespace lambda::compositor {

using wm::isManagedToplevel;
using wm::kMinWindowHeight;
using wm::kMinWindowWidth;
using wm::clearPreFullscreenState;
using wm::fallbackFloatingGeometry;
using wm::frameOutsetForSurface;
using wm::snapOutputGeometryFor;
using wm::startGeometryAnimation;
using wm::topInsetForSurface;

bool restoreToplevel(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface) {
  if (!isManagedToplevel(surface) || (!surface->snapped && !surface->maximized && !surface->fullscreen)) return false;
  bool const wasFullscreen = surface->fullscreen;
  bool const restoreMaximized = wasFullscreen && surface->preFullscreenMaximized;
  bool const restoreSnapped = wasFullscreen && surface->preFullscreenSnapped;
  if (restoreMaximized) {
    WindowGeometry const target = maximizedWindowGeometry(snapOutputGeometryFor(server),
                                                          topInsetForSurface(server, surface),
                                                          frameOutsetForSurface(server, surface));
    surface->fullscreen = false;
    surface->maximized = true;
    surface->snapped = false;
    clearPreFullscreenState(surface);
    startGeometryAnimation(server, surface, target.x, target.y, target.width, target.height);
    return true;
  }
  if (restoreSnapped) {
    std::int32_t const targetWidth =
        std::max(kMinWindowWidth,
                 surface->preFullscreenWidth > 0 ? surface->preFullscreenWidth : displayWidth(surface));
    std::int32_t const targetHeight =
        std::max(kMinWindowHeight,
                 surface->preFullscreenHeight > 0 ? surface->preFullscreenHeight : displayHeight(surface));
    std::int32_t const frameOutset = frameOutsetForSurface(server, surface);
    std::int32_t const minX = std::min(frameOutset, std::max(0, server->logicalOutputWidth() - targetWidth));
    std::int32_t const maxX = std::max(minX, server->logicalOutputWidth() - targetWidth - frameOutset);
    std::int32_t const targetX = std::clamp(surface->preFullscreenX, minX, maxX);
    OutputGeometry const output = snapOutputGeometryFor(server);
    std::int32_t const targetY =
        std::clamp(surface->preFullscreenY,
                   topInsetForSurface(server, surface),
                   std::max(topInsetForSurface(server, surface), output.height - targetHeight - frameOutset));
    surface->fullscreen = false;
    surface->maximized = false;
    surface->snapped = true;
    clearPreFullscreenState(surface);
    startGeometryAnimation(server, surface, targetX, targetY, targetWidth, targetHeight);
    return true;
  }
  WindowGeometry const fallback = fallbackFloatingGeometry(server, surface);
  std::int32_t const restoreWidth =
      std::max(kMinWindowWidth, surface->restoreWidth > 0 ? surface->restoreWidth : fallback.width);
  std::int32_t const restoreHeight =
      std::max(kMinWindowHeight, surface->restoreHeight > 0 ? surface->restoreHeight : fallback.height);
  std::int32_t const frameOutset = frameOutsetForSurface(server, surface);
  std::int32_t const minX = std::min(frameOutset, std::max(0, server->logicalOutputWidth() - restoreWidth));
  std::int32_t const maxX = std::max(minX, server->logicalOutputWidth() - restoreWidth - frameOutset);
  std::int32_t const restoreX = std::clamp(surface->restoreX, minX, maxX);
  OutputGeometry const restoreOutput = snapOutputGeometryFor(server);
  std::int32_t const restoreY =
      std::clamp(surface->restoreY,
                 topInsetForSurface(server, surface),
                 std::max(topInsetForSurface(server, surface), restoreOutput.height - restoreHeight - frameOutset));
  surface->maximized = false;
  surface->snapped = false;
  surface->fullscreen = false;
  if (wasFullscreen) clearPreFullscreenState(surface);
  startGeometryAnimation(server, surface, restoreX, restoreY, restoreWidth, restoreHeight);
  return true;
}

} // namespace lambda::compositor

namespace lambda::compositor {

using wm::isManagedToplevel;
using wm::clearPreFullscreenState;
using wm::fallbackFloatingGeometry;
using wm::frameOutsetForSurface;
using wm::snapOutputGeometryFor;
using wm::startGeometryAnimation;
using wm::topInsetForSurface;

void maximizeToplevel(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface) {
  if (!isManagedToplevel(surface)) return;
  if (surface->maximized || surface->fullscreen) return;
  if (!surface->snapped) {
    WindowGeometry const fallback = fallbackFloatingGeometry(server, surface);
    std::int32_t const currentWidth = displayWidth(surface);
    std::int32_t const currentHeight = displayHeight(surface);
    surface->restoreX = surface->windowX;
    surface->restoreY = surface->windowY;
    surface->restoreWidth = currentWidth > 0 ? currentWidth : fallback.width;
    surface->restoreHeight = currentHeight > 0 ? currentHeight : fallback.height;
  }
  WindowGeometry const target = maximizedWindowGeometry(snapOutputGeometryFor(server),
                                                        topInsetForSurface(server, surface),
                                                        frameOutsetForSurface(server, surface));
  surface->maximized = true;
  surface->snapped = false;
  surface->fullscreen = false;
  clearPreFullscreenState(surface);
  startGeometryAnimation(server, surface, target.x, target.y, target.width, target.height);
}

} // namespace lambda::compositor

namespace lambda::compositor::wm {

void snapFocusedToplevel(WaylandServer::Impl* server, bool leftHalf) {
  snapToplevel(server, server->keyboardFocus_, leftHalf);
}

} // namespace lambda::compositor::wm

namespace lambda::compositor::wm {

void maximizeFocusedToplevel(WaylandServer::Impl* server) {
  lambda::compositor::maximizeToplevel(server, server->keyboardFocus_);
}

} // namespace lambda::compositor::wm

namespace lambda::compositor::wm {

bool restoreFocusedToplevel(WaylandServer::Impl* server) {
  return lambda::compositor::restoreToplevel(server, server->keyboardFocus_);
}

} // namespace lambda::compositor::wm

namespace lambda::compositor::wm {

void fullscreenToplevel(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface) {
  if (!isManagedToplevel(surface) || surface->fullscreen) return;
  surface->preFullscreenSnapped = surface->snapped;
  surface->preFullscreenMaximized = surface->maximized;
  surface->preFullscreenX = surface->windowX;
  surface->preFullscreenY = surface->windowY;
  surface->preFullscreenWidth = displayWidth(surface);
  surface->preFullscreenHeight = displayHeight(surface);
  if (!surface->snapped && !surface->maximized) {
    surface->restoreX = surface->windowX;
    surface->restoreY = surface->windowY;
    surface->restoreWidth = displayWidth(surface);
    surface->restoreHeight = displayHeight(surface);
  }
  std::int32_t const targetWidth = std::max(kMinWindowWidth, server->logicalOutputWidth());
  std::int32_t const targetHeight = std::max(kMinWindowHeight, server->logicalOutputHeight());
  surface->fullscreen = true;
  surface->maximized = false;
  surface->snapped = false;
  surface->minimized = false;
  raiseSurface(server, surface);
  startGeometryAnimation(server, surface, 0, 0, targetWidth, targetHeight);
}

} // namespace lambda::compositor::wm

namespace lambda::compositor::wm {

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
      .frameOutset = frameOutsetForSurface(server, surface),
  });
  surface->windowX = restored.x;
  surface->windowY = restored.y;
  setConfiguredFrameSize(surface, restored.width, restored.height);
  surface->snapped = false;
  surface->maximized = false;
  surface->fullscreen = false;
  clearPreFullscreenState(surface);
  surface->geometryAnimationActive = false;
  clearSnapPreview(server);
  server->dragOffsetX_ = server->pointerX_ - static_cast<float>(surface->windowX);
  server->dragOffsetY_ = server->pointerY_ - static_cast<float>(surface->windowY);
  if (requestToplevelResizeConfigure(server, surface, restored.x, restored.y, restored.width, restored.height)) {
    server->flushClients();
  }
}

} // namespace lambda::compositor::wm

namespace lambda::compositor::wm {

void toggleMaximizedToplevel(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface) {
  if (!isManagedToplevel(surface)) return;
  if (surface->maximized) {
    lambda::compositor::restoreToplevel(server, surface);
    return;
  }
  lambda::compositor::maximizeToplevel(server, surface);
}

} // namespace lambda::compositor::wm

namespace lambda::compositor::wm {

void updateDrag(WaylandServer::Impl* server) {
  if (!server->dragSurface_) return;
  WaylandServer::Impl::Surface* surface = server->dragSurface_;
  restoreSnappedForDrag(server, surface);
  std::int32_t const oldX = surface->windowX;
  std::int32_t const oldY = surface->windowY;
  std::int32_t const topInset = topInsetForSurface(server, surface);
  std::int32_t const frameOutset = frameOutsetForSurface(server, surface);
  OutputGeometry const output = snapOutputGeometryFor(server);
  WindowGeometry const proposed = {
      .x = static_cast<std::int32_t>(server->pointerX_ - server->dragOffsetX_),
      .y = static_cast<std::int32_t>(server->pointerY_ - server->dragOffsetY_),
      .width = displayWidth(surface),
      .height = displayHeight(surface),
  };
  WindowGeometry const next =
      centerSnappedWindowGeometry(proposed, output, topInset, kCompositorCenterSnapThreshold, frameOutset);
  surface->windowX = next.x;
  surface->windowY = next.y;
  if (surface->windowX != oldX || surface->windowY != oldY) {
    ++server->contentSerial_;
  }
  activeDragSnapTarget(server, surface, monotonicMilliseconds());
}

} // namespace lambda::compositor::wm

namespace lambda::compositor::wm {

void updateResize(WaylandServer::Impl* server) {
  WaylandServer::Impl::Surface* surface = server->resizeSurface_;
  if (!surface) return;
  surface->geometryAnimationActive = false;
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

  std::int32_t const nextX = left ? next.x : surface->windowX;
  std::int32_t const nextY = top ? next.y : surface->windowY;
  if (nextX == server->resizeLastX_ &&
      nextY == server->resizeLastY_ &&
      next.width == server->resizeLastWidth_ &&
      next.height == server->resizeLastHeight_) {
    return;
  }
  if (lambda::detail::resizeTraceMetadataEnabled()) {
    surface->lastResizeInputNsec = lambda::detail::resizeTraceTimestampNanoseconds();
  }
  server->noteResizePacingActivity();
  server->resizeLastX_ = nextX;
  server->resizeLastY_ = nextY;
  server->resizeLastWidth_ = next.width;
  server->resizeLastHeight_ = next.height;
  LAMBDA_RESIZE_TRACE("compositor",
                            "update-resize surface=%llu pointer=%.1f,%.1f window=%d,%d size=%dx%d "
                            "delta=%.1f,%.1f edges=%u\n",
                            static_cast<unsigned long long>(surface->id),
                            server->pointerX_,
                            server->pointerY_,
                            nextX,
                            nextY,
                            next.width,
                            next.height,
                            dx,
                            dy,
                            server->resizeEdges_);
  if (requestToplevelResizeConfigure(server, surface, nextX, nextY, next.width, next.height)) {
    server->flushClients();
  }
}

} // namespace lambda::compositor::wm
