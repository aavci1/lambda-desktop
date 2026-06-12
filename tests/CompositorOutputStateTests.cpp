#include "Compositor/Wayland/OutputState.hpp"

#include <doctest/doctest.h>

namespace lambda::compositor {

TEST_CASE("output resources use the implemented protocol version") {
  CHECK(kOutputVersion == 4);
  CHECK(outputResourceVersion(1) == 1);
  CHECK(outputResourceVersion(4) == 4);
  CHECK(outputResourceVersion(5) == 4);
}

TEST_CASE("legacy output scale rounds fractional scales up") {
  CHECK(outputIntegerScale(0.5f) == 1);
  CHECK(outputIntegerScale(1.0f) == 1);
  CHECK(outputIntegerScale(1.25f) == 2);
  CHECK(outputIntegerScale(1.5f) == 2);
  CHECK(outputIntegerScale(2.0f) == 2);
  CHECK(outputIntegerScale(2.25f) == 3);
  CHECK(outputIntegerScale(4.0f) == 4);
}

TEST_CASE("selected output layout carries logical position scale and transform") {
  OutputLayoutBox const current = selectedOutputLayoutBox(3840, 2160, 2.0f);
  CHECK(current.x == 0);
  CHECK(current.y == 0);
  CHECK(current.width == 1920);
  CHECK(current.height == 1080);
  CHECK(current.scale == 2.0f);
  CHECK(current.transform == 0);

  OutputLayoutBox const future = selectedOutputLayoutBox(2560, 1440, 1.25f, 1920, 0, 90);
  CHECK(future.x == 1920);
  CHECK(future.y == 0);
  CHECK(future.width == 2048);
  CHECK(future.height == 1152);
  CHECK(future.scale == 1.25f);
  CHECK(future.transform == 90);
}

} // namespace lambda::compositor
