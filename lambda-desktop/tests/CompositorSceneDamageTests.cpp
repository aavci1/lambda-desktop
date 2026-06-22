#include "Compositor/SceneDamage.hpp"
#ifdef LAMBDA_TESTS_HAVE_COMPOSITOR_SCENE_GRAPH
#include "Compositor/Chrome/ChromeMetrics.hpp"
#include "Compositor/Chrome/WindowFrameGeometry.hpp"
#include "Compositor/SceneGraph/CompositorSceneGraph.hpp"
#endif

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace {

using lambda::compositor::CommittedSurfaceSnapshot;
#ifdef LAMBDA_TESTS_HAVE_COMPOSITOR_SCENE_GRAPH
using lambda::compositor::CompositorSceneFrameInput;
using lambda::compositor::CompositorSceneGraphState;
using lambda::compositor::SurfaceVisualState;
#endif
using lambda::compositor::SceneDamageState;
using RegionRect = CommittedSurfaceSnapshot::RegionRect;

CommittedSurfaceSnapshot surface(std::uint64_t id,
                                 std::int32_t x,
                                 std::int32_t y,
                                 std::int32_t width,
                                 std::int32_t height,
                                 std::uint64_t serial = 1) {
  return CommittedSurfaceSnapshot{
      .id = id,
      .x = x,
      .y = y,
      .width = width,
      .height = height,
      .committedWidth = width,
      .committedHeight = height,
      .bufferWidth = width,
      .bufferHeight = height,
      .destinationWidth = width,
      .destinationHeight = height,
      .serial = serial,
  };
}

bool containsRect(std::vector<RegionRect> const& rects, RegionRect expected) {
  return std::any_of(rects.begin(), rects.end(), [&](RegionRect const& rect) {
    return rect.x == expected.x &&
           rect.y == expected.y &&
           rect.width == expected.width &&
           rect.height == expected.height;
  });
}

bool rectEquals(RegionRect actual, RegionRect expected) {
  return actual.x == expected.x &&
         actual.y == expected.y &&
         actual.width == expected.width &&
         actual.height == expected.height;
}

bool rectCovers(RegionRect outer, RegionRect inner) {
  return inner.x >= outer.x &&
         inner.y >= outer.y &&
         inner.x + inner.width <= outer.x + outer.width &&
         inner.y + inner.height <= outer.y + outer.height;
}

bool anyRectCovers(std::vector<RegionRect> const& rects, RegionRect expected) {
  return std::any_of(rects.begin(), rects.end(), [&](RegionRect const& rect) {
    return rectCovers(rect, expected);
  });
}

#ifdef LAMBDA_TESTS_HAVE_COMPOSITOR_SCENE_GRAPH
bool rectContainedBy(RegionRect inner, RegionRect outer) {
  return inner.x >= outer.x &&
         inner.y >= outer.y &&
         inner.x + inner.width <= outer.x + outer.width &&
         inner.y + inner.height <= outer.y + outer.height;
}

RegionRect regionFromRect(lambda::Rect const& rect) {
  std::int32_t const left = static_cast<std::int32_t>(std::floor(rect.x));
  std::int32_t const top = static_cast<std::int32_t>(std::floor(rect.y));
  std::int32_t const right = static_cast<std::int32_t>(std::ceil(rect.x + rect.width));
  std::int32_t const bottom = static_cast<std::int32_t>(std::ceil(rect.y + rect.height));
  return RegionRect{.x = left, .y = top, .width = right - left, .height = bottom - top};
}
#endif

} // namespace

TEST_CASE("scene damage starts with full output damage") {
  SceneDamageState state;
  std::vector<CommittedSurfaceSnapshot> surfaces{surface(1, 10, 20, 100, 80)};

  auto damage = lambda::compositor::updateSceneDamage(state, surfaces, std::nullopt, 800, 600);

  CHECK(damage.fullOutput);
  REQUIRE(damage.rects.size() == 1);
  CHECK(damage.rects[0].width == 800);
  CHECK(damage.rects[0].height == 600);
}

TEST_CASE("scene damage is empty for unchanged snapshots") {
  SceneDamageState state;
  std::vector<CommittedSurfaceSnapshot> surfaces{surface(1, 10, 20, 100, 80)};

  (void)lambda::compositor::updateSceneDamage(state, surfaces, std::nullopt, 800, 600);
  auto damage = lambda::compositor::updateSceneDamage(state, surfaces, std::nullopt, 800, 600);

  CHECK(damage.empty());
}

TEST_CASE("scene damage state does not retain surface pixel storage") {
  SceneDamageState state;
  auto first = surface(1, 10, 20, 2, 2);
  auto pixels = std::make_shared<std::vector<std::uint8_t> const>(
      std::vector<std::uint8_t>{255, 0, 0, 255});
  first.rgbaPixels = pixels;
  first.shmPixels = pixels->data();
  first.shmPixelBytes = pixels->size();
  first.bufferDamageRects.push_back(RegionRect{.x = 0, .y = 0, .width = 1, .height = 1});

  (void)lambda::compositor::updateSceneDamage(state, {first}, std::nullopt, 800, 600);

  REQUIRE(state.surfaces.size() == 1);
  CHECK_FALSE(state.surfaces[0].rgbaPixels);
  CHECK(state.surfaces[0].shmPixels == nullptr);
  CHECK(state.surfaces[0].shmPixelBytes == 0);
  CHECK(state.surfaces[0].bufferDamageRects.empty());
}

TEST_CASE("scene damage covers old and new frame rectangles when a surface moves") {
  SceneDamageState state;
  auto first = surface(1, 10, 40, 100, 80);
  first.titleBarHeight = 28;
  auto moved = first;
  moved.x = 70;
  moved.y = 90;

  (void)lambda::compositor::updateSceneDamage(state, {first}, std::nullopt, 800, 600);
  auto damage = lambda::compositor::updateSceneDamage(state, {moved}, std::nullopt, 800, 600);

  CHECK_FALSE(damage.fullOutput);
  CHECK(anyRectCovers(damage.rects, RegionRect{.x = 10, .y = 12, .width = 100, .height = 108}));
  CHECK(anyRectCovers(damage.rects, RegionRect{.x = 70, .y = 62, .width = 100, .height = 108}));
}

TEST_CASE("scene damage maps buffer damage into stable surface coordinates") {
  SceneDamageState state;
  auto first = surface(1, 100, 120, 200, 100, 1);
  first.bufferWidth = 400;
  first.bufferHeight = 200;
  first.sourceWidth = 400.f;
  first.sourceHeight = 200.f;
  first.destinationWidth = 200;
  first.destinationHeight = 100;
  auto next = first;
  next.serial = 2;
  next.bufferDamageRects.push_back(RegionRect{.x = 20, .y = 40, .width = 80, .height = 20});

  (void)lambda::compositor::updateSceneDamage(state, {first}, std::nullopt, 800, 600);
  auto damage = lambda::compositor::updateSceneDamage(state, {next}, std::nullopt, 800, 600);

  CHECK_FALSE(damage.fullOutput);
  REQUIRE(damage.rects.size() == 1);
  CHECK(rectEquals(damage.rects[0], RegionRect{.x = 110, .y = 140, .width = 40, .height = 10}));
}

TEST_CASE("scene damage merges adjacent rects before the max rect fallback") {
  SceneDamageState state;
  auto first = surface(1, 0, 0, 800, 20, 1);
  auto next = first;
  next.serial = 2;
  for (std::int32_t i = 0; i < 70; ++i) {
    next.bufferDamageRects.push_back(RegionRect{.x = i * 10, .y = 0, .width = 10, .height = 10});
  }

  (void)lambda::compositor::updateSceneDamage(state, {first}, std::nullopt, 800, 600);
  auto damage = lambda::compositor::updateSceneDamage(state, {next}, std::nullopt, 800, 600);

  CHECK_FALSE(damage.fullOutput);
  REQUIRE(damage.rects.size() == 1);
  CHECK(rectEquals(damage.rects[0], RegionRect{.x = 0, .y = 0, .width = 700, .height = 10}));
}

TEST_CASE("scene damage keeps sparse rects separate") {
  SceneDamageState state;
  auto first = surface(1, 0, 0, 800, 200, 1);
  auto next = first;
  next.serial = 2;
  next.bufferDamageRects.push_back(RegionRect{.x = 0, .y = 0, .width = 100, .height = 100});
  next.bufferDamageRects.push_back(RegionRect{.x = 300, .y = 0, .width = 100, .height = 100});

  (void)lambda::compositor::updateSceneDamage(state, {first}, std::nullopt, 800, 600);
  auto damage = lambda::compositor::updateSceneDamage(state, {next}, std::nullopt, 800, 600);

  CHECK_FALSE(damage.fullOutput);
  REQUIRE(damage.rects.size() == 2);
  CHECK(containsRect(damage.rects, RegionRect{.x = 0, .y = 0, .width = 100, .height = 100}));
  CHECK(containsRect(damage.rects, RegionRect{.x = 300, .y = 0, .width = 100, .height = 100}));
}

TEST_CASE("scene damage uses full output damage when stacking order changes") {
  SceneDamageState state;
  auto first = surface(1, 0, 0, 100, 100);
  auto second = surface(2, 20, 20, 100, 100);

  (void)lambda::compositor::updateSceneDamage(state, {first, second}, std::nullopt, 800, 600);
  auto damage = lambda::compositor::updateSceneDamage(state, {second, first}, std::nullopt, 800, 600);

  CHECK(damage.fullOutput);
  REQUIRE(damage.rects.size() == 1);
  CHECK(rectEquals(damage.rects[0], RegionRect{.x = 0, .y = 0, .width = 800, .height = 600}));
}

TEST_CASE("scene damage detects relative order changes across added surfaces") {
  SceneDamageState state;
  auto first = surface(1, 0, 0, 100, 100);
  auto second = surface(2, 20, 20, 100, 100);
  auto third = surface(3, 40, 40, 100, 100);

  (void)lambda::compositor::updateSceneDamage(state, {first, second}, std::nullopt, 800, 600);
  auto damage = lambda::compositor::updateSceneDamage(state, {third, second, first}, std::nullopt, 800, 600);

  CHECK(damage.fullOutput);
}

TEST_CASE("scene damage includes old and new software cursor rectangles") {
  SceneDamageState state;
  auto cursor = surface(9, 20, 30, 16, 16);
  auto movedCursor = cursor;
  movedCursor.x = 40;
  movedCursor.y = 50;

  (void)lambda::compositor::updateSceneDamage(state, {}, cursor, 800, 600);
  auto damage = lambda::compositor::updateSceneDamage(state, {}, movedCursor, 800, 600);

  CHECK_FALSE(damage.fullOutput);
  CHECK(containsRect(damage.rects, RegionRect{.x = 20, .y = 30, .width = 16, .height = 16}));
  CHECK(containsRect(damage.rects, RegionRect{.x = 40, .y = 50, .width = 16, .height = 16}));
}

#ifdef LAMBDA_TESTS_HAVE_COMPOSITOR_SCENE_GRAPH
TEST_CASE("scene graph damage merges adjacent rects before the max rect fallback") {
  lambda::compositor::ChromeConfig chrome;
  chrome.glass.blurRadius = 0.f;
  chrome.focusedShadowRadius = 0.f;
  chrome.unfocusedShadowRadius = 0.f;
  auto first = surface(77, 0, 0, 800, 20, 1);
  std::vector<CommittedSurfaceSnapshot> surfaces{first};
  std::optional<CommittedSurfaceSnapshot> cursor;
  std::unordered_map<std::uint64_t, SurfaceVisualState> visuals;

  CompositorSceneGraphState state;
  auto initial = lambda::compositor::buildCompositorSceneFrame(
      state,
      CompositorSceneFrameInput{
          .duplicateDmabufFds = {},
          .chrome = chrome,
          .surfaceVisuals = visuals,
          .surfaces = surfaces,
          .softwareCursor = cursor,
          .logicalOutputWidth = 800,
          .logicalOutputHeight = 600,
          .animationsEnabled = false,
          .selectScanout = false,
      });

  auto next = first;
  next.serial = 2;
  for (std::int32_t i = 0; i < 100; ++i) {
    next.bufferDamageRects.push_back(RegionRect{.x = i * 8, .y = 0, .width = 8, .height = 10});
  }
  surfaces = {next};
  auto damagePlan = lambda::compositor::buildCompositorSceneFrame(
      initial.nextState,
      CompositorSceneFrameInput{
          .duplicateDmabufFds = {},
          .chrome = chrome,
          .surfaceVisuals = visuals,
          .surfaces = surfaces,
          .softwareCursor = cursor,
          .logicalOutputWidth = 800,
          .logicalOutputHeight = 600,
          .animationsEnabled = false,
          .selectScanout = false,
      });

  CHECK_FALSE(damagePlan.damage.fullOutput);
  REQUIRE(damagePlan.damage.rects.size() == 1);
  CHECK(rectEquals(damagePlan.damage.rects[0], RegionRect{.x = 0, .y = 0, .width = 800, .height = 10}));
}

TEST_CASE("scene graph damage with backdrop blur keeps small updates partial") {
  lambda::compositor::ChromeConfig chrome;
  chrome.glass.blurRadius = 32.f;
  chrome.focusedShadowRadius = 0.f;
  chrome.unfocusedShadowRadius = 0.f;
  auto first = surface(78, 100, 120, 200, 100, 1);
  first.serverSideDecorated = true;
  first.focused = true;
  first.titleBarHeight = chrome.titleBarHeight;
  std::vector<CommittedSurfaceSnapshot> surfaces{first};
  std::optional<CommittedSurfaceSnapshot> cursor;
  std::unordered_map<std::uint64_t, SurfaceVisualState> visuals;

  CompositorSceneGraphState state;
  auto initial = lambda::compositor::buildCompositorSceneFrame(
      state,
      CompositorSceneFrameInput{
          .duplicateDmabufFds = {},
          .chrome = chrome,
          .surfaceVisuals = visuals,
          .surfaces = surfaces,
          .softwareCursor = cursor,
          .logicalOutputWidth = 800,
          .logicalOutputHeight = 600,
          .animationsEnabled = false,
          .selectScanout = false,
      });

  auto next = first;
  next.serial = 2;
  next.bufferDamageRects.push_back(RegionRect{.x = 20, .y = 20, .width = 10, .height = 10});
  surfaces = {next};
  auto damagePlan = lambda::compositor::buildCompositorSceneFrame(
      initial.nextState,
      CompositorSceneFrameInput{
          .duplicateDmabufFds = {},
          .chrome = chrome,
          .surfaceVisuals = visuals,
          .surfaces = surfaces,
          .softwareCursor = cursor,
          .logicalOutputWidth = 800,
          .logicalOutputHeight = 600,
          .animationsEnabled = false,
          .selectScanout = false,
      });

  CHECK_FALSE(damagePlan.damage.fullOutput);
  REQUIRE(damagePlan.damage.rects.size() == 1);
  CHECK(rectEquals(damagePlan.damage.rects[0], RegionRect{.x = 70, .y = 90, .width = 110, .height = 110}));
}

TEST_CASE("scene graph damage inflates neighboring content updates around glass chrome edges") {
  lambda::compositor::ChromeConfig chrome;
  chrome.glass.blurRadius = 32.f;
  chrome.focusedShadowRadius = 0.f;
  chrome.unfocusedShadowRadius = 0.f;
  auto backing = surface(81, 40, 60, 500, 300, 1);
  backing.contentFullyOpaque = true;
  auto glass = surface(82, 140, 160, 220, 120, 1);
  glass.serverSideDecorated = true;
  glass.focused = true;
  glass.titleBarHeight = chrome.titleBarHeight;
  std::optional<CommittedSurfaceSnapshot> cursor;
  std::unordered_map<std::uint64_t, SurfaceVisualState> visuals;

  CompositorSceneGraphState state;
  auto initial = lambda::compositor::buildCompositorSceneFrame(
      state,
      CompositorSceneFrameInput{
          .duplicateDmabufFds = {},
          .chrome = chrome,
          .surfaceVisuals = visuals,
          .surfaces = std::vector<CommittedSurfaceSnapshot>{backing, glass},
          .softwareCursor = cursor,
          .logicalOutputWidth = 800,
          .logicalOutputHeight = 600,
          .animationsEnabled = false,
          .selectScanout = false,
      });

  auto damagedBacking = backing;
  damagedBacking.serial = 2;
  damagedBacking.bufferDamageRects.push_back(RegionRect{.x = 112, .y = 82, .width = 12, .height = 12});
  auto damagePlan = lambda::compositor::buildCompositorSceneFrame(
      initial.nextState,
      CompositorSceneFrameInput{
          .duplicateDmabufFds = {},
          .chrome = chrome,
          .surfaceVisuals = visuals,
          .surfaces = std::vector<CommittedSurfaceSnapshot>{damagedBacking, glass},
          .softwareCursor = cursor,
          .logicalOutputWidth = 800,
          .logicalOutputHeight = 600,
          .animationsEnabled = false,
          .selectScanout = false,
      });

  CHECK_FALSE(damagePlan.damage.fullOutput);
  REQUIRE(damagePlan.damage.rects.size() == 1);
  CHECK(rectEquals(damagePlan.damage.rects[0], RegionRect{.x = 102, .y = 92, .width = 112, .height = 112}));
  CHECK(anyRectCovers(damagePlan.damage.rects, RegionRect{.x = 140, .y = 132, .width = 24, .height = 28}));
}

TEST_CASE("scene graph opaque content damage does not require background fill") {
  lambda::compositor::ChromeConfig chrome;
  chrome.glass.blurRadius = 32.f;
  chrome.focusedShadowRadius = 0.f;
  chrome.unfocusedShadowRadius = 0.f;
  auto first = surface(80, 100, 120, 200, 100, 1);
  first.serverSideDecorated = true;
  first.focused = true;
  first.titleBarHeight = chrome.titleBarHeight;
  first.contentFullyOpaque = true;
  std::optional<CommittedSurfaceSnapshot> cursor;
  std::unordered_map<std::uint64_t, SurfaceVisualState> visuals;

  CompositorSceneGraphState state;
  auto initial = lambda::compositor::buildCompositorSceneFrame(
      state,
      CompositorSceneFrameInput{
          .duplicateDmabufFds = {},
          .chrome = chrome,
          .surfaceVisuals = visuals,
          .surfaces = std::vector<CommittedSurfaceSnapshot>{first},
          .softwareCursor = cursor,
          .logicalOutputWidth = 800,
          .logicalOutputHeight = 600,
          .animationsEnabled = false,
          .selectScanout = false,
      });

  auto opaqueNext = first;
  opaqueNext.serial = 2;
  opaqueNext.bufferDamageRects.push_back(RegionRect{.x = 20, .y = 20, .width = 10, .height = 10});
  auto opaquePlan = lambda::compositor::buildCompositorSceneFrame(
      initial.nextState,
      CompositorSceneFrameInput{
          .duplicateDmabufFds = {},
          .chrome = chrome,
          .surfaceVisuals = visuals,
          .surfaces = std::vector<CommittedSurfaceSnapshot>{opaqueNext},
          .softwareCursor = cursor,
          .logicalOutputWidth = 800,
          .logicalOutputHeight = 600,
          .animationsEnabled = false,
          .selectScanout = false,
      });

  CHECK_FALSE(opaquePlan.damage.fullOutput);
  CHECK_FALSE(opaquePlan.damage.backgroundFillRequired);

  auto transparentNext = first;
  transparentNext.contentFullyOpaque = false;
  transparentNext.serial = 3;
  transparentNext.bufferDamageRects.push_back(RegionRect{.x = 20, .y = 20, .width = 10, .height = 10});
  auto transparentPlan = lambda::compositor::buildCompositorSceneFrame(
      opaquePlan.nextState,
      CompositorSceneFrameInput{
          .duplicateDmabufFds = {},
          .chrome = chrome,
          .surfaceVisuals = visuals,
          .surfaces = std::vector<CommittedSurfaceSnapshot>{transparentNext},
          .softwareCursor = cursor,
          .logicalOutputWidth = 800,
          .logicalOutputHeight = 600,
          .animationsEnabled = false,
          .selectScanout = false,
      });

  CHECK_FALSE(transparentPlan.damage.fullOutput);
  CHECK(transparentPlan.damage.backgroundFillRequired);
}

TEST_CASE("scene graph damage with backdrop blur falls back when expanded damage is large") {
  lambda::compositor::ChromeConfig chrome;
  chrome.glass.blurRadius = 220.f;
  chrome.focusedShadowRadius = 0.f;
  chrome.unfocusedShadowRadius = 0.f;
  auto first = surface(79, 380, 260, 100, 100, 1);
  first.serverSideDecorated = true;
  first.focused = true;
  first.titleBarHeight = chrome.titleBarHeight;
  std::vector<CommittedSurfaceSnapshot> surfaces{first};
  std::optional<CommittedSurfaceSnapshot> cursor;
  std::unordered_map<std::uint64_t, SurfaceVisualState> visuals;

  CompositorSceneGraphState state;
  auto initial = lambda::compositor::buildCompositorSceneFrame(
      state,
      CompositorSceneFrameInput{
          .duplicateDmabufFds = {},
          .chrome = chrome,
          .surfaceVisuals = visuals,
          .surfaces = surfaces,
          .softwareCursor = cursor,
          .logicalOutputWidth = 800,
          .logicalOutputHeight = 600,
          .animationsEnabled = false,
          .selectScanout = false,
      });

  auto next = first;
  next.serial = 2;
  next.bufferDamageRects.push_back(RegionRect{.x = 10, .y = 10, .width = 20, .height = 20});
  surfaces = {next};
  auto damagePlan = lambda::compositor::buildCompositorSceneFrame(
      initial.nextState,
      CompositorSceneFrameInput{
          .duplicateDmabufFds = {},
          .chrome = chrome,
          .surfaceVisuals = visuals,
          .surfaces = surfaces,
          .softwareCursor = cursor,
          .logicalOutputWidth = 800,
          .logicalOutputHeight = 600,
          .animationsEnabled = false,
          .selectScanout = false,
      });

  CHECK(damagePlan.damage.fullOutput);
  REQUIRE(damagePlan.damage.rects.size() == 1);
  CHECK(rectEquals(damagePlan.damage.rects[0], RegionRect{.x = 0, .y = 0, .width = 800, .height = 600}));
}

TEST_CASE("scene damage for chrome hover is constrained to control bounds") {
  lambda::compositor::ChromeConfig chrome;
  chrome.glass.blurRadius = 0.f;
  chrome.focusedShadowRadius = 0.f;
  chrome.unfocusedShadowRadius = 0.f;
  auto first = surface(42, 100, 80, 320, 200, 7);
  first.serverSideDecorated = true;
  first.focused = true;
  first.titleBarHeight = chrome.titleBarHeight;
  std::vector<CommittedSurfaceSnapshot> surfaces{first};
  std::optional<CommittedSurfaceSnapshot> cursor;
  std::unordered_map<std::uint64_t, SurfaceVisualState> visuals;

  CompositorSceneGraphState state;
  auto initial = lambda::compositor::buildCompositorSceneFrame(
      state,
      CompositorSceneFrameInput{
          .duplicateDmabufFds = {},
          .chrome = chrome,
          .surfaceVisuals = visuals,
          .surfaces = surfaces,
          .softwareCursor = cursor,
          .logicalOutputWidth = 800,
          .logicalOutputHeight = 600,
          .animationsEnabled = false,
          .selectScanout = false,
      });

  auto hovered = first;
  hovered.closeButtonHovered = true;
  surfaces = {hovered};
  auto hoverPlan = lambda::compositor::buildCompositorSceneFrame(
      initial.nextState,
      CompositorSceneFrameInput{
          .duplicateDmabufFds = {},
          .chrome = chrome,
          .surfaceVisuals = visuals,
          .surfaces = surfaces,
          .softwareCursor = cursor,
          .logicalOutputWidth = 800,
          .logicalOutputHeight = 600,
          .animationsEnabled = false,
          .selectScanout = false,
      });

  lambda::Rect const titleRect = lambda::compositor::windowTitleBarRect(first, chrome.contentInsetWidth);
  lambda::compositor::ChromeControlRects const controlRects =
      lambda::compositor::chromeControlRects(chrome, titleRect.x, titleRect.y, titleRect.width, titleRect.height);
  float const controlsLeft =
      std::min({controlRects.minimizeButton.x, controlRects.maximizeButton.x, controlRects.closeButton.x});
  float const controlsTop =
      std::min({controlRects.minimizeButton.y, controlRects.maximizeButton.y, controlRects.closeButton.y});
  float const controlsRight =
      std::max({controlRects.minimizeButton.x + controlRects.minimizeButton.width,
                controlRects.maximizeButton.x + controlRects.maximizeButton.width,
                controlRects.closeButton.x + controlRects.closeButton.width});
  float const controlsBottom =
      std::max({controlRects.minimizeButton.y + controlRects.minimizeButton.height,
                controlRects.maximizeButton.y + controlRects.maximizeButton.height,
                controlRects.closeButton.y + controlRects.closeButton.height});
  RegionRect const controlsBounds =
      regionFromRect(lambda::Rect::sharp(controlsLeft,
                                         controlsTop,
                                         controlsRight - controlsLeft,
                                         controlsBottom - controlsTop));
  RegionRect const frameBounds =
      regionFromRect(lambda::compositor::windowFrameRect(first, chrome.contentInsetWidth));

  CHECK_FALSE(hoverPlan.damage.fullOutput);
  REQUIRE(hoverPlan.damage.rects.size() == 1);
  CHECK(rectContainedBy(hoverPlan.damage.rects[0], controlsBounds));
  CHECK_FALSE(rectEquals(hoverPlan.damage.rects[0], frameBounds));
}
#endif
