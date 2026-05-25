#pragma once

#include "Compositor/Chrome/ChromeMetrics.hpp"
#include "Compositor/Wayland/WaylandServerImpl.hpp"
#include "Compositor/Wayland/WaylandTypes.hpp"
#include "Compositor/Window/WindowGeometry.hpp"

#include <cstdint>
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
inline bool markToplevelMinimized(WaylandServer::Impl::Surface* surface) {
  if (!surfaceIsXdgToplevel(surface) || surface->minimized) {
    return false;
  }
  surface->minimized = true;
  return true;
}
bool containsPoint(float x, float y, float left, float top, float right, float bottom);
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
void restoreSnappedForDrag(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface);
void toggleMaximizedToplevel(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface);
void updateDrag(WaylandServer::Impl* server);
void updateResize(WaylandServer::Impl* server);

bool updateShortcutModifier(WaylandServer::Impl* server, std::uint32_t key, bool pressed);
bool handleCompositorShortcut(WaylandServer::Impl* server, std::uint32_t key, bool pressed, std::uint32_t timeMs);

} // namespace flux::compositor::wm
