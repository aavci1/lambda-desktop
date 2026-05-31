#include "Compositor/Wayland/LayerShellZones.hpp"

#include <doctest/doctest.h>

#include <vector>

TEST_CASE("layer shell reserved zones aggregate dock") {
  std::vector<lambda::compositor::LayerShellReservedZoneInput> layers{
      {.nameSpace = "lambda.dock",
       .exclusiveZone = 0,
       .anchor = lambda::compositor::kLayerShellAnchorBottom,
       .marginBottom = 8,
       .extent = 64},
  };
  auto const zones = lambda::compositor::aggregateLayerShellReservedZones(layers);
  CHECK(zones.dock == 72);
}

TEST_CASE("layer shell reserved zones ignore unrelated namespaces") {
  std::vector<lambda::compositor::LayerShellReservedZoneInput> layers{
      {.nameSpace = "com.example.panel", .exclusiveZone = 48},
  };
  auto const zones = lambda::compositor::aggregateLayerShellReservedZones(layers);
  CHECK(zones.dock == 0);
}

TEST_CASE("layer shell configure size resolves output-relative dimensions") {
  using namespace lambda::compositor;

  auto const horizontal = resolveLayerShellConfigureSize({
      .requestedWidth = 0,
      .requestedHeight = 36,
      .anchor = kLayerShellAnchorLeft | kLayerShellAnchorRight,
      .marginRight = 30,
      .marginLeft = 10,
      .outputWidth = 3840,
      .outputHeight = 2160,
  });
  CHECK(horizontal.width == 3800);
  CHECK(horizontal.height == 36);

  auto const vertical = resolveLayerShellConfigureSize({
      .requestedWidth = 64,
      .requestedHeight = 0,
      .anchor = kLayerShellAnchorTop | kLayerShellAnchorBottom,
      .marginTop = 12,
      .marginBottom = 20,
      .outputWidth = 3840,
      .outputHeight = 2160,
  });
  CHECK(vertical.width == 64);
  CHECK(vertical.height == 2128);

  auto const floating = resolveLayerShellConfigureSize({
      .requestedWidth = 0,
      .requestedHeight = 0,
      .anchor = kLayerShellAnchorTop,
      .outputWidth = 3840,
      .outputHeight = 2160,
  });
  CHECK(floating.width == 0);
  CHECK(floating.height == 0);
}
