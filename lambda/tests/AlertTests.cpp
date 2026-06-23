#include <doctest/doctest.h>

#include <Lambda/Graphics/TextSystem.hpp>
#include <Lambda/Reactive/Scope.hpp>
#include <Lambda/SceneGraph/SceneNode.hpp>
#include <Lambda/UI/Environment.hpp>
#include <Lambda/UI/MeasureContext.hpp>
#include <Lambda/UI/MountContext.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/Views/Alert.hpp>

#include "UI/ViewLayout/OverlayLayout.hpp"
#include "UI/Views/AlertActionHelpers.hpp"

#include <functional>
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
    return {64.f, 18.f};
  }

  lambdaui::Size measure(std::string_view text, lambdaui::Font const&, lambdaui::Color const&, float,
                     lambdaui::TextLayoutOptions const&) override {
    return {std::max(24.f, static_cast<float>(text.size()) * 7.f), 18.f};
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

} // namespace

TEST_CASE("Alert action wrapper preserves the original action after dismiss tears down the owner") {
  bool ranAction = false;
  std::function<void()> wrapped;

  wrapped = lambdaui::detail::wrapDismissThenInvoke(
      [&wrapped] { wrapped = {}; },
      [&ranAction] { ranAction = true; });

  REQUIRE(wrapped);
  wrapped();
  CHECK(ranAction);
}

TEST_CASE("Alert mounts as an intrinsic card for overlay centering") {
  FakeTextSystem textSystem;
  lambdaui::Reactive::Scope scope;
  lambdaui::Theme const theme = lambdaui::Theme::light();
  lambdaui::EnvironmentBinding environment =
      lambdaui::EnvironmentBinding{}.withValue<lambdaui::ThemeKey>(theme);
  lambdaui::MeasureContext measureContext{textSystem, environment};

  lambdaui::LayoutConstraints constraints{
      .maxWidth = 800.f,
      .maxHeight = 600.f,
      .minWidth = 0.f,
      .minHeight = 0.f,
  };
  lambdaui::MountContext context{scope, textSystem, measureContext, constraints, {}, {}, environment};
  lambdaui::Element alertElement{lambdaui::Alert{
      .title = "Delete item?",
      .message = "This action cannot be undone.",
      .buttons = {lambdaui::AlertButton{.label = "Cancel"},
                  lambdaui::AlertButton{.label = "Delete",
                                    .variant = lambdaui::ButtonVariant::Destructive}},
  }};

  std::unique_ptr<lambdaui::scenegraph::SceneNode> node = alertElement.mount(context);

  REQUIRE(node);
  CHECK(node->bounds().width == doctest::Approx(360.f));
  CHECK(node->bounds().height > 0.f);
  REQUIRE(node->children().size() == 1);

  lambdaui::scenegraph::SceneNode const& cardContent = *node->children()[0];
  float const contentWidth = 360.f - 2.f * theme.space6;
  CHECK(cardContent.bounds().width == doctest::Approx(contentWidth));
  REQUIRE(cardContent.children().size() == 3);

  lambdaui::scenegraph::SceneNode const& title = *cardContent.children()[0];
  lambdaui::scenegraph::SceneNode const& message = *cardContent.children()[1];
  lambdaui::scenegraph::SceneNode const& buttonRow = *cardContent.children()[2];
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

  lambdaui::OverlayConfig config{};
  lambdaui::Rect const frame =
      lambdaui::layout::resolveOverlayFrame({800.f, 600.f}, config, node->bounds());
  CHECK(frame.x == doctest::Approx(220.f));
  CHECK(frame.width == doctest::Approx(360.f));
}
