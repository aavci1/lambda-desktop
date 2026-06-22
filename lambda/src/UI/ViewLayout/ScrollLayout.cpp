#include "UI/ViewLayout/ScrollLayout.hpp"

#include "Layout/LayoutHelpers.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lambda::layout {

namespace {

struct ScrollIndicatorStyle {
  static constexpr float thickness = 4.f;
  static constexpr float outerInset = 3.f;
  static constexpr float minLength = 24.f;
};

float indicatorTrackLength(float viewportExtent, bool reserveTrailing) {
  float const trailingInset =
      ScrollIndicatorStyle::outerInset +
      (reserveTrailing ? ScrollIndicatorStyle::thickness + ScrollIndicatorStyle::outerInset : 0.f);
  return std::max(0.f, viewportExtent - ScrollIndicatorStyle::outerInset - trailingInset);
}

float indicatorThumbLength(float viewportExtent, float contentExtent, float trackLength) {
  if (!std::isfinite(viewportExtent) || !std::isfinite(contentExtent) ||
      !std::isfinite(trackLength) || viewportExtent <= 0.f || contentExtent <= 0.f ||
      trackLength <= 0.f) {
    return 0.f;
  }
  float const minLength = std::min(ScrollIndicatorStyle::minLength, trackLength);
  return std::clamp((viewportExtent / contentExtent) * trackLength, minLength, trackLength);
}

} // namespace

LayoutConstraints scrollChildConstraints(ScrollAxis axis, LayoutConstraints constraints, Size viewport) {
  switch (axis) {
  case ScrollAxis::Vertical:
    constraints.minHeight = 0.f;
    constraints.maxWidth =
        viewport.width > 0.f ? viewport.width : std::numeric_limits<float>::infinity();
    constraints.maxHeight = std::numeric_limits<float>::infinity();
    break;
  case ScrollAxis::Horizontal:
    constraints.minWidth = 0.f;
    constraints.maxWidth = std::numeric_limits<float>::infinity();
    constraints.maxHeight =
        viewport.height > 0.f ? viewport.height : std::numeric_limits<float>::infinity();
    break;
  case ScrollAxis::Both:
    constraints.minWidth = 0.f;
    constraints.minHeight = 0.f;
    constraints.maxWidth = std::numeric_limits<float>::infinity();
    constraints.maxHeight = std::numeric_limits<float>::infinity();
    break;
  }
  clampLayoutMinToMax(constraints);
  return constraints;
}

Size scrollContentSize(ScrollAxis axis, std::span<Size const> childSizes) {
  float totalWidth = 0.f;
  float totalHeight = 0.f;

  switch (axis) {
  case ScrollAxis::Horizontal:
    for (Size const size : childSizes) {
      totalWidth += size.width;
      totalHeight = std::max(totalHeight, size.height);
    }
    break;
  case ScrollAxis::Vertical:
    for (Size const size : childSizes) {
      totalWidth = std::max(totalWidth, size.width);
      totalHeight += size.height;
    }
    break;
  case ScrollAxis::Both:
    for (Size const size : childSizes) {
      totalWidth = std::max(totalWidth, size.width);
      totalHeight = std::max(totalHeight, size.height);
    }
    break;
  }

  return Size{totalWidth, totalHeight};
}

Point maxScrollOffset(ScrollAxis axis, Size const& viewport, Size const& content) {
  return Point{
      (axis == ScrollAxis::Horizontal || axis == ScrollAxis::Both)
          ? std::max(0.f, content.width - viewport.width)
          : 0.f,
      (axis == ScrollAxis::Vertical || axis == ScrollAxis::Both)
          ? std::max(0.f, content.height - viewport.height)
          : 0.f,
  };
}

Point clampScrollOffset(ScrollAxis axis, Point offset, Size const& viewport, Size const& content) {
  Point const maxOffset = maxScrollOffset(axis, viewport, content);
  Point clamped = offset;
  if (axis == ScrollAxis::Vertical || axis == ScrollAxis::Both) {
    clamped.y = std::clamp(clamped.y, 0.f, maxOffset.y);
  } else {
    clamped.y = 0.f;
  }
  if (axis == ScrollAxis::Horizontal || axis == ScrollAxis::Both) {
    clamped.x = std::clamp(clamped.x, 0.f, maxOffset.x);
  } else {
    clamped.x = 0.f;
  }
  return clamped;
}

ScrollContentLayout layoutScrollContent(ScrollAxis axis, Size viewport, Point offset,
                                        std::span<Size const> childSizes) {
  ScrollContentLayout result{};
  result.contentSize = scrollContentSize(axis, childSizes);
  result.clampedOffset = clampScrollOffset(axis, offset, viewport, result.contentSize);
  result.slots.reserve(childSizes.size());

  if (axis == ScrollAxis::Horizontal) {
    float x = -result.clampedOffset.x;
    for (Size const size : childSizes) {
      result.slots.push_back(ScrollChildSlot{
          .origin = Point{x, 0.f},
          .assignedSize = Size{size.width, viewport.height},
      });
      x += size.width;
    }
  } else if (axis == ScrollAxis::Vertical) {
    float y = -result.clampedOffset.y;
    for (Size const size : childSizes) {
      result.slots.push_back(ScrollChildSlot{
          .origin = Point{0.f, y},
          .assignedSize = Size{viewport.width, size.height},
      });
      y += size.height;
    }
  } else {
    for (Size const size : childSizes) {
      result.slots.push_back(ScrollChildSlot{
          .origin = Point{-result.clampedOffset.x, -result.clampedOffset.y},
          .assignedSize = size,
      });
    }
  }

  return result;
}

Size resolveMeasuredScrollViewSize(ScrollAxis axis, Size contentSize, LayoutConstraints const& constraints) {
  Size out = contentSize;

  switch (axis) {
  case ScrollAxis::Vertical:
  case ScrollAxis::Horizontal:
    if (std::isfinite(constraints.maxWidth) && constraints.maxWidth > 0.f) {
      out.width = constraints.maxWidth;
    }
    if (std::isfinite(constraints.maxHeight) && constraints.maxHeight > 0.f) {
      out.height = std::min(out.height, constraints.maxHeight);
    }
    out.width = std::max(out.width, constraints.minWidth);
    out.height = std::max(out.height, constraints.minHeight);
    break;
  case ScrollAxis::Both:
    if (std::isfinite(constraints.maxWidth) && constraints.maxWidth > 0.f) {
      out.width = std::min(out.width, constraints.maxWidth);
    }
    if (std::isfinite(constraints.maxHeight) && constraints.maxHeight > 0.f) {
      out.height = std::min(out.height, constraints.maxHeight);
    }
    out.width = std::max(out.width, constraints.minWidth);
    out.height = std::max(out.height, constraints.minHeight);
    break;
  }

  return out;
}

ScrollIndicatorMetrics makeVerticalIndicator(Point const& offset, Size const& viewport,
                                             Size const& content, bool reserveBottom) {
  if (viewport.width <= 0.f || viewport.height <= 0.f || content.height <= viewport.height) {
    return {};
  }

  float const trackLength = indicatorTrackLength(viewport.height, reserveBottom);
  if (trackLength <= 0.f) {
    return {};
  }

  float const maxOffset = maxScrollOffset(ScrollAxis::Vertical, viewport, content).y;
  float const thumbLength = indicatorThumbLength(viewport.height, content.height, trackLength);
  float const travel = std::max(0.f, trackLength - thumbLength);
  float const t = maxOffset > 0.f ? std::clamp(offset.y / maxOffset, 0.f, 1.f) : 0.f;

  return ScrollIndicatorMetrics{
      .x = std::max(0.f, viewport.width - ScrollIndicatorStyle::thickness - ScrollIndicatorStyle::outerInset),
      .y = ScrollIndicatorStyle::outerInset + travel * t,
      .width = ScrollIndicatorStyle::thickness,
      .height = thumbLength,
  };
}

ScrollIndicatorMetrics makeHorizontalIndicator(Point const& offset, Size const& viewport,
                                               Size const& content, bool reserveTrailing) {
  if (viewport.width <= 0.f || viewport.height <= 0.f || content.width <= viewport.width) {
    return {};
  }

  float const trackLength = indicatorTrackLength(viewport.width, reserveTrailing);
  if (trackLength <= 0.f) {
    return {};
  }

  float const maxOffset = maxScrollOffset(ScrollAxis::Horizontal, viewport, content).x;
  float const thumbLength = indicatorThumbLength(viewport.width, content.width, trackLength);
  float const travel = std::max(0.f, trackLength - thumbLength);
  float const t = maxOffset > 0.f ? std::clamp(offset.x / maxOffset, 0.f, 1.f) : 0.f;

  return ScrollIndicatorMetrics{
      .x = ScrollIndicatorStyle::outerInset + travel * t,
      .y = std::max(0.f, viewport.height - ScrollIndicatorStyle::thickness - ScrollIndicatorStyle::outerInset),
      .width = thumbLength,
      .height = ScrollIndicatorStyle::thickness,
  };
}

} // namespace lambda::layout
