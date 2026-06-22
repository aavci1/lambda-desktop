#include "Compositor/Chrome/WindowFrameGeometry.hpp"

#include <doctest/doctest.h>

namespace {

lambda::compositor::CommittedSurfaceSnapshot decoratedSurface() {
  return lambda::compositor::CommittedSurfaceSnapshot{
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

  CHECK(lambda::compositor::windowExternalTitleBarHeight(surface) == doctest::Approx(32.f));
  CHECK(lambda::compositor::windowContentRect(surface) == lambda::Rect::sharp(40.f, 100.f, 640.f, 480.f));
  CHECK(lambda::compositor::windowTitleBarRect(surface) == lambda::Rect::sharp(40.f, 68.f, 640.f, 32.f));
  CHECK(lambda::compositor::windowFrameRect(surface) == lambda::Rect::sharp(40.f, 68.f, 640.f, 512.f));

  lambda::CornerRadius const frameRadius{6.f, 7.f, 8.f, 9.f};
  CHECK(lambda::compositor::windowTitleBarCornerRadius(frameRadius) ==
        lambda::CornerRadius{6.f, 7.f, 0.f, 0.f});
  CHECK(lambda::compositor::windowContentCornerRadius(surface, frameRadius) ==
        lambda::CornerRadius{0.f, 0.f, 8.f, 9.f});
}

TEST_CASE("window frame geometry keeps chrome outset outside content") {
  auto surface = decoratedSurface();

  CHECK(lambda::compositor::windowContentRect(surface) == lambda::Rect::sharp(40.f, 100.f, 640.f, 480.f));
  CHECK(lambda::compositor::windowTitleBarRect(surface, 4.f) == lambda::Rect::sharp(36.f, 68.f, 648.f, 32.f));
  CHECK(lambda::compositor::windowFrameRect(surface, 4.f) == lambda::Rect::sharp(36.f, 68.f, 648.f, 516.f));
  CHECK(lambda::compositor::windowVisibleContentRect(surface, 4.f) ==
        lambda::Rect::sharp(40.f, 100.f, 640.f, 480.f));
}

TEST_CASE("window frame geometry treats cutout and undecorated windows as content frames") {
  auto cutout = decoratedSurface();
  cutout.cutoutsBound = true;

  CHECK(lambda::compositor::windowUsesCutoutChrome(cutout));
  CHECK(lambda::compositor::windowExternalTitleBarHeight(cutout) == doctest::Approx(0.f));
  CHECK(lambda::compositor::windowFrameRect(cutout) == lambda::Rect::sharp(40.f, 100.f, 640.f, 480.f));

  lambda::CornerRadius const frameRadius{6.f, 7.f, 8.f, 9.f};
  CHECK(lambda::compositor::windowContentCornerRadius(cutout, frameRadius) == frameRadius);

  auto undecorated = decoratedSurface();
  undecorated.serverSideDecorated = false;
  CHECK(lambda::compositor::windowExternalTitleBarHeight(undecorated) == doctest::Approx(0.f));
  CHECK(lambda::compositor::windowFrameRect(undecorated) == lambda::Rect::sharp(40.f, 100.f, 640.f, 480.f));
  CHECK(lambda::compositor::windowContentCornerRadius(undecorated, frameRadius) == frameRadius);
}

TEST_CASE("window shadow layer expands from the same rounded frame") {
  lambda::Rect const frame = lambda::Rect::sharp(40.f, 68.f, 640.f, 512.f);
  lambda::CornerRadius const frameRadius{6.f, 7.f, 8.f, 9.f};
  lambda::ShadowStyle const shadow{
      .radius = 30.f,
      .offset = {0.f, 14.f},
      .color = lambda::Color{0.f, 0.f, 0.f, 0.35f},
  };

  auto const layer = lambda::compositor::windowShadowLayerGeometry(frame, frameRadius, shadow, 30.f);

  CHECK(layer.rect == lambda::Rect::sharp(10.f, 52.f, 700.f, 572.f));
  CHECK(layer.cornerRadius == lambda::CornerRadius{36.f, 37.f, 52.f, 53.f});
}
