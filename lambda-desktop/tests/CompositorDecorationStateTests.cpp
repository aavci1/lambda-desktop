#include "Compositor/Wayland/DecorationState.hpp"

#include "xdg-decoration-unstable-v1-server-protocol.h"

#include <doctest/doctest.h>

TEST_CASE("compositor decoration mode honors explicit client-side requests") {
  CHECK(lambdaui::compositor::xdgTitlebarModeForClientRequest(
            ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE,
            ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE,
            ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE) ==
        ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);

  CHECK(lambdaui::compositor::xdgTitlebarModeForClientRequest(
            ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE,
            ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE,
            ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE) ==
        ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);

  CHECK(lambdaui::compositor::defaultDecorationMode(ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE) ==
        ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

TEST_CASE("compositor controls cutout geometry is surface-local and clamped") {
  auto box = lambdaui::compositor::compositorControlsCutout(640, 480, 58, 28);
  CHECK(box.x == 582);
  CHECK(box.y == 0);
  CHECK(box.width == 58);
  CHECK(box.height == 28);
  CHECK(box.id == lambdaui::compositor::kCompositorControlsCutoutId);

  auto tiny = lambdaui::compositor::compositorControlsCutout(48, 24, 58, 28);
  CHECK(tiny.x == 0);
  CHECK(tiny.y == 0);
  CHECK(tiny.width == 48);
  CHECK(tiny.height == 24);
}

TEST_CASE("cutout configure sends initial and changed reservations only") {
  lambdaui::compositor::CutoutSendState state{};
  auto box = lambdaui::compositor::compositorControlsCutout(640, 480, 58, 28);
  CHECK(lambdaui::compositor::shouldSendCutoutConfigure(state, box));

  state.lastSent = true;
  state.lastX = box.x;
  state.lastY = box.y;
  state.lastWidth = box.width;
  state.lastHeight = box.height;
  CHECK_FALSE(lambdaui::compositor::shouldSendCutoutConfigure(state, box));
  CHECK(lambdaui::compositor::shouldSendCutoutConfigure(state, box, true));

  auto resized = lambdaui::compositor::compositorControlsCutout(800, 480, 58, 28);
  CHECK(lambdaui::compositor::shouldSendCutoutConfigure(state, resized));
}

TEST_CASE("cutout configure clears stale reservations when cutouts stop applying") {
  CHECK(lambdaui::compositor::shouldSendEmptyCutoutConfigure(true, true, false));
  CHECK_FALSE(lambdaui::compositor::shouldSendEmptyCutoutConfigure(false, true, false));
  CHECK_FALSE(lambdaui::compositor::shouldSendEmptyCutoutConfigure(true, false, false));
  CHECK_FALSE(lambdaui::compositor::shouldSendEmptyCutoutConfigure(true, true, true));
}

TEST_CASE("cutout configure waits for the first usable surface size") {
  CHECK_FALSE(lambdaui::compositor::shouldSendInitialCutoutConfigure(true, false, false, 0, 0));
  CHECK_FALSE(lambdaui::compositor::shouldSendInitialCutoutConfigure(true, false, true, 640, 480));
  CHECK_FALSE(lambdaui::compositor::shouldSendInitialCutoutConfigure(true, true, false, 640, 480));
  CHECK(lambdaui::compositor::shouldSendInitialCutoutConfigure(true, false, false, 640, 480));
}

TEST_CASE("live cutout object is defunct when its toplevel is destroyed first") {
  CHECK(lambdaui::compositor::shouldReportDefunctCutoutsOnToplevelDestroy(true));
  CHECK_FALSE(lambdaui::compositor::shouldReportDefunctCutoutsOnToplevelDestroy(false));
}
