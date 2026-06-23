#include "Compositor/Window/WindowGeometry.hpp"
#include "Compositor/Wayland/XdgPositionerState.hpp"

#include <doctest/doctest.h>

#include <array>

TEST_CASE("compositor snap geometry uses full output height minus title bar") {
  lambdaui::compositor::OutputGeometry const output{.width = 1920, .height = 1080};

  auto left = lambdaui::compositor::snappedWindowGeometry(output, true);
  CHECK(left.x == 0);
  CHECK(left.y == lambdaui::compositor::kCompositorTitleBarHeight);
  CHECK(left.width == 960);
  CHECK(left.height == 1052);

  auto right = lambdaui::compositor::snappedWindowGeometry(output, false);
  CHECK(right.x == 960);
  CHECK(right.y == lambdaui::compositor::kCompositorTitleBarHeight);
  CHECK(right.width == 960);
  CHECK(right.height == 1052);
}

TEST_CASE("compositor snap geometry can use a zero chrome inset") {
  lambdaui::compositor::OutputGeometry const output{.width = 1920, .height = 1080};

  auto left = lambdaui::compositor::snappedWindowGeometry(output, true, 0);
  CHECK(left.x == 0);
  CHECK(left.y == 0);
  CHECK(left.width == 960);
  CHECK(left.height == 1080);

  auto maximized = lambdaui::compositor::maximizedWindowGeometry(output, 0);
  CHECK(maximized.x == 0);
  CHECK(maximized.y == 0);
  CHECK(maximized.width == 1920);
  CHECK(maximized.height == 1080);
}

TEST_CASE("compositor snap preview appears only within a small edge threshold") {
  lambdaui::compositor::OutputGeometry const output{.width = 1280, .height = 720};
  CHECK(lambdaui::compositor::snapPreviewGeometry({.x = 64, .y = 96, .width = 400, .height = 240}, output) == std::nullopt);
  CHECK(lambdaui::compositor::snapPreviewGeometry({.x = 16, .y = 96, .width = 400, .height = 240}, output) == std::nullopt);

  auto left = lambdaui::compositor::snapPreviewGeometry({
      .x = lambdaui::compositor::kCompositorSnapEdgeThreshold,
      .y = 96,
      .width = 400,
      .height = 240,
  }, output);
  REQUIRE(left);
  CHECK(left->x == 0);
  CHECK(left->width == 640);

  auto right = lambdaui::compositor::snapPreviewGeometry({.x = 920, .y = 96, .width = 360, .height = 240}, output);
  REQUIRE(right);
  CHECK(right->x == 640);
  CHECK(right->width == 640);

  auto top = lambdaui::compositor::snapPreviewGeometry({
      .x = 280,
      .y = lambdaui::compositor::kCompositorTitleBarHeight,
      .width = 500,
      .height = 320,
  }, output);
  REQUIRE(top);
  CHECK(top->x == 0);
  CHECK(top->y == lambdaui::compositor::kCompositorTitleBarHeight);
  CHECK(top->width == 1280);
  CHECK(top->height == 692);
}

TEST_CASE("compositor snap preview supports quarter targets") {
  lambdaui::compositor::OutputGeometry const output{.width = 1280, .height = 720};

  auto topLeft = lambdaui::compositor::snapPreviewGeometry({
      .x = 0,
      .y = lambdaui::compositor::kCompositorTitleBarHeight,
      .width = 360,
      .height = 240,
  }, output);
  REQUIRE(topLeft);
  CHECK(topLeft->x == 0);
  CHECK(topLeft->y == lambdaui::compositor::kCompositorTitleBarHeight);
  CHECK(topLeft->width == 640);
  CHECK(topLeft->height == 346);

  auto bottomRight = lambdaui::compositor::snapPreviewGeometry({
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
  lambdaui::compositor::WindowGeometry const restored = lambdaui::compositor::restoredDragGeometry({
      .pointerX = 480.f,
      .pointerY = 80.f,
      .dragOffsetY = 20.f,
      .snappedWindow = {.x = 0, .y = lambdaui::compositor::kCompositorTitleBarHeight, .width = 960, .height = 1052},
      .restoreWindow = {.x = 200, .y = 120, .width = 500, .height = 400},
      .output = {.width = 1920, .height = 1080},
  });

  CHECK(restored.x == 230);
  CHECK(restored.y == 60);
  CHECK(restored.width == 500);
  CHECK(restored.height == 400);
}

TEST_CASE("compositor resize geometry clamps minimum size and adjusts anchored edges") {
  using lambdaui::compositor::ResizeEdge;
  auto resized = lambdaui::compositor::resizedWindowGeometry({
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

  auto minClamped = lambdaui::compositor::resizedWindowGeometry({
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
  CHECK(minClamped.width == lambdaui::compositor::kCompositorMinWindowWidth);
  CHECK(minClamped.height == lambdaui::compositor::kCompositorMinWindowHeight);
}

TEST_CASE("compositor maximized geometry uses the same top inset as snap") {
  lambdaui::compositor::OutputGeometry const output{.width = 1366, .height = 768};

  auto maximized = lambdaui::compositor::maximizedWindowGeometry(output);
  CHECK(maximized.x == 0);
  CHECK(maximized.y == lambdaui::compositor::kCompositorTitleBarHeight);
  CHECK(maximized.width == 1366);
  CHECK(maximized.height == 740);
}

TEST_CASE("compositor snap and maximize geometry keep the chrome ring inside target bounds") {
  lambdaui::compositor::OutputGeometry const output{.width = 1920, .height = 1080};
  constexpr std::int32_t titleBar = lambdaui::compositor::kCompositorTitleBarHeight;
  constexpr std::int32_t ring = 4;

  auto left = lambdaui::compositor::snappedWindowGeometry(output, true, titleBar, ring);
  CHECK(left.x == ring);
  CHECK(left.y == titleBar);
  CHECK(left.width == 952);
  CHECK(left.height == 1048);
  auto leftFrame = lambdaui::compositor::windowFrameGeometryForContent(left, titleBar, ring);
  CHECK(leftFrame.x == 0);
  CHECK(leftFrame.y == 0);
  CHECK(leftFrame.width == 960);
  CHECK(leftFrame.height == 1080);

  auto right = lambdaui::compositor::snappedWindowGeometry(output, false, titleBar, ring);
  CHECK(right.x == 964);
  CHECK(right.y == titleBar);
  CHECK(right.width == 952);
  CHECK(right.height == 1048);
  auto rightFrame = lambdaui::compositor::windowFrameGeometryForContent(right, titleBar, ring);
  CHECK(rightFrame.x == 960);
  CHECK(rightFrame.y == 0);
  CHECK(rightFrame.width == 960);
  CHECK(rightFrame.height == 1080);

  auto maximized = lambdaui::compositor::maximizedWindowGeometry(output, titleBar, ring);
  CHECK(maximized.x == ring);
  CHECK(maximized.y == titleBar);
  CHECK(maximized.width == 1912);
  CHECK(maximized.height == 1048);
  auto maximizedFrame = lambdaui::compositor::windowFrameGeometryForContent(maximized, titleBar, ring);
  CHECK(maximizedFrame.x == 0);
  CHECK(maximizedFrame.y == 0);
  CHECK(maximizedFrame.width == 1920);
  CHECK(maximizedFrame.height == 1080);
}

TEST_CASE("compositor snap and maximize geometry apply logical output origin") {
  lambdaui::compositor::OutputGeometry const output{.x = 1920, .y = 80, .width = 1280, .height = 720};
  constexpr std::int32_t titleBar = 40;
  constexpr std::int32_t ring = 4;

  auto left = lambdaui::compositor::snappedWindowGeometry(output, true, titleBar, ring);
  CHECK(left.x == 1924);
  CHECK(left.y == 120);
  CHECK(left.width == 632);
  CHECK(left.height == 676);
  auto leftFrame = lambdaui::compositor::windowFrameGeometryForContent(left, titleBar, ring);
  CHECK(leftFrame.x == 1920);
  CHECK(leftFrame.y == 80);
  CHECK(leftFrame.width == 640);
  CHECK(leftFrame.height == 720);

  auto right = lambdaui::compositor::snappedWindowGeometry(output, false, titleBar, ring);
  CHECK(right.x == 2564);
  CHECK(right.y == 120);
  CHECK(right.width == 632);
  CHECK(right.height == 676);
  auto rightFrame = lambdaui::compositor::windowFrameGeometryForContent(right, titleBar, ring);
  CHECK(rightFrame.x == 2560);
  CHECK(rightFrame.y == 80);
  CHECK(rightFrame.width == 640);
  CHECK(rightFrame.height == 720);

  auto maximized = lambdaui::compositor::maximizedWindowGeometry(output, titleBar, ring);
  CHECK(maximized.x == 1924);
  CHECK(maximized.y == 120);
  CHECK(maximized.width == 1272);
  CHECK(maximized.height == 676);
  auto maximizedFrame = lambdaui::compositor::windowFrameGeometryForContent(maximized, titleBar, ring);
  CHECK(maximizedFrame.x == 1920);
  CHECK(maximizedFrame.y == 80);
  CHECK(maximizedFrame.width == 1280);
  CHECK(maximizedFrame.height == 720);
}

TEST_CASE("compositor snap edge detection uses the chrome ring as the visible window edge") {
  lambdaui::compositor::OutputGeometry const output{.width = 1920, .height = 1080};
  constexpr std::int32_t titleBar = lambdaui::compositor::kCompositorTitleBarHeight;
  constexpr std::int32_t ring = 4;

  auto leftTarget =
      lambdaui::compositor::snapTargetForWindow({.x = ring, .y = 120, .width = 600, .height = 400},
                                              output,
                                              titleBar,
                                              ring);
  REQUIRE(leftTarget);
  CHECK(*leftTarget == lambdaui::compositor::SnapTarget::LeftHalf);

  auto rightTarget =
      lambdaui::compositor::snapTargetForWindow({.x = 1316, .y = 120, .width = 600, .height = 400},
                                              output,
                                              titleBar,
                                              ring);
  REQUIRE(rightTarget);
  CHECK(*rightTarget == lambdaui::compositor::SnapTarget::RightHalf);
}

TEST_CASE("compositor snap edge detection applies logical output origin") {
  lambdaui::compositor::OutputGeometry const output{.x = 1920, .y = 80, .width = 1280, .height = 720};
  constexpr std::int32_t titleBar = 40;
  constexpr std::int32_t ring = 4;

  auto leftTarget =
      lambdaui::compositor::snapTargetForWindow({.x = 1924, .y = 180, .width = 420, .height = 300},
                                              output,
                                              titleBar,
                                              ring);
  REQUIRE(leftTarget);
  CHECK(*leftTarget == lambdaui::compositor::SnapTarget::LeftHalf);

  auto bottomRightTarget =
      lambdaui::compositor::snapTargetForWindow({.x = 3010, .y = 620, .width = 190, .height = 176},
                                              output,
                                              titleBar,
                                              ring);
  REQUIRE(bottomRightTarget);
  CHECK(*bottomRightTarget == lambdaui::compositor::SnapTarget::BottomRightQuarter);
}

TEST_CASE("compositor snap geometry uses output work area with reserved dock already removed") {
  lambdaui::compositor::OutputGeometry const workArea{.width = 1920, .height = 1000};

  auto maximized = lambdaui::compositor::maximizedWindowGeometry(workArea, 40);
  CHECK(maximized.x == 0);
  CHECK(maximized.y == 40);
  CHECK(maximized.width == 1920);
  CHECK(maximized.height == 960);

  auto bottomRight = lambdaui::compositor::snapTargetGeometry(workArea,
                                                         lambdaui::compositor::SnapTarget::BottomRightQuarter,
                                                         40);
  CHECK(bottomRight.x == 960);
  CHECK(bottomRight.y == 520);
  CHECK(bottomRight.width == 960);
  CHECK(bottomRight.height == 480);
}

TEST_CASE("compositor drag geometry softly snaps near the center of the work area") {
  lambdaui::compositor::OutputGeometry const workArea{.width = 1200, .height = 760};
  lambdaui::compositor::WindowGeometry const nearCenter{.x = 397, .y = 246, .width = 400, .height = 300};

  auto centered = lambdaui::compositor::centerSnappedWindowGeometry(nearCenter, workArea, 40);
  CHECK(centered.x == 400);
  CHECK(centered.y == 250);
  CHECK(centered.width == 400);
  CHECK(centered.height == 300);

  auto outsideThreshold = lambdaui::compositor::centerSnappedWindowGeometry({
      .x = 360,
      .y = 246,
      .width = 400,
      .height = 300,
  }, workArea, 40);
  CHECK(outsideThreshold.x == 360);
  CHECK(outsideThreshold.y == 250);
}

TEST_CASE("compositor drag geometry softly snaps near logical output center") {
  lambdaui::compositor::OutputGeometry const workArea{.x = -640, .y = 80, .width = 1280, .height = 720};
  lambdaui::compositor::WindowGeometry const nearCenter{.x = -203, .y = 306, .width = 400, .height = 300};

  auto centered = lambdaui::compositor::centerSnappedWindowGeometry(nearCenter, workArea, 40);
  CHECK(centered.x == -200);
  CHECK(centered.y == 310);
  CHECK(centered.width == 400);
  CHECK(centered.height == 300);
}

TEST_CASE("compositor geometry stays valid on tiny outputs") {
  lambdaui::compositor::OutputGeometry const output{.width = 120, .height = 80};

  auto left = lambdaui::compositor::snappedWindowGeometry(output, true);
  CHECK(left.width == lambdaui::compositor::kCompositorMinWindowWidth);
  CHECK(left.height == lambdaui::compositor::kCompositorMinWindowHeight);
  CHECK(left.y == lambdaui::compositor::kCompositorTitleBarHeight);

  auto maximized = lambdaui::compositor::maximizedWindowGeometry(output);
  CHECK(maximized.width == lambdaui::compositor::kCompositorMinWindowWidth);
  CHECK(maximized.height == lambdaui::compositor::kCompositorMinWindowHeight);
}

TEST_CASE("compositor resize geometry clamps to logical output origin") {
  using lambdaui::compositor::ResizeEdge;

  auto resized = lambdaui::compositor::resizedWindowGeometry({
      .startPointerX = 0.f,
      .startPointerY = 0.f,
      .pointerX = 0.f,
      .pointerY = 0.f,
      .startWindow = {.x = 1800, .y = 60, .width = 300, .height = 200},
      .edges = ResizeEdge::None,
      .output = {.x = 1920, .y = 100, .width = 800, .height = 600},
  });
  CHECK(resized.x == 1920);
  CHECK(resized.y == 100 + lambdaui::compositor::kCompositorTitleBarHeight);
  CHECK(resized.width == 300);
  CHECK(resized.height == 200);
}

TEST_CASE("compositor snap preview rejects points outside the small edge threshold") {
  lambdaui::compositor::OutputGeometry const output{.width = 1000, .height = 700};

  auto left = lambdaui::compositor::snapPreviewGeometry({
      .x = lambdaui::compositor::kCompositorSnapEdgeThreshold + 1,
      .y = 100,
      .width = 300,
      .height = 200,
  }, output);
  CHECK(left == std::nullopt);

  auto top = lambdaui::compositor::snapPreviewGeometry({
      .x = 300,
      .y = lambdaui::compositor::kCompositorTitleBarHeight + lambdaui::compositor::kCompositorSnapEdgeThreshold + 1,
      .width = 300,
      .height = 200,
  }, output);
  CHECK(top == std::nullopt);
}

TEST_CASE("compositor popup geometry uses anchor gravity and parent-relative configure") {
  lambdaui::compositor::PopupPositionerGeometry const positioner{
      .parent = lambdaui::compositor::WindowGeometry{.x = 200, .y = 100, .width = 500, .height = 400},
      .output = {.width = 1280, .height = 720},
      .anchorRectX = 40,
      .anchorRectY = 30,
      .anchorRectWidth = 120,
      .anchorRectHeight = 24,
      .width = 240,
      .height = 180,
      .offsetX = 8,
      .offsetY = 6,
      .anchor = lambdaui::compositor::PopupAnchor::BottomRight,
      .gravity = lambdaui::compositor::PopupGravity::BottomLeft,
  };

  auto popup = lambdaui::compositor::positionedPopupGeometry(positioner);
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
  lambdaui::compositor::PopupPositionerGeometry const positioner{
      .parent = lambdaui::compositor::WindowGeometry{.x = 700, .y = 500, .width = 300, .height = 200},
      .output = {.width = 800, .height = 600},
      .anchorRectX = 280,
      .anchorRectY = 180,
      .anchorRectWidth = 40,
      .anchorRectHeight = 40,
      .width = 220,
      .height = 160,
      .anchor = lambdaui::compositor::PopupAnchor::BottomRight,
      .gravity = lambdaui::compositor::PopupGravity::BottomRight,
      .constraintAdjustment = lambdaui::compositor::PopupConstraintAdjustment::SlideX |
                              lambdaui::compositor::PopupConstraintAdjustment::SlideY,
  };

  auto popup = lambdaui::compositor::positionedPopupGeometry(positioner);
  CHECK(popup.window.x == 580);
  CHECK(popup.window.y == 440);
  CHECK(popup.configureX == -120);
  CHECK(popup.configureY == -60);
}

TEST_CASE("compositor popup geometry constrains to logical output origin") {
  lambdaui::compositor::PopupPositionerGeometry const positioner{
      .parent = lambdaui::compositor::WindowGeometry{.x = 2620, .y = 600, .width = 300, .height = 200},
      .output = {.x = 1920, .y = 100, .width = 800, .height = 600},
      .anchorRectX = 280,
      .anchorRectY = 180,
      .anchorRectWidth = 40,
      .anchorRectHeight = 40,
      .width = 220,
      .height = 160,
      .anchor = lambdaui::compositor::PopupAnchor::BottomRight,
      .gravity = lambdaui::compositor::PopupGravity::BottomRight,
      .constraintAdjustment = lambdaui::compositor::PopupConstraintAdjustment::SlideX |
                              lambdaui::compositor::PopupConstraintAdjustment::SlideY,
  };

  auto popup = lambdaui::compositor::positionedPopupGeometry(positioner);
  CHECK(popup.window.x == 2500);
  CHECK(popup.window.y == 540);
  CHECK(popup.configureX == -120);
  CHECK(popup.configureY == -60);
}

TEST_CASE("compositor popup geometry leaves constrained boxes unchanged without adjustment flags") {
  lambdaui::compositor::PopupPositionerGeometry const positioner{
      .parent = lambdaui::compositor::WindowGeometry{.x = 700, .y = 500, .width = 300, .height = 200},
      .output = {.width = 800, .height = 600},
      .anchorRectX = 280,
      .anchorRectY = 180,
      .anchorRectWidth = 40,
      .anchorRectHeight = 40,
      .width = 220,
      .height = 160,
      .anchor = lambdaui::compositor::PopupAnchor::BottomRight,
      .gravity = lambdaui::compositor::PopupGravity::BottomRight,
  };

  auto popup = lambdaui::compositor::positionedPopupGeometry(positioner);
  CHECK(popup.window.x == 1020);
  CHECK(popup.window.y == 720);
  CHECK(popup.configureX == 320);
  CHECK(popup.configureY == 220);
}

TEST_CASE("compositor popup geometry flips when that fully satisfies constraints") {
  lambdaui::compositor::PopupPositionerGeometry const positioner{
      .parent = lambdaui::compositor::WindowGeometry{.x = 700, .y = 100, .width = 80, .height = 80},
      .output = {.width = 800, .height = 600},
      .anchorRectX = 0,
      .anchorRectY = 20,
      .anchorRectWidth = 80,
      .anchorRectHeight = 20,
      .width = 100,
      .height = 80,
      .anchor = lambdaui::compositor::PopupAnchor::Right,
      .gravity = lambdaui::compositor::PopupGravity::Right,
      .constraintAdjustment = lambdaui::compositor::PopupConstraintAdjustment::FlipX,
  };

  auto popup = lambdaui::compositor::positionedPopupGeometry(positioner);
  CHECK(popup.window.x == 600);
  CHECK(popup.window.y == 90);
  CHECK(popup.configureX == -100);
  CHECK(popup.configureY == -10);
}

TEST_CASE("compositor popup geometry resizes when slide and flip cannot satisfy constraints") {
  lambdaui::compositor::PopupPositionerGeometry const positioner{
      .output = {.width = 200, .height = 100},
      .anchorRectX = 100,
      .anchorRectY = 50,
      .width = 300,
      .height = 120,
      .constraintAdjustment = lambdaui::compositor::PopupConstraintAdjustment::ResizeX |
                              lambdaui::compositor::PopupConstraintAdjustment::ResizeY,
  };

  auto popup = lambdaui::compositor::positionedPopupGeometry(positioner);
  CHECK(popup.window.x == 0);
  CHECK(popup.window.y == 0);
  CHECK(popup.window.width == 200);
  CHECK(popup.window.height == 100);
  CHECK(popup.configureWidth == 200);
  CHECK(popup.configureHeight == 100);
}

TEST_CASE("compositor popup geometry clamps empty size to one pixel") {
  lambdaui::compositor::PopupPositionerGeometry const positioner{
      .output = {.width = 100, .height = 100},
      .width = 0,
      .height = -10,
  };

  auto popup = lambdaui::compositor::positionedPopupGeometry(positioner);
  CHECK(popup.window.width == 1);
  CHECK(popup.window.height == 1);
  CHECK(popup.configureWidth == 1);
  CHECK(popup.configureHeight == 1);
}

TEST_CASE("compositor xdg positioner validation mirrors wlroots request rules") {
  CHECK(lambdaui::compositor::xdgPositionerSizeInputValid(1, 1));
  CHECK_FALSE(lambdaui::compositor::xdgPositionerSizeInputValid(0, 1));
  CHECK_FALSE(lambdaui::compositor::xdgPositionerSizeInputValid(1, 0));
  CHECK_FALSE(lambdaui::compositor::xdgPositionerSizeInputValid(-1, 1));

  CHECK(lambdaui::compositor::xdgPositionerAnchorRectInputValid(0, 0));
  CHECK(lambdaui::compositor::xdgPositionerAnchorRectInputValid(32, 0));
  CHECK_FALSE(lambdaui::compositor::xdgPositionerAnchorRectInputValid(-1, 0));
  CHECK_FALSE(lambdaui::compositor::xdgPositionerAnchorRectInputValid(1, -1));
}

TEST_CASE("compositor xdg positioner completeness follows wlroots post-validation rule") {
  lambdaui::compositor::XdgPositionerRules complete{
      .width = 64,
      .height = 32,
      .anchorRectWidth = 1,
      .anchorRectHeight = 0,
  };
  CHECK(lambdaui::compositor::xdgPositionerComplete(complete));

  auto missingSize = complete;
  missingSize.width = 0;
  CHECK_FALSE(lambdaui::compositor::xdgPositionerComplete(missingSize));

  auto missingAnchorWidth = complete;
  missingAnchorWidth.anchorRectWidth = 0;
  CHECK_FALSE(lambdaui::compositor::xdgPositionerComplete(missingAnchorWidth));
}

TEST_CASE("compositor popup screen geometry accumulates parent offsets") {
  std::array<lambdaui::compositor::WindowGeometry, 2> const chain{{
      {.x = 100, .y = 100, .width = 400, .height = 300},
      {.x = 50, .y = 200, .width = 150, .height = 180},
  }};

  auto popup = lambdaui::compositor::popupScreenGeometry(chain);
  REQUIRE(popup);
  CHECK(popup->x == 150);
  CHECK(popup->y == 300);
  CHECK(popup->width == 150);
  CHECK(popup->height == 180);
}

TEST_CASE("compositor popup screen geometry accepts a parentless root popup") {
  std::array<lambdaui::compositor::WindowGeometry, 1> const chain{{
      {.x = 320, .y = 180, .width = 240, .height = 160},
  }};

  auto popup = lambdaui::compositor::popupScreenGeometry(chain);
  REQUIRE(popup);
  CHECK(popup->x == 320);
  CHECK(popup->y == 180);
  CHECK(popup->width == 240);
  CHECK(popup->height == 160);
}

TEST_CASE("compositor nested popup screen geometry accumulates every popup offset") {
  std::array<lambdaui::compositor::WindowGeometry, 3> const chain{{
      {.x = 100, .y = 100, .width = 400, .height = 300},
      {.x = 50, .y = 50, .width = 150, .height = 180},
      {.x = 40, .y = 30, .width = 120, .height = 90},
  }};

  auto popup = lambdaui::compositor::popupScreenGeometry(chain);
  REQUIRE(popup);
  CHECK(popup->x == 190);
  CHECK(popup->y == 180);
  CHECK(popup->width == 120);
  CHECK(popup->height == 90);
}

TEST_CASE("compositor popup screen geometry rejects invalid chain entries") {
  CHECK_FALSE(lambdaui::compositor::popupScreenGeometry({}));

  std::array<lambdaui::compositor::WindowGeometry, 2> const invalidPopup{{
      {.x = 100, .y = 100, .width = 400, .height = 300},
      {.x = 50, .y = 50, .width = 0, .height = 180},
  }};
  CHECK_FALSE(lambdaui::compositor::popupScreenGeometry(invalidPopup));
}
