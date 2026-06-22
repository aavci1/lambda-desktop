#include "Compositor/Wayland/DecorationState.hpp"

#include "xdg-decoration-unstable-v1-server-protocol.h"

#include <doctest/doctest.h>

TEST_CASE("compositor decoration mode honors explicit client-side requests") {
  CHECK(lambda::compositor::xdgTitlebarModeForClientRequest(
            ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE,
            ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE,
            ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE) ==
        ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);

  CHECK(lambda::compositor::xdgTitlebarModeForClientRequest(
            ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE,
            ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE,
            ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE) ==
        ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);

  CHECK(lambda::compositor::defaultDecorationMode(ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE) ==
        ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

TEST_CASE("compositor controls cutout geometry is surface-local and clamped") {
  auto box = lambda::compositor::compositorControlsCutout(640, 480, 58, 28);
  CHECK(box.x == 582);
  CHECK(box.y == 0);
  CHECK(box.width == 58);
  CHECK(box.height == 28);
  CHECK(box.id == lambda::compositor::kCompositorControlsCutoutId);

  auto tiny = lambda::compositor::compositorControlsCutout(48, 24, 58, 28);
  CHECK(tiny.x == 0);
  CHECK(tiny.y == 0);
  CHECK(tiny.width == 48);
  CHECK(tiny.height == 24);
}

TEST_CASE("cutout configure sends initial and changed reservations only") {
  lambda::compositor::CutoutSendState state{};
  auto box = lambda::compositor::compositorControlsCutout(640, 480, 58, 28);
  CHECK(lambda::compositor::shouldSendCutoutConfigure(state, box));

  state.lastSent = true;
  state.lastX = box.x;
  state.lastY = box.y;
  state.lastWidth = box.width;
  state.lastHeight = box.height;
  CHECK_FALSE(lambda::compositor::shouldSendCutoutConfigure(state, box));
  CHECK(lambda::compositor::shouldSendCutoutConfigure(state, box, true));

  auto resized = lambda::compositor::compositorControlsCutout(800, 480, 58, 28);
  CHECK(lambda::compositor::shouldSendCutoutConfigure(state, resized));
}

TEST_CASE("cutout configure clears stale reservations when cutouts stop applying") {
  CHECK(lambda::compositor::shouldSendEmptyCutoutConfigure(true, true, false));
  CHECK_FALSE(lambda::compositor::shouldSendEmptyCutoutConfigure(false, true, false));
  CHECK_FALSE(lambda::compositor::shouldSendEmptyCutoutConfigure(true, false, false));
  CHECK_FALSE(lambda::compositor::shouldSendEmptyCutoutConfigure(true, true, true));
}

TEST_CASE("cutout configure waits for the first usable surface size") {
  CHECK_FALSE(lambda::compositor::shouldSendInitialCutoutConfigure(true, false, false, 0, 0));
  CHECK_FALSE(lambda::compositor::shouldSendInitialCutoutConfigure(true, false, true, 640, 480));
  CHECK_FALSE(lambda::compositor::shouldSendInitialCutoutConfigure(true, true, false, 640, 480));
  CHECK(lambda::compositor::shouldSendInitialCutoutConfigure(true, false, false, 640, 480));
}

TEST_CASE("live cutout object is defunct when its toplevel is destroyed first") {
  CHECK(lambda::compositor::shouldReportDefunctCutoutsOnToplevelDestroy(true));
  CHECK_FALSE(lambda::compositor::shouldReportDefunctCutoutsOnToplevelDestroy(false));
}
