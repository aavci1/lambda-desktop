#include <doctest/doctest.h>

#include "Platform/Linux/WaylandScrollAccumulator.hpp"

TEST_CASE("Wayland scroll accumulator batches horizontal and vertical axes") {
  lambda::WaylandScrollAccumulator scroll;

  scroll.addAxis(true, 3.f);
  scroll.addAxis(false, -5.f);

  auto delta = scroll.take();
  REQUIRE(delta.has_value());
  CHECK(delta->x == doctest::Approx(3.f));
  CHECK(delta->y == doctest::Approx(-5.f));
  CHECK_FALSE(scroll.pending());
  CHECK_FALSE(scroll.take().has_value());
}

TEST_CASE("Wayland scroll accumulator drops zero-sum frames") {
  lambda::WaylandScrollAccumulator scroll;

  scroll.addAxis(false, 4.f);
  scroll.addAxis(false, -4.f);

  CHECK_FALSE(scroll.take().has_value());
  CHECK_FALSE(scroll.pending());
}
