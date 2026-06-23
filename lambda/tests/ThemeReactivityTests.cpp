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

lambdaui::EnvironmentBinding themeBinding(lambdaui::Reactive::Signal<lambdaui::Theme> theme) {
  return lambdaui::EnvironmentBinding{}.withSignal<lambdaui::ThemeKey>(std::move(theme));
}

lambdaui::Color solidColor(lambdaui::scenegraph::RectNode const& rect) {
  lambdaui::Color color{};
  CHECK(rect.fill().solidColor(&color));
  return color;
}

void checkSameChannels(lambdaui::Color actual, lambdaui::Color expected) {
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

    lambdaui::Element body() const {
      ++*bodyCalls;
      auto theme = lambdaui::useEnvironment<lambdaui::ThemeKey>();
      lambdaui::Reactive::onCleanup([cleanups = cleanups] {
        ++*cleanups;
      });

      return lambdaui::Element{lambdaui::Rectangle{}}
          .size(32.f, 18.f)
          .fill([theme] {
            return lambdaui::Color::windowBackground();
          })
          .stroke([theme] {
            return theme().separatorColor;
          }, 1.f);
    }
  };

  int bodyCalls = 0;
  int cleanups = 0;
  lambdaui::Reactive::Signal<lambdaui::Theme> theme{lambdaui::Theme::light()};
  FakeTextSystem textSystem;
  lambdaui::scenegraph::SceneGraph sceneGraph;
  lambdaui::MountRoot root{
      std::make_unique<lambdaui::TypedRootHolder<Root>>(std::in_place, Root{&bodyCalls, &cleanups}),
      textSystem,
      themeBinding(theme),
      lambdaui::Size{200.f, 120.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == lambdaui::scenegraph::SceneNodeKind::Rect);
  auto const* initialNode = &static_cast<lambdaui::scenegraph::RectNode const&>(sceneGraph.root());
  CHECK(bodyCalls == 1);
  CHECK(cleanups == 0);
  checkSameChannels(solidColor(*initialNode), lambdaui::Theme::light().windowBackgroundColor);

  auto const toggleStart = std::chrono::steady_clock::now();
  theme.set(lambdaui::Theme::dark());
  auto const toggleElapsed = std::chrono::steady_clock::now() - toggleStart;
  CHECK(std::chrono::duration<float, std::milli>(toggleElapsed).count() < 16.67f);

  REQUIRE(sceneGraph.root().kind() == lambdaui::scenegraph::SceneNodeKind::Rect);
  auto const* updatedNode = &static_cast<lambdaui::scenegraph::RectNode const&>(sceneGraph.root());
  CHECK(updatedNode == initialNode);
  CHECK(bodyCalls == 1);
  CHECK(cleanups == 0);
  checkSameChannels(solidColor(*updatedNode), lambdaui::Theme::dark().windowBackgroundColor);
  CHECK(updatedNode->stroke().color == lambdaui::Theme::dark().separatorColor);

  root.unmount(sceneGraph);
  CHECK(cleanups == 1);
}

TEST_CASE("theme signal resolves semantic gradient fill stops") {
  struct Root {
    lambdaui::Element body() const {
      auto theme = lambdaui::useEnvironment<lambdaui::ThemeKey>();
      return lambdaui::Element{lambdaui::Rectangle{}}
          .size(32.f, 18.f)
          .fill([theme] {
            (void)theme();
            return lambdaui::FillStyle::linearGradient({
                lambdaui::GradientStop{0.f, lambdaui::Color::accent()},
                lambdaui::GradientStop{1.f, lambdaui::Color::danger()},
            });
          });
    }
  };

  lambdaui::Reactive::Signal<lambdaui::Theme> theme{lambdaui::Theme::light()};
  FakeTextSystem textSystem;
  lambdaui::scenegraph::SceneGraph sceneGraph;
  lambdaui::MountRoot root{
      std::make_unique<lambdaui::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      themeBinding(theme),
      lambdaui::Size{200.f, 120.f},
  };

  root.mount(sceneGraph);
  REQUIRE(sceneGraph.root().kind() == lambdaui::scenegraph::SceneNodeKind::Rect);
  auto const* rect = &static_cast<lambdaui::scenegraph::RectNode const&>(sceneGraph.root());

  lambdaui::LinearGradient gradient{};
  REQUIRE(rect->fill().linearGradient(&gradient));
  REQUIRE(gradient.stopCount == 2);
  checkSameChannels(gradient.stops[0].color, lambdaui::Theme::light().accentColor);
  checkSameChannels(gradient.stops[1].color, lambdaui::Theme::light().dangerColor);

  theme.set(lambdaui::Theme::dark());
  REQUIRE(rect->fill().linearGradient(&gradient));
  REQUIRE(gradient.stopCount == 2);
  checkSameChannels(gradient.stops[0].color, lambdaui::Theme::dark().accentColor);
  checkSameChannels(gradient.stops[1].color, lambdaui::Theme::dark().dangerColor);
}

TEST_CASE("element environment applies to modifier bindings on the same retained subtree") {
  struct Root {
    lambdaui::Element body() const {
      return lambdaui::Element{lambdaui::Rectangle{}}
          .size(32.f, 18.f)
          .fill(lambdaui::Color::controlBackground())
          .stroke(lambdaui::Color::separator(), 1.f)
          .environment<lambdaui::ThemeKey>(lambdaui::Theme::dark());
    }
  };

  FakeTextSystem textSystem;
  lambdaui::scenegraph::SceneGraph sceneGraph;
  lambdaui::MountRoot root{
      std::make_unique<lambdaui::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      lambdaui::EnvironmentBinding{}.withValue<lambdaui::ThemeKey>(lambdaui::Theme::light()),
      lambdaui::Size{200.f, 120.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == lambdaui::scenegraph::SceneNodeKind::Rect);
  auto const& rect = static_cast<lambdaui::scenegraph::RectNode const&>(sceneGraph.root());
  checkSameChannels(solidColor(rect), lambdaui::Theme::dark().controlBackgroundColor);
  CHECK(rect.stroke().color == lambdaui::Theme::dark().separatorColor);
}
