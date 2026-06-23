#include "Compositor/Window/WindowManagerInternal.hpp"

#include "Compositor/Wayland/WaylandServerImpl.hpp"
#include "Compositor/Wayland/XdgPopupState.hpp"
#include "Compositor/Wayland/LayerShellState.hpp"
#include "Compositor/Wayland/OutputState.hpp"
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

namespace lambdaui::compositor::wm {

bool isManagedToplevel(WaylandServer::Impl::Surface const* surface) {
  return surfaceIsXdgToplevel(surface);
}

bool containsPoint(float x, float y, float left, float top, float right, float bottom) {
  return x >= left && x < right && y >= top && y < bottom;
}

OutputGeometry outputGeometryFor(WaylandServer::Impl const* server) {
  OutputLayoutBox const layout = server ? selectedOutputLayoutBox(server->output_.width,
                                                                  server->output_.height,
                                                                  server->preferredScale())
                                        : OutputLayoutBox{};
  return {
      .x = server ? layout.x : 0,
      .y = server ? layout.y : 0,
      .width = server ? layout.width : 0,
      .height = server ? layout.height : 0,
  };
}

OutputGeometry snapOutputGeometryFor(WaylandServer::Impl const* server) {
  OutputGeometry output = outputGeometryFor(server);
  output.height = std::max(0, output.height - (server ? server->dockReservedZone_ : 0));
  return output;
}

bool layerSurfaceAboveWindows(WaylandServer::Impl::Surface const* surface) {
  return surfaceIsLayerSurface(surface) && surface->layerSurface &&
         surface->layerSurface->mapped &&
         surface->layerSurface->layer >= ZWLR_LAYER_SHELL_V1_LAYER_TOP;
}

bool hasVisibleFullscreenToplevel(WaylandServer::Impl const* server) {
  if (!server) return false;
  return std::any_of(server->surfaces_.begin(), server->surfaces_.end(), [](auto const& surface) {
    return surface && surfaceIsXdgToplevel(surface.get()) && surface->fullscreen && !surface->minimized;
  });
}

bool fullscreenHiddenShellPanel(WaylandServer::Impl::Surface const* surface) {
  return surfaceIsLayerSurface(surface) && surface->layerSurface &&
         surface->layerSurface->mapped &&
         layerShellNamespaceHidesForFullscreen(surface->layerSurface->nameSpace);
}

WaylandServer::Impl::Surface* aboveWindowLayerAt(WaylandServer::Impl* server, float x, float y) {
  if (!server) return nullptr;

  bool const fullscreenActive = hasVisibleFullscreenToplevel(server);
  for (auto it = server->surfaces_.rbegin(); it != server->surfaces_.rend(); ++it) {
    WaylandServer::Impl::Surface* surface = it->get();
    if (!layerSurfaceAboveWindows(surface) || surface->minimized) continue;
    if (surface->layerSurface &&
        surface->layerSurface->keyboardInteractivity ==
            ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE &&
        surface->layerSurface->nameSpace == "lambda.command-launcher") {
      return surface;
    }
    if (fullscreenActive && fullscreenHiddenShellPanel(surface)) continue;
  }

  for (std::uint32_t layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY; layer >= ZWLR_LAYER_SHELL_V1_LAYER_TOP;
       --layer) {
    for (auto it = server->surfaces_.rbegin(); it != server->surfaces_.rend(); ++it) {
      WaylandServer::Impl::Surface* surface = it->get();
      std::int32_t const width = displayWidth(surface);
      std::int32_t const height = displayHeight(surface);
      if (!layerSurfaceAboveWindows(surface) || surface->minimized || width <= 0 || height <= 0) continue;
      if (!surface->layerSurface || surface->layerSurface->layer != layer) continue;
      if (fullscreenActive && fullscreenHiddenShellPanel(surface)) continue;
      float const left = static_cast<float>(surface->windowX);
      float const top = static_cast<float>(surface->windowY);
      if (containsPoint(x, y, left, top, left + static_cast<float>(width), top + static_cast<float>(height))) {
        if (!inputRegionContains(surface, x - left, y - top)) continue;
        return surface;
      }
    }
  }
  return nullptr;
}

std::int32_t titleBarHeightFor(WaylandServer::Impl const* server) {
  return std::max(0, server ? server->chromeConfig_.titleBarHeight : kTitleBarHeight);
}

std::int32_t resizeGripSizeFor(WaylandServer::Impl const* server) {
  return std::clamp(server ? server->chromeConfig_.resizeGripSize : 4, 1, 24);
}

CornerRadius clampedCornerRadius(CornerRadius radius, float width, float height) {
  float const maximum = std::max(0.f, std::min(width, height) * 0.5f);
  radius.topLeft = std::clamp(radius.topLeft, 0.f, maximum);
  radius.topRight = std::clamp(radius.topRight, 0.f, maximum);
  radius.bottomRight = std::clamp(radius.bottomRight, 0.f, maximum);
  radius.bottomLeft = std::clamp(radius.bottomLeft, 0.f, maximum);
  return radius;
}

CornerRadius insetCornerRadius(CornerRadius radius, float inset) {
  radius.topLeft = std::max(0.f, radius.topLeft - inset);
  radius.topRight = std::max(0.f, radius.topRight - inset);
  radius.bottomRight = std::max(0.f, radius.bottomRight - inset);
  radius.bottomLeft = std::max(0.f, radius.bottomLeft - inset);
  return radius;
}

bool pointInCorner(float x, float y, float centerX, float centerY, float radius) {
  float const dx = x - centerX;
  float const dy = y - centerY;
  return dx * dx + dy * dy <= radius * radius;
}

bool roundedRectContainsPoint(float x, float y, float left, float top, float right, float bottom, CornerRadius radius) {
  if (!containsPoint(x, y, left, top, right, bottom)) return false;
  float const width = right - left;
  float const height = bottom - top;
  radius = clampedCornerRadius(radius, width, height);
  if (radius.topLeft > 0.f && x < left + radius.topLeft && y < top + radius.topLeft) {
    return pointInCorner(x, y, left + radius.topLeft, top + radius.topLeft, radius.topLeft);
  }
  if (radius.topRight > 0.f && x >= right - radius.topRight && y < top + radius.topRight) {
    return pointInCorner(x, y, right - radius.topRight, top + radius.topRight, radius.topRight);
  }
  if (radius.bottomRight > 0.f && x >= right - radius.bottomRight && y >= bottom - radius.bottomRight) {
    return pointInCorner(x, y, right - radius.bottomRight, bottom - radius.bottomRight, radius.bottomRight);
  }
  if (radius.bottomLeft > 0.f && x < left + radius.bottomLeft && y >= bottom - radius.bottomLeft) {
    return pointInCorner(x, y, left + radius.bottomLeft, bottom - radius.bottomLeft, radius.bottomLeft);
  }
  return true;
}

WaylandServer::Impl::XdgToplevel* toplevelForSurfaceConst(WaylandServer::Impl* server,
                                                          WaylandServer::Impl::Surface const* surface) {
  return toplevelForSurface(server, const_cast<WaylandServer::Impl::Surface*>(surface));
}

bool surfaceServerSideDecorated(WaylandServer::Impl* server, WaylandServer::Impl::Surface const* surface) {
  return toplevelServerSideDecorated(server, toplevelForSurfaceConst(server, surface));
}

bool surfaceUsesCutouts(WaylandServer::Impl* server, WaylandServer::Impl::Surface const* surface) {
  return toplevelUsesCutouts(server, toplevelForSurfaceConst(server, surface));
}

std::int32_t externalTitleBarHeight(WaylandServer::Impl* server, WaylandServer::Impl::Surface const* surface) {
  return surfaceServerSideDecorated(server, surface) && !surfaceUsesCutouts(server, surface)
             ? titleBarHeightFor(server)
             : 0;
}

std::int32_t frameOutsetForSurface(WaylandServer::Impl* server, WaylandServer::Impl::Surface const* surface) {
  if (!server || !surface) return 0;
  if (!surfaceServerSideDecorated(server, surface) || surfaceUsesCutouts(server, surface)) return 0;
  if (externalTitleBarHeight(server, surface) <= 0) return 0;
  return static_cast<std::int32_t>(std::ceil(std::max(0.f, server->chromeConfig_.contentInsetWidth)));
}

std::int32_t topInsetForSurface(WaylandServer::Impl* server, WaylandServer::Impl::Surface const* surface) {
  return externalTitleBarHeight(server, surface);
}

ResizeEdge resizeEdgesFromXdg(std::uint32_t edges) {
  switch (edges) {
  case XDG_TOPLEVEL_RESIZE_EDGE_TOP:
    return ResizeEdge::Top;
  case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM:
    return ResizeEdge::Bottom;
  case XDG_TOPLEVEL_RESIZE_EDGE_LEFT:
    return ResizeEdge::Left;
  case XDG_TOPLEVEL_RESIZE_EDGE_RIGHT:
    return ResizeEdge::Right;
  case XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT:
    return ResizeEdge::Top | ResizeEdge::Left;
  case XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT:
    return ResizeEdge::Top | ResizeEdge::Right;
  case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT:
    return ResizeEdge::Bottom | ResizeEdge::Left;
  case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT:
    return ResizeEdge::Bottom | ResizeEdge::Right;
  default:
    return ResizeEdge::None;
  }
}

CursorShape cursorShapeForResizeEdges(std::uint32_t edges) {
  switch (edges) {
  case XDG_TOPLEVEL_RESIZE_EDGE_LEFT:
  case XDG_TOPLEVEL_RESIZE_EDGE_RIGHT:
    return CursorShape::ResizeEW;
  case XDG_TOPLEVEL_RESIZE_EDGE_TOP:
  case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM:
    return CursorShape::ResizeNS;
  case XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT:
  case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT:
    return CursorShape::ResizeNWSE;
  case XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT:
  case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT:
    return CursorShape::ResizeNESW;
  default:
    return CursorShape::Arrow;
  }
}

std::uint32_t monotonicMilliseconds() {
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<std::uint32_t>(static_cast<std::uint64_t>(now.tv_sec) * 1000ull +
                                    static_cast<std::uint64_t>(now.tv_nsec) / 1'000'000ull);
}

float clamp01(float value) {
  return std::clamp(value, 0.f, 1.f);
}

float easeOutCubic(float value) {
  float const t = clamp01(value);
  float const inverse = 1.f - t;
  return 1.f - inverse * inverse * inverse;
}

std::int32_t lerpInt(std::int32_t from, std::int32_t to, float progress) {
  return static_cast<std::int32_t>(std::lround(static_cast<float>(from) +
                                               static_cast<float>(to - from) * progress));
}

WaylandServer::Impl::XdgPopup* popupForSurface(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface) {
  if (!surface) return nullptr;
  for (auto const& popup : server->popups_) {
    if (popup && popup->xdgSurface && popup->xdgSurface->surface == surface) return popup.get();
  }
  return nullptr;
}

bool popupTraceEnabled() {
  static bool const enabled = [] {
    return lambdaui::debug::envNonZero(std::getenv("LAMBDA_WINDOW_MANAGER_POPUP_TRACE"));
  }();
  return enabled;
}

void popupTrace(char const* fmt, ...) {
  if (!popupTraceEnabled()) return;
  va_list args;
  va_start(args, fmt);
  std::vfprintf(stderr, fmt, args);
  va_end(args);
}

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

bool popupIsDescendantOf(WaylandServer::Impl* server,
                         WaylandServer::Impl::XdgPopup* popup,
                         WaylandServer::Impl::XdgPopup* ancestor) {
  if (!popup || !ancestor || !ancestor->xdgSurface || !ancestor->xdgSurface->surface) return false;
  if (popup == ancestor) return true;
  WaylandServer::Impl::Surface* parent = popup->parentSurface;
  while (parent) {
    if (parent == ancestor->xdgSurface->surface) return true;
    if (!surfaceIsXdgPopup(parent)) return false;
    WaylandServer::Impl::XdgPopup* parentPopup = popupForSurface(server, parent);
    if (!parentPopup) return false;
    parent = parentPopup->parentSurface;
  }
  return false;
}

std::optional<WindowGeometry> popupScreenBounds(WaylandServer::Impl* server, WaylandServer::Impl::XdgPopup* popup) {
  if (!server || !popup || popup->dismissed || !popup->xdgSurface || !popup->xdgSurface->surface) {
    return std::nullopt;
  }

  WindowGeometry bounds{
      .x = popup->configuredX,
      .y = popup->configuredY,
      .width = popup->configuredWidth,
      .height = popup->configuredHeight,
  };
  if (bounds.width <= 0 || bounds.height <= 0) return std::nullopt;

  WaylandServer::Impl::Surface* parent = popup->parentSurface;
  while (surfaceIsXdgPopup(parent)) {
    WaylandServer::Impl::XdgPopup* parentPopup = popupForSurface(server, parent);
    if (!parentPopup || parentPopup->dismissed) return std::nullopt;
    if (parentPopup->configuredWidth <= 0 || parentPopup->configuredHeight <= 0) return std::nullopt;
    bounds.x += parentPopup->configuredX;
    bounds.y += parentPopup->configuredY;
    parent = parentPopup->parentSurface;
  }
  if (!parent) return bounds;
  auto const parentGeometry = usableWindowGeometryFor(parent);
  if (!parentGeometry) return std::nullopt;

  bounds.x += parentGeometry->x;
  bounds.y += parentGeometry->y;
  return bounds;
}

WaylandServer::Impl::Surface* popupAt(WaylandServer::Impl* server, float x, float y) {
  wl_client* const grabClient = popupGrabClient(server);
  for (auto it = server->popups_.rbegin(); it != server->popups_.rend(); ++it) {
    WaylandServer::Impl::XdgPopup* popup = it->get();
    if (!popup || popup->dismissed || !popup->resource || !popup->xdgSurface || !popup->xdgSurface->surface) {
      continue;
    }
    WaylandServer::Impl::Surface* surface = popup->xdgSurface->surface;
    if (grabClient && !surfaceBelongsToClient(surface, grabClient)) continue;
    auto const bounds = popupScreenBounds(server, popup);
    if (!bounds) continue;
    float const left = static_cast<float>(bounds->x);
    float const top = static_cast<float>(bounds->y);
    float const right = left + static_cast<float>(bounds->width);
    float const bottom = top + static_cast<float>(bounds->height);
    if (containsPoint(x, y, left, top, right, bottom) &&
        inputRegionContains(surface, surfaceLocalX(surface, x), surfaceLocalY(surface, y))) {
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

std::optional<ChromeHitContext> topChromeHitContext(WaylandServer::Impl* server, float x, float y) {
  if (aboveWindowLayerAt(server, x, y)) return std::nullopt;
  if (popupAt(server, x, y)) return std::nullopt;
  for (auto it = server->surfaces_.rbegin(); it != server->surfaces_.rend(); ++it) {
    WaylandServer::Impl::Surface* surface = it->get();
    FrameDisplaySize const frameSize = interactiveFrameDisplaySize(surface);
    std::int32_t const width = frameSize.width;
    std::int32_t const height = frameSize.height;
    if (!surface || surface->minimized || surfaceIsXdgPopup(surface) || width <= 0 || height <= 0) continue;

    float const contentLeft = static_cast<float>(surface->windowX);
    float const contentTop = static_cast<float>(surface->windowY);
    float const contentRight = contentLeft + static_cast<float>(width);
    float const contentBottom = contentTop + static_cast<float>(height);
    if (!isManagedToplevel(surface)) {
      if (surfaceIsTopLevelRenderable(surface) &&
          containsPoint(x, y, contentLeft, contentTop, contentRight, contentBottom)) {
        return std::nullopt;
      }
      continue;
    }

    bool const decorated = surfaceServerSideDecorated(server, surface);
    bool const cutouts = surfaceUsesCutouts(server, surface);
    std::int32_t const titleBarHeight = externalTitleBarHeight(server, surface);
    float const frameOutset = decorated && !cutouts && titleBarHeight > 0
                                  ? std::max(0.f, server ? server->chromeConfig_.contentInsetWidth : 0.f)
                                  : 0.f;
    float const frameLeft = contentLeft - frameOutset;
    float const frameRight = contentRight + frameOutset;
    float const frameTop = contentTop - static_cast<float>(titleBarHeight);
    float const frameBottom = contentBottom + frameOutset;
    CornerRadius const cornerRadius = server ? server->chromeConfig_.windowCornerRadius : CornerRadius{14.f};
    if (!roundedRectContainsPoint(x, y, frameLeft, frameTop, frameRight, frameBottom, cornerRadius)) continue;
    return ChromeHitContext{
        .surface = surface,
        .left = frameLeft,
        .top = frameTop,
        .right = frameRight,
        .bottom = frameBottom,
        .contentTop = contentTop,
        .cornerRadius = cornerRadius,
        .serverSideDecorated = decorated,
        .cutouts = cutouts,
    };
  }
  return std::nullopt;
}

bool controlsRegionContains(ChromeHitContext const& context, float x, float y) {
  if (!context.serverSideDecorated || !context.cutouts) return false;
  auto const& chrome = context.surface->server->chromeConfig_;
  ChromeControlsMetrics const metrics = chromeControlsMetrics(chrome);
  FrameDisplaySize const frameSize = interactiveFrameDisplaySize(context.surface);
  float const cutoutWidth = std::min(metrics.controlsWidth, static_cast<float>(frameSize.width));
  float const cutoutHeight = std::min(metrics.titleBarHeight, static_cast<float>(frameSize.height));
  return containsPoint(x, y, context.right - cutoutWidth, context.contentTop, context.right, context.contentTop + cutoutHeight);
}

ChromeButton chromeButtonAt(ChromeHitContext const& context, float x, float y) {
  if (!context.serverSideDecorated) return ChromeButton::None;
  if (context.cutouts && !controlsRegionContains(context, x, y)) return ChromeButton::None;
  auto const& chrome = context.surface->server->chromeConfig_;
  float const titleBarHeight = context.cutouts
                                   ? static_cast<float>(chrome.titleBarHeight)
                                   : std::max(0.f, context.contentTop - context.top);
  ChromeControlRects const rects =
      chromeControlRects(chrome, context.left, context.top, context.right - context.left, titleBarHeight);
  if (containsPoint(x,
                    y,
                    rects.closeButton.x,
                    rects.closeButton.y,
                    rects.closeButton.x + rects.closeButton.width,
                    rects.closeButton.y + rects.closeButton.height)) {
    return ChromeButton::Close;
  }
  if (containsPoint(x,
                    y,
                    rects.maximizeButton.x,
                    rects.maximizeButton.y,
                    rects.maximizeButton.x + rects.maximizeButton.width,
                    rects.maximizeButton.y + rects.maximizeButton.height)) {
    return ChromeButton::Maximize;
  }
  if (containsPoint(x,
                    y,
                    rects.minimizeButton.x,
                    rects.minimizeButton.y,
                    rects.minimizeButton.x + rects.minimizeButton.width,
                    rects.minimizeButton.y + rects.minimizeButton.height)) {
    return ChromeButton::Minimize;
  }
  return ChromeButton::None;
}

float cornerGripSpan(float radius, float gripSize) {
  return std::max(radius, gripSize);
}

std::uint32_t resizeEdgesForContext(ChromeHitContext const& context, float x, float y) {
  if (!context.surface || !context.surface->server) return XDG_TOPLEVEL_RESIZE_EDGE_NONE;
  float const gripSize = static_cast<float>(resizeGripSizeFor(context.surface->server));
  float const width = context.right - context.left;
  float const height = context.bottom - context.top;
  if (width <= 0.f || height <= 0.f) return XDG_TOPLEVEL_RESIZE_EDGE_NONE;

  CornerRadius const outerRadius = clampedCornerRadius(context.cornerRadius, width, height);
  if (!roundedRectContainsPoint(x, y, context.left, context.top, context.right, context.bottom, outerRadius)) {
    return XDG_TOPLEVEL_RESIZE_EDGE_NONE;
  }

  float const innerLeft = context.left + gripSize;
  float const innerTop = context.top + gripSize;
  float const innerRight = context.right - gripSize;
  float const innerBottom = context.bottom - gripSize;
  if (innerRight > innerLeft && innerBottom > innerTop) {
    CornerRadius const innerRadius = insetCornerRadius(outerRadius, gripSize);
    if (roundedRectContainsPoint(x, y, innerLeft, innerTop, innerRight, innerBottom, innerRadius)) {
      return XDG_TOPLEVEL_RESIZE_EDGE_NONE;
    }
  }

  float const topLeftSpan = cornerGripSpan(outerRadius.topLeft, gripSize);
  float const topRightSpan = cornerGripSpan(outerRadius.topRight, gripSize);
  float const bottomRightSpan = cornerGripSpan(outerRadius.bottomRight, gripSize);
  float const bottomLeftSpan = cornerGripSpan(outerRadius.bottomLeft, gripSize);
  if (x < context.left + topLeftSpan && y < context.top + topLeftSpan) {
    return XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT;
  }
  if (x >= context.right - topRightSpan && y < context.top + topRightSpan) {
    return XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT;
  }
  if (x >= context.right - bottomRightSpan && y >= context.bottom - bottomRightSpan) {
    return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT;
  }
  if (x < context.left + bottomLeftSpan && y >= context.bottom - bottomLeftSpan) {
    return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT;
  }

  bool const nearLeft = x < context.left + gripSize;
  bool const nearRight = x >= context.right - gripSize;
  bool const nearTop = y < context.top + gripSize;
  bool const nearBottom = y >= context.bottom - gripSize;
  if (nearLeft && nearTop) return XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT;
  if (nearRight && nearTop) return XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT;
  if (nearLeft && nearBottom) return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT;
  if (nearRight && nearBottom) return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT;
  if (nearLeft) return XDG_TOPLEVEL_RESIZE_EDGE_LEFT;
  if (nearRight) return XDG_TOPLEVEL_RESIZE_EDGE_RIGHT;
  if (nearTop) return XDG_TOPLEVEL_RESIZE_EDGE_TOP;
  if (nearBottom) return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM;
  return XDG_TOPLEVEL_RESIZE_EDGE_NONE;
}


} // namespace lambdaui::compositor::wm
