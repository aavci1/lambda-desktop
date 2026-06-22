#include <doctest/doctest.h>

#include <Lambda/UI/Detail/RootHolder.hpp>
#include <Lambda/Graphics/TextSystem.hpp>
#include <Lambda/Reactive/Scope.hpp>
#include <Lambda/Reactive/Signal.hpp>
#include <Lambda/SceneGraph/SceneNode.hpp>
#include <Lambda/SceneGraph/RectNode.hpp>
#include <Lambda/SceneGraph/SceneGraph.hpp>
#include <Lambda/UI/Hooks.hpp>
#include <Lambda/UI/MeasureContext.hpp>
#include <Lambda/UI/MountRoot.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/Views/For.hpp>
#include <Lambda/UI/Views/Rectangle.hpp>
#include <Lambda/UI/Views/VStack.hpp>

#include <memory>
#include <string_view>
#include <vector>

namespace {

class FakeTextSystem final : public lambda::TextSystem {
public:
  std::shared_ptr<lambda::TextLayout const>
  layout(lambda::AttributedString const&, float, lambda::TextLayoutOptions const&) override {
    return std::make_shared<lambda::TextLayout>();
  }

  std::shared_ptr<lambda::TextLayout const>
  layout(std::string_view, lambda::Font const&, lambda::Color const&, float,
         lambda::TextLayoutOptions const&) override {
    return std::make_shared<lambda::TextLayout>();
  }

  lambda::Size measure(lambda::AttributedString const&, float,
                     lambda::TextLayoutOptions const&) override {
    return {0.f, 0.f};
  }

  lambda::Size measure(std::string_view, lambda::Font const&, lambda::Color const&, float,
                     lambda::TextLayoutOptions const&) override {
    return {0.f, 0.f};
  }

  std::uint32_t resolveFontId(std::string_view, float, bool) override { return 0; }

  std::vector<std::uint8_t> rasterizeGlyph(std::uint32_t, std::uint32_t, float,
                                           std::uint32_t& outWidth,
                                           std::uint32_t& outHeight,
                                           lambda::Point& outBearing) override {
    outWidth = 0;
    outHeight = 0;
    outBearing = {};
    return {};
  }
};

lambda::EnvironmentBinding testEnvironment() {
  return lambda::EnvironmentBinding{}.withValue<lambda::ThemeKey>(lambda::Theme::light());
}

lambda::scenegraph::SceneNode const& rootGroup(lambda::scenegraph::SceneGraph const& sceneGraph) {
  REQUIRE(sceneGraph.root().kind() == lambda::scenegraph::SceneNodeKind::Group);
  return sceneGraph.root();
}

} // namespace

TEST_CASE("For preserves row scopes and scene nodes across reorder") {
  struct Root {
    lambda::Reactive::Signal<std::vector<int>> items;
    int* created = nullptr;
    int* disposed = nullptr;

    lambda::Element body() const {
      return lambda::For(
          items,
          [](int value) { return value; },
          [created = created, disposed = disposed](int value,
                                                   lambda::Reactive::Signal<std::size_t> index) {
            auto local = lambda::useState(value * 10);
            (void)local;
            ++*created;
            lambda::Reactive::onCleanup([disposed] {
              ++*disposed;
            });

            lambda::Reactive::Bindable<float> width{[index] {
              return 20.f + static_cast<float>(index.get());
            }};
            return lambda::Element{lambda::Rectangle{}}
                .size(std::move(width), 8.f)
                .fill(lambda::Colors::blue);
          },
          2.f);
    }
  };

  int created = 0;
  int disposed = 0;
  lambda::Reactive::Signal<std::vector<int>> items{{1, 2, 3}};
  FakeTextSystem textSystem;
  lambda::scenegraph::SceneGraph sceneGraph;
  lambda::MountRoot root{
      std::make_unique<lambda::TypedRootHolder<Root>>(std::in_place, Root{items, &created, &disposed}),
      textSystem,
      testEnvironment(),
      lambda::Size{240.f, 160.f},
  };

  root.mount(sceneGraph);

  auto const& initial = rootGroup(sceneGraph);
  REQUIRE(initial.children().size() == 3);
  CHECK(created == 3);
  CHECK(disposed == 0);
  auto* first = initial.children()[0].get();
  auto* second = initial.children()[1].get();
  auto* third = initial.children()[2].get();

  items.set({3, 2, 1});

  auto const& reordered = rootGroup(sceneGraph);
  REQUIRE(reordered.children().size() == 3);
  CHECK(created == 3);
  CHECK(disposed == 0);
  CHECK(reordered.children()[0].get() == third);
  CHECK(reordered.children()[1].get() == second);
  CHECK(reordered.children()[2].get() == first);
  CHECK(reordered.children()[0]->size().width == doctest::Approx(20.f));
  CHECK(reordered.children()[1]->size().width == doctest::Approx(21.f));
  CHECK(reordered.children()[2]->size().width == doctest::Approx(22.f));

  items.set({3, 1});

  auto const& removed = rootGroup(sceneGraph);
  REQUIRE(removed.children().size() == 2);
  CHECK(created == 3);
  CHECK(disposed == 1);
  CHECK(removed.children()[0].get() == third);
  CHECK(removed.children()[1].get() == first);

  items.set({4, 5});

  auto const& replaced = rootGroup(sceneGraph);
  REQUIRE(replaced.children().size() == 2);
  CHECK(created == 5);
  CHECK(disposed == 3);

  root.unmount(sceneGraph);
  CHECK(disposed == 5);
}

TEST_CASE("For empty stack child stays mounted and lays out inserted rows") {
  struct Root {
    lambda::Reactive::Signal<std::vector<int>> items;

    lambda::Element body() const {
      return lambda::VStack{
          .spacing = 12.f,
          .children = lambda::children(
              lambda::Element{lambda::Rectangle{}}
                  .size(20.f, 10.f)
                  .fill(lambda::Colors::red),
              lambda::For(
                  items,
                  [](int value) { return value; },
                  [](int) {
                    return lambda::Element{lambda::Rectangle{}}
                        .size(20.f, 8.f)
                        .fill(lambda::Colors::blue);
                  }),
              lambda::Element{lambda::Rectangle{}}
                  .size(20.f, 10.f)
                  .fill(lambda::Colors::green)),
      };
    }
  };

  lambda::Reactive::Signal<std::vector<int>> items{{}};
  FakeTextSystem textSystem;
  lambda::scenegraph::SceneGraph sceneGraph;
  lambda::MountRoot root{
      std::make_unique<lambda::TypedRootHolder<Root>>(std::in_place, Root{items}),
      textSystem,
      testEnvironment(),
      lambda::Size{240.f, 160.f},
  };

  root.mount(sceneGraph);

  auto const& hidden = rootGroup(sceneGraph);
  REQUIRE(hidden.children().size() == 3);
  CHECK(hidden.children()[1]->size().height == doctest::Approx(0.f));
  CHECK(hidden.children()[2]->position().y == doctest::Approx(22.f));

  items.set({7});

  auto const& shown = rootGroup(sceneGraph);
  REQUIRE(shown.children().size() == 3);
  CHECK(shown.children()[1]->position().y == doctest::Approx(22.f));
  CHECK(shown.children()[1]->size().height == doctest::Approx(8.f));
  CHECK(shown.children()[2]->position().y == doctest::Approx(42.f));
}

TEST_CASE("For measures retained rows without rebuilding factory output") {
  struct Root {
    lambda::Reactive::Signal<std::vector<int>> items;
    int* factoryCalls = nullptr;

    lambda::Element body() const {
      return lambda::VStack{
          .spacing = 12.f,
          .children = lambda::children(
              lambda::Element{lambda::Rectangle{}}
                  .size(20.f, 10.f)
                  .fill(lambda::Colors::red),
              lambda::For(
                  items,
                  [](int value) { return value; },
                  [factoryCalls = factoryCalls](int) {
                    ++*factoryCalls;
                    return lambda::Element{lambda::Rectangle{}}
                        .size(20.f, 8.f)
                        .fill(lambda::Colors::blue);
                  },
                  2.f),
              lambda::Element{lambda::Rectangle{}}
                  .size(20.f, 10.f)
                  .fill(lambda::Colors::green)),
      };
    }
  };

  int factoryCalls = 0;
  int measureOnlyFactoryCalls = 0;
  lambda::Reactive::Signal<std::vector<int>> items{{1, 2}};
  FakeTextSystem textSystem;

  {
    auto measuredOnly = lambda::For(
        items,
        [](int value) { return value; },
        [&measureOnlyFactoryCalls](int) {
          ++measureOnlyFactoryCalls;
          return lambda::Element{lambda::Rectangle{}}
              .size(20.f, 8.f)
              .fill(lambda::Colors::blue);
        },
        2.f);
    lambda::MeasureContext measureContext{textSystem, testEnvironment()};
    lambda::LayoutConstraints constraints{
        .maxWidth = 240.f,
        .maxHeight = 160.f,
        .minWidth = 0.f,
        .minHeight = 0.f,
    };
    measureContext.pushConstraints(constraints);
    CHECK(measuredOnly.measure(measureContext, constraints, {}, textSystem).height == doctest::Approx(18.f));
    CHECK(measureOnlyFactoryCalls == 2);
    CHECK(measuredOnly.measure(measureContext, constraints, {}, textSystem).height == doctest::Approx(18.f));
    measureContext.popConstraints();
  }
  CHECK(measureOnlyFactoryCalls == 2);

  lambda::scenegraph::SceneGraph sceneGraph;
  lambda::MountRoot root{
      std::make_unique<lambda::TypedRootHolder<Root>>(
          std::in_place, Root{items, &factoryCalls}),
      textSystem,
      testEnvironment(),
      lambda::Size{240.f, 160.f},
  };

  root.mount(sceneGraph);

  auto const& mounted = rootGroup(sceneGraph);
  REQUIRE(mounted.children().size() == 3);
  CHECK(factoryCalls == 2);
  CHECK(mounted.children()[1]->position().y == doctest::Approx(22.f));
  CHECK(mounted.children()[1]->size().height == doctest::Approx(18.f));
  CHECK(mounted.children()[2]->position().y == doctest::Approx(52.f));

  root.resize(lambda::Size{260.f, 180.f}, sceneGraph);

  auto const& resized = rootGroup(sceneGraph);
  REQUIRE(resized.children().size() == 3);
  CHECK(factoryCalls == 2);
  CHECK(resized.children()[1]->position().y == doctest::Approx(22.f));
  CHECK(resized.children()[1]->size().height == doctest::Approx(18.f));
  CHECK(resized.children()[2]->position().y == doctest::Approx(52.f));
}
