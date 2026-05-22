#include <doctest/doctest.h>

#include <Flux/UI/Detail/RootHolder.hpp>
#include <Flux/Graphics/TextSystem.hpp>
#include <Flux/Reactive/Bindable.hpp>
#include <Flux/Reactive/Scope.hpp>
#include <Flux/Reactive/Signal.hpp>
#include <Flux/SceneGraph/RectNode.hpp>
#include <Flux/SceneGraph/SceneGraph.hpp>
#include <Flux/UI/Hooks.hpp>
#include <Flux/UI/MountRoot.hpp>
#include <Flux/UI/Theme.hpp>
#include <Flux/UI/Views/Rectangle.hpp>

#include <chrono>
#include <memory>
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

flux::EnvironmentBinding themeBinding(flux::Reactive::Signal<flux::Theme> theme) {
  return flux::EnvironmentBinding{}.withSignal<flux::ThemeKey>(std::move(theme));
}

flux::Color solidColor(flux::scenegraph::RectNode const& rect) {
  flux::Color color{};
  CHECK(rect.fill().solidColor(&color));
  return color;
}

void checkSameChannels(flux::Color actual, flux::Color expected) {
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

    flux::Element body() const {
      ++*bodyCalls;
      auto theme = flux::useEnvironment<flux::ThemeKey>();
      flux::Reactive::onCleanup([cleanups = cleanups] {
        ++*cleanups;
      });

      return flux::Element{flux::Rectangle{}}
          .size(32.f, 18.f)
          .fill([theme] {
            return flux::Color::windowBackground();
          })
          .stroke([theme] {
            return theme().separatorColor;
          }, 1.f);
    }
  };

  int bodyCalls = 0;
  int cleanups = 0;
  flux::Reactive::Signal<flux::Theme> theme{flux::Theme::light()};
  FakeTextSystem textSystem;
  flux::scenegraph::SceneGraph sceneGraph;
  flux::MountRoot root{
      std::make_unique<flux::TypedRootHolder<Root>>(std::in_place, Root{&bodyCalls, &cleanups}),
      textSystem,
      themeBinding(theme),
      flux::Size{200.f, 120.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == flux::scenegraph::SceneNodeKind::Rect);
  auto const* initialNode = &static_cast<flux::scenegraph::RectNode const&>(sceneGraph.root());
  CHECK(bodyCalls == 1);
  CHECK(cleanups == 0);
  checkSameChannels(solidColor(*initialNode), flux::Theme::light().windowBackgroundColor);

  auto const toggleStart = std::chrono::steady_clock::now();
  theme.set(flux::Theme::dark());
  auto const toggleElapsed = std::chrono::steady_clock::now() - toggleStart;
  CHECK(std::chrono::duration<float, std::milli>(toggleElapsed).count() < 16.67f);

  REQUIRE(sceneGraph.root().kind() == flux::scenegraph::SceneNodeKind::Rect);
  auto const* updatedNode = &static_cast<flux::scenegraph::RectNode const&>(sceneGraph.root());
  CHECK(updatedNode == initialNode);
  CHECK(bodyCalls == 1);
  CHECK(cleanups == 0);
  checkSameChannels(solidColor(*updatedNode), flux::Theme::dark().windowBackgroundColor);
  CHECK(updatedNode->stroke().color == flux::Theme::dark().separatorColor);

  root.unmount(sceneGraph);
  CHECK(cleanups == 1);
}

TEST_CASE("theme signal resolves semantic gradient fill stops") {
  struct Root {
    flux::Element body() const {
      auto theme = flux::useEnvironment<flux::ThemeKey>();
      return flux::Element{flux::Rectangle{}}
          .size(32.f, 18.f)
          .fill([theme] {
            (void)theme();
            return flux::FillStyle::linearGradient({
                flux::GradientStop{0.f, flux::Color::accent()},
                flux::GradientStop{1.f, flux::Color::danger()},
            });
          });
    }
  };

  flux::Reactive::Signal<flux::Theme> theme{flux::Theme::light()};
  FakeTextSystem textSystem;
  flux::scenegraph::SceneGraph sceneGraph;
  flux::MountRoot root{
      std::make_unique<flux::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      themeBinding(theme),
      flux::Size{200.f, 120.f},
  };

  root.mount(sceneGraph);
  REQUIRE(sceneGraph.root().kind() == flux::scenegraph::SceneNodeKind::Rect);
  auto const* rect = &static_cast<flux::scenegraph::RectNode const&>(sceneGraph.root());

  flux::LinearGradient gradient{};
  REQUIRE(rect->fill().linearGradient(&gradient));
  REQUIRE(gradient.stopCount == 2);
  checkSameChannels(gradient.stops[0].color, flux::Theme::light().accentColor);
  checkSameChannels(gradient.stops[1].color, flux::Theme::light().dangerColor);

  theme.set(flux::Theme::dark());
  REQUIRE(rect->fill().linearGradient(&gradient));
  REQUIRE(gradient.stopCount == 2);
  checkSameChannels(gradient.stops[0].color, flux::Theme::dark().accentColor);
  checkSameChannels(gradient.stops[1].color, flux::Theme::dark().dangerColor);
}

TEST_CASE("element environment applies to modifier bindings on the same retained subtree") {
  struct Root {
    flux::Element body() const {
      return flux::Element{flux::Rectangle{}}
          .size(32.f, 18.f)
          .fill(flux::Color::controlBackground())
          .stroke(flux::Color::separator(), 1.f)
          .environment<flux::ThemeKey>(flux::Theme::dark());
    }
  };

  FakeTextSystem textSystem;
  flux::scenegraph::SceneGraph sceneGraph;
  flux::MountRoot root{
      std::make_unique<flux::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      flux::EnvironmentBinding{}.withValue<flux::ThemeKey>(flux::Theme::light()),
      flux::Size{200.f, 120.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == flux::scenegraph::SceneNodeKind::Rect);
  auto const& rect = static_cast<flux::scenegraph::RectNode const&>(sceneGraph.root());
  checkSameChannels(solidColor(rect), flux::Theme::dark().controlBackgroundColor);
  CHECK(rect.stroke().color == flux::Theme::dark().separatorColor);
}
