#include "Graphics/Linux/FreeTypeTextSystem.hpp"

#include <Lambda/Graphics/TextLayoutOptions.hpp>
#include <doctest/doctest.h>

using namespace lambdaui;

TEST_CASE("FreeTypeTextSystem reports line boxes from resolved line height") {
  FreeTypeTextSystem textSystem([] {
    return "lambda-tests";
  });

  TextLayoutOptions options{};
  options.wrapping = TextWrapping::NoWrap;
  options.lineHeight = 30.f;

  auto layout = textSystem.layout("Agy", Font::body(), Colors::black, 0.f, options);
  REQUIRE(layout);
  REQUIRE(layout->lines.size() == 1);
  CHECK(layout->lines.front().top == doctest::Approx(0.f));
  CHECK(layout->lines.front().bottom == doctest::Approx(30.f));
  CHECK(layout->measuredSize.height == doctest::Approx(30.f));
  CHECK(layout->firstBaseline == doctest::Approx(layout->lines.front().baseline));
}

TEST_CASE("FreeTypeTextSystem centers boxed text using line box height") {
  FreeTypeTextSystem textSystem([] {
    return "lambda-tests";
  });

  TextLayoutOptions options{};
  options.wrapping = TextWrapping::NoWrap;
  options.verticalAlignment = VerticalAlignment::Center;
  options.lineHeight = 30.f;

  Rect const box = Rect::sharp(0.f, 0.f, 200.f, 50.f);
  TextSystem& boxedTextSystem = textSystem;
  auto layout = boxedTextSystem.layout("Agy", Font::body(), Colors::black, box, options);
  REQUIRE(layout);
  REQUIRE(layout->lines.size() == 1);
  CHECK(layout->lines.front().top == doctest::Approx(10.f));
  CHECK(layout->lines.front().bottom == doctest::Approx(40.f));
  CHECK(layout->measuredSize.height == doctest::Approx(30.f));
}
