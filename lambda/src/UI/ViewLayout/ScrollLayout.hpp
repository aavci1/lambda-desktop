#pragma once

#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Layout/LayoutEngine.hpp>
#include <Lambda/UI/Views/OffsetView.hpp>

#include <span>
#include <vector>

namespace lambda::layout {

struct ScrollIndicatorMetrics {
  float x = 0.f;
  float y = 0.f;
  float width = 0.f;
  float height = 0.f;

  [[nodiscard]] bool visible() const { return width > 0.f && height > 0.f; }
};

struct ScrollChildSlot {
  Point origin{};
  Size assignedSize{};
};

struct ScrollContentLayout {
  Size contentSize{};
  Point clampedOffset{};
  std::vector<ScrollChildSlot> slots{};
};

LayoutConstraints scrollChildConstraints(ScrollAxis axis, LayoutConstraints constraints, Size viewport);

Size scrollContentSize(ScrollAxis axis, std::span<Size const> childSizes);

Point maxScrollOffset(ScrollAxis axis, Size const& viewport, Size const& content);

Point clampScrollOffset(ScrollAxis axis, Point offset, Size const& viewport, Size const& content);

ScrollContentLayout layoutScrollContent(ScrollAxis axis, Size viewport, Point offset,
                                        std::span<Size const> childSizes);

Size resolveMeasuredScrollViewSize(ScrollAxis axis, Size contentSize, LayoutConstraints const& constraints);

ScrollIndicatorMetrics makeVerticalIndicator(Point const& offset, Size const& viewport,
                                             Size const& content, bool reserveBottom);

ScrollIndicatorMetrics makeHorizontalIndicator(Point const& offset, Size const& viewport,
                                               Size const& content, bool reserveTrailing);

} // namespace lambda::layout
