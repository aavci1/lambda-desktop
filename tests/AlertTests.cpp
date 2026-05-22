#include <doctest/doctest.h>

#include <Flux/Graphics/TextSystem.hpp>
#include <Flux/Reactive/Scope.hpp>
#include <Flux/SceneGraph/SceneNode.hpp>
#include <Flux/UI/Environment.hpp>
#include <Flux/UI/MeasureContext.hpp>
#include <Flux/UI/MountContext.hpp>
#include <Flux/UI/Theme.hpp>
#include <Flux/UI/Views/Alert.hpp>

#include "UI/ViewLayout/OverlayLayout.hpp"
#include "UI/Views/AlertActionHelpers.hpp"

#include <functional>
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
    return {64.f, 18.f};
  }

  flux::Size measure(std::string_view text, flux::Font const&, flux::Color const&, float,
                     flux::TextLayoutOptions const&) override {
    return {std::max(24.f, static_cast<float>(text.size()) * 7.f), 18.f};
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

} // namespace

TEST_CASE("Alert action wrapper preserves the original action after dismiss tears down the owner") {
  bool ranAction = false;
  std::function<void()> wrapped;

  wrapped = flux::detail::wrapDismissThenInvoke(
      [&wrapped] { wrapped = {}; },
      [&ranAction] { ranAction = true; });

  REQUIRE(wrapped);
  wrapped();
  CHECK(ranAction);
}

TEST_CASE("Alert mounts as an intrinsic card for overlay centering") {
  FakeTextSystem textSystem;
  flux::Reactive::Scope scope;
  flux::Theme const theme = flux::Theme::light();
  flux::EnvironmentBinding environment =
      flux::EnvironmentBinding{}.withValue<flux::ThemeKey>(theme);
  flux::MeasureContext measureContext{textSystem, environment};

  flux::LayoutConstraints constraints{
      .maxWidth = 800.f,
      .maxHeight = 600.f,
      .minWidth = 0.f,
      .minHeight = 0.f,
  };
  flux::MountContext context{scope, textSystem, measureContext, constraints, {}, {}, environment};
  flux::Element alertElement{flux::Alert{
      .title = "Delete item?",
      .message = "This action cannot be undone.",
      .buttons = {flux::AlertButton{.label = "Cancel"},
                  flux::AlertButton{.label = "Delete",
                                    .variant = flux::ButtonVariant::Destructive}},
  }};

  std::unique_ptr<flux::scenegraph::SceneNode> node = alertElement.mount(context);

  REQUIRE(node);
  CHECK(node->bounds().width == doctest::Approx(360.f));
  CHECK(node->bounds().height > 0.f);
  REQUIRE(node->children().size() == 1);

  flux::scenegraph::SceneNode const& cardContent = *node->children()[0];
  float const contentWidth = 360.f - 2.f * theme.space6;
  CHECK(cardContent.bounds().width == doctest::Approx(contentWidth));
  REQUIRE(cardContent.children().size() == 3);

  flux::scenegraph::SceneNode const& title = *cardContent.children()[0];
  flux::scenegraph::SceneNode const& message = *cardContent.children()[1];
  flux::scenegraph::SceneNode const& buttonRow = *cardContent.children()[2];
  CHECK(title.bounds().height > 0.f);
  CHECK(message.bounds().height > 0.f);
  CHECK(message.position().y > title.position().y + title.bounds().height);
  CHECK(buttonRow.position().y > message.position().y + message.bounds().height);
  CHECK(buttonRow.bounds().width == doctest::Approx(contentWidth));
  REQUIRE(buttonRow.children().size() == 2);
  float const firstButtonWidth = buttonRow.children()[0]->bounds().width;
  float const secondButtonWidth = buttonRow.children()[1]->bounds().width;
  CHECK(firstButtonWidth > 0.f);
  CHECK(secondButtonWidth > 0.f);
  CHECK(buttonRow.children()[0]->position().x == doctest::Approx(0.f));
  CHECK(buttonRow.children()[1]->position().x > firstButtonWidth);
  CHECK(buttonRow.children()[1]->position().x + secondButtonWidth <= contentWidth + 2.f);

  flux::OverlayConfig config{};
  flux::Rect const frame =
      flux::layout::resolveOverlayFrame({800.f, 600.f}, config, node->bounds());
  CHECK(frame.x == doctest::Approx(220.f));
  CHECK(frame.width == doctest::Approx(360.f));
}
