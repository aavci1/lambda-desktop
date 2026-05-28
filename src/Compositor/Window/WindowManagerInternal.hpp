#pragma once

#include "Compositor/Chrome/ChromeMetrics.hpp"
#include "Compositor/Wayland/WaylandServerImpl.hpp"
#include "Compositor/Wayland/WaylandTypes.hpp"
#include "Compositor/Window/WindowGeometry.hpp"
#include "Shell/ShellAppRegistry.hpp"

#include <cstdint>
#include <algorithm>
#include <optional>
#include <string>

struct wl_resource;

namespace flux::compositor::wm {

constexpr std::int32_t kTitleBarHeight = kCompositorTitleBarHeight;
constexpr std::int32_t kMinWindowWidth = kCompositorMinWindowWidth;
constexpr std::int32_t kMinWindowHeight = kCompositorMinWindowHeight;
constexpr std::uint32_t kGeometryAnimationMs = 180;
constexpr std::uint32_t kSnapDwellMs = 250;
constexpr std::uint32_t kSnapPreviewAnimationMs = 180;
constexpr std::uint32_t kInvalidModifierIndex = ~0u;

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

enum class ChromeButton {
  None,
  Close,
  Maximize,
  Minimize,
};

ChromeButton chromeButtonAt(ChromeHitContext const& context, float x, float y);

bool isManagedToplevel(WaylandServer::Impl::Surface const* surface);
struct ToplevelSizeHints {
  std::int32_t minWidth = 0;
  std::int32_t minHeight = 0;
  std::int32_t maxWidth = 0;
  std::int32_t maxHeight = 0;
};
inline bool xdgWindowGeometrySizeValid(std::int32_t width, std::int32_t height) {
  return width > 0 && height > 0;
}
inline bool toplevelSizeHintsValid(ToplevelSizeHints const& hints) {
  if (hints.minWidth < 0 || hints.minHeight < 0 || hints.maxWidth < 0 || hints.maxHeight < 0) {
    return false;
  }
  if (hints.maxWidth > 0 && hints.minWidth > 0 && hints.minWidth > hints.maxWidth) {
    return false;
  }
  if (hints.maxHeight > 0 && hints.minHeight > 0 && hints.minHeight > hints.maxHeight) {
    return false;
  }
  return true;
}
inline ToplevelSizeHints committedSizeHints(WaylandServer::Impl::XdgToplevel const* toplevel) {
  return toplevel ? ToplevelSizeHints{
                        .minWidth = toplevel->minWidth,
                        .minHeight = toplevel->minHeight,
                        .maxWidth = toplevel->maxWidth,
                        .maxHeight = toplevel->maxHeight,
                    }
                  : ToplevelSizeHints{};
}
inline ToplevelSizeHints pendingSizeHints(WaylandServer::Impl::XdgToplevel const* toplevel) {
  if (!toplevel) return {};
  return {
      .minWidth = toplevel->pendingMinSizeSet ? toplevel->pendingMinWidth : toplevel->minWidth,
      .minHeight = toplevel->pendingMinSizeSet ? toplevel->pendingMinHeight : toplevel->minHeight,
      .maxWidth = toplevel->pendingMaxSizeSet ? toplevel->pendingMaxWidth : toplevel->maxWidth,
      .maxHeight = toplevel->pendingMaxSizeSet ? toplevel->pendingMaxHeight : toplevel->maxHeight,
  };
}
inline WindowGeometry clampToplevelGeometryToSizeHints(WindowGeometry geometry,
                                                      ToplevelSizeHints const& hints,
                                                      ResizeEdge anchoredEdges = ResizeEdge::None) {
  if (!toplevelSizeHintsValid(hints)) return geometry;
  std::int32_t const originalRight = geometry.x + geometry.width;
  std::int32_t const originalBottom = geometry.y + geometry.height;
  std::int32_t const minWidth = hints.minWidth > 0 ? hints.minWidth : kMinWindowWidth;
  std::int32_t const minHeight = hints.minHeight > 0 ? hints.minHeight : kMinWindowHeight;
  geometry.width = std::max(minWidth, geometry.width);
  geometry.height = std::max(minHeight, geometry.height);
  if (hints.maxWidth > 0) geometry.width = std::min(hints.maxWidth, geometry.width);
  if (hints.maxHeight > 0) geometry.height = std::min(hints.maxHeight, geometry.height);
  if (hasResizeEdge(anchoredEdges, ResizeEdge::Left)) geometry.x = originalRight - geometry.width;
  if (hasResizeEdge(anchoredEdges, ResizeEdge::Top)) geometry.y = originalBottom - geometry.height;
  return geometry;
}
inline bool markToplevelMinimized(WaylandServer::Impl::Surface* surface) {
  if (!surfaceIsXdgToplevel(surface) || surface->minimized) {
    return false;
  }
  surface->minimized = true;
  return true;
}
inline bool restoreSurfaceForShellFocus(WaylandServer::Impl::Surface* surface) {
  if (!surfaceIsXdgToplevel(surface)) return false;
  surface->minimized = false;
  return true;
}
inline bool surfaceEligibleForPresentation(WaylandServer::Impl::Surface const* surface) {
  if (!surface) return false;
  if (surface->minimized) return false;
  if (surface->xdgPopup && surface->xdgPopup->dismissed) return false;
  return true;
}
inline bool surfaceFocusableInOrder(WaylandServer::Impl::Surface const* surface) {
  return surfaceIsXdgToplevel(surface) && !surface->minimized;
}
inline WaylandServer::Impl::Surface* mostRecentToplevelFromOrders(
    std::vector<WaylandServer::Impl::Surface*> const& focusOrder,
    std::vector<std::unique_ptr<WaylandServer::Impl::Surface>> const& surfaces) {
  for (auto it = focusOrder.rbegin(); it != focusOrder.rend(); ++it) {
    if (surfaceFocusableInOrder(*it)) return *it;
  }
  for (auto it = surfaces.rbegin(); it != surfaces.rend(); ++it) {
    if (surfaceFocusableInOrder(it->get())) return it->get();
  }
  return nullptr;
}
inline WaylandServer::Impl::Surface* previousFocusedToplevelFromOrders(
    std::vector<WaylandServer::Impl::Surface*> const& focusOrder,
    std::vector<std::unique_ptr<WaylandServer::Impl::Surface>> const& surfaces,
    WaylandServer::Impl::Surface* current) {
  auto currentIt = std::find(focusOrder.begin(), focusOrder.end(), current);
  if (currentIt != focusOrder.end()) {
    for (auto it = std::make_reverse_iterator(currentIt); it != focusOrder.rend(); ++it) {
      if (surfaceFocusableInOrder(*it)) return *it;
    }
  }
  for (auto it = focusOrder.rbegin(); it != focusOrder.rend(); ++it) {
    if (*it != current && surfaceFocusableInOrder(*it)) return *it;
  }
  for (auto it = surfaces.rbegin(); it != surfaces.rend(); ++it) {
    if (it->get() != current && surfaceFocusableInOrder(it->get())) return it->get();
  }
  return nullptr;
}
inline bool shellAppIdMatches(std::string const& requested, std::string const& actual) {
  return lambda_shell::shellAppIdMatches(requested, actual) ||
         lambda_shell::shellAppIdMatches(actual, requested);
}
bool containsPoint(float x, float y, float left, float top, float right, float bottom);
inline bool inputRegionContains(WaylandServer::Impl::Surface const* surface, float localX, float localY) {
  if (!surface) return false;
  if (surface->inputRegionInfinite) return true;
  return std::any_of(surface->inputRegionRects.begin(), surface->inputRegionRects.end(),
                     [&](CommittedSurfaceSnapshot::RegionRect const& rect) {
                       return localX >= static_cast<float>(rect.x) &&
                              localY >= static_cast<float>(rect.y) &&
                              localX < static_cast<float>(rect.x + rect.width) &&
                              localY < static_cast<float>(rect.y + rect.height);
                     });
}
inline std::int32_t xdgWindowGeometryOffsetX(WaylandServer::Impl::Surface const* surface) {
  return surface && surface->xdgWindowGeometrySet ? surface->xdgWindowGeometryX : 0;
}
inline std::int32_t xdgWindowGeometryOffsetY(WaylandServer::Impl::Surface const* surface) {
  return surface && surface->xdgWindowGeometrySet ? surface->xdgWindowGeometryY : 0;
}
inline float surfaceBufferOriginX(WaylandServer::Impl::Surface const* surface) {
  return surface ? static_cast<float>(surface->windowX - xdgWindowGeometryOffsetX(surface)) : 0.f;
}
inline float surfaceBufferOriginY(WaylandServer::Impl::Surface const* surface) {
  return surface ? static_cast<float>(surface->windowY - xdgWindowGeometryOffsetY(surface)) : 0.f;
}
inline float surfaceLocalX(WaylandServer::Impl::Surface const* surface, float globalX) {
  return globalX - surfaceBufferOriginX(surface);
}
inline float surfaceLocalY(WaylandServer::Impl::Surface const* surface, float globalY) {
  return globalY - surfaceBufferOriginY(surface);
}
WindowGeometry windowGeometryFor(WaylandServer::Impl::Surface const* surface);
OutputGeometry outputGeometryFor(WaylandServer::Impl const* server);
OutputGeometry snapOutputGeometryFor(WaylandServer::Impl const* server);
std::int32_t titleBarHeightFor(WaylandServer::Impl const* server);
std::int32_t resizeGripSizeFor(WaylandServer::Impl const* server);
std::int32_t externalTitleBarHeight(WaylandServer::Impl* server, WaylandServer::Impl::Surface const* surface);
bool surfaceUsesCutouts(WaylandServer::Impl* server, WaylandServer::Impl::Surface const* surface);
std::int32_t topInsetForSurface(WaylandServer::Impl* server, WaylandServer::Impl::Surface const* surface);
ResizeEdge resizeEdgesFromXdg(std::uint32_t edges);
CursorShape cursorShapeForResizeEdges(std::uint32_t edges);
std::uint32_t monotonicMilliseconds();
float easeOutCubic(float value);
std::int32_t lerpInt(std::int32_t from, std::int32_t to, float progress);
void popupTrace(char const* fmt, ...);

WaylandServer::Impl::Surface* aboveWindowLayerAt(WaylandServer::Impl* server, float x, float y);
WaylandServer::Impl::XdgPopup* popupForSurface(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface);
bool popupIsDescendantOf(WaylandServer::Impl* server,
                         WaylandServer::Impl::XdgPopup* popup,
                         WaylandServer::Impl::XdgPopup* ancestor);
std::optional<WindowGeometry> popupScreenBounds(WaylandServer::Impl* server, WaylandServer::Impl::XdgPopup* popup);
WaylandServer::Impl::Surface* popupAt(WaylandServer::Impl* server, float x, float y);
std::optional<ChromeHitContext> topChromeHitContext(WaylandServer::Impl* server, float x, float y);
bool controlsRegionContains(ChromeHitContext const& context, float x, float y);
std::uint32_t resizeEdgesForContext(ChromeHitContext const& context, float x, float y);

WaylandServer::Impl::Surface* subsurfaceAt(WaylandServer::Impl* server,
                                           WaylandServer::Impl::Surface* parent,
                                           float parentX,
                                           float parentY,
                                           float x,
                                           float y);
WaylandServer::Impl::Surface* titlebarAt(WaylandServer::Impl* server, float x, float y);
WaylandServer::Impl::Surface* closeButtonAt(WaylandServer::Impl* server, float x, float y);
WaylandServer::Impl::Surface* minimizeButtonAt(WaylandServer::Impl* server, float x, float y);
WaylandServer::Impl::Surface* maximizeButtonAt(WaylandServer::Impl* server, float x, float y);
WaylandServer::Impl::Surface* resizeGripAt(WaylandServer::Impl* server, float x, float y, std::uint32_t& edges);
WaylandServer::Impl::Surface* resizeOrCloseChromeAt(WaylandServer::Impl* server,
                                                    float x,
                                                    float y,
                                                    bool& closeButton,
                                                    std::uint32_t& resizeEdges);
void setCompositorCursorOverride(WaylandServer::Impl* server, CursorShape shape);
void clearCompositorCursorOverride(WaylandServer::Impl* server);
void updateCompositorCursorForPointer(WaylandServer::Impl* server);
void sendPointerFocus(WaylandServer::Impl* server, WaylandServer::Impl::Surface* next, std::uint32_t timeMs);
void sendRelativePointerMotion(WaylandServer::Impl* server, double dx, double dy, std::uint32_t timeMs);
WaylandServer::Impl::XdgPopup* topmostPopup(WaylandServer::Impl* server);
bool surfaceBelongsToPopup(WaylandServer::Impl::Surface* surface, WaylandServer::Impl::XdgPopup* popup);
bool dismissPopup(WaylandServer::Impl::XdgPopup* popup);
bool dismissTopPopup(WaylandServer::Impl* server);
bool dismissTopPopupOutside(WaylandServer::Impl* server, WaylandServer::Impl::Surface* target);

bool resourceBelongsToSurfaceClient(wl_resource* resource, WaylandServer::Impl::Surface const* surface);

void raiseSurface(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface);
void lowerSurface(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface);
WaylandServer::Impl::Surface* surfaceById(WaylandServer::Impl* server, std::uint64_t surfaceId);
WaylandServer::Impl::Surface* mostRecentToplevel(WaylandServer::Impl* server);
void noteFocusedToplevel(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface);
void setKeyboardFocus(WaylandServer::Impl* server, WaylandServer::Impl::Surface* next);
std::uint32_t keyboardModifierMask(WaylandServer::Impl* server);
std::uint32_t keyboardLatchedModifierMask(WaylandServer::Impl* server);
std::uint32_t keyboardLockedModifierMask(WaylandServer::Impl* server);
std::uint32_t keyboardLayoutIndex(WaylandServer::Impl* server);
void sendKeyboardModifiers(WaylandServer::Impl* server);
WaylandServer::Impl::Surface* previousFocusedToplevel(WaylandServer::Impl* server,
                                                      WaylandServer::Impl::Surface* current);
WaylandServer::Impl::XdgToplevel* focusedToplevel(WaylandServer::Impl* server);
bool closeFocusedToplevel(WaylandServer::Impl* server);
bool cycleFocus(WaylandServer::Impl* server, std::uint32_t timeMs);

void clearSnapPreview(WaylandServer::Impl* server);
void resetDragSnapState(WaylandServer::Impl* server);
WindowGeometry frameGeometryFor(WaylandServer::Impl* server, WaylandServer::Impl::Surface const* surface);
WindowGeometry snapPreviewCurrentWindow(WaylandServer::Impl* server, std::uint32_t nowMs);
std::optional<SnapTarget> activeDragSnapTarget(WaylandServer::Impl* server,
                                               WaylandServer::Impl::Surface const* surface,
                                               std::uint32_t nowMs);
void startGeometryAnimation(WaylandServer::Impl* server,
                            WaylandServer::Impl::Surface* surface,
                            std::int32_t targetX,
                            std::int32_t targetY,
                            std::int32_t targetWidth,
                            std::int32_t targetHeight);
void snapToplevel(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface, SnapTarget target);
void snapToplevel(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface, bool leftHalf);
void snapFocusedToplevel(WaylandServer::Impl* server, bool leftHalf);
void maximizeFocusedToplevel(WaylandServer::Impl* server);
bool restoreFocusedToplevel(WaylandServer::Impl* server);
void fullscreenToplevel(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface);
void restoreSnappedForDrag(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface);
void toggleMaximizedToplevel(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface);
void updateDrag(WaylandServer::Impl* server);
void updateResize(WaylandServer::Impl* server);

bool updateShortcutModifier(WaylandServer::Impl* server, std::uint32_t key, bool pressed);
bool handleCompositorShortcut(WaylandServer::Impl* server, std::uint32_t key, bool pressed, std::uint32_t timeMs);
bool handleScreenshotSelectionPointerMotion(WaylandServer::Impl* server,
                                            double dx,
                                            double dy,
                                            std::uint32_t timeMs);
bool handleScreenshotSelectionPointerPosition(WaylandServer::Impl* server,
                                              double x,
                                              double y,
                                              std::uint32_t timeMs);
bool handleScreenshotSelectionPointerButton(WaylandServer::Impl* server,
                                            std::uint32_t button,
                                            bool pressed,
                                            std::uint32_t timeMs);
bool handleScreenshotSelectionKey(WaylandServer::Impl* server,
                                  std::uint32_t key,
                                  bool pressed,
                                  std::uint32_t timeMs);

} // namespace flux::compositor::wm
