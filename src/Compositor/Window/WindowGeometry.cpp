#include "Compositor/Window/WindowGeometry.hpp"

#include <algorithm>
#include <cmath>

namespace flux::compositor {

std::optional<WindowGeometry> snapPreviewGeometry(WindowGeometry const& window, OutputGeometry output) {
  if (window.width <= 0) return std::nullopt;
  bool const leftHalf = window.x <= kCompositorSnapEdgeThreshold;
  bool const rightHalf = window.x + window.width >= output.width - kCompositorSnapEdgeThreshold;
  if (!leftHalf && !rightHalf) return std::nullopt;
  return snappedWindowGeometry(output, leftHalf);
}

WindowGeometry snappedWindowGeometry(OutputGeometry output, bool leftHalf) {
  std::int32_t const width = std::max(kCompositorMinWindowWidth, output.width / 2);
  std::int32_t const height = std::max(kCompositorMinWindowHeight, output.height - kCompositorTitleBarHeight);
  return {
      .x = leftHalf ? 0 : std::max(0, output.width - width),
      .y = kCompositorTitleBarHeight,
      .width = width,
      .height = height,
  };
}

WindowGeometry restoredDragGeometry(RestoreDragGeometry const& geometry) {
  WindowGeometry restore = geometry.restoreWindow;
  restore.width = std::max(kCompositorMinWindowWidth, restore.width);
  restore.height = std::max(kCompositorMinWindowHeight, restore.height);
  std::int32_t const snappedWidth = std::max(1, geometry.snappedWindow.width);
  float const horizontalRatio =
      std::clamp((geometry.pointerX - static_cast<float>(geometry.snappedWindow.x)) /
                     static_cast<float>(snappedWidth),
                 0.f,
                 1.f);
  int const maxX = std::max(0, geometry.output.width - restore.width);
  int const maxY = std::max(kCompositorTitleBarHeight, geometry.output.height - restore.height);
  restore.x = std::clamp(static_cast<int>(std::lround(geometry.pointerX -
                                                       horizontalRatio * static_cast<float>(restore.width))),
                         0,
                         maxX);
  restore.y = std::clamp(static_cast<int>(std::lround(geometry.pointerY - geometry.dragOffsetY)),
                         kCompositorTitleBarHeight,
                         maxY);
  return restore;
}

WindowGeometry resizedWindowGeometry(ResizeDragGeometry const& geometry) {
  float const dx = geometry.pointerX - geometry.startPointerX;
  float const dy = geometry.pointerY - geometry.startPointerY;
  bool const left = hasResizeEdge(geometry.edges, ResizeEdge::Left);
  bool const right = hasResizeEdge(geometry.edges, ResizeEdge::Right);
  bool const top = hasResizeEdge(geometry.edges, ResizeEdge::Top);
  bool const bottom = hasResizeEdge(geometry.edges, ResizeEdge::Bottom);

  WindowGeometry next = geometry.startWindow;
  if (right) next.width = geometry.startWindow.width + static_cast<std::int32_t>(std::lround(dx));
  if (bottom) next.height = geometry.startWindow.height + static_cast<std::int32_t>(std::lround(dy));
  if (left) {
    next.width = geometry.startWindow.width - static_cast<std::int32_t>(std::lround(dx));
    next.x = geometry.startWindow.x + (geometry.startWindow.width - next.width);
  }
  if (top) {
    next.height = geometry.startWindow.height - static_cast<std::int32_t>(std::lround(dy));
    next.y = geometry.startWindow.y + (geometry.startWindow.height - next.height);
  }

  next.width = std::max(kCompositorMinWindowWidth, next.width);
  next.height = std::max(kCompositorMinWindowHeight, next.height);
  if (left) next.x = geometry.startWindow.x + (geometry.startWindow.width - next.width);
  if (top) next.y = geometry.startWindow.y + (geometry.startWindow.height - next.height);
  next.x = std::clamp(next.x, 0, std::max(0, geometry.output.width - kCompositorMinWindowWidth));
  next.y = std::clamp(next.y,
                      kCompositorTitleBarHeight,
                      std::max(kCompositorTitleBarHeight,
                               geometry.output.height - kCompositorMinWindowHeight));
  return next;
}

} // namespace flux::compositor
