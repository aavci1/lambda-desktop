#include "Compositor/OutputSelector.hpp"

#include <doctest/doctest.h>

#include <array>
#include <optional>
#include <string>

TEST_CASE("compositor output selector accepts names aliases and indexes") {
  std::array<std::string, 3> const outputs{"eDP-1", "DP-1", "HDMI-A-1"};

  CHECK(lambdaui::compositor::presentation::selectOutputNameIndex(outputs, std::nullopt) == 0u);
  CHECK(lambdaui::compositor::presentation::selectOutputNameIndex(outputs, std::string{"eDP-1"}) == 0u);
  CHECK(lambdaui::compositor::presentation::selectOutputNameIndex(outputs, std::string{"dp-1"}) == 1u);
  CHECK(lambdaui::compositor::presentation::selectOutputNameIndex(outputs, std::string{"primary"}) == 0u);
  CHECK(lambdaui::compositor::presentation::selectOutputNameIndex(outputs, std::string{"first"}) == 0u);
  CHECK(lambdaui::compositor::presentation::selectOutputNameIndex(outputs, std::string{"secondary"}) == 1u);
  CHECK(lambdaui::compositor::presentation::selectOutputNameIndex(outputs, std::string{"second"}) == 1u);
  CHECK(lambdaui::compositor::presentation::selectOutputNameIndex(outputs, std::string{"2"}) == 2u);
}

TEST_CASE("compositor output selector rejects invalid or ambiguous selectors") {
  std::array<std::string, 1> const oneOutput{"eDP-1"};
  std::array<std::string, 2> const twoOutputs{"eDP-1", "DP-1"};
  std::array<std::string, 0> const noOutputs{};

  CHECK_FALSE(lambdaui::compositor::presentation::selectOutputNameIndex(noOutputs, std::nullopt).has_value());
  CHECK_FALSE(lambdaui::compositor::presentation::selectOutputNameIndex(oneOutput, std::string{"secondary"}).has_value());
  CHECK_FALSE(lambdaui::compositor::presentation::selectOutputNameIndex(twoOutputs, std::string{"missing"}).has_value());
  CHECK_FALSE(lambdaui::compositor::presentation::selectOutputNameIndex(twoOutputs, std::string{"2"}).has_value());
  CHECK_FALSE(lambdaui::compositor::presentation::selectOutputNameIndex(twoOutputs, std::string{"1x"}).has_value());
}
