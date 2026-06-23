#include "Compositor/Window/WindowManagerInternal.hpp"

#include "Compositor/Wayland/WaylandServerImpl.hpp"

#include <linux/input-event-codes.h>

#include <algorithm>
#include <cmath>
#include <optional>

namespace lambdaui::compositor {
namespace {

constexpr std::int32_t kMinimumRegionSize = 2;

void markScreenshotSelectionDirty(WaylandServer::Impl* server) {
  if (!server) return;
  ++server->contentSerial_;
}

void updateSelectionPointerFromDelta(WaylandServer::Impl* server, double dx, double dy) {
  float const scale = std::max(0.5f, server->preferredScale_);
  server->pointerX_ = std::clamp(server->pointerX_ + static_cast<float>(dx) / scale,
                                 0.f,
                                 std::max(0.f, static_cast<float>(server->logicalOutputWidth() - 1)));
  server->pointerY_ = std::clamp(server->pointerY_ + static_cast<float>(dy) / scale,
                                 0.f,
                                 std::max(0.f, static_cast<float>(server->logicalOutputHeight() - 1)));
}

void updateSelectionPointerFromPosition(WaylandServer::Impl* server, double x, double y) {
  float const scale = std::max(0.5f, server->preferredScale_);
  server->pointerX_ = std::clamp(static_cast<float>(x) / scale,
                                 0.f,
                                 std::max(0.f, static_cast<float>(server->logicalOutputWidth() - 1)));
  server->pointerY_ = std::clamp(static_cast<float>(y) / scale,
                                 0.f,
                                 std::max(0.f, static_cast<float>(server->logicalOutputHeight() - 1)));
}

std::optional<ScreenshotRegion> selectedRegion(WaylandServer::Impl const* server) {
  if (!server || !server->screenshotSelection_.active) return std::nullopt;
  auto const& selection = server->screenshotSelection_;
  if (!selection.dragging) return std::nullopt;
  return screenshotSelectionRegion(selection.startX,
                                   selection.startY,
                                   selection.currentX,
                                   selection.currentY,
                                   server->logicalOutputWidth(),
                                   server->logicalOutputHeight(),
                                   kMinimumRegionSize);
}

void restorePointerRouting(WaylandServer::Impl* server, std::uint32_t timeMs) {
  wm::sendPointerFocus(server, surfaceAt(server, server->pointerX_, server->pointerY_), timeMs);
  wm::updateCompositorCursorForPointer(server);
}

void cancelScreenshotSelection(WaylandServer::Impl* server, std::uint32_t timeMs) {
  if (!server || !server->screenshotSelection_.active) return;
  server->screenshotSelection_ = {};
  markScreenshotSelectionDirty(server);
  restorePointerRouting(server, timeMs);
}

void finishScreenshotSelection(WaylandServer::Impl* server, std::uint32_t timeMs) {
  if (!server || !server->screenshotSelection_.active) return;
  std::optional<ScreenshotRegion> region = selectedRegion(server);
  server->screenshotSelection_ = {};
  if (region) {
    server->screenshotRequest_ = makeScreenshotRequest(ScreenshotMode::Region, *region);
  }
  markScreenshotSelectionDirty(server);
  restorePointerRouting(server, timeMs);
}

void beginScreenshotSelection(WaylandServer::Impl* server, std::uint32_t timeMs) {
  if (!server) return;
  server->dragSurface_ = nullptr;
  server->resizeSurface_ = nullptr;
  server->closePressSurface_ = nullptr;
  server->maximizePressSurface_ = nullptr;
  server->minimizePressSurface_ = nullptr;
  wm::clearSnapPreview(server);
  wm::sendPointerFocus(server, nullptr, timeMs);
  server->screenshotSelection_ = WaylandServer::Impl::ScreenshotSelectionState{
      .active = true,
      .dragging = false,
      .startX = server->pointerX_,
      .startY = server->pointerY_,
      .currentX = server->pointerX_,
      .currentY = server->pointerY_,
  };
  wm::setCompositorCursorOverride(server, CursorShape::Crosshair);
  markScreenshotSelectionDirty(server);
}

} // namespace

void WaylandServer::Impl::requestScreenshot(ScreenshotMode mode, std::uint32_t timeMs) {
  if (mode == ScreenshotMode::Region) {
    beginScreenshotSelection(this, timeMs);
    return;
  }
  if (screenshotSelection_.active) {
    cancelScreenshotSelection(this, timeMs);
  }
  screenshotRequest_ = makeScreenshotRequest(mode);
  ++contentSerial_;
}

std::optional<ScreenshotRequest> WaylandServer::Impl::consumeScreenshotRequest() {
  std::optional<ScreenshotRequest> request = screenshotRequest_;
  screenshotRequest_.reset();
  return request;
}

std::optional<ScreenshotSelectionOverlay> WaylandServer::Impl::screenshotSelectionOverlay() const {
  if (!screenshotSelection_.active) return std::nullopt;
  return ScreenshotSelectionOverlay{
      .dragging = screenshotSelection_.dragging,
      .startX = screenshotSelection_.startX,
      .startY = screenshotSelection_.startY,
      .currentX = screenshotSelection_.currentX,
      .currentY = screenshotSelection_.currentY,
      .region = selectedRegion(this),
  };
}

} // namespace lambdaui::compositor

namespace lambdaui::compositor::wm {

bool handleScreenshotSelectionPointerMotion(WaylandServer::Impl* server,
                                            double dx,
                                            double dy,
                                            std::uint32_t timeMs) {
  (void)timeMs;
  if (!server || !server->screenshotSelection_.active) return false;
  updateSelectionPointerFromDelta(server, dx, dy);
  server->screenshotSelection_.currentX = server->pointerX_;
  server->screenshotSelection_.currentY = server->pointerY_;
  setCompositorCursorOverride(server, CursorShape::Crosshair);
  markScreenshotSelectionDirty(server);
  return true;
}

bool handleScreenshotSelectionPointerPosition(WaylandServer::Impl* server,
                                              double x,
                                              double y,
                                              std::uint32_t timeMs) {
  (void)timeMs;
  if (!server || !server->screenshotSelection_.active) return false;
  updateSelectionPointerFromPosition(server, x, y);
  server->screenshotSelection_.currentX = server->pointerX_;
  server->screenshotSelection_.currentY = server->pointerY_;
  setCompositorCursorOverride(server, CursorShape::Crosshair);
  markScreenshotSelectionDirty(server);
  return true;
}

bool handleScreenshotSelectionPointerButton(WaylandServer::Impl* server,
                                            std::uint32_t button,
                                            bool pressed,
                                            std::uint32_t timeMs) {
  if (!server || !server->screenshotSelection_.active) return false;
  if (button == BTN_LEFT) {
    if (pressed) {
      server->screenshotSelection_.dragging = true;
      server->screenshotSelection_.startX = server->pointerX_;
      server->screenshotSelection_.startY = server->pointerY_;
      server->screenshotSelection_.currentX = server->pointerX_;
      server->screenshotSelection_.currentY = server->pointerY_;
      markScreenshotSelectionDirty(server);
    } else {
      if (server->screenshotSelection_.dragging) {
        server->screenshotSelection_.currentX = server->pointerX_;
        server->screenshotSelection_.currentY = server->pointerY_;
        finishScreenshotSelection(server, timeMs);
      }
    }
    return true;
  }
  if (pressed) {
    cancelScreenshotSelection(server, timeMs);
  }
  return true;
}

bool handleScreenshotSelectionKey(WaylandServer::Impl* server,
                                  std::uint32_t key,
                                  bool pressed,
                                  std::uint32_t timeMs) {
  if (!server || !server->screenshotSelection_.active) return false;
  if (pressed && key == KEY_ESC) {
    cancelScreenshotSelection(server, timeMs);
    return true;
  }
  if (pressed && (key == KEY_ENTER || key == KEY_KPENTER || key == KEY_SPACE)) {
    finishScreenshotSelection(server, timeMs);
    return true;
  }
  return true;
}

} // namespace lambdaui::compositor::wm
