#include "Compositor/Window/WindowGeometry.hpp"

#include <doctest/doctest.h>

#include <array>

TEST_CASE("compositor snap geometry uses full output height minus title bar") {
  flux::compositor::OutputGeometry const output{.width = 1920, .height = 1080};

  auto left = flux::compositor::snappedWindowGeometry(output, true);
  CHECK(left.x == 0);
  CHECK(left.y == flux::compositor::kCompositorTitleBarHeight);
  CHECK(left.width == 960);
  CHECK(left.height == 1052);

  auto right = flux::compositor::snappedWindowGeometry(output, false);
  CHECK(right.x == 960);
  CHECK(right.y == flux::compositor::kCompositorTitleBarHeight);
  CHECK(right.width == 960);
  CHECK(right.height == 1052);
}

TEST_CASE("compositor snap geometry can use a zero chrome inset") {
  flux::compositor::OutputGeometry const output{.width = 1920, .height = 1080};

  auto left = flux::compositor::snappedWindowGeometry(output, true, 0);
  CHECK(left.x == 0);
  CHECK(left.y == 0);
  CHECK(left.width == 960);
  CHECK(left.height == 1080);

  auto maximized = flux::compositor::maximizedWindowGeometry(output, 0);
  CHECK(maximized.x == 0);
  CHECK(maximized.y == 0);
  CHECK(maximized.width == 1920);
  CHECK(maximized.height == 1080);
}

TEST_CASE("compositor snap preview appears only within a small edge threshold") {
  flux::compositor::OutputGeometry const output{.width = 1280, .height = 720};
  CHECK(flux::compositor::snapPreviewGeometry({.x = 64, .y = 96, .width = 400, .height = 240}, output) == std::nullopt);
  CHECK(flux::compositor::snapPreviewGeometry({.x = 16, .y = 96, .width = 400, .height = 240}, output) == std::nullopt);

  auto left = flux::compositor::snapPreviewGeometry({
      .x = flux::compositor::kCompositorSnapEdgeThreshold,
      .y = 96,
      .width = 400,
      .height = 240,
  }, output);
  REQUIRE(left);
  CHECK(left->x == 0);
  CHECK(left->width == 640);

  auto right = flux::compositor::snapPreviewGeometry({.x = 920, .y = 96, .width = 360, .height = 240}, output);
  REQUIRE(right);
  CHECK(right->x == 640);
  CHECK(right->width == 640);

  auto top = flux::compositor::snapPreviewGeometry({
      .x = 280,
      .y = flux::compositor::kCompositorTitleBarHeight,
      .width = 500,
      .height = 320,
  }, output);
  REQUIRE(top);
  CHECK(top->x == 0);
  CHECK(top->y == flux::compositor::kCompositorTitleBarHeight);
  CHECK(top->width == 1280);
  CHECK(top->height == 692);
}

TEST_CASE("compositor snap preview supports quarter targets") {
  flux::compositor::OutputGeometry const output{.width = 1280, .height = 720};

  auto topLeft = flux::compositor::snapPreviewGeometry({
      .x = 0,
      .y = flux::compositor::kCompositorTitleBarHeight,
      .width = 360,
      .height = 240,
  }, output);
  REQUIRE(topLeft);
  CHECK(topLeft->x == 0);
  CHECK(topLeft->y == flux::compositor::kCompositorTitleBarHeight);
  CHECK(topLeft->width == 640);
  CHECK(topLeft->height == 346);

  auto bottomRight = flux::compositor::snapPreviewGeometry({
      .x = 920,
      .y = 480,
      .width = 360,
      .height = 240,
  }, output);
  REQUIRE(bottomRight);
  CHECK(bottomRight->x == 640);
  CHECK(bottomRight->y == 374);
  CHECK(bottomRight->width == 640);
  CHECK(bottomRight->height == 346);
}

TEST_CASE("compositor restored drag geometry keeps title bar under cursor") {
  flux::compositor::WindowGeometry const restored = flux::compositor::restoredDragGeometry({
      .pointerX = 480.f,
      .pointerY = 80.f,
      .dragOffsetY = 20.f,
      .snappedWindow = {.x = 0, .y = flux::compositor::kCompositorTitleBarHeight, .width = 960, .height = 1052},
      .restoreWindow = {.x = 200, .y = 120, .width = 500, .height = 400},
      .output = {.width = 1920, .height = 1080},
  });

  CHECK(restored.x == 230);
  CHECK(restored.y == 60);
  CHECK(restored.width == 500);
  CHECK(restored.height == 400);
}

TEST_CASE("compositor resize geometry clamps minimum size and adjusts anchored edges") {
  using flux::compositor::ResizeEdge;
  auto resized = flux::compositor::resizedWindowGeometry({
      .startPointerX = 400.f,
      .startPointerY = 300.f,
      .pointerX = 700.f,
      .pointerY = 500.f,
      .startWindow = {.x = 100, .y = 100, .width = 640, .height = 480},
      .edges = ResizeEdge::Right | ResizeEdge::Bottom,
      .output = {.width = 1920, .height = 1080},
  });
  CHECK(resized.x == 100);
  CHECK(resized.y == 100);
  CHECK(resized.width == 940);
  CHECK(resized.height == 680);

  auto minClamped = flux::compositor::resizedWindowGeometry({
      .startPointerX = 100.f,
      .startPointerY = 100.f,
      .pointerX = 800.f,
      .pointerY = 700.f,
      .startWindow = {.x = 240, .y = 180, .width = 300, .height = 200},
      .edges = ResizeEdge::Left | ResizeEdge::Top,
      .output = {.width = 1024, .height = 768},
  });
  CHECK(minClamped.x == 380);
  CHECK(minClamped.y == 260);
  CHECK(minClamped.width == flux::compositor::kCompositorMinWindowWidth);
  CHECK(minClamped.height == flux::compositor::kCompositorMinWindowHeight);
}

TEST_CASE("compositor maximized geometry uses the same top inset as snap") {
  flux::compositor::OutputGeometry const output{.width = 1366, .height = 768};

  auto maximized = flux::compositor::maximizedWindowGeometry(output);
  CHECK(maximized.x == 0);
  CHECK(maximized.y == flux::compositor::kCompositorTitleBarHeight);
  CHECK(maximized.width == 1366);
  CHECK(maximized.height == 740);
}

TEST_CASE("compositor snap geometry uses output work area with reserved dock already removed") {
  flux::compositor::OutputGeometry const workArea{.width = 1920, .height = 1000};

  auto maximized = flux::compositor::maximizedWindowGeometry(workArea, 40);
  CHECK(maximized.x == 0);
  CHECK(maximized.y == 40);
  CHECK(maximized.width == 1920);
  CHECK(maximized.height == 960);

  auto bottomRight = flux::compositor::snapTargetGeometry(workArea,
                                                         flux::compositor::SnapTarget::BottomRightQuarter,
                                                         40);
  CHECK(bottomRight.x == 960);
  CHECK(bottomRight.y == 520);
  CHECK(bottomRight.width == 960);
  CHECK(bottomRight.height == 480);
}

TEST_CASE("compositor drag geometry softly snaps near the center of the work area") {
  flux::compositor::OutputGeometry const workArea{.width = 1200, .height = 760};
  flux::compositor::WindowGeometry const nearCenter{.x = 397, .y = 246, .width = 400, .height = 300};

  auto centered = flux::compositor::centerSnappedWindowGeometry(nearCenter, workArea, 40);
  CHECK(centered.x == 400);
  CHECK(centered.y == 250);
  CHECK(centered.width == 400);
  CHECK(centered.height == 300);

  auto outsideThreshold = flux::compositor::centerSnappedWindowGeometry({
      .x = 360,
      .y = 246,
      .width = 400,
      .height = 300,
  }, workArea, 40);
  CHECK(outsideThreshold.x == 360);
  CHECK(outsideThreshold.y == 250);
}

TEST_CASE("compositor geometry stays valid on tiny outputs") {
  flux::compositor::OutputGeometry const output{.width = 120, .height = 80};

  auto left = flux::compositor::snappedWindowGeometry(output, true);
  CHECK(left.width == flux::compositor::kCompositorMinWindowWidth);
  CHECK(left.height == flux::compositor::kCompositorMinWindowHeight);
  CHECK(left.y == flux::compositor::kCompositorTitleBarHeight);

  auto maximized = flux::compositor::maximizedWindowGeometry(output);
  CHECK(maximized.width == flux::compositor::kCompositorMinWindowWidth);
  CHECK(maximized.height == flux::compositor::kCompositorMinWindowHeight);
}

TEST_CASE("compositor snap preview rejects points outside the small edge threshold") {
  flux::compositor::OutputGeometry const output{.width = 1000, .height = 700};

  auto left = flux::compositor::snapPreviewGeometry({
      .x = flux::compositor::kCompositorSnapEdgeThreshold + 1,
      .y = 100,
      .width = 300,
      .height = 200,
  }, output);
  CHECK(left == std::nullopt);

  auto top = flux::compositor::snapPreviewGeometry({
      .x = 300,
      .y = flux::compositor::kCompositorTitleBarHeight + flux::compositor::kCompositorSnapEdgeThreshold + 1,
      .width = 300,
      .height = 200,
  }, output);
  CHECK(top == std::nullopt);
}

TEST_CASE("compositor popup geometry uses anchor gravity and parent-relative configure") {
  flux::compositor::PopupPositionerGeometry const positioner{
      .parent = flux::compositor::WindowGeometry{.x = 200, .y = 100, .width = 500, .height = 400},
      .output = {.width = 1280, .height = 720},
      .anchorRectX = 40,
      .anchorRectY = 30,
      .anchorRectWidth = 120,
      .anchorRectHeight = 24,
      .width = 240,
      .height = 180,
      .offsetX = 8,
      .offsetY = 6,
      .anchor = flux::compositor::PopupAnchor::BottomRight,
      .gravity = flux::compositor::PopupGravity::BottomLeft,
  };

  auto popup = flux::compositor::positionedPopupGeometry(positioner);
  CHECK(popup.window.x == 128);
  CHECK(popup.window.y == 160);
  CHECK(popup.window.width == 240);
  CHECK(popup.window.height == 180);
  CHECK(popup.configureX == -72);
  CHECK(popup.configureY == 60);
  CHECK(popup.configureWidth == 240);
  CHECK(popup.configureHeight == 180);
}

TEST_CASE("compositor popup geometry clamps to output") {
  flux::compositor::PopupPositionerGeometry const positioner{
      .parent = flux::compositor::WindowGeometry{.x = 700, .y = 500, .width = 300, .height = 200},
      .output = {.width = 800, .height = 600},
      .anchorRectX = 280,
      .anchorRectY = 180,
      .anchorRectWidth = 40,
      .anchorRectHeight = 40,
      .width = 220,
      .height = 160,
      .anchor = flux::compositor::PopupAnchor::BottomRight,
      .gravity = flux::compositor::PopupGravity::BottomRight,
  };

  auto popup = flux::compositor::positionedPopupGeometry(positioner);
  CHECK(popup.window.x == 580);
  CHECK(popup.window.y == 440);
  CHECK(popup.configureX == -120);
  CHECK(popup.configureY == -60);
}

TEST_CASE("compositor popup geometry clamps empty size to one pixel") {
  flux::compositor::PopupPositionerGeometry const positioner{
      .output = {.width = 100, .height = 100},
      .width = 0,
      .height = -10,
  };

  auto popup = flux::compositor::positionedPopupGeometry(positioner);
  CHECK(popup.window.width == 1);
  CHECK(popup.window.height == 1);
  CHECK(popup.configureWidth == 1);
  CHECK(popup.configureHeight == 1);
}

TEST_CASE("compositor popup screen geometry accumulates parent offsets") {
  std::array<flux::compositor::WindowGeometry, 2> const chain{{
      {.x = 100, .y = 100, .width = 400, .height = 300},
      {.x = 50, .y = 200, .width = 150, .height = 180},
  }};

  auto popup = flux::compositor::popupScreenGeometry(chain);
  REQUIRE(popup);
  CHECK(popup->x == 150);
  CHECK(popup->y == 300);
  CHECK(popup->width == 150);
  CHECK(popup->height == 180);
}

TEST_CASE("compositor nested popup screen geometry accumulates every popup offset") {
  std::array<flux::compositor::WindowGeometry, 3> const chain{{
      {.x = 100, .y = 100, .width = 400, .height = 300},
      {.x = 50, .y = 50, .width = 150, .height = 180},
      {.x = 40, .y = 30, .width = 120, .height = 90},
  }};

  auto popup = flux::compositor::popupScreenGeometry(chain);
  REQUIRE(popup);
  CHECK(popup->x == 190);
  CHECK(popup->y == 180);
  CHECK(popup->width == 120);
  CHECK(popup->height == 90);
}

TEST_CASE("compositor popup screen geometry rejects invalid chain entries") {
  CHECK_FALSE(flux::compositor::popupScreenGeometry({}));

  std::array<flux::compositor::WindowGeometry, 2> const invalidPopup{{
      {.x = 100, .y = 100, .width = 400, .height = 300},
      {.x = 50, .y = 50, .width = 0, .height = 180},
  }};
  CHECK_FALSE(flux::compositor::popupScreenGeometry(invalidPopup));
}
