#include <doctest/doctest.h>

#include "Shell/UI/LambdaDock.hpp"

#include <Lambda/Graphics/TextSystem.hpp>
#include <Lambda/Reactive/Signal.hpp>
#include <Lambda/SceneGraph/SceneGraph.hpp>
#include <Lambda/SceneGraph/SceneNode.hpp>
#include <Lambda/UI/Detail/RootHolder.hpp>
#include <Lambda/UI/MountRoot.hpp>
#include <Lambda/UI/Theme.hpp>

#include <cmath>
#include <memory>
#include <string>
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
    return {64.f, 18.f};
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

lambda_shell::DockItem launcherItem() {
  return {.id = "launcher", .kind = "launcher", .label = "Launcher", .icon = "lambda"};
}

lambda_shell::DockItem separatorItem() {
  return {.id = "sep1", .kind = "separator"};
}

lambda_shell::DockItem appItem(std::string id, bool running = false) {
  lambda_shell::DockItem item;
  item.id = id;
  item.kind = "app";
  item.label = id;
  item.appId = id;
  item.running = running;
  item.pinned = !running;
  item.icon = id;
  return item;
}

std::vector<lambda_shell::DockItem> pinnedDockItems() {
  return {
      launcherItem(),
      separatorItem(),
      appItem("lambda-files"),
      appItem("lambda-editor"),
      appItem("lambda-preview"),
      appItem("lambda-terminal", true),
      appItem("lambda-settings"),
      appItem("browser"),
  };
}

std::vector<lambda_shell::DockItem> dockItemsWithToggle() {
  auto items = pinnedDockItems();
  items.push_back(appItem("toggle-demo", true));
  return items;
}

struct DockRoot {
  lambda::Signal<std::vector<lambda_shell::DockItem>> items;
  lambda::Signal<std::string> timeText;
  lambda::Signal<int> clockWidth;

  lambda::Element body() const {
    lambda::Reactive::Bindable<int> width{[items = items, clockWidth = clockWidth] {
      return lambda_shell::dockWidth(items(), clockWidth());
    }};
    return lambda::Element{lambda_shell::LambdaDock{lambda_shell::DockProps{
        .items = items,
        .timeText = timeText,
        .clockWidth = clockWidth,
        .system = lambda::Reactive::Bindable<lambda_shell::SystemStatus>{lambda_shell::SystemStatus{}},
        .width = width,
    }}};
  }
};

void checkDockAppSection(lambda::scenegraph::SceneGraph const& sceneGraph,
                         std::vector<lambda_shell::DockItem> const& expectedItems) {
  auto const& root = sceneGraph.root();
  REQUIRE(root.children().size() == 5);

  auto const& appSection = *root.children()[0];
  auto const& statusSeparator = *root.children()[1];
  auto const& clock = *root.children()[4];

  float const expectedWidth = static_cast<float>(lambda_shell::dockItemsWidth(expectedItems));
  float const expectedFrameWidth =
      static_cast<float>(lambda_shell::dockWidth(expectedItems, 116));
  auto const* renderedItems = &appSection;
  while (renderedItems &&
         (std::abs(renderedItems->size().width - expectedWidth) > 0.01f ||
          renderedItems->children().size() != expectedItems.size())) {
    REQUIRE(renderedItems->children().size() == 1);
    renderedItems = renderedItems->children().front().get();
  }
  REQUIRE(renderedItems != nullptr);
  CHECK(appSection.size().width == doctest::Approx(expectedWidth));
  CHECK(renderedItems->size().width == doctest::Approx(expectedWidth));
  CHECK(renderedItems->children().size() == expectedItems.size());
  CHECK(statusSeparator.position().x ==
        doctest::Approx(appSection.position().x + appSection.size().width +
                        static_cast<float>(lambda_shell::kDockGap)));
  CHECK(root.size().width == doctest::Approx(expectedFrameWidth));
  CHECK(appSection.position().x == doctest::Approx(static_cast<float>(lambda_shell::kDockPaddingX)));
  CHECK(clock.position().x + clock.size().width <=
        expectedFrameWidth - static_cast<float>(lambda_shell::kDockPaddingX) + 0.01f);
}

} // namespace

TEST_CASE("LambdaDock app section does not keep stale blank item slots") {
  auto pinned = pinnedDockItems();
  auto withToggle = dockItemsWithToggle();

  lambda::Signal<std::vector<lambda_shell::DockItem>> items{pinned};
  lambda::Signal<std::string> timeText{"Sat 30 May, 18:31"};
  lambda::Signal<int> clockWidth{116};

  FakeTextSystem textSystem;
  lambda::scenegraph::SceneGraph sceneGraph;
  lambda::MountRoot root{
      std::make_unique<lambda::TypedRootHolder<DockRoot>>(
          std::in_place, DockRoot{items, timeText, clockWidth}),
      textSystem,
      testEnvironment(),
      lambda::Size{900.f, static_cast<float>(lambda_shell::dockHeight())},
  };

  root.mount(sceneGraph);
  checkDockAppSection(sceneGraph, pinned);

  items.set(withToggle);
  checkDockAppSection(sceneGraph, withToggle);

  items.set(pinned);
  checkDockAppSection(sceneGraph, pinned);
}
