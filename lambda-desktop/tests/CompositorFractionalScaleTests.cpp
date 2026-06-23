#include "Compositor/Wayland/FractionalScaleState.hpp"

#include <doctest/doctest.h>

namespace lambdaui::compositor {

TEST_CASE("fractional scale preferred scale keeps sub-1 output scales") {
  CHECK(fractionalScalePreferredScale120(0.5f) == 60);
  CHECK(fractionalScalePreferredScale120(0.75f) == 90);
  CHECK(fractionalScalePreferredScale120(1.0f) == 120);
  CHECK(fractionalScalePreferredScale120(1.25f) == 150);
  CHECK(fractionalScalePreferredScale120(4.5f) == 480);
}

TEST_CASE("fractional scale resources use the implemented protocol version") {
  CHECK(kFractionalScaleVersion == 1);
  CHECK(fractionalScaleResourceVersion(1) == 1);
  CHECK(fractionalScaleResourceVersion(2) == 1);
}

} // namespace lambdaui::compositor
