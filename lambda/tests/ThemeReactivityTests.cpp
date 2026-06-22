#include <doctest/doctest.h>

#include <Lambda/UI/Detail/RootHolder.hpp>
#include <Lambda/Graphics/TextSystem.hpp>
#include <Lambda/Reactive/Bindable.hpp>
#include <Lambda/Reactive/Scope.hpp>
#include <Lambda/Reactive/Signal.hpp>
#include <Lambda/SceneGraph/RectNode.hpp>
#include <Lambda/SceneGraph/SceneGraph.hpp>
#include <Lambda/UI/Hooks.hpp>
#include <Lambda/UI/MountRoot.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/Views/Rectangle.hpp>

#include <chrono>
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

lambda::EnvironmentBinding themeBinding(lambda::Reactive::Signal<lambda::Theme> theme) {
  return lambda::EnvironmentBinding{}.withSignal<lambda::ThemeKey>(std::move(theme));
}

lambda::Color solidColor(lambda::scenegraph::RectNode const& rect) {
  lambda::Color color{};
  CHECK(rect.fill().solidColor(&color));
  return color;
}

void checkSameChannels(lambda::Color actual, lambda::Color expected) {
  CHECK(actual.r == doctest::Approx(expected.r));
  CHECK(actual.g == doctest::Approx(expected.g));
  CHECK(actual.b == doctest::Approx(expected.b));
  CHECK(actual.a == doctest::Approx(expected.a));
}

} // namespace

TEST_CASE("theme signal updates retained leaf bindings without remounting") {
  struct Root {
    int* bodyCalls = nullptr;
    int* cleanups = nullptr;

    lambda::Element body() const {
      ++*bodyCalls;
      auto theme = lambda::useEnvironment<lambda::ThemeKey>();
      lambda::Reactive::onCleanup([cleanups = cleanups] {
        ++*cleanups;
      });

      return lambda::Element{lambda::Rectangle{}}
          .size(32.f, 18.f)
          .fill([theme] {
            return lambda::Color::windowBackground();
          })
          .stroke([theme] {
            return theme().separatorColor;
          }, 1.f);
    }
  };

  int bodyCalls = 0;
  int cleanups = 0;
  lambda::Reactive::Signal<lambda::Theme> theme{lambda::Theme::light()};
  FakeTextSystem textSystem;
  lambda::scenegraph::SceneGraph sceneGraph;
  lambda::MountRoot root{
      std::make_unique<lambda::TypedRootHolder<Root>>(std::in_place, Root{&bodyCalls, &cleanups}),
      textSystem,
      themeBinding(theme),
      lambda::Size{200.f, 120.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == lambda::scenegraph::SceneNodeKind::Rect);
  auto const* initialNode = &static_cast<lambda::scenegraph::RectNode const&>(sceneGraph.root());
  CHECK(bodyCalls == 1);
  CHECK(cleanups == 0);
  checkSameChannels(solidColor(*initialNode), lambda::Theme::light().windowBackgroundColor);

  auto const toggleStart = std::chrono::steady_clock::now();
  theme.set(lambda::Theme::dark());
  auto const toggleElapsed = std::chrono::steady_clock::now() - toggleStart;
  CHECK(std::chrono::duration<float, std::milli>(toggleElapsed).count() < 16.67f);

  REQUIRE(sceneGraph.root().kind() == lambda::scenegraph::SceneNodeKind::Rect);
  auto const* updatedNode = &static_cast<lambda::scenegraph::RectNode const&>(sceneGraph.root());
  CHECK(updatedNode == initialNode);
  CHECK(bodyCalls == 1);
  CHECK(cleanups == 0);
  checkSameChannels(solidColor(*updatedNode), lambda::Theme::dark().windowBackgroundColor);
  CHECK(updatedNode->stroke().color == lambda::Theme::dark().separatorColor);

  root.unmount(sceneGraph);
  CHECK(cleanups == 1);
}

TEST_CASE("theme signal resolves semantic gradient fill stops") {
  struct Root {
    lambda::Element body() const {
      auto theme = lambda::useEnvironment<lambda::ThemeKey>();
      return lambda::Element{lambda::Rectangle{}}
          .size(32.f, 18.f)
          .fill([theme] {
            (void)theme();
            return lambda::FillStyle::linearGradient({
                lambda::GradientStop{0.f, lambda::Color::accent()},
                lambda::GradientStop{1.f, lambda::Color::danger()},
            });
          });
    }
  };

  lambda::Reactive::Signal<lambda::Theme> theme{lambda::Theme::light()};
  FakeTextSystem textSystem;
  lambda::scenegraph::SceneGraph sceneGraph;
  lambda::MountRoot root{
      std::make_unique<lambda::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      themeBinding(theme),
      lambda::Size{200.f, 120.f},
  };

  root.mount(sceneGraph);
  REQUIRE(sceneGraph.root().kind() == lambda::scenegraph::SceneNodeKind::Rect);
  auto const* rect = &static_cast<lambda::scenegraph::RectNode const&>(sceneGraph.root());

  lambda::LinearGradient gradient{};
  REQUIRE(rect->fill().linearGradient(&gradient));
  REQUIRE(gradient.stopCount == 2);
  checkSameChannels(gradient.stops[0].color, lambda::Theme::light().accentColor);
  checkSameChannels(gradient.stops[1].color, lambda::Theme::light().dangerColor);

  theme.set(lambda::Theme::dark());
  REQUIRE(rect->fill().linearGradient(&gradient));
  REQUIRE(gradient.stopCount == 2);
  checkSameChannels(gradient.stops[0].color, lambda::Theme::dark().accentColor);
  checkSameChannels(gradient.stops[1].color, lambda::Theme::dark().dangerColor);
}

TEST_CASE("element environment applies to modifier bindings on the same retained subtree") {
  struct Root {
    lambda::Element body() const {
      return lambda::Element{lambda::Rectangle{}}
          .size(32.f, 18.f)
          .fill(lambda::Color::controlBackground())
          .stroke(lambda::Color::separator(), 1.f)
          .environment<lambda::ThemeKey>(lambda::Theme::dark());
    }
  };

  FakeTextSystem textSystem;
  lambda::scenegraph::SceneGraph sceneGraph;
  lambda::MountRoot root{
      std::make_unique<lambda::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      lambda::EnvironmentBinding{}.withValue<lambda::ThemeKey>(lambda::Theme::light()),
      lambda::Size{200.f, 120.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == lambda::scenegraph::SceneNodeKind::Rect);
  auto const& rect = static_cast<lambda::scenegraph::RectNode const&>(sceneGraph.root());
  checkSameChannels(solidColor(rect), lambda::Theme::dark().controlBackgroundColor);
  CHECK(rect.stroke().color == lambda::Theme::dark().separatorColor);
}
