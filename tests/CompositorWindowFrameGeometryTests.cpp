#include "Compositor/Chrome/WindowFrameGeometry.hpp"

#include <doctest/doctest.h>

namespace {

flux::compositor::CommittedSurfaceSnapshot decoratedSurface() {
  return flux::compositor::CommittedSurfaceSnapshot{
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

  CHECK(flux::compositor::windowExternalTitleBarHeight(surface) == doctest::Approx(32.f));
  CHECK(flux::compositor::windowContentRect(surface) == flux::Rect::sharp(40.f, 100.f, 640.f, 480.f));
  CHECK(flux::compositor::windowTitleBarRect(surface) == flux::Rect::sharp(40.f, 68.f, 640.f, 32.f));
  CHECK(flux::compositor::windowFrameRect(surface) == flux::Rect::sharp(40.f, 68.f, 640.f, 512.f));

  flux::CornerRadius const frameRadius{6.f, 7.f, 8.f, 9.f};
  CHECK(flux::compositor::windowTitleBarCornerRadius(frameRadius) ==
        flux::CornerRadius{6.f, 7.f, 0.f, 0.f});
  CHECK(flux::compositor::windowContentCornerRadius(surface, frameRadius) ==
        flux::CornerRadius{0.f, 0.f, 8.f, 9.f});
}

TEST_CASE("window frame geometry treats cutout and undecorated windows as content frames") {
  auto cutout = decoratedSurface();
  cutout.cutoutsBound = true;

  CHECK(flux::compositor::windowUsesCutoutChrome(cutout));
  CHECK(flux::compositor::windowExternalTitleBarHeight(cutout) == doctest::Approx(0.f));
  CHECK(flux::compositor::windowFrameRect(cutout) == flux::Rect::sharp(40.f, 100.f, 640.f, 480.f));

  flux::CornerRadius const frameRadius{6.f, 7.f, 8.f, 9.f};
  CHECK(flux::compositor::windowContentCornerRadius(cutout, frameRadius) == frameRadius);

  auto undecorated = decoratedSurface();
  undecorated.serverSideDecorated = false;
  CHECK(flux::compositor::windowExternalTitleBarHeight(undecorated) == doctest::Approx(0.f));
  CHECK(flux::compositor::windowFrameRect(undecorated) == flux::Rect::sharp(40.f, 100.f, 640.f, 480.f));
  CHECK(flux::compositor::windowContentCornerRadius(undecorated, frameRadius) == frameRadius);
}

TEST_CASE("window shadow layer expands from the same rounded frame") {
  flux::Rect const frame = flux::Rect::sharp(40.f, 68.f, 640.f, 512.f);
  flux::CornerRadius const frameRadius{6.f, 7.f, 8.f, 9.f};
  flux::ShadowStyle const shadow{
      .radius = 30.f,
      .offset = {0.f, 14.f},
      .color = flux::Color{0.f, 0.f, 0.f, 0.35f},
  };

  auto const layer = flux::compositor::windowShadowLayerGeometry(frame, frameRadius, shadow, 30.f);

  CHECK(layer.rect == flux::Rect::sharp(10.f, 52.f, 700.f, 572.f));
  CHECK(layer.cornerRadius == flux::CornerRadius{36.f, 37.f, 52.f, 53.f});
}
