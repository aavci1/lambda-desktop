#include "Compositor/Window/WindowGeometry.hpp"

#include <algorithm>
#include <cmath>

namespace flux::compositor {

std::optional<WindowGeometry> snapPreviewGeometry(WindowGeometry const& window,
                                                 OutputGeometry output,
                                                 std::int32_t topInset) {
  if (window.width <= 0) return std::nullopt;
  topInset = std::max(0, topInset);
  bool const topEdge = window.y <= topInset + kCompositorSnapEdgeThreshold;
  if (topEdge) return maximizedWindowGeometry(output, topInset);
  bool const leftHalf = window.x <= kCompositorSnapEdgeThreshold;
  bool const rightHalf = window.x + window.width >= output.width - kCompositorSnapEdgeThreshold;
  if (!leftHalf && !rightHalf) return std::nullopt;
  return snappedWindowGeometry(output, leftHalf, topInset);
}

WindowGeometry snappedWindowGeometry(OutputGeometry output, bool leftHalf, std::int32_t topInset) {
  topInset = std::max(0, topInset);
  std::int32_t const width = std::max(kCompositorMinWindowWidth, output.width / 2);
  std::int32_t const height = std::max(kCompositorMinWindowHeight, output.height - topInset);
  return {
      .x = leftHalf ? 0 : std::max(0, output.width - width),
      .y = topInset,
      .width = width,
      .height = height,
  };
}

WindowGeometry maximizedWindowGeometry(OutputGeometry output, std::int32_t topInset) {
  topInset = std::max(0, topInset);
  return {
      .x = 0,
      .y = topInset,
      .width = std::max(kCompositorMinWindowWidth, output.width),
      .height = std::max(kCompositorMinWindowHeight, output.height - topInset),
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
  std::int32_t const topInset = std::max(0, geometry.topInset);
  int const maxY = std::max(topInset, geometry.output.height - restore.height);
  restore.x = std::clamp(static_cast<int>(std::lround(geometry.pointerX -
                                                       horizontalRatio * static_cast<float>(restore.width))),
                         0,
                         maxX);
  restore.y = std::clamp(static_cast<int>(std::lround(geometry.pointerY - geometry.dragOffsetY)),
                         topInset,
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
  std::int32_t const topInset = std::max(0, geometry.topInset);
  next.y = std::clamp(next.y,
                      topInset,
                      std::max(topInset,
                               geometry.output.height - kCompositorMinWindowHeight));
  return next;
}

namespace {

std::int32_t popupAnchorX(PopupPositionerGeometry const& geometry) {
  std::int32_t x = geometry.anchorRectX;
  if (geometry.anchor == PopupAnchor::Top ||
      geometry.anchor == PopupAnchor::Bottom ||
      geometry.anchor == PopupAnchor::None) {
    x += geometry.anchorRectWidth / 2;
  } else if (geometry.anchor == PopupAnchor::TopRight ||
             geometry.anchor == PopupAnchor::Right ||
             geometry.anchor == PopupAnchor::BottomRight) {
    x += geometry.anchorRectWidth;
  }
  return x + geometry.offsetX;
}

std::int32_t popupAnchorY(PopupPositionerGeometry const& geometry) {
  std::int32_t y = geometry.anchorRectY;
  if (geometry.anchor == PopupAnchor::Left ||
      geometry.anchor == PopupAnchor::Right ||
      geometry.anchor == PopupAnchor::None) {
    y += geometry.anchorRectHeight / 2;
  } else if (geometry.anchor == PopupAnchor::BottomLeft ||
             geometry.anchor == PopupAnchor::Bottom ||
             geometry.anchor == PopupAnchor::BottomRight) {
    y += geometry.anchorRectHeight;
  }
  return y + geometry.offsetY;
}

} // namespace

PopupGeometry positionedPopupGeometry(PopupPositionerGeometry const& geometry) {
  std::int32_t const width = std::max(1, geometry.width);
  std::int32_t const height = std::max(1, geometry.height);
  std::int32_t const parentX = geometry.parent ? geometry.parent->x : 0;
  std::int32_t const parentY = geometry.parent ? geometry.parent->y : 0;
  std::int32_t x = parentX + popupAnchorX(geometry);
  std::int32_t y = parentY + popupAnchorY(geometry);

  if (geometry.gravity == PopupGravity::Left ||
      geometry.gravity == PopupGravity::TopLeft ||
      geometry.gravity == PopupGravity::BottomLeft) {
    x -= width;
  } else if (geometry.gravity == PopupGravity::None ||
             geometry.gravity == PopupGravity::Top ||
             geometry.gravity == PopupGravity::Bottom) {
    x -= width / 2;
  }
  if (geometry.gravity == PopupGravity::Top ||
      geometry.gravity == PopupGravity::TopLeft ||
      geometry.gravity == PopupGravity::TopRight) {
    y -= height;
  } else if (geometry.gravity == PopupGravity::None ||
             geometry.gravity == PopupGravity::Left ||
             geometry.gravity == PopupGravity::Right) {
    y -= height / 2;
  }

  x = std::clamp(x, 0, std::max(0, geometry.output.width - width));
  y = std::clamp(y, 0, std::max(0, geometry.output.height - height));

  return PopupGeometry{
      .window = {.x = x, .y = y, .width = width, .height = height},
      .configureX = geometry.parent ? x - parentX : x,
      .configureY = geometry.parent ? y - parentY : y,
      .configureWidth = width,
      .configureHeight = height,
  };
}

std::optional<WindowGeometry> popupScreenGeometry(std::span<WindowGeometry const> parentToChildChain) {
  if (parentToChildChain.empty()) return std::nullopt;

  WindowGeometry result = parentToChildChain.front();
  if (result.width <= 0 || result.height <= 0) return std::nullopt;
  for (std::size_t i = 1; i < parentToChildChain.size(); ++i) {
    WindowGeometry const& popup = parentToChildChain[i];
    if (popup.width <= 0 || popup.height <= 0) return std::nullopt;
    result.x += popup.x;
    result.y += popup.y;
    result.width = popup.width;
    result.height = popup.height;
  }
  return result;
}

} // namespace flux::compositor
