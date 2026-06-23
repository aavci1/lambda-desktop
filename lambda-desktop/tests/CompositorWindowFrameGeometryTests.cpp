#include "Compositor/Chrome/WindowFrameGeometry.hpp"

#include <doctest/doctest.h>

namespace {

lambdaui::compositor::CommittedSurfaceSnapshot decoratedSurface() {
  return lambdaui::compositor::CommittedSurfaceSnapshot{
      .x = 40,
      .y = 100,
      .width = 640,
      .height = 480,
      .titleBarHeight = 32,
      .serverSideDecorated = true,
  };
}

} // namespace

TEST_CASE("window frame geometry includes system titlebar") {
  auto surface = decoratedSurface();

  CHECK(lambdaui::compositor::windowExternalTitleBarHeight(surface) == doctest::Approx(32.f));
  CHECK(lambdaui::compositor::windowContentRect(surface) == lambdaui::Rect::sharp(40.f, 100.f, 640.f, 480.f));
  CHECK(lambdaui::compositor::windowTitleBarRect(surface) == lambdaui::Rect::sharp(40.f, 68.f, 640.f, 32.f));
  CHECK(lambdaui::compositor::windowFrameRect(surface) == lambdaui::Rect::sharp(40.f, 68.f, 640.f, 512.f));

  lambdaui::CornerRadius const frameRadius{6.f, 7.f, 8.f, 9.f};
  CHECK(lambdaui::compositor::windowTitleBarCornerRadius(frameRadius) ==
        lambdaui::CornerRadius{6.f, 7.f, 0.f, 0.f});
  CHECK(lambdaui::compositor::windowContentCornerRadius(surface, frameRadius) ==
        lambdaui::CornerRadius{0.f, 0.f, 8.f, 9.f});
}

TEST_CASE("window frame geometry keeps chrome outset outside content") {
  auto surface = decoratedSurface();

  CHECK(lambdaui::compositor::windowContentRect(surface) == lambdaui::Rect::sharp(40.f, 100.f, 640.f, 480.f));
  CHECK(lambdaui::compositor::windowTitleBarRect(surface, 4.f) == lambdaui::Rect::sharp(36.f, 68.f, 648.f, 32.f));
  CHECK(lambdaui::compositor::windowFrameRect(surface, 4.f) == lambdaui::Rect::sharp(36.f, 68.f, 648.f, 516.f));
  CHECK(lambdaui::compositor::windowVisibleContentRect(surface, 4.f) ==
        lambdaui::Rect::sharp(40.f, 100.f, 640.f, 480.f));
}

TEST_CASE("window frame geometry treats cutout and undecorated windows as content frames") {
  auto cutout = decoratedSurface();
  cutout.cutoutsBound = true;

  CHECK(lambdaui::compositor::windowUsesCutoutChrome(cutout));
  CHECK(lambdaui::compositor::windowExternalTitleBarHeight(cutout) == doctest::Approx(0.f));
  CHECK(lambdaui::compositor::windowFrameRect(cutout) == lambdaui::Rect::sharp(40.f, 100.f, 640.f, 480.f));

  lambdaui::CornerRadius const frameRadius{6.f, 7.f, 8.f, 9.f};
  CHECK(lambdaui::compositor::windowContentCornerRadius(cutout, frameRadius) == frameRadius);

  auto undecorated = decoratedSurface();
  undecorated.serverSideDecorated = false;
  CHECK(lambdaui::compositor::windowExternalTitleBarHeight(undecorated) == doctest::Approx(0.f));
  CHECK(lambdaui::compositor::windowFrameRect(undecorated) == lambdaui::Rect::sharp(40.f, 100.f, 640.f, 480.f));
  CHECK(lambdaui::compositor::windowContentCornerRadius(undecorated, frameRadius) == frameRadius);
}

TEST_CASE("window shadow layer expands from the same rounded frame") {
  lambdaui::Rect const frame = lambdaui::Rect::sharp(40.f, 68.f, 640.f, 512.f);
  lambdaui::CornerRadius const frameRadius{6.f, 7.f, 8.f, 9.f};
  lambdaui::ShadowStyle const shadow{
      .radius = 30.f,
      .offset = {0.f, 14.f},
      .color = lambdaui::Color{0.f, 0.f, 0.f, 0.35f},
  };

  auto const layer = lambdaui::compositor::windowShadowLayerGeometry(frame, frameRadius, shadow, 30.f);

  CHECK(layer.rect == lambdaui::Rect::sharp(10.f, 52.f, 700.f, 572.f));
  CHECK(layer.cornerRadius == lambdaui::CornerRadius{36.f, 37.f, 52.f, 53.f});
}
