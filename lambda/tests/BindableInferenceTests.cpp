#include <doctest/doctest.h>

#include <Lambda/Reactive/Signal.hpp>
#include <Lambda/UI/Element.hpp>
#include <Lambda/UI/Views/Rectangle.hpp>

TEST_CASE("view modifiers infer bindables from mixed values and closures") {
  lambda::Reactive::Signal<float> opacity{0.5f};

  auto sized = lambda::Rectangle{}.size(22.f, [] { return 18.f; });
  auto filled = lambda::Rectangle{}.fill(lambda::Colors::red);
  auto faded = lambda::Rectangle{}.opacity([opacity] { return opacity.get(); });

  CHECK(sized.modifiers() != nullptr);
  CHECK(filled.modifiers() != nullptr);
  CHECK(faded.modifiers() != nullptr);
}
