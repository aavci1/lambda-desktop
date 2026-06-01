#include "Compositor/Window/WindowGeometry.hpp"
#include "Compositor/Wayland/XdgPositionerState.hpp"

#include <doctest/doctest.h>

#include <array>

TEST_CASE("compositor snap geometry uses full output height minus title bar") {
  lambda::compositor::OutputGeometry const output{.width = 1920, .height = 1080};

  auto left = lambda::compositor::snappedWindowGeometry(output, true);
  CHECK(left.x == 0);
  CHECK(left.y == lambda::compositor::kCompositorTitleBarHeight);
  CHECK(left.width == 960);
  CHECK(left.height == 1052);

  auto right = lambda::compositor::snappedWindowGeometry(output, false);
  CHECK(right.x == 960);
  CHECK(right.y == lambda::compositor::kCompositorTitleBarHeight);
  CHECK(right.width == 960);
  CHECK(right.height == 1052);
}

TEST_CASE("compositor snap geometry can use a zero chrome inset") {
  lambda::compositor::OutputGeometry const output{.width = 1920, .height = 1080};

  auto left = lambda::compositor::snappedWindowGeometry(output, true, 0);
  CHECK(left.x == 0);
  CHECK(left.y == 0);
  CHECK(left.width == 960);
  CHECK(left.height == 1080);

  auto maximized = lambda::compositor::maximizedWindowGeometry(output, 0);
  CHECK(maximized.x == 0);
  CHECK(maximized.y == 0);
  CHECK(maximized.width == 1920);
  CHECK(maximized.height == 1080);
}

TEST_CASE("compositor snap preview appears only within a small edge threshold") {
  lambda::compositor::OutputGeometry const output{.width = 1280, .height = 720};
  CHECK(lambda::compositor::snapPreviewGeometry({.x = 64, .y = 96, .width = 400, .height = 240}, output) == std::nullopt);
  CHECK(lambda::compositor::snapPreviewGeometry({.x = 16, .y = 96, .width = 400, .height = 240}, output) == std::nullopt);

  auto left = lambda::compositor::snapPreviewGeometry({
      .x = lambda::compositor::kCompositorSnapEdgeThreshold,
      .y = 96,
      .width = 400,
      .height = 240,
  }, output);
  REQUIRE(left);
  CHECK(left->x == 0);
  CHECK(left->width == 640);

  auto right = lambda::compositor::snapPreviewGeometry({.x = 920, .y = 96, .width = 360, .height = 240}, output);
  REQUIRE(right);
  CHECK(right->x == 640);
  CHECK(right->width == 640);

  auto top = lambda::compositor::snapPreviewGeometry({
      .x = 280,
      .y = lambda::compositor::kCompositorTitleBarHeight,
      .width = 500,
      .height = 320,
  }, output);
  REQUIRE(top);
  CHECK(top->x == 0);
  CHECK(top->y == lambda::compositor::kCompositorTitleBarHeight);
  CHECK(top->width == 1280);
  CHECK(top->height == 692);
}

TEST_CASE("compositor snap preview supports quarter targets") {
  lambda::compositor::OutputGeometry const output{.width = 1280, .height = 720};

  auto topLeft = lambda::compositor::snapPreviewGeometry({
      .x = 0,
      .y = lambda::compositor::kCompositorTitleBarHeight,
      .width = 360,
      .height = 240,
  }, output);
  REQUIRE(topLeft);
  CHECK(topLeft->x == 0);
  CHECK(topLeft->y == lambda::compositor::kCompositorTitleBarHeight);
  CHECK(topLeft->width == 640);
  CHECK(topLeft->height == 346);

  auto bottomRight = lambda::compositor::snapPreviewGeometry({
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
  lambda::compositor::WindowGeometry const restored = lambda::compositor::restoredDragGeometry({
      .pointerX = 480.f,
      .pointerY = 80.f,
      .dragOffsetY = 20.f,
      .snappedWindow = {.x = 0, .y = lambda::compositor::kCompositorTitleBarHeight, .width = 960, .height = 1052},
      .restoreWindow = {.x = 200, .y = 120, .width = 500, .height = 400},
      .output = {.width = 1920, .height = 1080},
  });

  CHECK(restored.x == 230);
  CHECK(restored.y == 60);
  CHECK(restored.width == 500);
  CHECK(restored.height == 400);
}

TEST_CASE("compositor resize geometry clamps minimum size and adjusts anchored edges") {
  using lambda::compositor::ResizeEdge;
  auto resized = lambda::compositor::resizedWindowGeometry({
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

  auto minClamped = lambda::compositor::resizedWindowGeometry({
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
  CHECK(minClamped.width == lambda::compositor::kCompositorMinWindowWidth);
  CHECK(minClamped.height == lambda::compositor::kCompositorMinWindowHeight);
}

TEST_CASE("compositor maximized geometry uses the same top inset as snap") {
  lambda::compositor::OutputGeometry const output{.width = 1366, .height = 768};

  auto maximized = lambda::compositor::maximizedWindowGeometry(output);
  CHECK(maximized.x == 0);
  CHECK(maximized.y == lambda::compositor::kCompositorTitleBarHeight);
  CHECK(maximized.width == 1366);
  CHECK(maximized.height == 740);
}

TEST_CASE("compositor snap geometry uses output work area with reserved dock already removed") {
  lambda::compositor::OutputGeometry const workArea{.width = 1920, .height = 1000};

  auto maximized = lambda::compositor::maximizedWindowGeometry(workArea, 40);
  CHECK(maximized.x == 0);
  CHECK(maximized.y == 40);
  CHECK(maximized.width == 1920);
  CHECK(maximized.height == 960);

  auto bottomRight = lambda::compositor::snapTargetGeometry(workArea,
                                                         lambda::compositor::SnapTarget::BottomRightQuarter,
                                                         40);
  CHECK(bottomRight.x == 960);
  CHECK(bottomRight.y == 520);
  CHECK(bottomRight.width == 960);
  CHECK(bottomRight.height == 480);
}

TEST_CASE("compositor drag geometry softly snaps near the center of the work area") {
  lambda::compositor::OutputGeometry const workArea{.width = 1200, .height = 760};
  lambda::compositor::WindowGeometry const nearCenter{.x = 397, .y = 246, .width = 400, .height = 300};

  auto centered = lambda::compositor::centerSnappedWindowGeometry(nearCenter, workArea, 40);
  CHECK(centered.x == 400);
  CHECK(centered.y == 250);
  CHECK(centered.width == 400);
  CHECK(centered.height == 300);

  auto outsideThreshold = lambda::compositor::centerSnappedWindowGeometry({
      .x = 360,
      .y = 246,
      .width = 400,
      .height = 300,
  }, workArea, 40);
  CHECK(outsideThreshold.x == 360);
  CHECK(outsideThreshold.y == 250);
}

TEST_CASE("compositor geometry stays valid on tiny outputs") {
  lambda::compositor::OutputGeometry const output{.width = 120, .height = 80};

  auto left = lambda::compositor::snappedWindowGeometry(output, true);
  CHECK(left.width == lambda::compositor::kCompositorMinWindowWidth);
  CHECK(left.height == lambda::compositor::kCompositorMinWindowHeight);
  CHECK(left.y == lambda::compositor::kCompositorTitleBarHeight);

  auto maximized = lambda::compositor::maximizedWindowGeometry(output);
  CHECK(maximized.width == lambda::compositor::kCompositorMinWindowWidth);
  CHECK(maximized.height == lambda::compositor::kCompositorMinWindowHeight);
}

TEST_CASE("compositor snap preview rejects points outside the small edge threshold") {
  lambda::compositor::OutputGeometry const output{.width = 1000, .height = 700};

  auto left = lambda::compositor::snapPreviewGeometry({
      .x = lambda::compositor::kCompositorSnapEdgeThreshold + 1,
      .y = 100,
      .width = 300,
      .height = 200,
  }, output);
  CHECK(left == std::nullopt);

  auto top = lambda::compositor::snapPreviewGeometry({
      .x = 300,
      .y = lambda::compositor::kCompositorTitleBarHeight + lambda::compositor::kCompositorSnapEdgeThreshold + 1,
      .width = 300,
      .height = 200,
  }, output);
  CHECK(top == std::nullopt);
}

TEST_CASE("compositor popup geometry uses anchor gravity and parent-relative configure") {
  lambda::compositor::PopupPositionerGeometry const positioner{
      .parent = lambda::compositor::WindowGeometry{.x = 200, .y = 100, .width = 500, .height = 400},
      .output = {.width = 1280, .height = 720},
      .anchorRectX = 40,
      .anchorRectY = 30,
      .anchorRectWidth = 120,
      .anchorRectHeight = 24,
      .width = 240,
      .height = 180,
      .offsetX = 8,
      .offsetY = 6,
      .anchor = lambda::compositor::PopupAnchor::BottomRight,
      .gravity = lambda::compositor::PopupGravity::BottomLeft,
  };

  auto popup = lambda::compositor::positionedPopupGeometry(positioner);
  CHECK(popup.window.x == 128);
  CHECK(popup.window.y == 160);
  CHECK(popup.window.width == 240);
  CHECK(popup.window.height == 180);
  CHECK(popup.configureX == -72);
  CHECK(popup.configureY == 60);
  CHECK(popup.configureWidth == 240);
  CHECK(popup.configureHeight == 180);
}

TEST_CASE("compositor popup geometry slides to fit output when requested") {
  lambda::compositor::PopupPositionerGeometry const positioner{
      .parent = lambda::compositor::WindowGeometry{.x = 700, .y = 500, .width = 300, .height = 200},
      .output = {.width = 800, .height = 600},
      .anchorRectX = 280,
      .anchorRectY = 180,
      .anchorRectWidth = 40,
      .anchorRectHeight = 40,
      .width = 220,
      .height = 160,
      .anchor = lambda::compositor::PopupAnchor::BottomRight,
      .gravity = lambda::compositor::PopupGravity::BottomRight,
      .constraintAdjustment = lambda::compositor::PopupConstraintAdjustment::SlideX |
                              lambda::compositor::PopupConstraintAdjustment::SlideY,
  };

  auto popup = lambda::compositor::positionedPopupGeometry(positioner);
  CHECK(popup.window.x == 580);
  CHECK(popup.window.y == 440);
  CHECK(popup.configureX == -120);
  CHECK(popup.configureY == -60);
}

TEST_CASE("compositor popup geometry leaves constrained boxes unchanged without adjustment flags") {
  lambda::compositor::PopupPositionerGeometry const positioner{
      .parent = lambda::compositor::WindowGeometry{.x = 700, .y = 500, .width = 300, .height = 200},
      .output = {.width = 800, .height = 600},
      .anchorRectX = 280,
      .anchorRectY = 180,
      .anchorRectWidth = 40,
      .anchorRectHeight = 40,
      .width = 220,
      .height = 160,
      .anchor = lambda::compositor::PopupAnchor::BottomRight,
      .gravity = lambda::compositor::PopupGravity::BottomRight,
  };

  auto popup = lambda::compositor::positionedPopupGeometry(positioner);
  CHECK(popup.window.x == 1020);
  CHECK(popup.window.y == 720);
  CHECK(popup.configureX == 320);
  CHECK(popup.configureY == 220);
}

TEST_CASE("compositor popup geometry flips when that fully satisfies constraints") {
  lambda::compositor::PopupPositionerGeometry const positioner{
      .parent = lambda::compositor::WindowGeometry{.x = 700, .y = 100, .width = 80, .height = 80},
      .output = {.width = 800, .height = 600},
      .anchorRectX = 0,
      .anchorRectY = 20,
      .anchorRectWidth = 80,
      .anchorRectHeight = 20,
      .width = 100,
      .height = 80,
      .anchor = lambda::compositor::PopupAnchor::Right,
      .gravity = lambda::compositor::PopupGravity::Right,
      .constraintAdjustment = lambda::compositor::PopupConstraintAdjustment::FlipX,
  };

  auto popup = lambda::compositor::positionedPopupGeometry(positioner);
  CHECK(popup.window.x == 600);
  CHECK(popup.window.y == 90);
  CHECK(popup.configureX == -100);
  CHECK(popup.configureY == -10);
}

TEST_CASE("compositor popup geometry resizes when slide and flip cannot satisfy constraints") {
  lambda::compositor::PopupPositionerGeometry const positioner{
      .output = {.width = 200, .height = 100},
      .anchorRectX = 100,
      .anchorRectY = 50,
      .width = 300,
      .height = 120,
      .constraintAdjustment = lambda::compositor::PopupConstraintAdjustment::ResizeX |
                              lambda::compositor::PopupConstraintAdjustment::ResizeY,
  };

  auto popup = lambda::compositor::positionedPopupGeometry(positioner);
  CHECK(popup.window.x == 0);
  CHECK(popup.window.y == 0);
  CHECK(popup.window.width == 200);
  CHECK(popup.window.height == 100);
  CHECK(popup.configureWidth == 200);
  CHECK(popup.configureHeight == 100);
}

TEST_CASE("compositor popup geometry clamps empty size to one pixel") {
  lambda::compositor::PopupPositionerGeometry const positioner{
      .output = {.width = 100, .height = 100},
      .width = 0,
      .height = -10,
  };

  auto popup = lambda::compositor::positionedPopupGeometry(positioner);
  CHECK(popup.window.width == 1);
  CHECK(popup.window.height == 1);
  CHECK(popup.configureWidth == 1);
  CHECK(popup.configureHeight == 1);
}

TEST_CASE("compositor xdg positioner validation mirrors wlroots request rules") {
  CHECK(lambda::compositor::xdgPositionerSizeInputValid(1, 1));
  CHECK_FALSE(lambda::compositor::xdgPositionerSizeInputValid(0, 1));
  CHECK_FALSE(lambda::compositor::xdgPositionerSizeInputValid(1, 0));
  CHECK_FALSE(lambda::compositor::xdgPositionerSizeInputValid(-1, 1));

  CHECK(lambda::compositor::xdgPositionerAnchorRectInputValid(0, 0));
  CHECK(lambda::compositor::xdgPositionerAnchorRectInputValid(32, 0));
  CHECK_FALSE(lambda::compositor::xdgPositionerAnchorRectInputValid(-1, 0));
  CHECK_FALSE(lambda::compositor::xdgPositionerAnchorRectInputValid(1, -1));
}

TEST_CASE("compositor xdg positioner completeness follows wlroots post-validation rule") {
  lambda::compositor::XdgPositionerRules complete{
      .width = 64,
      .height = 32,
      .anchorRectWidth = 1,
      .anchorRectHeight = 0,
  };
  CHECK(lambda::compositor::xdgPositionerComplete(complete));

  auto missingSize = complete;
  missingSize.width = 0;
  CHECK_FALSE(lambda::compositor::xdgPositionerComplete(missingSize));

  auto missingAnchorWidth = complete;
  missingAnchorWidth.anchorRectWidth = 0;
  CHECK_FALSE(lambda::compositor::xdgPositionerComplete(missingAnchorWidth));
}

TEST_CASE("compositor popup screen geometry accumulates parent offsets") {
  std::array<lambda::compositor::WindowGeometry, 2> const chain{{
      {.x = 100, .y = 100, .width = 400, .height = 300},
      {.x = 50, .y = 200, .width = 150, .height = 180},
  }};

  auto popup = lambda::compositor::popupScreenGeometry(chain);
  REQUIRE(popup);
  CHECK(popup->x == 150);
  CHECK(popup->y == 300);
  CHECK(popup->width == 150);
  CHECK(popup->height == 180);
}

TEST_CASE("compositor popup screen geometry accepts a parentless root popup") {
  std::array<lambda::compositor::WindowGeometry, 1> const chain{{
      {.x = 320, .y = 180, .width = 240, .height = 160},
  }};

  auto popup = lambda::compositor::popupScreenGeometry(chain);
  REQUIRE(popup);
  CHECK(popup->x == 320);
  CHECK(popup->y == 180);
  CHECK(popup->width == 240);
  CHECK(popup->height == 160);
}

TEST_CASE("compositor nested popup screen geometry accumulates every popup offset") {
  std::array<lambda::compositor::WindowGeometry, 3> const chain{{
      {.x = 100, .y = 100, .width = 400, .height = 300},
      {.x = 50, .y = 50, .width = 150, .height = 180},
      {.x = 40, .y = 30, .width = 120, .height = 90},
  }};

  auto popup = lambda::compositor::popupScreenGeometry(chain);
  REQUIRE(popup);
  CHECK(popup->x == 190);
  CHECK(popup->y == 180);
  CHECK(popup->width == 120);
  CHECK(popup->height == 90);
}

TEST_CASE("compositor popup screen geometry rejects invalid chain entries") {
  CHECK_FALSE(lambda::compositor::popupScreenGeometry({}));

  std::array<lambda::compositor::WindowGeometry, 2> const invalidPopup{{
      {.x = 100, .y = 100, .width = 400, .height = 300},
      {.x = 50, .y = 50, .width = 0, .height = 180},
  }};
  CHECK_FALSE(lambda::compositor::popupScreenGeometry(invalidPopup));
}
