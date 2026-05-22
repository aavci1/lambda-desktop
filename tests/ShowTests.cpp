#include <doctest/doctest.h>

#include <Flux/UI/Detail/RootHolder.hpp>
#include <Flux/Graphics/TextSystem.hpp>
#include <Flux/Reactive/Scope.hpp>
#include <Flux/Reactive/Signal.hpp>
#include <Flux/SceneGraph/SceneNode.hpp>
#include <Flux/SceneGraph/SceneGraph.hpp>
#include <Flux/UI/Hooks.hpp>
#include <Flux/UI/MountRoot.hpp>
#include <Flux/UI/Theme.hpp>
#include <Flux/UI/Views/HStack.hpp>
#include <Flux/UI/Views/Rectangle.hpp>
#include <Flux/UI/Views/Show.hpp>
#include <Flux/UI/Views/Switch.hpp>
#include <Flux/UI/Views/VStack.hpp>

#include <memory>
#include <limits>
#include <string_view>
#include <vector>

namespace {

class FakeTextSystem final : public flux::TextSystem {
public:
  std::shared_ptr<flux::TextLayout const>
  layout(flux::AttributedString const&, float, flux::TextLayoutOptions const&) override {
    return std::make_shared<flux::TextLayout>();
  }

  std::shared_ptr<flux::TextLayout const>
  layout(std::string_view, flux::Font const&, flux::Color const&, float,
         flux::TextLayoutOptions const&) override {
    return std::make_shared<flux::TextLayout>();
  }

  flux::Size measure(flux::AttributedString const&, float,
                     flux::TextLayoutOptions const&) override {
    return {0.f, 0.f};
  }

  flux::Size measure(std::string_view, flux::Font const&, flux::Color const&, float,
                     flux::TextLayoutOptions const&) override {
    return {0.f, 0.f};
  }

  std::uint32_t resolveFontId(std::string_view, float, bool) override { return 0; }

  std::vector<std::uint8_t> rasterizeGlyph(std::uint32_t, std::uint32_t, float,
                                           std::uint32_t& outWidth,
                                           std::uint32_t& outHeight,
                                           flux::Point& outBearing) override {
    outWidth = 0;
    outHeight = 0;
    outBearing = {};
    return {};
  }
};

flux::EnvironmentBinding testEnvironment() {
  return flux::EnvironmentBinding{}.withValue<flux::ThemeKey>(flux::Theme::light());
}

flux::scenegraph::SceneNode const& rootGroup(flux::scenegraph::SceneGraph const& sceneGraph) {
  REQUIRE(sceneGraph.root().kind() == flux::scenegraph::SceneNodeKind::Group);
  return sceneGraph.root();
}

} // namespace

TEST_CASE("Show replaces branches and disposes the inactive scope") {
  struct Root {
    flux::Reactive::Signal<bool> visible;
    int* thenCreated = nullptr;
    int* thenDisposed = nullptr;
    int* elseCreated = nullptr;
    int* elseDisposed = nullptr;

    flux::Element body() const {
      return flux::Show(
          visible,
          [thenCreated = thenCreated, thenDisposed = thenDisposed] {
            ++*thenCreated;
            flux::Reactive::onCleanup([thenDisposed] {
              ++*thenDisposed;
            });
            return flux::Element{flux::Rectangle{}}
                .size(20.f, 10.f)
                .fill(flux::Colors::red);
          },
          [elseCreated = elseCreated, elseDisposed = elseDisposed] {
            ++*elseCreated;
            flux::Reactive::onCleanup([elseDisposed] {
              ++*elseDisposed;
            });
            return flux::Element{flux::Rectangle{}}
                .size(12.f, 8.f)
                .fill(flux::Colors::blue);
          });
    }
  };

  int thenCreated = 0;
  int thenDisposed = 0;
  int elseCreated = 0;
  int elseDisposed = 0;
  flux::Reactive::Signal<bool> visible{true};
  FakeTextSystem textSystem;
  flux::scenegraph::SceneGraph sceneGraph;
  flux::MountRoot root{
      std::make_unique<flux::TypedRootHolder<Root>>(
          std::in_place, Root{visible, &thenCreated, &thenDisposed, &elseCreated, &elseDisposed}),
      textSystem,
      testEnvironment(),
      flux::Size{200.f, 100.f},
  };

  root.mount(sceneGraph);

  auto const& initial = rootGroup(sceneGraph);
  REQUIRE(initial.children().size() == 1);
  CHECK(thenCreated == 1);
  CHECK(thenDisposed == 0);
  CHECK(elseCreated == 0);
  CHECK(elseDisposed == 0);

  visible.set(false);

  auto const& hidden = rootGroup(sceneGraph);
  REQUIRE(hidden.children().size() == 1);
  CHECK(hidden.children()[0]->size().width == doctest::Approx(12.f));
  CHECK(thenCreated == 1);
  CHECK(thenDisposed == 1);
  CHECK(elseCreated == 1);
  CHECK(elseDisposed == 0);

  visible.set(true);

  auto const& shownAgain = rootGroup(sceneGraph);
  REQUIRE(shownAgain.children().size() == 1);
  CHECK(thenCreated == 2);
  CHECK(thenDisposed == 1);
  CHECK(elseCreated == 1);
  CHECK(elseDisposed == 1);

  root.unmount(sceneGraph);
  CHECK(thenDisposed == 2);
}

TEST_CASE("Show false branch collapses out of stack spacing") {
  struct Root {
    flux::Element body() const {
      return flux::VStack{
          .spacing = 12.f,
          .children = flux::children(
              flux::Element{flux::Rectangle{}}
                  .size(20.f, 10.f)
                  .fill(flux::Colors::red),
              flux::Show(false, [] {
                return flux::Element{flux::Rectangle{}}
                    .size(20.f, 10.f)
                    .fill(flux::Colors::blue);
              })),
      };
    }
  };

  FakeTextSystem textSystem;
  flux::scenegraph::SceneGraph sceneGraph;
  flux::MountRoot root{
      std::make_unique<flux::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      testEnvironment(),
      flux::Size{100.f, 100.f},
  };

  root.mount(sceneGraph);

  auto const& group = rootGroup(sceneGraph);
  REQUIRE(group.children().size() == 2);
  CHECK(group.children()[1]->size().height == doctest::Approx(0.f));
  CHECK(group.children()[1]->position().y == doctest::Approx(22.f));
}

TEST_CASE("Show branch composite effects are scoped once and disposed with branch") {
  struct EffectfulChild {
    int* activeEffects = nullptr;
    int* cleanups = nullptr;

    flux::Element body() const {
      flux::useEffect([activeEffects = activeEffects, cleanups = cleanups] {
        ++*activeEffects;
        flux::Reactive::onCleanup([activeEffects, cleanups] {
          --*activeEffects;
          ++*cleanups;
        });
      });
      return flux::Element{flux::Rectangle{}}
          .size(20.f, 10.f)
          .fill(flux::Colors::red);
    }
  };

  struct Root {
    flux::Reactive::Signal<bool> visible;
    int* activeEffects = nullptr;
    int* cleanups = nullptr;

    flux::Element body() const {
      return flux::Show(
          visible,
          [activeEffects = activeEffects, cleanups = cleanups] {
            return flux::Element{EffectfulChild{activeEffects, cleanups}};
          },
          [] {
            return flux::Element{flux::Rectangle{}}
                .size(12.f, 8.f)
                .fill(flux::Colors::blue);
          });
    }
  };

  int activeEffects = 0;
  int cleanups = 0;
  flux::Reactive::Signal<bool> visible{true};
  FakeTextSystem textSystem;
  flux::scenegraph::SceneGraph sceneGraph;
  flux::MountRoot root{
      std::make_unique<flux::TypedRootHolder<Root>>(
          std::in_place, Root{visible, &activeEffects, &cleanups}),
      textSystem,
      testEnvironment(),
      flux::Size{200.f, 100.f},
  };

  root.mount(sceneGraph);
  CHECK(activeEffects == 1);

  visible.set(false);
  CHECK(activeEffects == 0);

  visible.set(true);
  CHECK(activeEffects == 1);

  root.unmount(sceneGraph);
  CHECK(activeEffects == 0);
}

TEST_CASE("Show hidden stack child stays mounted and expands later") {
  struct Root {
    flux::Reactive::Signal<bool> visible;

    flux::Element body() const {
      return flux::VStack{
          .spacing = 12.f,
          .children = flux::children(
              flux::Element{flux::Rectangle{}}
                  .size(20.f, 10.f)
                  .fill(flux::Colors::red),
              flux::Show(visible, [] {
                return flux::Element{flux::Rectangle{}}
                    .size(20.f, 10.f)
                    .fill(flux::Colors::blue);
              }),
              flux::Element{flux::Rectangle{}}
                  .size(20.f, 10.f)
                  .fill(flux::Colors::green)),
      };
    }
  };

  FakeTextSystem textSystem;
  flux::scenegraph::SceneGraph sceneGraph;
  flux::Reactive::Signal<bool> visible{false};
  flux::MountRoot root{
      std::make_unique<flux::TypedRootHolder<Root>>(std::in_place, Root{visible}),
      textSystem,
      testEnvironment(),
      flux::Size{100.f, 100.f},
  };

  root.mount(sceneGraph);

  auto const& hidden = rootGroup(sceneGraph);
  REQUIRE(hidden.children().size() == 3);
  CHECK(hidden.children()[1]->size().height == doctest::Approx(0.f));
  CHECK(hidden.children()[2]->position().y == doctest::Approx(22.f));

  visible.set(true);

  auto const& shown = rootGroup(sceneGraph);
  REQUIRE(shown.children().size() == 3);
  CHECK(shown.children()[1]->position().y == doctest::Approx(22.f));
  CHECK(shown.children()[1]->size().height == doctest::Approx(10.f));
  CHECK(shown.children()[2]->position().y == doctest::Approx(44.f));

  visible.set(false);

  auto const& hiddenAgain = rootGroup(sceneGraph);
  REQUIRE(hiddenAgain.children().size() == 3);
  CHECK(hiddenAgain.children()[1]->size().height == doctest::Approx(0.f));
  CHECK(hiddenAgain.children()[2]->position().y == doctest::Approx(22.f));
}

TEST_CASE("Show keeps natural constraints after transient zero-size layout") {
  FakeTextSystem textSystem;
  flux::EnvironmentBinding environment = testEnvironment();
  flux::MeasureContext measure{textSystem, environment};
  flux::Reactive::Scope owner;
  flux::Reactive::Signal<bool> visible{false};
  flux::Element show{flux::Show(
      [visible] {
        return visible.get();
      },
      [] {
        return flux::Element{flux::Rectangle{}}
            .size(80.f, 20.f)
            .fill(flux::Colors::blue);
      })};

  flux::LayoutConstraints natural{
      .maxWidth = 100.f,
      .maxHeight = std::numeric_limits<float>::infinity(),
      .minWidth = 0.f,
      .minHeight = 0.f,
  };
  flux::MountContext mount{owner, textSystem, measure, natural, {}, {}, environment};
  std::unique_ptr<flux::scenegraph::SceneNode> node = show.mount(mount);

  node->relayout(flux::LayoutConstraints{
                     .maxWidth = 100.f,
                     .maxHeight = 0.f,
                     .minWidth = 100.f,
                     .minHeight = 0.f,
                 },
                 false);
  CHECK(node->size().height == doctest::Approx(0.f));

  visible.set(true);

  CHECK(node->size().width == doctest::Approx(100.f));
  CHECK(node->size().height == doctest::Approx(20.f));
}

TEST_CASE("Show relayouts active branch into flexible stack slot") {
  struct Root {
    flux::Element body() const {
      return flux::HStack{
          .spacing = 0.f,
          .alignment = flux::Alignment::Stretch,
          .children = flux::children(
              flux::Element{flux::Rectangle{}}
                  .size(20.f, 10.f)
                  .fill(flux::Colors::red),
              flux::Element{flux::Show(true, [] {
                return flux::Rectangle{}.fill(flux::Colors::blue);
              })}.flex(1.f, 1.f, 0.f)),
      };
    }
  };

  FakeTextSystem textSystem;
  flux::scenegraph::SceneGraph sceneGraph;
  flux::MountRoot root{
      std::make_unique<flux::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      testEnvironment(),
      flux::Size{100.f, 40.f},
  };

  root.mount(sceneGraph);

  auto const& group = rootGroup(sceneGraph);
  REQUIRE(group.children().size() == 2);
  CHECK(group.children()[1]->position().x == doctest::Approx(20.f));
  CHECK(group.children()[1]->size().width == doctest::Approx(80.f));
  CHECK(group.children()[1]->size().height == doctest::Approx(40.f));
  REQUIRE(group.children()[1]->children().size() == 1);
  CHECK(group.children()[1]->children()[0]->size().width == doctest::Approx(80.f));
  CHECK(group.children()[1]->children()[0]->size().height == doctest::Approx(40.f));
}

TEST_CASE("Show size changes grow wrapper ancestors and move following stack siblings") {
  struct Root {
    flux::Reactive::Signal<bool> visible;

    flux::Element body() const {
      return flux::VStack{
          .spacing = 12.f,
          .children = flux::children(
              flux::Element{flux::VStack{
                  .spacing = 12.f,
                  .children = flux::children(
                      flux::Element{flux::Rectangle{}}
                          .size(20.f, 10.f)
                          .fill(flux::Colors::red),
                      flux::Show(visible, [] {
                        return flux::Element{flux::Rectangle{}}
                            .size(20.f, 10.f)
                            .fill(flux::Colors::blue);
                      })),
              }}.padding(16.f),
              flux::Element{flux::Rectangle{}}
                  .size(20.f, 10.f)
                  .fill(flux::Colors::green)),
      };
    }
  };

  FakeTextSystem textSystem;
  flux::scenegraph::SceneGraph sceneGraph;
  flux::Reactive::Signal<bool> visible{false};
  flux::MountRoot root{
      std::make_unique<flux::TypedRootHolder<Root>>(std::in_place, Root{visible}),
      textSystem,
      testEnvironment(),
      flux::Size{120.f, 120.f},
  };

  root.mount(sceneGraph);

  auto const& hidden = rootGroup(sceneGraph);
  REQUIRE(hidden.children().size() == 2);
  CHECK(hidden.children()[0]->size().height == doctest::Approx(42.f));
  CHECK(hidden.children()[1]->position().y == doctest::Approx(54.f));

  visible.set(true);

  auto const& shown = rootGroup(sceneGraph);
  REQUIRE(shown.children().size() == 2);
  CHECK(shown.children()[0]->size().height == doctest::Approx(64.f));
  CHECK(shown.children()[1]->position().y == doctest::Approx(76.f));

  visible.set(false);

  auto const& hiddenAgain = rootGroup(sceneGraph);
  REQUIRE(hiddenAgain.children().size() == 2);
  CHECK(hiddenAgain.children()[0]->size().height == doctest::Approx(42.f));
  CHECK(hiddenAgain.children()[1]->position().y == doctest::Approx(54.f));
}

TEST_CASE("Switch replaces scopes when the selected case changes") {
  struct Root {
    flux::Reactive::Signal<int> mode;
    int* created = nullptr;
    int* disposed = nullptr;

    flux::Element body() const {
      auto branch = [created = created, disposed = disposed](flux::Color color) {
        return [created, disposed, color] {
          ++*created;
          flux::Reactive::onCleanup([disposed] {
            ++*disposed;
          });
          return flux::Element{flux::Rectangle{}}
              .size(18.f, 18.f)
              .fill(color);
        };
      };

      return flux::Switch(
          [mode = mode] { return mode.get(); },
          std::vector{
              flux::Case(0, branch(flux::Colors::red)),
              flux::Case(1, branch(flux::Colors::green)),
          },
          branch(flux::Colors::blue));
    }
  };

  int created = 0;
  int disposed = 0;
  flux::Reactive::Signal<int> mode{0};
  FakeTextSystem textSystem;
  flux::scenegraph::SceneGraph sceneGraph;
  flux::MountRoot root{
      std::make_unique<flux::TypedRootHolder<Root>>(std::in_place, Root{mode, &created, &disposed}),
      textSystem,
      testEnvironment(),
      flux::Size{160.f, 100.f},
  };

  root.mount(sceneGraph);

  REQUIRE(rootGroup(sceneGraph).children().size() == 1);
  CHECK(created == 1);
  CHECK(disposed == 0);

  mode.set(1);
  REQUIRE(rootGroup(sceneGraph).children().size() == 1);
  CHECK(created == 2);
  CHECK(disposed == 1);

  mode.set(2);
  REQUIRE(rootGroup(sceneGraph).children().size() == 1);
  CHECK(created == 3);
  CHECK(disposed == 2);

  mode.set(3);
  REQUIRE(rootGroup(sceneGraph).children().size() == 1);
  CHECK(created == 3);
  CHECK(disposed == 2);

  root.unmount(sceneGraph);
  CHECK(disposed == 3);
}

TEST_CASE("Switch relayouts newly selected branch into flexible stack slot") {
  struct Root {
    flux::Reactive::Signal<int> mode;

    flux::Element body() const {
      auto branch = [](flux::Color color) {
        return [color] {
          return flux::Rectangle{}.fill(color);
        };
      };

      return flux::HStack{
          .spacing = 0.f,
          .alignment = flux::Alignment::Stretch,
          .children = flux::children(
              flux::Element{flux::Rectangle{}}
                  .size(20.f, 10.f)
                  .fill(flux::Colors::red),
              flux::Element{flux::Switch(
                  [mode = mode] { return mode.get(); },
                  std::vector{
                      flux::Case(0, branch(flux::Colors::blue)),
                      flux::Case(1, branch(flux::Colors::green)),
                  })}.flex(1.f, 1.f, 0.f)),
      };
    }
  };

  FakeTextSystem textSystem;
  flux::scenegraph::SceneGraph sceneGraph;
  flux::Reactive::Signal<int> mode{0};
  flux::MountRoot root{
      std::make_unique<flux::TypedRootHolder<Root>>(std::in_place, Root{mode}),
      textSystem,
      testEnvironment(),
      flux::Size{100.f, 40.f},
  };

  root.mount(sceneGraph);

  auto assertSwitchSlot = [&sceneGraph] {
    auto const& group = rootGroup(sceneGraph);
    REQUIRE(group.children().size() == 2);
    auto const& slot = *group.children()[1];
    CHECK(slot.position().x == doctest::Approx(20.f));
    CHECK(slot.size().width == doctest::Approx(80.f));
    CHECK(slot.size().height == doctest::Approx(40.f));
    REQUIRE(slot.children().size() == 1);
    CHECK(slot.children()[0]->size().width == doctest::Approx(80.f));
    CHECK(slot.children()[0]->size().height == doctest::Approx(40.f));
  };

  assertSwitchSlot();

  mode.set(1);

  assertSwitchSlot();
}
