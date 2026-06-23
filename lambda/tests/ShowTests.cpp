#include <doctest/doctest.h>

#include <Lambda/UI/Detail/RootHolder.hpp>
#include <Lambda/Graphics/TextSystem.hpp>
#include <Lambda/Reactive/Scope.hpp>
#include <Lambda/Reactive/Signal.hpp>
#include <Lambda/SceneGraph/SceneNode.hpp>
#include <Lambda/SceneGraph/SceneGraph.hpp>
#include <Lambda/UI/Hooks.hpp>
#include <Lambda/UI/MountRoot.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/Views/HStack.hpp>
#include <Lambda/UI/Views/Rectangle.hpp>
#include <Lambda/UI/Views/Show.hpp>
#include <Lambda/UI/Views/Switch.hpp>
#include <Lambda/UI/Views/VStack.hpp>

#include <memory>
#include <limits>
#include <string_view>
#include <vector>

namespace {

class FakeTextSystem final : public lambdaui::TextSystem {
public:
  std::shared_ptr<lambdaui::TextLayout const>
  layout(lambdaui::AttributedString const&, float, lambdaui::TextLayoutOptions const&) override {
    return std::make_shared<lambdaui::TextLayout>();
  }

  std::shared_ptr<lambdaui::TextLayout const>
  layout(std::string_view, lambdaui::Font const&, lambdaui::Color const&, float,
         lambdaui::TextLayoutOptions const&) override {
    return std::make_shared<lambdaui::TextLayout>();
  }

  lambdaui::Size measure(lambdaui::AttributedString const&, float,
                     lambdaui::TextLayoutOptions const&) override {
    return {0.f, 0.f};
  }

  lambdaui::Size measure(std::string_view, lambdaui::Font const&, lambdaui::Color const&, float,
                     lambdaui::TextLayoutOptions const&) override {
    return {0.f, 0.f};
  }

  std::uint32_t resolveFontId(std::string_view, float, bool) override { return 0; }

  std::vector<std::uint8_t> rasterizeGlyph(std::uint32_t, std::uint32_t, float,
                                           std::uint32_t& outWidth,
                                           std::uint32_t& outHeight,
                                           lambdaui::Point& outBearing) override {
    outWidth = 0;
    outHeight = 0;
    outBearing = {};
    return {};
  }
};

lambdaui::EnvironmentBinding testEnvironment() {
  return lambdaui::EnvironmentBinding{}.withValue<lambdaui::ThemeKey>(lambdaui::Theme::light());
}

lambdaui::scenegraph::SceneNode const& rootGroup(lambdaui::scenegraph::SceneGraph const& sceneGraph) {
  REQUIRE(sceneGraph.root().kind() == lambdaui::scenegraph::SceneNodeKind::Group);
  return sceneGraph.root();
}

} // namespace

TEST_CASE("Show replaces branches and disposes the inactive scope") {
  struct Root {
    lambdaui::Reactive::Signal<bool> visible;
    int* thenCreated = nullptr;
    int* thenDisposed = nullptr;
    int* elseCreated = nullptr;
    int* elseDisposed = nullptr;

    lambdaui::Element body() const {
      return lambdaui::Show(
          visible,
          [thenCreated = thenCreated, thenDisposed = thenDisposed] {
            ++*thenCreated;
            lambdaui::Reactive::onCleanup([thenDisposed] {
              ++*thenDisposed;
            });
            return lambdaui::Element{lambdaui::Rectangle{}}
                .size(20.f, 10.f)
                .fill(lambdaui::Colors::red);
          },
          [elseCreated = elseCreated, elseDisposed = elseDisposed] {
            ++*elseCreated;
            lambdaui::Reactive::onCleanup([elseDisposed] {
              ++*elseDisposed;
            });
            return lambdaui::Element{lambdaui::Rectangle{}}
                .size(12.f, 8.f)
                .fill(lambdaui::Colors::blue);
          });
    }
  };

  int thenCreated = 0;
  int thenDisposed = 0;
  int elseCreated = 0;
  int elseDisposed = 0;
  lambdaui::Reactive::Signal<bool> visible{true};
  FakeTextSystem textSystem;
  lambdaui::scenegraph::SceneGraph sceneGraph;
  lambdaui::MountRoot root{
      std::make_unique<lambdaui::TypedRootHolder<Root>>(
          std::in_place, Root{visible, &thenCreated, &thenDisposed, &elseCreated, &elseDisposed}),
      textSystem,
      testEnvironment(),
      lambdaui::Size{200.f, 100.f},
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
    lambdaui::Element body() const {
      return lambdaui::VStack{
          .spacing = 12.f,
          .children = lambdaui::children(
              lambdaui::Element{lambdaui::Rectangle{}}
                  .size(20.f, 10.f)
                  .fill(lambdaui::Colors::red),
              lambdaui::Show(false, [] {
                return lambdaui::Element{lambdaui::Rectangle{}}
                    .size(20.f, 10.f)
                    .fill(lambdaui::Colors::blue);
              })),
      };
    }
  };

  FakeTextSystem textSystem;
  lambdaui::scenegraph::SceneGraph sceneGraph;
  lambdaui::MountRoot root{
      std::make_unique<lambdaui::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      testEnvironment(),
      lambdaui::Size{100.f, 100.f},
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

    lambdaui::Element body() const {
      lambdaui::useEffect([activeEffects = activeEffects, cleanups = cleanups] {
        ++*activeEffects;
        lambdaui::Reactive::onCleanup([activeEffects, cleanups] {
          --*activeEffects;
          ++*cleanups;
        });
      });
      return lambdaui::Element{lambdaui::Rectangle{}}
          .size(20.f, 10.f)
          .fill(lambdaui::Colors::red);
    }
  };

  struct Root {
    lambdaui::Reactive::Signal<bool> visible;
    int* activeEffects = nullptr;
    int* cleanups = nullptr;

    lambdaui::Element body() const {
      return lambdaui::Show(
          visible,
          [activeEffects = activeEffects, cleanups = cleanups] {
            return lambdaui::Element{EffectfulChild{activeEffects, cleanups}};
          },
          [] {
            return lambdaui::Element{lambdaui::Rectangle{}}
                .size(12.f, 8.f)
                .fill(lambdaui::Colors::blue);
          });
    }
  };

  int activeEffects = 0;
  int cleanups = 0;
  lambdaui::Reactive::Signal<bool> visible{true};
  FakeTextSystem textSystem;
  lambdaui::scenegraph::SceneGraph sceneGraph;
  lambdaui::MountRoot root{
      std::make_unique<lambdaui::TypedRootHolder<Root>>(
          std::in_place, Root{visible, &activeEffects, &cleanups}),
      textSystem,
      testEnvironment(),
      lambdaui::Size{200.f, 100.f},
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
    lambdaui::Reactive::Signal<bool> visible;

    lambdaui::Element body() const {
      return lambdaui::VStack{
          .spacing = 12.f,
          .children = lambdaui::children(
              lambdaui::Element{lambdaui::Rectangle{}}
                  .size(20.f, 10.f)
                  .fill(lambdaui::Colors::red),
              lambdaui::Show(visible, [] {
                return lambdaui::Element{lambdaui::Rectangle{}}
                    .size(20.f, 10.f)
                    .fill(lambdaui::Colors::blue);
              }),
              lambdaui::Element{lambdaui::Rectangle{}}
                  .size(20.f, 10.f)
                  .fill(lambdaui::Colors::green)),
      };
    }
  };

  FakeTextSystem textSystem;
  lambdaui::scenegraph::SceneGraph sceneGraph;
  lambdaui::Reactive::Signal<bool> visible{false};
  lambdaui::MountRoot root{
      std::make_unique<lambdaui::TypedRootHolder<Root>>(std::in_place, Root{visible}),
      textSystem,
      testEnvironment(),
      lambdaui::Size{100.f, 100.f},
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
  lambdaui::EnvironmentBinding environment = testEnvironment();
  lambdaui::MeasureContext measure{textSystem, environment};
  lambdaui::Reactive::Scope owner;
  lambdaui::Reactive::Signal<bool> visible{false};
  lambdaui::Element show{lambdaui::Show(
      [visible] {
        return visible.get();
      },
      [] {
        return lambdaui::Element{lambdaui::Rectangle{}}
            .size(80.f, 20.f)
            .fill(lambdaui::Colors::blue);
      })};

  lambdaui::LayoutConstraints natural{
      .maxWidth = 100.f,
      .maxHeight = std::numeric_limits<float>::infinity(),
      .minWidth = 0.f,
      .minHeight = 0.f,
  };
  lambdaui::MountContext mount{owner, textSystem, measure, natural, {}, {}, environment};
  std::unique_ptr<lambdaui::scenegraph::SceneNode> node = show.mount(mount);

  node->relayout(lambdaui::LayoutConstraints{
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
    lambdaui::Element body() const {
      return lambdaui::HStack{
          .spacing = 0.f,
          .alignment = lambdaui::Alignment::Stretch,
          .children = lambdaui::children(
              lambdaui::Element{lambdaui::Rectangle{}}
                  .size(20.f, 10.f)
                  .fill(lambdaui::Colors::red),
              lambdaui::Element{lambdaui::Show(true, [] {
                return lambdaui::Rectangle{}.fill(lambdaui::Colors::blue);
              })}.flex(1.f, 1.f, 0.f)),
      };
    }
  };

  FakeTextSystem textSystem;
  lambdaui::scenegraph::SceneGraph sceneGraph;
  lambdaui::MountRoot root{
      std::make_unique<lambdaui::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      testEnvironment(),
      lambdaui::Size{100.f, 40.f},
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
    lambdaui::Reactive::Signal<bool> visible;

    lambdaui::Element body() const {
      return lambdaui::VStack{
          .spacing = 12.f,
          .children = lambdaui::children(
              lambdaui::Element{lambdaui::VStack{
                  .spacing = 12.f,
                  .children = lambdaui::children(
                      lambdaui::Element{lambdaui::Rectangle{}}
                          .size(20.f, 10.f)
                          .fill(lambdaui::Colors::red),
                      lambdaui::Show(visible, [] {
                        return lambdaui::Element{lambdaui::Rectangle{}}
                            .size(20.f, 10.f)
                            .fill(lambdaui::Colors::blue);
                      })),
              }}.padding(16.f),
              lambdaui::Element{lambdaui::Rectangle{}}
                  .size(20.f, 10.f)
                  .fill(lambdaui::Colors::green)),
      };
    }
  };

  FakeTextSystem textSystem;
  lambdaui::scenegraph::SceneGraph sceneGraph;
  lambdaui::Reactive::Signal<bool> visible{false};
  lambdaui::MountRoot root{
      std::make_unique<lambdaui::TypedRootHolder<Root>>(std::in_place, Root{visible}),
      textSystem,
      testEnvironment(),
      lambdaui::Size{120.f, 120.f},
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
    lambdaui::Reactive::Signal<int> mode;
    int* created = nullptr;
    int* disposed = nullptr;

    lambdaui::Element body() const {
      auto branch = [created = created, disposed = disposed](lambdaui::Color color) {
        return [created, disposed, color] {
          ++*created;
          lambdaui::Reactive::onCleanup([disposed] {
            ++*disposed;
          });
          return lambdaui::Element{lambdaui::Rectangle{}}
              .size(18.f, 18.f)
              .fill(color);
        };
      };

      return lambdaui::Switch(
          [mode = mode] { return mode.get(); },
          std::vector{
              lambdaui::Case(0, branch(lambdaui::Colors::red)),
              lambdaui::Case(1, branch(lambdaui::Colors::green)),
          },
          branch(lambdaui::Colors::blue));
    }
  };

  int created = 0;
  int disposed = 0;
  lambdaui::Reactive::Signal<int> mode{0};
  FakeTextSystem textSystem;
  lambdaui::scenegraph::SceneGraph sceneGraph;
  lambdaui::MountRoot root{
      std::make_unique<lambdaui::TypedRootHolder<Root>>(std::in_place, Root{mode, &created, &disposed}),
      textSystem,
      testEnvironment(),
      lambdaui::Size{160.f, 100.f},
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
    lambdaui::Reactive::Signal<int> mode;

    lambdaui::Element body() const {
      auto branch = [](lambdaui::Color color) {
        return [color] {
          return lambdaui::Rectangle{}.fill(color);
        };
      };

      return lambdaui::HStack{
          .spacing = 0.f,
          .alignment = lambdaui::Alignment::Stretch,
          .children = lambdaui::children(
              lambdaui::Element{lambdaui::Rectangle{}}
                  .size(20.f, 10.f)
                  .fill(lambdaui::Colors::red),
              lambdaui::Element{lambdaui::Switch(
                  [mode = mode] { return mode.get(); },
                  std::vector{
                      lambdaui::Case(0, branch(lambdaui::Colors::blue)),
                      lambdaui::Case(1, branch(lambdaui::Colors::green)),
                  })}.flex(1.f, 1.f, 0.f)),
      };
    }
  };

  FakeTextSystem textSystem;
  lambdaui::scenegraph::SceneGraph sceneGraph;
  lambdaui::Reactive::Signal<int> mode{0};
  lambdaui::MountRoot root{
      std::make_unique<lambdaui::TypedRootHolder<Root>>(std::in_place, Root{mode}),
      textSystem,
      testEnvironment(),
      lambdaui::Size{100.f, 40.f},
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
