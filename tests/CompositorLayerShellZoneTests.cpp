#include "Compositor/Wayland/LayerShellZones.hpp"
#include "Compositor/Wayland/LayerShellState.hpp"
#include "Compositor/Wayland/WaylandServerImpl.hpp"

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
  CHECK(zones.dock == 80);
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

TEST_CASE("layer shell keyboard interactivity normalizes by protocol version") {
  using namespace lambda::compositor;

  CHECK_FALSE(layerShellSupportsOnDemandKeyboardInteractivity(
      ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND_SINCE_VERSION - 1));
  CHECK(layerShellSupportsOnDemandKeyboardInteractivity(
      ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND_SINCE_VERSION));

  CHECK(normalizeLayerShellKeyboardInteractivity(1, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE) ==
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
  CHECK(normalizeLayerShellKeyboardInteractivity(1, 1) ==
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
  CHECK(normalizeLayerShellKeyboardInteractivity(
            ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND_SINCE_VERSION - 1,
            ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND) ==
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
  CHECK(normalizeLayerShellKeyboardInteractivity(
            ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND_SINCE_VERSION,
            ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND) ==
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND);

  CHECK(validLayerShellKeyboardInteractivity(ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE));
  CHECK(validLayerShellKeyboardInteractivity(ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE));
  CHECK(validLayerShellKeyboardInteractivity(ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND));
  CHECK_FALSE(validLayerShellKeyboardInteractivity(ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND + 1));
}

TEST_CASE("layer shell pending state applies only on commit") {
  using namespace lambda::compositor;

  WaylandServer::Impl::LayerSurface layer;
  layer.width = 120;
  layer.height = 36;
  layer.anchor = kLayerShellAnchorBottom;

  layer.pending.width = 240;
  layer.pending.height = 48;
  layer.pending.sizeSet = true;
  layer.pending.anchor = kLayerShellAnchorBottom | kLayerShellAnchorLeft | kLayerShellAnchorRight;
  layer.pending.anchorSet = true;
  layer.pending.exclusiveZone = 48;
  layer.pending.exclusiveZoneSet = true;
  layer.pending.marginBottom = 8;
  layer.pending.marginSet = true;
  layer.pending.keyboardInteractivity = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE;
  layer.pending.keyboardInteractivitySet = true;

  CHECK(layer.width == 120);
  CHECK(layer.height == 36);
  CHECK(layer.anchor == kLayerShellAnchorBottom);

  LayerSurfaceCommitResult const result = applyLayerSurfacePendingState(&layer);

  CHECK(result.valid);
  CHECK(result.stateChanged);
  CHECK(result.configureNeeded);
  CHECK(layer.width == 240);
  CHECK(layer.height == 48);
  CHECK(layer.anchor == (kLayerShellAnchorBottom | kLayerShellAnchorLeft | kLayerShellAnchorRight));
  CHECK(layer.exclusiveZone == 48);
  CHECK(layer.marginBottom == 8);
  CHECK(layer.keyboardInteractivity == ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
  CHECK_FALSE(layer.pending.sizeSet);
  CHECK_FALSE(layer.pending.anchorSet);
  CHECK_FALSE(layer.pending.exclusiveZoneSet);
  CHECK_FALSE(layer.pending.marginSet);
  CHECK_FALSE(layer.pending.keyboardInteractivitySet);
}

TEST_CASE("layer shell pending state rejects omitted dimensions without opposing anchors") {
  using namespace lambda::compositor;

  WaylandServer::Impl::LayerSurface layer;
  layer.width = 120;
  layer.height = 36;
  layer.anchor = kLayerShellAnchorTop;
  layer.pending.width = 0;
  layer.pending.height = 36;
  layer.pending.sizeSet = true;

  LayerSurfaceCommitResult const result = applyLayerSurfacePendingState(&layer);

  CHECK_FALSE(result.valid);
  CHECK(layer.width == 120);
  CHECK(layer.pending.sizeSet);
}

TEST_CASE("layer shell zero size is valid when opposing anchors commit with it") {
  using namespace lambda::compositor;

  WaylandServer::Impl::LayerSurface layer;
  layer.width = 1;
  layer.height = 1;
  layer.anchor = kLayerShellAnchorBottom;
  layer.pending.width = 0;
  layer.pending.height = 0;
  layer.pending.sizeSet = true;
  layer.pending.anchor = kLayerShellAnchorTop | kLayerShellAnchorBottom |
                         kLayerShellAnchorLeft | kLayerShellAnchorRight;
  layer.pending.anchorSet = true;

  LayerSurfaceCommitResult const result = applyLayerSurfacePendingState(&layer);

  CHECK(result.valid);
  CHECK(result.stateChanged);
  CHECK(result.configureNeeded);
  CHECK(layer.width == 0);
  CHECK(layer.height == 0);
  CHECK(layer.anchor == (kLayerShellAnchorTop | kLayerShellAnchorBottom |
                         kLayerShellAnchorLeft | kLayerShellAnchorRight));
}

TEST_CASE("layer shell configure ack validates serials and applies on commit") {
  using namespace lambda::compositor;

  WaylandServer::Impl::LayerSurface layer;
  layer.width = 120;
  layer.height = 36;
  layer.pendingConfigures.push_back({.serial = 11, .width = 640, .height = 32});
  layer.pendingConfigures.push_back({.serial = 12, .width = 800, .height = 32});
  layer.latestConfigureSerial = 12;

  CHECK(ackLayerSurfaceConfigure(&layer, 10));
  CHECK_FALSE(layer.pending.configureAcked);
  CHECK_FALSE(ackLayerSurfaceConfigure(&layer, 13));

  CHECK(ackLayerSurfaceConfigure(&layer, 11));
  CHECK(layer.configured);
  CHECK(layer.pending.configureAcked);
  CHECK(layer.pending.configureSerial == 11);
  CHECK(layer.pending.configureWidth == 640);
  CHECK(layer.pending.configureHeight == 32);
  REQUIRE(layer.pendingConfigures.size() == 1);
  CHECK(layer.pendingConfigures.front().serial == 12);

  LayerSurfaceCommitResult const result = applyLayerSurfacePendingState(&layer);

  CHECK(result.valid);
  CHECK(result.stateChanged);
  CHECK(layer.configureSerial == 11);
  CHECK(layer.configureWidth == 640);
  CHECK(layer.configureHeight == 32);
  CHECK_FALSE(layer.pending.configureAcked);
}

TEST_CASE("layer shell map and unmap reset configure state") {
  using namespace lambda::compositor;

  WaylandServer::Impl::LayerSurface layer;
  layer.initialized = true;
  layer.configured = true;
  layer.configureSerial = 42;
  layer.configureWidth = 640;
  layer.configureHeight = 32;
  layer.latestConfigureSerial = 44;
  layer.pending.configureAcked = true;
  layer.pending.configureSerial = 43;
  layer.pending.configureWidth = 800;
  layer.pending.configureHeight = 32;
  layer.pendingConfigures.push_back({.serial = 44, .width = 800, .height = 32});

  CHECK(markLayerSurfaceMapped(&layer));
  CHECK(layer.mapped);
  CHECK_FALSE(markLayerSurfaceMapped(&layer));

  CHECK(resetLayerSurfaceForUnmap(&layer));
  CHECK_FALSE(layer.mapped);
  CHECK_FALSE(layer.configured);
  CHECK_FALSE(layer.initialized);
  CHECK(layer.configureSerial == 0);
  CHECK(layer.configureWidth == 0);
  CHECK(layer.configureHeight == 0);
  CHECK(layer.latestConfigureSerial == 44);
  CHECK_FALSE(layer.pending.configureAcked);
  CHECK(layer.pending.configureSerial == 0);
  CHECK(layer.pending.configureWidth == 0);
  CHECK(layer.pending.configureHeight == 0);
  CHECK(layer.pendingConfigures.empty());
  CHECK(ackLayerSurfaceConfigure(&layer, 44));
  CHECK_FALSE(layer.pending.configureAcked);
  CHECK_FALSE(resetLayerSurfaceForUnmap(&layer));
}
