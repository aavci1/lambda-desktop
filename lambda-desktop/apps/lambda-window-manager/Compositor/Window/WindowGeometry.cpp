#include "Compositor/Window/WindowGeometry.hpp"

#include <algorithm>
#include <cmath>

namespace lambdaui::compositor {

std::optional<WindowGeometry> snapPreviewGeometry(WindowGeometry const& window,
                                                 OutputGeometry output,
                                                 std::int32_t topInset,
                                                 std::int32_t frameOutset) {
  auto const target = snapTargetForWindow(window, output, topInset, frameOutset);
  if (!target) return std::nullopt;
  return snapTargetGeometry(output, *target, topInset, frameOutset);
}

std::optional<SnapTarget> snapTargetForWindow(WindowGeometry const& window,
                                             OutputGeometry output,
                                             std::int32_t topInset,
                                             std::int32_t frameOutset) {
  if (window.width <= 0 || window.height <= 0 || output.width <= 0 || output.height <= 0) {
    return std::nullopt;
  }
  topInset = std::max(0, topInset);
  frameOutset = std::max(0, frameOutset);
  WindowGeometry const frame = windowFrameGeometryForContent(window, topInset, frameOutset);
  bool const left = frame.x <= output.x + kCompositorSnapEdgeThreshold;
  bool const right = frame.x + frame.width >= output.x + output.width - kCompositorSnapEdgeThreshold;
  bool const top = window.y <= output.y + topInset + kCompositorSnapEdgeThreshold;
  bool const bottom = frame.y + frame.height >= output.y + output.height - kCompositorSnapEdgeThreshold;
  if (left || right) {
    bool const useLeft = left && (!right || frame.x + frame.width / 2 <= output.x + output.width / 2);
    if (top) return useLeft ? SnapTarget::TopLeftQuarter : SnapTarget::TopRightQuarter;
    if (bottom) return useLeft ? SnapTarget::BottomLeftQuarter : SnapTarget::BottomRightQuarter;
    return useLeft ? SnapTarget::LeftHalf : SnapTarget::RightHalf;
  }
  if (top) return SnapTarget::Maximized;
  return std::nullopt;
}

namespace {

WindowGeometry insetTargetForFrameOutset(WindowGeometry geometry, std::int32_t frameOutset) {
  frameOutset = std::max(0, frameOutset);
  if (frameOutset <= 0) return geometry;
  geometry.x += frameOutset;
  geometry.width = std::max(kCompositorMinWindowWidth, geometry.width - frameOutset * 2);
  geometry.height = std::max(kCompositorMinWindowHeight, geometry.height - frameOutset);
  return geometry;
}

} // namespace

WindowGeometry snapTargetGeometry(OutputGeometry output,
                                  SnapTarget target,
                                  std::int32_t topInset,
                                  std::int32_t frameOutset) {
  topInset = std::max(0, topInset);
  std::int32_t const halfWidth = std::max(kCompositorMinWindowWidth, output.width / 2);
  std::int32_t const availableHeight = std::max(0, output.height - topInset);
  std::int32_t const topHalfHeight = std::max(kCompositorMinWindowHeight, availableHeight / 2);
  std::int32_t const bottomHalfHeight =
      std::max(kCompositorMinWindowHeight, availableHeight - availableHeight / 2);
  switch (target) {
  case SnapTarget::LeftHalf:
  case SnapTarget::RightHalf:
    return snappedWindowGeometry(output, target == SnapTarget::LeftHalf, topInset, frameOutset);
  case SnapTarget::TopLeftQuarter:
    return insetTargetForFrameOutset({.x = output.x,
                                      .y = output.y + topInset,
                                      .width = halfWidth,
                                      .height = topHalfHeight},
                                     frameOutset);
  case SnapTarget::TopRightQuarter:
    return insetTargetForFrameOutset({.x = output.x + std::max(0, output.width - halfWidth),
                                      .y = output.y + topInset,
                                      .width = halfWidth,
                                      .height = topHalfHeight},
                                     frameOutset);
  case SnapTarget::BottomLeftQuarter:
    return insetTargetForFrameOutset({.x = output.x,
                                      .y = output.y + std::max(topInset, output.height - bottomHalfHeight),
                                      .width = halfWidth,
                                      .height = bottomHalfHeight},
                                     frameOutset);
  case SnapTarget::BottomRightQuarter:
    return insetTargetForFrameOutset({.x = output.x + std::max(0, output.width - halfWidth),
                                      .y = output.y + std::max(topInset, output.height - bottomHalfHeight),
                                      .width = halfWidth,
                                      .height = bottomHalfHeight},
                                     frameOutset);
  case SnapTarget::Maximized: return maximizedWindowGeometry(output, topInset, frameOutset);
  }
  return maximizedWindowGeometry(output, topInset, frameOutset);
}

WindowGeometry snappedWindowGeometry(OutputGeometry output,
                                     bool leftHalf,
                                     std::int32_t topInset,
                                     std::int32_t frameOutset) {
  topInset = std::max(0, topInset);
  std::int32_t const width = std::max(kCompositorMinWindowWidth, output.width / 2);
  std::int32_t const height = std::max(kCompositorMinWindowHeight, output.height - topInset);
  return insetTargetForFrameOutset(
      {
          .x = leftHalf ? output.x : output.x + std::max(0, output.width - width),
          .y = output.y + topInset,
          .width = width,
          .height = height,
      },
      frameOutset);
}

WindowGeometry maximizedWindowGeometry(OutputGeometry output,
                                       std::int32_t topInset,
                                       std::int32_t frameOutset) {
  topInset = std::max(0, topInset);
  return insetTargetForFrameOutset(
      {
          .x = output.x,
          .y = output.y + topInset,
          .width = std::max(kCompositorMinWindowWidth, output.width),
          .height = std::max(kCompositorMinWindowHeight, output.height - topInset),
      },
      frameOutset);
}

WindowGeometry centerSnappedWindowGeometry(WindowGeometry window,
                                           OutputGeometry output,
                                           std::int32_t topInset,
                                           std::int32_t threshold,
                                           std::int32_t frameOutset) {
  if (window.width <= 0 || window.height <= 0 || output.width <= 0 || output.height <= 0) return window;
  topInset = std::max(0, topInset);
  threshold = std::max(0, threshold);
  frameOutset = std::max(0, frameOutset);

  std::int32_t const minRelativeX = std::min(frameOutset, std::max(0, output.width - window.width));
  std::int32_t const maxRelativeX =
      std::max(minRelativeX, output.width - window.width - frameOutset);
  std::int32_t const minX = output.x + minRelativeX;
  std::int32_t const maxX = output.x + maxRelativeX;
  std::int32_t const minY = output.y + topInset;
  std::int32_t const maxY =
      output.y + std::max(topInset, output.height - window.height - frameOutset);
  window.x = std::clamp(window.x, minX, maxX);
  window.y = std::clamp(window.y, minY, maxY);

  std::int32_t const centeredX =
      output.x + std::clamp((output.width - window.width) / 2, minRelativeX, maxRelativeX);
  std::int32_t const availableHeight = std::max(0, output.height - topInset);
  std::int32_t const centeredY =
      output.y + std::clamp(topInset + (availableHeight - window.height) / 2,
                            topInset,
                            maxY - output.y);
  if (std::abs(window.x - centeredX) <= threshold) window.x = centeredX;
  if (std::abs(window.y - centeredY) <= threshold) window.y = centeredY;
  return window;
}

WindowGeometry windowFrameGeometryForContent(WindowGeometry const& content,
                                             std::int32_t topInset,
                                             std::int32_t frameOutset) {
  topInset = std::max(0, topInset);
  frameOutset = std::max(0, frameOutset);
  return {
      .x = content.x - frameOutset,
      .y = content.y - topInset,
      .width = content.width + frameOutset * 2,
      .height = content.height + topInset + frameOutset,
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
  std::int32_t const frameOutset = std::max(0, geometry.frameOutset);
  int const minRelativeX = std::min(frameOutset, std::max(0, geometry.output.width - restore.width));
  int const maxRelativeX =
      std::max(minRelativeX, geometry.output.width - restore.width - frameOutset);
  int const minX = geometry.output.x + minRelativeX;
  int const maxX = geometry.output.x + maxRelativeX;
  std::int32_t const topInset = std::max(0, geometry.topInset);
  int const minY = geometry.output.y + topInset;
  int const maxY = geometry.output.y +
                   std::max(topInset, geometry.output.height - restore.height - frameOutset);
  restore.x = std::clamp(static_cast<int>(std::lround(geometry.pointerX -
                                                       horizontalRatio * static_cast<float>(restore.width))),
                         minX,
                         maxX);
  restore.y = std::clamp(static_cast<int>(std::lround(geometry.pointerY - geometry.dragOffsetY)),
                         minY,
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
  next.x = std::clamp(next.x,
                      geometry.output.x,
                      geometry.output.x + std::max(0, geometry.output.width - kCompositorMinWindowWidth));
  std::int32_t const topInset = std::max(0, geometry.topInset);
  next.y = std::clamp(next.y,
                      geometry.output.y + topInset,
                      geometry.output.y + std::max(topInset,
                                                   geometry.output.height - kCompositorMinWindowHeight));
  return next;
}

namespace {

constexpr std::uint8_t kPopupEdgeTop = 1u << 0u;
constexpr std::uint8_t kPopupEdgeBottom = 1u << 1u;
constexpr std::uint8_t kPopupEdgeLeft = 1u << 2u;
constexpr std::uint8_t kPopupEdgeRight = 1u << 3u;

struct PopupConstraintOffsets {
  std::int32_t top = 0;
  std::int32_t bottom = 0;
  std::int32_t left = 0;
  std::int32_t right = 0;
};

std::uint8_t popupGravityEdges(PopupGravity gravity) {
  switch (gravity) {
  case PopupGravity::Top: return kPopupEdgeTop;
  case PopupGravity::Bottom: return kPopupEdgeBottom;
  case PopupGravity::Left: return kPopupEdgeLeft;
  case PopupGravity::Right: return kPopupEdgeRight;
  case PopupGravity::TopLeft: return kPopupEdgeTop | kPopupEdgeLeft;
  case PopupGravity::BottomLeft: return kPopupEdgeBottom | kPopupEdgeLeft;
  case PopupGravity::TopRight: return kPopupEdgeTop | kPopupEdgeRight;
  case PopupGravity::BottomRight: return kPopupEdgeBottom | kPopupEdgeRight;
  case PopupGravity::None: return 0;
  }
  return 0;
}

PopupAnchor invertPopupAnchorX(PopupAnchor anchor) {
  switch (anchor) {
  case PopupAnchor::Left: return PopupAnchor::Right;
  case PopupAnchor::Right: return PopupAnchor::Left;
  case PopupAnchor::TopLeft: return PopupAnchor::TopRight;
  case PopupAnchor::TopRight: return PopupAnchor::TopLeft;
  case PopupAnchor::BottomLeft: return PopupAnchor::BottomRight;
  case PopupAnchor::BottomRight: return PopupAnchor::BottomLeft;
  case PopupAnchor::None:
  case PopupAnchor::Top:
  case PopupAnchor::Bottom:
    return anchor;
  }
  return anchor;
}

PopupGravity invertPopupGravityX(PopupGravity gravity) {
  switch (gravity) {
  case PopupGravity::Left: return PopupGravity::Right;
  case PopupGravity::Right: return PopupGravity::Left;
  case PopupGravity::TopLeft: return PopupGravity::TopRight;
  case PopupGravity::TopRight: return PopupGravity::TopLeft;
  case PopupGravity::BottomLeft: return PopupGravity::BottomRight;
  case PopupGravity::BottomRight: return PopupGravity::BottomLeft;
  case PopupGravity::None:
  case PopupGravity::Top:
  case PopupGravity::Bottom:
    return gravity;
  }
  return gravity;
}

PopupAnchor invertPopupAnchorY(PopupAnchor anchor) {
  switch (anchor) {
  case PopupAnchor::Top: return PopupAnchor::Bottom;
  case PopupAnchor::Bottom: return PopupAnchor::Top;
  case PopupAnchor::TopLeft: return PopupAnchor::BottomLeft;
  case PopupAnchor::BottomLeft: return PopupAnchor::TopLeft;
  case PopupAnchor::TopRight: return PopupAnchor::BottomRight;
  case PopupAnchor::BottomRight: return PopupAnchor::TopRight;
  case PopupAnchor::None:
  case PopupAnchor::Left:
  case PopupAnchor::Right:
    return anchor;
  }
  return anchor;
}

PopupGravity invertPopupGravityY(PopupGravity gravity) {
  switch (gravity) {
  case PopupGravity::Top: return PopupGravity::Bottom;
  case PopupGravity::Bottom: return PopupGravity::Top;
  case PopupGravity::TopLeft: return PopupGravity::BottomLeft;
  case PopupGravity::BottomLeft: return PopupGravity::TopLeft;
  case PopupGravity::TopRight: return PopupGravity::BottomRight;
  case PopupGravity::BottomRight: return PopupGravity::TopRight;
  case PopupGravity::None:
  case PopupGravity::Left:
  case PopupGravity::Right:
    return gravity;
  }
  return gravity;
}

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

WindowGeometry requestedPopupBox(PopupPositionerGeometry const& geometry) {
  std::int32_t const width = std::max(1, geometry.width);
  std::int32_t const height = std::max(1, geometry.height);
  std::int32_t const parentX = geometry.parent ? geometry.parent->x : 0;
  std::int32_t const parentY = geometry.parent ? geometry.parent->y : 0;
  std::int32_t x = parentX + popupAnchorX(geometry);
  std::int32_t y = parentY + popupAnchorY(geometry);

  std::uint8_t const gravity = popupGravityEdges(geometry.gravity);
  if ((gravity & kPopupEdgeLeft) != 0u) {
    x -= width;
  } else if ((gravity & kPopupEdgeRight) == 0u) {
    x -= width / 2;
  }
  if ((gravity & kPopupEdgeTop) != 0u) {
    y -= height;
  } else if ((gravity & kPopupEdgeBottom) == 0u) {
    y -= height / 2;
  }

  return {.x = x, .y = y, .width = width, .height = height};
}

PopupConstraintOffsets popupConstraintOffsets(OutputGeometry output, WindowGeometry const& box) {
  return {
      .top = output.y - box.y,
      .bottom = box.y + box.height - (output.y + output.height),
      .left = output.x - box.x,
      .right = box.x + box.width - (output.x + output.width),
  };
}

bool popupUnconstrained(PopupConstraintOffsets const& offsets) {
  return offsets.top <= 0 && offsets.bottom <= 0 && offsets.left <= 0 && offsets.right <= 0;
}

bool unconstrainPopupByFlip(PopupPositionerGeometry const& geometry,
                            OutputGeometry output,
                            WindowGeometry& box,
                            PopupConstraintOffsets& offsets) {
  bool const flipX = ((offsets.left > 0) != (offsets.right > 0)) &&
                     hasPopupConstraintAdjustment(geometry.constraintAdjustment, PopupConstraintAdjustment::FlipX);
  bool const flipY = ((offsets.top > 0) != (offsets.bottom > 0)) &&
                     hasPopupConstraintAdjustment(geometry.constraintAdjustment, PopupConstraintAdjustment::FlipY);
  if (!flipX && !flipY) return false;

  PopupPositionerGeometry flipped = geometry;
  if (flipX) {
    flipped.anchor = invertPopupAnchorX(flipped.anchor);
    flipped.gravity = invertPopupGravityX(flipped.gravity);
  }
  if (flipY) {
    flipped.anchor = invertPopupAnchorY(flipped.anchor);
    flipped.gravity = invertPopupGravityY(flipped.gravity);
  }

  WindowGeometry const flippedBox = requestedPopupBox(flipped);
  PopupConstraintOffsets const flippedOffsets = popupConstraintOffsets(output, flippedBox);
  if (flippedOffsets.left <= 0 && flippedOffsets.right <= 0) {
    box.x = flippedBox.x;
    offsets.left = flippedOffsets.left;
    offsets.right = flippedOffsets.right;
  }
  if (flippedOffsets.top <= 0 && flippedOffsets.bottom <= 0) {
    box.y = flippedBox.y;
    offsets.top = flippedOffsets.top;
    offsets.bottom = flippedOffsets.bottom;
  }
  return popupUnconstrained(offsets);
}

bool unconstrainPopupBySlide(PopupPositionerGeometry const& geometry,
                             OutputGeometry output,
                             WindowGeometry& box,
                             PopupConstraintOffsets& offsets) {
  std::uint8_t const gravity = popupGravityEdges(geometry.gravity);
  bool const slideX = (offsets.left > 0 || offsets.right > 0) &&
                      hasPopupConstraintAdjustment(geometry.constraintAdjustment, PopupConstraintAdjustment::SlideX);
  bool const slideY = (offsets.top > 0 || offsets.bottom > 0) &&
                      hasPopupConstraintAdjustment(geometry.constraintAdjustment, PopupConstraintAdjustment::SlideY);
  if (!slideX && !slideY) return false;

  if (slideX) {
    if (offsets.left > 0 && offsets.right > 0) {
      if ((gravity & kPopupEdgeLeft) != 0u) {
        box.x -= offsets.right;
      } else {
        box.x += offsets.left;
      }
    } else {
      std::int32_t const absLeft = offsets.left > 0 ? offsets.left : -offsets.left;
      std::int32_t const absRight = offsets.right > 0 ? offsets.right : -offsets.right;
      if (absLeft < absRight) {
        box.x += offsets.left;
      } else {
        box.x -= offsets.right;
      }
    }
  }
  if (slideY) {
    if (offsets.top > 0 && offsets.bottom > 0) {
      if ((gravity & kPopupEdgeTop) != 0u) {
        box.y -= offsets.bottom;
      } else {
        box.y += offsets.top;
      }
    } else {
      std::int32_t const absTop = offsets.top > 0 ? offsets.top : -offsets.top;
      std::int32_t const absBottom = offsets.bottom > 0 ? offsets.bottom : -offsets.bottom;
      if (absTop < absBottom) {
        box.y += offsets.top;
      } else {
        box.y -= offsets.bottom;
      }
    }
  }

  offsets = popupConstraintOffsets(output, box);
  return popupUnconstrained(offsets);
}

bool unconstrainPopupByResize(PopupPositionerGeometry const& geometry,
                              OutputGeometry output,
                              WindowGeometry& box,
                              PopupConstraintOffsets& offsets) {
  bool const resizeX = (offsets.left > 0 || offsets.right > 0) &&
                       hasPopupConstraintAdjustment(geometry.constraintAdjustment, PopupConstraintAdjustment::ResizeX);
  bool const resizeY = (offsets.top > 0 || offsets.bottom > 0) &&
                       hasPopupConstraintAdjustment(geometry.constraintAdjustment, PopupConstraintAdjustment::ResizeY);
  if (!resizeX && !resizeY) return false;

  PopupConstraintOffsets clipped = offsets;
  clipped.left = std::max(0, clipped.left);
  clipped.right = std::max(0, clipped.right);
  clipped.top = std::max(0, clipped.top);
  clipped.bottom = std::max(0, clipped.bottom);

  WindowGeometry resized = box;
  if (resizeX) {
    resized.x += clipped.left;
    resized.width -= clipped.left + clipped.right;
  }
  if (resizeY) {
    resized.y += clipped.top;
    resized.height -= clipped.top + clipped.bottom;
  }
  if (resized.width <= 0 || resized.height <= 0) return false;

  box = resized;
  offsets = popupConstraintOffsets(output, box);
  return popupUnconstrained(offsets);
}

void unconstrainPopup(PopupPositionerGeometry const& geometry, WindowGeometry& box) {
  if (geometry.output.width <= 0 || geometry.output.height <= 0) return;
  PopupConstraintOffsets offsets = popupConstraintOffsets(geometry.output, box);
  if (popupUnconstrained(offsets)) return;
  if (unconstrainPopupByFlip(geometry, geometry.output, box, offsets)) return;
  if (unconstrainPopupBySlide(geometry, geometry.output, box, offsets)) return;
  unconstrainPopupByResize(geometry, geometry.output, box, offsets);
}

} // namespace

PopupGeometry positionedPopupGeometry(PopupPositionerGeometry const& geometry) {
  std::int32_t const parentX = geometry.parent ? geometry.parent->x : 0;
  std::int32_t const parentY = geometry.parent ? geometry.parent->y : 0;
  WindowGeometry box = requestedPopupBox(geometry);
  unconstrainPopup(geometry, box);

  return PopupGeometry{
      .window = box,
      .configureX = geometry.parent ? box.x - parentX : box.x,
      .configureY = geometry.parent ? box.y - parentY : box.y,
      .configureWidth = box.width,
      .configureHeight = box.height,
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

} // namespace lambdaui::compositor
