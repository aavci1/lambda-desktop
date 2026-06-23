#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Core/Color.hpp>
#include <Lambda/Reactive/Bindable.hpp>

#include <doctest/doctest.h>

#include <string>

using namespace lambdaui;
using namespace lambdaui::Reactive;

TEST_CASE("Reactive Bindable stores plain values") {
  Bindable<int> integer(42);
  CHECK(integer.isValue());
  CHECK_FALSE(integer.isReactive());
  CHECK(integer.evaluate() == 42);

  Bindable<float> floating(3.5f);
  CHECK(floating.evaluate() == doctest::Approx(3.5f));

  Bindable<std::string> text("hello");
  CHECK(text.evaluate() == "hello");
}

TEST_CASE("Reactive Bindable stores reactive closures") {
  int source = 4;
  Bindable<int> integer([&] {
    return source * 2;
  });

  CHECK(integer.isReactive());
  CHECK(integer.evaluate() == 8);
  source = 5;
  CHECK(integer.evaluate() == 10);
}

TEST_CASE("Reactive Bindable works with Lambda value types") {
  Bindable<Color> color(Color::hex(0x4080BF));
  CHECK(color.evaluate() == Color::hex(0x4080BF));

  Bindable<EdgeInsets> insets(EdgeInsets::uniform(12.0f));
  CHECK(insets.evaluate().top == doctest::Approx(12.0f));

  Bindable<Color> computedColor([] {
    return Color::hex(0xFFFFFF);
  });
  CHECK(computedColor.evaluate() == Color::hex(0xFFFFFF));
}

TEST_CASE("Reactive Bindable equality compares only concrete values") {
  Bindable<int> first(4);
  Bindable<int> second(4);
  Bindable<int> different(5);
  Bindable<int> reactive([] { return 4; });

  CHECK(first == second);
  CHECK_FALSE(first == different);
  CHECK_FALSE(first == reactive);
  CHECK_FALSE(reactive == first);
}
