#include "Compositor/Wayland/WaylandServerImpl.hpp"
#include "Compositor/Chrome/ChromeMetrics.hpp"
#include "Compositor/Window/WindowGeometry.hpp"
#include "Detail/ResizeTrace.hpp"
#include "pointer-constraints-unstable-v1-server-protocol.h"
#include "relative-pointer-unstable-v1-server-protocol.h"
#include "xdg-shell-server-protocol.h"

#include <linux/input-event-codes.h>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <optional>
#include <vector>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <xkbcommon/xkbcommon.h>

namespace flux::compositor {
namespace {

constexpr std::int32_t kTitleBarHeight = kCompositorTitleBarHeight;
constexpr std::int32_t kMinWindowWidth = kCompositorMinWindowWidth;
constexpr std::int32_t kMinWindowHeight = kCompositorMinWindowHeight;
constexpr std::uint32_t kGeometryAnimationMs = 180;
constexpr std::uint32_t kInvalidModifierIndex = ~0u;

bool isManagedToplevel(WaylandServer::Impl::Surface const* surface) {
  return surfaceIsXdgToplevel(surface);
}

bool containsPoint(float x, float y, float left, float top, float right, float bottom) {
  return x >= left && x < right && y >= top && y < bottom;
}

WindowGeometry windowGeometryFor(WaylandServer::Impl::Surface const* surface) {
  return {
      .x = surface ? surface->windowX : 0,
      .y = surface ? surface->windowY : 0,
      .width = displayWidth(surface),
      .height = displayHeight(surface),
  };
}

OutputGeometry outputGeometryFor(WaylandServer::Impl const* server) {
  return {
      .width = server ? server->logicalOutputWidth() : 0,
      .height = server ? server->logicalOutputHeight() : 0,
  };
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

WaylandServer::Impl::XdgPopup* popupForSurface(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface) {
  if (!surface) return nullptr;
  for (auto const& popup : server->popups_) {
    if (popup && popup->xdgSurface && popup->xdgSurface->surface == surface) return popup.get();
  }
  return nullptr;
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
  if (!parent) return std::nullopt;
  if (displayWidth(parent) <= 0 || displayHeight(parent) <= 0) return std::nullopt;

  bounds.x += parent->windowX;
  bounds.y += parent->windowY;
  return bounds;
}

WaylandServer::Impl::Surface* popupAt(WaylandServer::Impl* server, float x, float y) {
  for (auto it = server->popups_.rbegin(); it != server->popups_.rend(); ++it) {
    WaylandServer::Impl::XdgPopup* popup = it->get();
    auto const bounds = popupScreenBounds(server, popup);
    if (!bounds) continue;
    float const left = static_cast<float>(bounds->x);
    float const top = static_cast<float>(bounds->y);
    float const right = left + static_cast<float>(bounds->width);
    float const bottom = top + static_cast<float>(bounds->height);
    if (containsPoint(x, y, left, top, right, bottom)) return popup->xdgSurface->surface;
  }
  return nullptr;
}

struct ChromeHitContext {
  WaylandServer::Impl::Surface* surface = nullptr;
  float left = 0.f;
  float top = 0.f;
  float right = 0.f;
  float bottom = 0.f;
  float contentTop = 0.f;
  CornerRadius cornerRadius{};
  bool serverSideDecorated = false;
  bool cutouts = false;
};

std::optional<ChromeHitContext> topChromeHitContext(WaylandServer::Impl* server, float x, float y) {
  if (popupAt(server, x, y)) return std::nullopt;
  for (auto it = server->surfaces_.rbegin(); it != server->surfaces_.rend(); ++it) {
    WaylandServer::Impl::Surface* surface = it->get();
    std::int32_t const width = displayWidth(surface);
    std::int32_t const height = displayHeight(surface);
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
    float const frameTop = contentTop - static_cast<float>(externalTitleBarHeight(server, surface));
    CornerRadius const cornerRadius = server ? server->chromeConfig_.windowCornerRadius : CornerRadius{14.f};
    if (!roundedRectContainsPoint(x, y, contentLeft, frameTop, contentRight, contentBottom, cornerRadius)) continue;
    return ChromeHitContext{
        .surface = surface,
        .left = contentLeft,
        .top = frameTop,
        .right = contentRight,
        .bottom = contentBottom,
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
  float const cutoutWidth = std::min(metrics.controlsWidth, static_cast<float>(displayWidth(context.surface)));
  float const cutoutHeight = std::min(metrics.titleBarHeight, static_cast<float>(displayHeight(context.surface)));
  return containsPoint(x, y, context.right - cutoutWidth, context.contentTop, context.right, context.contentTop + cutoutHeight);
}

enum class ChromeButton {
  None,
  Close,
  Minimize,
};

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

} // namespace

WaylandServer::Impl::Surface* surfaceAt(WaylandServer::Impl* server, float x, float y) {
  if (auto* popup = popupAt(server, x, y)) return popup;

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
      return surface;
    }
  }
  return nullptr;
}

WaylandServer::Impl::Surface* titlebarAt(WaylandServer::Impl* server, float x, float y) {
  auto context = topChromeHitContext(server, x, y);
  if (!context) return nullptr;
  if (!context->serverSideDecorated || context->cutouts) return nullptr;
  return containsPoint(x, y, context->left, context->top, context->right, context->contentTop)
             ? context->surface
             : nullptr;
}

WaylandServer::Impl::Surface* closeButtonAt(WaylandServer::Impl* server, float x, float y) {
  auto context = topChromeHitContext(server, x, y);
  if (!context) return nullptr;
  return chromeButtonAt(*context, x, y) == ChromeButton::Close ? context->surface : nullptr;
}

WaylandServer::Impl::Surface* minimizeButtonAt(WaylandServer::Impl* server, float x, float y) {
  auto context = topChromeHitContext(server, x, y);
  if (!context) return nullptr;
  return chromeButtonAt(*context, x, y) == ChromeButton::Minimize ? context->surface : nullptr;
}

WaylandServer::Impl::Surface* resizeGripAt(WaylandServer::Impl* server, float x, float y, std::uint32_t& edges) {
  edges = XDG_TOPLEVEL_RESIZE_EDGE_NONE;
  auto context = topChromeHitContext(server, x, y);
  if (!context) return nullptr;

  edges = resizeEdgesForContext(*context, x, y);
  if (edges == XDG_TOPLEVEL_RESIZE_EDGE_NONE) return nullptr;
  return context->surface;
}

WaylandServer::Impl::Surface* resizeOrCloseChromeAt(WaylandServer::Impl* server,
                                                    float x,
                                                    float y,
                                                    bool& closeButton,
                                                    std::uint32_t& resizeEdges) {
  closeButton = false;
  resizeEdges = XDG_TOPLEVEL_RESIZE_EDGE_NONE;
  auto context = topChromeHitContext(server, x, y);
  if (!context) return nullptr;

  if (chromeButtonAt(*context, x, y) == ChromeButton::Close) {
    closeButton = true;
    return context->surface;
  }

  resizeEdges = resizeEdgesForContext(*context, x, y);
  if (resizeEdges == XDG_TOPLEVEL_RESIZE_EDGE_NONE) return nullptr;
  return context->surface;
}

void setCompositorCursorOverride(WaylandServer::Impl* server, CursorShape shape) {
  if (!server) return;
  server->compositorCursorOverride_ = true;
  server->compositorCursorShape_ = shape;
}

void clearCompositorCursorOverride(WaylandServer::Impl* server) {
  if (!server) return;
  server->compositorCursorOverride_ = false;
  server->compositorCursorShape_ = CursorShape::Arrow;
}

void updateCompositorCursorForPointer(WaylandServer::Impl* server) {
  if (!server) return;
  if (server->commandLauncherVisible_ || server->dragSurface_ || server->dndSource_) {
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

void removeSurfaceFromFocusOrder(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface) {
  if (!server || !surface) return;
  server->focusOrder_.erase(std::remove(server->focusOrder_.begin(), server->focusOrder_.end(), surface),
                            server->focusOrder_.end());
}

void noteFocusedToplevel(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface) {
  if (!server || !isManagedToplevel(surface)) return;
  removeSurfaceFromFocusOrder(server, surface);
  server->focusOrder_.push_back(surface);
}

WaylandServer::Impl::Surface* mostRecentToplevel(WaylandServer::Impl* server) {
  if (!server) return nullptr;
  for (auto it = server->focusOrder_.rbegin(); it != server->focusOrder_.rend(); ++it) {
    if (isManagedToplevel(*it) && !(*it)->minimized) return *it;
  }
  for (auto it = server->surfaces_.rbegin(); it != server->surfaces_.rend(); ++it) {
    if (isManagedToplevel(it->get()) && !it->get()->minimized) return it->get();
  }
  return nullptr;
}

void activateMostRecentToplevel(WaylandServer::Impl* server, std::uint32_t timeMs) {
  WaylandServer::Impl::Surface* target = mostRecentToplevel(server);
  if (!target) return;
  focusSurface(server, target, timeMs);
}

bool resourceBelongsToSurfaceClient(wl_resource* resource, WaylandServer::Impl::Surface const* surface) {
  return resource && surface && surface->resource &&
         wl_resource_get_client(resource) == wl_resource_get_client(surface->resource);
}

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

WaylandServer::Impl::XdgPopup* topmostPopup(WaylandServer::Impl* server) {
  for (auto it = server->popups_.rbegin(); it != server->popups_.rend(); ++it) {
    WaylandServer::Impl::XdgPopup* popup = it->get();
    if (!popup || popup->dismissed || !popup->resource || !popup->xdgSurface || !popup->xdgSurface->surface) continue;
    return popup;
  }
  return nullptr;
}

bool surfaceBelongsToPopup(WaylandServer::Impl::Surface* surface, WaylandServer::Impl::XdgPopup* popup) {
  return surface && popup && popup->xdgSurface && surface == popup->xdgSurface->surface;
}

bool dismissPopup(WaylandServer::Impl::XdgPopup* popup) {
  if (!popup || popup->dismissed) return false;
  std::fprintf(stderr,
               "flux-compositor: xdg_popup dismissed surface=%llu\n",
               static_cast<unsigned long long>(popup->xdgSurface && popup->xdgSurface->surface
                                                   ? popup->xdgSurface->surface->id
                                                   : 0));
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

bool dismissTopPopup(WaylandServer::Impl* server) {
  return dismissPopup(topmostPopup(server));
}

bool dismissTopPopupOutside(WaylandServer::Impl* server, WaylandServer::Impl::Surface* target) {
  WaylandServer::Impl::XdgPopup* popup = topmostPopup(server);
  if (!popup || surfaceBelongsToPopup(target, popup)) return false;
  return dismissPopup(popup);
}

std::uint32_t keyboardModifierMask(WaylandServer::Impl* server);

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

std::uint32_t modifierBit(std::uint32_t index, bool active) {
  if (!active || index == kInvalidModifierIndex || index >= 32u) return 0u;
  return 1u << index;
}

std::uint32_t keyboardModifierMask(WaylandServer::Impl* server) {
  return modifierBit(server->shiftModifierIndex_, server->shiftDown_) |
         modifierBit(server->ctrlModifierIndex_, server->ctrlDown_) |
         modifierBit(server->altModifierIndex_, server->altDown_) |
         modifierBit(server->logoModifierIndex_, server->metaDown_);
}

void sendKeyboardModifiers(WaylandServer::Impl* server) {
  if (!server->keyboardFocus_) return;
  std::uint32_t const depressed = keyboardModifierMask(server);
  for (wl_resource* keyboard : server->keyboardResources_) {
    if (!resourceBelongsToSurfaceClient(keyboard, server->keyboardFocus_)) continue;
    wl_keyboard_send_modifiers(keyboard, server->nextInputSerial_++, depressed, 0, 0, 0);
  }
}

void focusSurface(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface, std::uint32_t timeMs) {
  if (!surface) return;
  surface->minimized = false;
  raiseSurface(server, surface);
  setKeyboardFocus(server, surface);
  sendPointerFocus(server, surfaceAt(server, server->pointerX_, server->pointerY_), timeMs);
}

WaylandServer::Impl::Surface* previousFocusedToplevel(WaylandServer::Impl* server,
                                                      WaylandServer::Impl::Surface* current) {
  if (!server) return nullptr;
  auto currentIt = std::find(server->focusOrder_.begin(), server->focusOrder_.end(), current);
  if (currentIt != server->focusOrder_.end()) {
    for (auto it = std::make_reverse_iterator(currentIt); it != server->focusOrder_.rend(); ++it) {
      if (isManagedToplevel(*it) && !(*it)->minimized) return *it;
    }
  }
  for (auto it = server->focusOrder_.rbegin(); it != server->focusOrder_.rend(); ++it) {
    if (*it != current && isManagedToplevel(*it) && !(*it)->minimized) return *it;
  }
  for (auto it = server->surfaces_.rbegin(); it != server->surfaces_.rend(); ++it) {
    if (it->get() != current && isManagedToplevel(it->get()) && !it->get()->minimized) return it->get();
  }
  return nullptr;
}

WaylandServer::Impl::XdgToplevel* focusedToplevel(WaylandServer::Impl* server) {
  if (!server->keyboardFocus_) return nullptr;
  if (auto* toplevel = toplevelForSurface(server, server->keyboardFocus_)) return toplevel;
  if (auto* popup = server->keyboardFocus_->xdgPopup; popup && popup->parentSurface) {
    return toplevelForSurface(server, popup->parentSurface);
  }
  return nullptr;
}

void minimizeToplevel(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface, std::uint32_t timeMs) {
  if (!isManagedToplevel(surface)) return;
  lowerSurface(server, surface);
  if (server->keyboardFocus_ == surface) {
    setKeyboardFocus(server, previousFocusedToplevel(server, surface));
  }
  if (server->pointerFocus_ == surface) {
    sendPointerFocus(server, surfaceAt(server, server->pointerX_, server->pointerY_), timeMs);
  }
}

bool closeFocusedToplevel(WaylandServer::Impl* server) {
  if (topmostPopup(server)) return dismissTopPopup(server);
  WaylandServer::Impl::XdgToplevel* toplevel = focusedToplevel(server);
  if (!toplevel || !toplevel->resource) return false;
  xdg_toplevel_send_close(toplevel->resource);
  return true;
}

bool cycleFocus(WaylandServer::Impl* server, std::uint32_t timeMs) {
  WaylandServer::Impl::Surface* target = previousFocusedToplevel(server, server->keyboardFocus_);
  if (!target || target == server->keyboardFocus_) return false;
  focusSurface(server, target, timeMs);
  return true;
}

std::optional<SnapPreviewSnapshot> snapPreviewForDrag(WaylandServer::Impl const* server) {
  WaylandServer::Impl::Surface const* surface = server->dragSurface_;
  if (!surface) return std::nullopt;
  auto* mutableServer = const_cast<WaylandServer::Impl*>(server);
  auto preview = snapPreviewGeometry(windowGeometryFor(surface),
                                     outputGeometryFor(server),
                                     topInsetForSurface(mutableServer, surface));
  if (!preview) return std::nullopt;
  return SnapPreviewSnapshot{
      .x = preview->x,
      .y = 0,
      .width = preview->width,
      .height = std::max(kMinWindowHeight, server->logicalOutputHeight()),
  };
}

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
  surface->geometryAnimationLastConfigureWidth = displayWidth(surface);
  surface->geometryAnimationLastConfigureHeight = displayHeight(surface);
  surface->geometryAnimationStartedAtMs = monotonicMilliseconds();
  surface->geometryAnimationActive = true;
  if (surface->geometryAnimationStartX == targetX && surface->geometryAnimationStartY == targetY &&
      surface->geometryAnimationStartWidth == targetWidth &&
      surface->geometryAnimationStartHeight == targetHeight) {
    surface->geometryAnimationActive = false;
    return;
  }
  server->flushClients();
}

void snapToplevel(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface, bool leftHalf) {
  if (!isManagedToplevel(surface)) return;
  if (!surface->snapped && !surface->maximized) {
    surface->restoreX = surface->windowX;
    surface->restoreY = surface->windowY;
    surface->restoreWidth = displayWidth(surface);
    surface->restoreHeight = displayHeight(surface);
  }
  WindowGeometry const target = snappedWindowGeometry(outputGeometryFor(server),
                                                      leftHalf,
                                                      topInsetForSurface(server, surface));
  surface->snapped = true;
  surface->maximized = false;
  startGeometryAnimation(server,
                         surface,
                         target.x,
                         target.y,
                         target.width,
                         target.height);
}

bool restoreToplevel(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface) {
  if (!isManagedToplevel(surface) || (!surface->snapped && !surface->maximized)) return false;
  std::int32_t const restoreWidth =
      std::max(kMinWindowWidth, surface->restoreWidth > 0 ? surface->restoreWidth : surface->width);
  std::int32_t const restoreHeight =
      std::max(kMinWindowHeight, surface->restoreHeight > 0 ? surface->restoreHeight : surface->height);
  std::int32_t const restoreX =
      std::clamp(surface->restoreX, 0, std::max(0, server->logicalOutputWidth() - restoreWidth));
  std::int32_t const restoreY =
      std::clamp(surface->restoreY,
                 topInsetForSurface(server, surface),
                 std::max(topInsetForSurface(server, surface), server->logicalOutputHeight() - restoreHeight));
  surface->maximized = false;
  surface->snapped = false;
  startGeometryAnimation(server, surface, restoreX, restoreY, restoreWidth, restoreHeight);
  return true;
}

void maximizeToplevel(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface) {
  if (!isManagedToplevel(surface)) return;
  if (surface->maximized) return;
  if (!surface->snapped) {
    surface->restoreX = surface->windowX;
    surface->restoreY = surface->windowY;
    surface->restoreWidth = displayWidth(surface);
    surface->restoreHeight = displayHeight(surface);
  }
  WindowGeometry const target = maximizedWindowGeometry(outputGeometryFor(server),
                                                        topInsetForSurface(server, surface));
  surface->maximized = true;
  surface->snapped = false;
  startGeometryAnimation(server, surface, target.x, target.y, target.width, target.height);
}

void snapFocusedToplevel(WaylandServer::Impl* server, bool leftHalf) {
  snapToplevel(server, server->keyboardFocus_, leftHalf);
}

void maximizeFocusedToplevel(WaylandServer::Impl* server) {
  maximizeToplevel(server, server->keyboardFocus_);
}

bool restoreFocusedToplevel(WaylandServer::Impl* server) {
  return restoreToplevel(server, server->keyboardFocus_);
}

void restoreSnappedForDrag(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface) {
  if (!surface || (!surface->snapped && !surface->maximized)) return;
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
      .output = outputGeometryFor(server),
      .topInset = topInsetForSurface(server, surface),
  });
  surface->windowX = restored.x;
  surface->windowY = restored.y;
  setConfiguredFrameSize(surface, restored.width, restored.height);
  surface->snapped = false;
  surface->maximized = false;
  surface->geometryAnimationActive = false;
  server->dragOffsetX_ = server->pointerX_ - static_cast<float>(surface->windowX);
  server->dragOffsetY_ = server->pointerY_ - static_cast<float>(surface->windowY);
  sendToplevelConfigure(server, toplevelForSurface(server, surface), restored.width, restored.height);
}

void toggleMaximizedToplevel(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface) {
  if (!isManagedToplevel(surface)) return;
  if (surface->maximized) {
    restoreToplevel(server, surface);
    return;
  }
  maximizeToplevel(server, surface);
}

bool updateShortcutModifier(WaylandServer::Impl* server, std::uint32_t key, bool pressed) {
  bool changed = false;
  if (key == KEY_LEFTMETA || key == KEY_RIGHTMETA) {
    changed = server->metaDown_ != pressed;
    server->metaDown_ = pressed;
    if (changed) sendKeyboardModifiers(server);
    return true;
  }
  if (key == KEY_LEFTCTRL || key == KEY_RIGHTCTRL) {
    changed = server->ctrlDown_ != pressed;
    server->ctrlDown_ = pressed;
    if (changed) sendKeyboardModifiers(server);
    return false;
  }
  if (key == KEY_LEFTALT || key == KEY_RIGHTALT) {
    changed = server->altDown_ != pressed;
    server->altDown_ = pressed;
    if (changed) sendKeyboardModifiers(server);
    return false;
  }
  if (key == KEY_LEFTSHIFT || key == KEY_RIGHTSHIFT) {
    changed = server->shiftDown_ != pressed;
    server->shiftDown_ = pressed;
    if (changed) sendKeyboardModifiers(server);
    return false;
  }
  return false;
}

bool handleCompositorShortcut(WaylandServer::Impl* server, std::uint32_t key, bool pressed, std::uint32_t timeMs) {
  if (!pressed) return false;
  for (auto const& binding : server->shortcutBindings_) {
    if (binding.key != key) continue;
    if (binding.meta != server->metaDown_ || binding.ctrl != server->ctrlDown_ ||
        binding.alt != server->altDown_ || binding.shift != server->shiftDown_) {
      continue;
    }

    switch (binding.action) {
    case WaylandServer::ShortcutAction::CloseFocused:
      return closeFocusedToplevel(server);
    case WaylandServer::ShortcutAction::CycleFocus:
      return cycleFocus(server, timeMs);
    case WaylandServer::ShortcutAction::SnapLeft:
      snapFocusedToplevel(server, true);
      return true;
    case WaylandServer::ShortcutAction::SnapRight:
      snapFocusedToplevel(server, false);
      return true;
    case WaylandServer::ShortcutAction::Maximize:
      maximizeFocusedToplevel(server);
      return true;
    case WaylandServer::ShortcutAction::Restore:
      restoreFocusedToplevel(server);
      return true;
    case WaylandServer::ShortcutAction::LaunchCommand:
      server->commandLauncherVisible_ = true;
      server->commandLauncherText_.clear();
      server->commandLauncherMessage_ = "Type a command and press Enter";
      sendPointerFocus(server, nullptr, timeMs);
      return true;
    case WaylandServer::ShortcutAction::Terminate:
      std::raise(SIGTERM);
      return true;
    }
  }
  return false;
}

std::string commandLauncherTextForKey(WaylandServer::Impl* server, std::uint32_t key) {
  if (!server || !server->xkbState_) return {};
  char utf8[64]{};
  int const written = xkb_state_key_get_utf8(server->xkbState_, key + 8u, utf8, sizeof(utf8));
  if (written <= 0) return {};
  return std::string(utf8, static_cast<std::size_t>(written));
}

std::vector<std::string> parseCommandLine(std::string const& command) {
  std::vector<std::string> args;
  std::string current;
  char quote = '\0';
  bool escaping = false;
  for (char c : command) {
    if (escaping) {
      current.push_back(c);
      escaping = false;
      continue;
    }
    if (c == '\\') {
      escaping = true;
      continue;
    }
    if (quote != '\0') {
      if (c == quote) {
        quote = '\0';
      } else {
        current.push_back(c);
      }
      continue;
    }
    if (c == '\'' || c == '"') {
      quote = c;
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(c))) {
      if (!current.empty()) {
        args.push_back(std::move(current));
        current.clear();
      }
      continue;
    }
    current.push_back(c);
  }
  if (escaping) current.push_back('\\');
  if (!current.empty()) args.push_back(std::move(current));
  return args;
}

void spawnCommand(std::string const& command, std::string const& waylandDisplay) {
  std::vector<std::string> args = parseCommandLine(command);
  if (args.empty()) return;
  pid_t const child = fork();
  if (child < 0) {
    std::fprintf(stderr, "flux-compositor: fork failed while launching command\n");
    return;
  }
  if (child == 0) {
    if (setsid() < 0) _exit(126);
    pid_t const grandchild = fork();
    if (grandchild < 0) _exit(126);
    if (grandchild > 0) _exit(0);
    setenv("WAYLAND_DISPLAY", waylandDisplay.c_str(), 1);
    std::vector<char*> argv;
    argv.reserve(args.size() + 1u);
    for (std::string& arg : args) argv.push_back(arg.data());
    argv.push_back(nullptr);
    execvp(argv.front(), argv.data());
    _exit(127);
  }
  int status = 0;
  while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
}

bool handleCommandLauncherKey(WaylandServer::Impl* server, std::uint32_t key, bool pressed) {
  if (!server->commandLauncherVisible_) return false;
  if (!pressed) return true;
  if (server->ctrlDown_ || server->altDown_ || server->metaDown_) return true;

  switch (key) {
  case KEY_ESC:
    server->commandLauncherVisible_ = false;
    server->commandLauncherText_.clear();
    server->commandLauncherMessage_.clear();
    return true;
  case KEY_ENTER:
  case KEY_KPENTER: {
    std::string command = server->commandLauncherText_;
    command.erase(command.begin(),
                  std::find_if(command.begin(), command.end(), [](unsigned char c) {
                    return !std::isspace(c);
                  }));
    command.erase(std::find_if(command.rbegin(), command.rend(), [](unsigned char c) {
                    return !std::isspace(c);
                  }).base(),
                  command.end());
    if (command.empty()) {
      server->commandLauncherMessage_ = "Type a command";
      return true;
    }
    std::fprintf(stderr, "flux-compositor: launching command: %s\n", command.c_str());
    server->commandLauncherVisible_ = false;
    server->commandLauncherText_.clear();
    server->commandLauncherMessage_.clear();
    spawnCommand(command, server->socketName_);
    return true;
  }
  case KEY_BACKSPACE:
    if (!server->commandLauncherText_.empty()) server->commandLauncherText_.pop_back();
    return true;
  default:
    if (std::string text = commandLauncherTextForKey(server, key); !text.empty()) {
      if (server->commandLauncherText_.size() + text.size() <= 512u) server->commandLauncherText_ += text;
      return true;
    }
    return true;
  }
}

void updateDrag(WaylandServer::Impl* server) {
  if (!server->dragSurface_) return;
  WaylandServer::Impl::Surface* surface = server->dragSurface_;
  restoreSnappedForDrag(server, surface);
  std::int32_t const topInset = topInsetForSurface(server, surface);
  int const maxX = std::max(0, server->logicalOutputWidth() - displayWidth(surface));
  int const maxY = std::max(topInset, server->logicalOutputHeight() - displayHeight(surface));
  surface->windowX = std::clamp(static_cast<int>(server->pointerX_ - server->dragOffsetX_), 0, maxX);
  surface->windowY = std::clamp(static_cast<int>(server->pointerY_ - server->dragOffsetY_), topInset, maxY);
}

void updateResize(WaylandServer::Impl* server) {
  WaylandServer::Impl::Surface* surface = server->resizeSurface_;
  if (!surface) return;
  surface->geometryAnimationActive = false;

  float const dx = server->pointerX_ - server->resizeStartX_;
  float const dy = server->pointerY_ - server->resizeStartY_;
  ResizeEdge const edges = resizeEdgesFromXdg(server->resizeEdges_);
  bool const left = hasResizeEdge(edges, ResizeEdge::Left);
  bool const top = hasResizeEdge(edges, ResizeEdge::Top);
  WindowGeometry const next = resizedWindowGeometry({
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

  if (left) surface->windowX = next.x;
  if (top) surface->windowY = next.y;
  if (next.width == server->resizeLastWidth_ && next.height == server->resizeLastHeight_) return;
  server->resizeLastWidth_ = next.width;
  server->resizeLastHeight_ = next.height;
  setConfiguredFrameSize(surface, next.width, next.height);
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
  sendToplevelConfigure(server, toplevelForSurface(server, surface), next.width, next.height);
}

void WaylandServer::Impl::handlePointerMotion(double dx, double dy, std::uint32_t timeMs) {
  if (auto* constraint = activePointerConstraint(this);
      constraint && constraint->kind == PointerConstraint::Kind::Lock) {
    sendRelativePointerMotion(this, dx, dy, timeMs);
    return;
  }
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
  if (commandLauncherVisible_) {
    sendPointerFocus(this, nullptr, timeMs);
    updateCompositorCursorForPointer(this);
    return;
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
  sendRelativePointerMotion(this, dx, dy, timeMs);
}

void WaylandServer::Impl::handlePointerPosition(double x, double y, std::uint32_t timeMs) {
  if (auto* constraint = activePointerConstraint(this);
      constraint && constraint->kind == PointerConstraint::Kind::Lock) {
    return;
  }
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
  if (commandLauncherVisible_) {
    sendPointerFocus(this, nullptr, timeMs);
    updateCompositorCursorForPointer(this);
    return;
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
}

void WaylandServer::Impl::handlePointerButton(std::uint32_t button, bool pressed, std::uint32_t timeMs) {
  if (commandLauncherVisible_) {
    (void)button;
    (void)pressed;
    (void)timeMs;
    updateCompositorCursorForPointer(this);
    return;
  }
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
        return;
      }
      if (Surface* minimizeTarget = minimizeButtonAt(this, pointerX_, pointerY_)) {
        raiseSurface(this, minimizeTarget);
        setKeyboardFocus(this, minimizeTarget);
        minimizePressSurface_ = minimizeTarget;
        return;
      }
      if (chromeControlTarget && resizeEdges != XDG_TOPLEVEL_RESIZE_EDGE_NONE) {
        raiseSurface(this, chromeControlTarget);
        setKeyboardFocus(this, chromeControlTarget);
        sendPointerFocus(this, nullptr, timeMs);
        chromeControlTarget->snapped = false;
        chromeControlTarget->maximized = false;
        chromeControlTarget->geometryAnimationActive = false;
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
          toggleMaximizedToplevel(this, chromeTarget);
          return;
        }
        dragSurface_ = chromeTarget;
        dragOffsetX_ = pointerX_ - static_cast<float>(chromeTarget->windowX);
        dragOffsetY_ = pointerY_ - static_cast<float>(chromeTarget->windowY);
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
      sendPointerFocus(this, surfaceAt(this, pointerX_, pointerY_), timeMs);
      updateCompositorCursorForPointer(this);
      return;
    } else if (resizeSurface_) {
      updateResize(this);
      traceResizeSurface("end-resize", resizeSurface_);
      Surface* resizedSurface = resizeSurface_;
      resizeSurface_ = nullptr;
      resizeEdges_ = XDG_TOPLEVEL_RESIZE_EDGE_NONE;
      sendToplevelStateConfigure(this, toplevelForSurface(this, resizedSurface));
      sendPointerFocus(this, surfaceAt(this, pointerX_, pointerY_), timeMs);
      updateCompositorCursorForPointer(this);
      return;
    } else if (dragSurface_) {
      if (auto preview = snapPreviewForDrag(this)) {
        if (preview->width >= logicalOutputWidth()) {
          maximizeToplevel(this, dragSurface_);
        } else {
          snapToplevel(this, dragSurface_, preview->x == 0);
        }
      }
      dragSurface_ = nullptr;
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
  if (commandLauncherVisible_) {
    (void)dx;
    (void)dy;
    (void)timeMs;
    return;
  }
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
  if (xkbState_) {
    xkb_state_update_key(xkbState_,
                         key + 8u,
                         pressed ? XKB_KEY_DOWN : XKB_KEY_UP);
  }
  bool const consumeModifier = updateShortcutModifier(this, key, pressed);
  if (consumeModifier) return;
  if (commandLauncherVisible_ && handleCommandLauncherKey(this, key, pressed)) return;
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

} // namespace flux::compositor
