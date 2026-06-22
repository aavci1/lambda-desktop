#include <doctest/doctest.h>

#include "Compositor/Surface/SurfaceUploadDamage.hpp"

#include <vector>

namespace {

using lambda::compositor::SurfaceUploadDamageRect;
using lambda::compositor::buildSurfaceUploadDamageRects;

bool rectEquals(SurfaceUploadDamageRect actual, SurfaceUploadDamageRect expected) {
  return actual.x == expected.x &&
         actual.y == expected.y &&
         actual.width == expected.width &&
         actual.height == expected.height;
}

} // namespace

TEST_CASE("surface upload damage merges adjacent rects") {
  std::vector<SurfaceUploadDamageRect> damage{
      {.x = 0, .y = 0, .width = 10, .height = 10},
      {.x = 10, .y = 0, .width = 10, .height = 10},
  };
  std::vector<SurfaceUploadDamageRect> upload;

  buildSurfaceUploadDamageRects(damage, upload, 100, 100);

  REQUIRE(upload.size() == 1);
  CHECK(rectEquals(upload[0], {.x = 0, .y = 0, .width = 20, .height = 10}));
}

TEST_CASE("surface upload damage clips and ignores empty rects") {
  std::vector<SurfaceUploadDamageRect> damage{
      {.x = -5, .y = -5, .width = 10, .height = 10},
      {.x = 25, .y = 0, .width = 10, .height = 10},
      {.x = 2, .y = 2, .width = 0, .height = 4},
  };
  std::vector<SurfaceUploadDamageRect> upload;

  buildSurfaceUploadDamageRects(damage, upload, 20, 20);

  REQUIRE(upload.size() == 1);
  CHECK(rectEquals(upload[0], {.x = 0, .y = 0, .width = 5, .height = 5}));
}

TEST_CASE("surface upload damage keeps sparse rects separate") {
  std::vector<SurfaceUploadDamageRect> damage{
      {.x = 0, .y = 0, .width = 10, .height = 10},
      {.x = 30, .y = 0, .width = 10, .height = 10},
  };
  std::vector<SurfaceUploadDamageRect> upload;

  buildSurfaceUploadDamageRects(damage, upload, 100, 100);

  REQUIRE(upload.size() == 2);
  CHECK(rectEquals(upload[0], {.x = 0, .y = 0, .width = 10, .height = 10}));
  CHECK(rectEquals(upload[1], {.x = 30, .y = 0, .width = 10, .height = 10}));
}
