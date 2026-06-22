#include <doctest/doctest.h>

#include <Lambda/UI/Views/Popover.hpp>
#include <Lambda/UI/Views/PopoverCalloutShape.hpp>

#include "UI/ViewLayout/OverlayLayout.hpp"
#include "UI/ViewLayout/ScrollLayout.hpp"
#include "Layout/Algorithms/StackLayout.hpp"

#include <cmath>
#include <limits>

namespace {

using namespace lambda;
using namespace lambda::layout;

TEST_CASE("StackLayout: grow distributes extra space by flex weight") {
  std::array<StackMainAxisChild, 2> children{{
      StackMainAxisChild{.naturalMainSize = 20.f, .minMainSize = 0.f, .flexGrow = 1.f, .flexShrink = 1.f},
      StackMainAxisChild{.naturalMainSize = 10.f, .minMainSize = 0.f, .flexGrow = 3.f, .flexShrink = 1.f},
  }};

  StackMainAxisLayout const layout = layoutStackMainAxis(children, 5.f, 60.f, true);
  REQUIRE(layout.mainSizes.size() == 2);
  CHECK(layout.mainSizes[0] == doctest::Approx(26.25f));
  CHECK(layout.mainSizes[1] == doctest::Approx(28.75f));
  CHECK(layout.containerMainSize == doctest::Approx(60.f));
  CHECK(layout.startOffset == doctest::Approx(0.f));
}

TEST_CASE("StackLayout: shrink clamps at child minimums") {
  std::array<StackMainAxisChild, 2> children{{
      StackMainAxisChild{.naturalMainSize = 40.f, .minMainSize = 35.f, .flexGrow = 0.f, .flexShrink = 1.f},
      StackMainAxisChild{.naturalMainSize = 30.f, .minMainSize = 10.f, .flexGrow = 0.f, .flexShrink = 1.f},
  }};

  StackMainAxisLayout const layout = layoutStackMainAxis(children, 5.f, 55.f, true);
  REQUIRE(layout.mainSizes.size() == 2);
  CHECK(layout.mainSizes[0] == doctest::Approx(35.f));
  CHECK(layout.mainSizes[1] == doctest::Approx(15.f));
  CHECK(layout.containerMainSize == doctest::Approx(55.f));
}

TEST_CASE("StackLayout: justify center offsets remaining main-axis space") {
  std::array<StackMainAxisChild, 2> children{{
      StackMainAxisChild{.naturalMainSize = 20.f, .minMainSize = 0.f},
      StackMainAxisChild{.naturalMainSize = 10.f, .minMainSize = 0.f},
  }};

  StackMainAxisLayout const layout =
      layoutStackMainAxis(children, 5.f, 50.f, true, JustifyContent::Center);
  CHECK(layout.startOffset == doctest::Approx(7.5f));
  CHECK(layout.itemSpacing == doctest::Approx(5.f));
}

TEST_CASE("StackLayout: justify space-between adds remaining main-axis space to gaps") {
  std::array<StackMainAxisChild, 3> children{{
      StackMainAxisChild{.naturalMainSize = 10.f, .minMainSize = 0.f},
      StackMainAxisChild{.naturalMainSize = 10.f, .minMainSize = 0.f},
      StackMainAxisChild{.naturalMainSize = 10.f, .minMainSize = 0.f},
  }};

  StackMainAxisLayout const layout =
      layoutStackMainAxis(children, 5.f, 100.f, true, JustifyContent::SpaceBetween);
  CHECK(layout.startOffset == doctest::Approx(0.f));
  CHECK(layout.itemSpacing == doctest::Approx(35.f));
}

TEST_CASE("StackLayout: justify space-around falls back to safe center on overflow") {
  std::array<StackMainAxisChild, 2> children{{
      StackMainAxisChild{.naturalMainSize = 20.f, .minMainSize = 0.f},
      StackMainAxisChild{.naturalMainSize = 20.f, .minMainSize = 0.f},
  }};

  StackMainAxisLayout const layout =
      layoutStackMainAxis(children, 5.f, 30.f, true, JustifyContent::SpaceAround);
  CHECK(layout.startOffset == doctest::Approx(0.f));
  CHECK(layout.itemSpacing == doctest::Approx(5.f));
}

TEST_CASE("StackLayout: justify space-evenly centers a single child in remaining space") {
  std::array<StackMainAxisChild, 1> children{{
      StackMainAxisChild{.naturalMainSize = 20.f, .minMainSize = 0.f},
  }};

  StackMainAxisLayout const layout =
      layoutStackMainAxis(children, 8.f, 80.f, true, JustifyContent::SpaceEvenly);
  CHECK(layout.startOffset == doctest::Approx(30.f));
  CHECK(layout.itemSpacing == doctest::Approx(38.f));
}

TEST_CASE("StackLayout: auto flex basis preserves intrinsic size before equal grow") {
  std::array<StackMainAxisChild, 2> children{{
      StackMainAxisChild{.naturalMainSize = 100.f, .minMainSize = 0.f, .flexGrow = 1.f, .flexShrink = 1.f},
      StackMainAxisChild{.naturalMainSize = 200.f, .minMainSize = 0.f, .flexGrow = 1.f, .flexShrink = 1.f},
  }};

  StackMainAxisLayout const layout = layoutStackMainAxis(children, 0.f, 700.f, true);
  REQUIRE(layout.mainSizes.size() == 2);
  CHECK(layout.mainSizes[0] == doctest::Approx(300.f));
  CHECK(layout.mainSizes[1] == doctest::Approx(400.f));
}

TEST_CASE("StackLayout: zero flex basis equalizes equal-grow children") {
  std::array<StackMainAxisChild, 2> children{{
      StackMainAxisChild{
          .naturalMainSize = 100.f,
          .flexBasis = 0.f,
          .minMainSize = 0.f,
          .flexGrow = 1.f,
          .flexShrink = 1.f,
      },
      StackMainAxisChild{
          .naturalMainSize = 200.f,
          .flexBasis = 0.f,
          .minMainSize = 0.f,
          .flexGrow = 1.f,
          .flexShrink = 1.f,
      },
  }};

  StackMainAxisLayout const layout = layoutStackMainAxis(children, 0.f, 700.f, true);
  REQUIRE(layout.mainSizes.size() == 2);
  CHECK(layout.mainSizes[0] == doctest::Approx(350.f));
  CHECK(layout.mainSizes[1] == doctest::Approx(350.f));
}

TEST_CASE("StackLayout: horizontal center alignment uses assigned cross size") {
  std::array<Size, 3> measuredSizes{{
      Size{40.f, 44.f},
      Size{56.f, 88.f},
      Size{32.f, 60.f},
  }};
  std::array<float, 3> mainSizes{{40.f, 56.f, 32.f}};

  StackLayoutResult const layout =
      layoutStack(StackAxis::Horizontal, Alignment::Center, measuredSizes, mainSizes, 8.f, 200.f, 0.f, 152.f, true);

  REQUIRE(layout.slots.size() == 3);
  CHECK(layout.containerSize.height == doctest::Approx(152.f));
  CHECK(layout.slots[0].origin.y == doctest::Approx(54.f));
  CHECK(layout.slots[1].origin.y == doctest::Approx(32.f));
  CHECK(layout.slots[2].origin.y == doctest::Approx(46.f));
}

TEST_CASE("ScrollLayout: clamps offsets and assigns vertical child slots") {
  std::array<Size, 2> childSizes{{
      Size{40.f, 60.f},
      Size{40.f, 50.f},
  }};

  ScrollContentLayout const layout =
      layoutScrollContent(ScrollAxis::Vertical, Size{80.f, 70.f}, Point{0.f, 100.f}, childSizes);
  REQUIRE(layout.slots.size() == 2);
  CHECK(layout.contentSize.width == doctest::Approx(40.f));
  CHECK(layout.contentSize.height == doctest::Approx(110.f));
  CHECK(layout.clampedOffset.y == doctest::Approx(40.f));
  CHECK(layout.slots[0].origin.y == doctest::Approx(-40.f));
  CHECK(layout.slots[1].origin.y == doctest::Approx(20.f));
  CHECK(layout.slots[0].assignedSize.width == doctest::Approx(80.f));
}

TEST_CASE("ScrollLayout: scroll child constraints clear minimum size on the scroll axis") {
  LayoutConstraints constraints{};
  constraints.minWidth = 120.f;
  constraints.minHeight = 300.f;
  constraints.maxWidth = 120.f;
  constraints.maxHeight = 300.f;

  LayoutConstraints const vertical =
      scrollChildConstraints(ScrollAxis::Vertical, constraints, Size{120.f, 300.f});
  CHECK(vertical.minWidth == doctest::Approx(120.f));
  CHECK(vertical.minHeight == doctest::Approx(0.f));
  CHECK(std::isinf(vertical.maxHeight));

  LayoutConstraints const horizontal =
      scrollChildConstraints(ScrollAxis::Horizontal, constraints, Size{120.f, 300.f});
  CHECK(horizontal.minWidth == doctest::Approx(0.f));
  CHECK(horizontal.minHeight == doctest::Approx(300.f));
  CHECK(std::isinf(horizontal.maxWidth));
}

TEST_CASE("ScrollLayout: measured viewport honors fixed minimum constraints") {
  LayoutConstraints constraints{};
  constraints.minWidth = 120.f;
  constraints.minHeight = 300.f;
  constraints.maxWidth = 120.f;
  constraints.maxHeight = 300.f;

  Size const vertical = resolveMeasuredScrollViewSize(
      ScrollAxis::Vertical, Size{80.f, 90.f}, constraints);
  CHECK(vertical.width == doctest::Approx(120.f));
  CHECK(vertical.height == doctest::Approx(300.f));

  Size const both = resolveMeasuredScrollViewSize(
      ScrollAxis::Both, Size{80.f, 90.f}, constraints);
  CHECK(both.width == doctest::Approx(120.f));
  CHECK(both.height == doctest::Approx(300.f));
}

TEST_CASE("ScrollLayout: indicator metrics track scroll progress") {
  Size const viewport{120.f, 80.f};
  Size const content{120.f, 240.f};
  ScrollIndicatorMetrics const indicator =
      makeVerticalIndicator(Point{0.f, 80.f}, viewport, content, false);

  CHECK(indicator.visible());
  CHECK(indicator.x == doctest::Approx(113.f));
  CHECK(indicator.height > 24.f);
  CHECK(indicator.y > 3.f);
}

TEST_CASE("OverlayLayout: popover callout layout reserves arrow depth in total size") {
  PopoverCalloutShape callout{
      .placement = PopoverPlacement::Below,
      .arrow = true,
      .padding = 12.f,
  };

  PopoverCalloutLayout const layout =
      layoutPopoverCallout(callout, Size{100.f, 20.f}, LayoutConstraints{});
  CHECK(std::isinf(layout.contentConstraints.maxWidth));
  CHECK(layout.totalSize.width == doctest::Approx(124.f));
  CHECK(layout.totalSize.height ==
        doctest::Approx(20.f + 24.f + PopoverCalloutShape::kArrowH));
  CHECK(layout.contentOrigin.y == doctest::Approx(PopoverCalloutShape::kArrowH + 12.f));
}

TEST_CASE("OverlayLayout: anchorless overlays are centered in the window") {
  OverlayConfig config{};
  Rect const frame = resolveOverlayFrame(Size{800.f, 600.f}, config, Rect{0.f, 0.f, 320.f, 180.f});

  CHECK(frame.x == doctest::Approx(240.f));
  CHECK(frame.y == doctest::Approx(210.f));
  CHECK(frame.width == doctest::Approx(320.f));
  CHECK(frame.height == doctest::Approx(180.f));
}

TEST_CASE("OverlayLayout: anchored overlays respect anchor outsets and max height") {
  OverlayConfig config{};
  config.anchor = Rect{100.f, 100.f, 80.f, 80.f};
  config.anchorOutsets = EdgeInsets{.top = 4.f, .right = 6.f, .bottom = 8.f, .left = 10.f};
  config.anchorMaxHeight = 32.f;
  config.crossAlignment = OverlayConfig::CrossAlignment::Start;

  Rect const frame =
      resolveOverlayFrame(Size{800.f, 600.f}, config, Rect{0.f, 0.f, 120.f, 60.f});

  CHECK(frame.x == doctest::Approx(90.f));
  CHECK(frame.y == doctest::Approx(100.f + 32.f + 8.f));
}

TEST_CASE("PopoverPlacement: measured popover size avoids premature flip from max-size estimate") {
  Rect const anchor{120.f, 500.f, 80.f, 32.f};
  Size const window{420.f, 640.f};
  std::optional<Rect> anchorOpt{anchor};

  CHECK(resolvePopoverPlacement(PopoverPlacement::Below, anchorOpt, Size{260.f, 220.f}, 14.f, window) ==
        PopoverPlacement::Above);
  CHECK(resolveMeasuredPopoverPlacement(PopoverPlacement::Below, anchorOpt, Size{180.f, 88.f}, 8.f, window) ==
        PopoverPlacement::Below);
}

} // namespace
