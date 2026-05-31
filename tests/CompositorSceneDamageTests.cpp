#include "Compositor/SceneDamage.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace {

using lambda::compositor::CommittedSurfaceSnapshot;
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
  CHECK(containsRect(damage.rects, RegionRect{.x = 10, .y = 12, .width = 100, .height = 108}));
  CHECK(containsRect(damage.rects, RegionRect{.x = 70, .y = 62, .width = 100, .height = 108}));
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
