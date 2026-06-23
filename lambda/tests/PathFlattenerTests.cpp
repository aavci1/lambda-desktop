#include "Graphics/PathFlattener.hpp"

#include <Lambda/Graphics/Path.hpp>

#include <doctest/doctest.h>

using namespace lambdaui;

TEST_CASE("PathFlattener flattens rounded rect commands without recursion") {
  Path path;
  path.rect(Rect{0.f, 0.f, 24.f, 12.f}, CornerRadius{4.f});

  auto subpaths = PathFlattener::flattenSubpaths(path);

  REQUIRE(subpaths.size() == 1);
  CHECK(subpaths[0].size() > 4);
  CHECK(subpaths[0].size() < 128);
  CHECK(subpaths[0].front() == subpaths[0].back());
}

TEST_CASE("PathFlattener flattens sharp rect commands as one closed contour") {
  Path path;
  path.rect(Rect{2.f, 3.f, 10.f, 8.f});

  auto subpaths = PathFlattener::flattenSubpaths(path);

  REQUIRE(subpaths.size() == 1);
  REQUIRE(subpaths[0].size() == 5);
  CHECK(subpaths[0][0] == Point{2.f, 3.f});
  CHECK(subpaths[0][1] == Point{12.f, 3.f});
  CHECK(subpaths[0][2] == Point{12.f, 11.f});
  CHECK(subpaths[0][3] == Point{2.f, 11.f});
  CHECK(subpaths[0][4] == Point{2.f, 3.f});
}
