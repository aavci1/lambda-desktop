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
  lambda::Signal<int> itemSize{lambda_shell::kDockIconSize};
  bool fullWidth = false;

  lambda::Element body() const {
    lambda::Reactive::Bindable<int> width{[items = items, clockWidth = clockWidth, itemSize = itemSize] {
      return lambda_shell::dockWidth(items(), clockWidth(), itemSize());
    }};
    return lambda::Element{lambda_shell::LambdaDock{lambda_shell::DockProps{
        .items = items,
        .timeText = timeText,
        .clockWidth = clockWidth,
        .itemSize = itemSize,
        .system = lambda::Reactive::Bindable<lambda_shell::SystemStatus>{lambda_shell::SystemStatus{}},
        .fullWidth = fullWidth,
        .width = width,
    }}};
  }
};

void checkDockAppSection(lambda::scenegraph::SceneGraph const& sceneGraph,
                         std::vector<lambda_shell::DockItem> const& expectedItems,
                         int itemSize = lambda_shell::kDockIconSize) {
  auto const& root = sceneGraph.root();
  REQUIRE(root.children().size() == 5);

  auto const& appSection = *root.children()[0];
  auto const& statusSeparator = *root.children()[1];
  auto const& clock = *root.children()[4];

  float const expectedWidth = static_cast<float>(lambda_shell::dockItemsWidth(expectedItems, itemSize));
  float const expectedFrameWidth =
      static_cast<float>(lambda_shell::dockWidth(expectedItems, 116, itemSize));
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
  REQUIRE(statusSeparator.children().size() == 1);
  auto const* separatorLine = statusSeparator.children().front().get();
  while (separatorLine->children().size() == 1) {
    separatorLine = separatorLine->children().front().get();
  }
  CHECK(separatorLine->size().height ==
        doctest::Approx(static_cast<float>(lambda_shell::clampedDockItemSize(itemSize))));
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

TEST_CASE("LambdaDock switches status and clock docklets to single row for compact item size") {
  auto pinned = pinnedDockItems();

  lambda::Signal<std::vector<lambda_shell::DockItem>> items{pinned};
  lambda::Signal<std::string> timeText{"Sat 30 May, 18:31"};
  lambda::Signal<int> clockWidth{148};
  lambda::Signal<int> itemSize{36};

  FakeTextSystem textSystem;
  lambda::scenegraph::SceneGraph sceneGraph;
  lambda::MountRoot root{
      std::make_unique<lambda::TypedRootHolder<DockRoot>>(
          std::in_place, DockRoot{items, timeText, clockWidth, itemSize}),
      textSystem,
      testEnvironment(),
      lambda::Size{1100.f, static_cast<float>(lambda_shell::dockHeight(36))},
  };

  root.mount(sceneGraph);

  auto const& sceneRoot = sceneGraph.root();
  REQUIRE(sceneRoot.children().size() == 5);
  auto const& status = *sceneRoot.children()[2];
  auto const& clock = *sceneRoot.children()[4];

  CHECK(sceneRoot.size().height == doctest::Approx(static_cast<float>(lambda_shell::dockHeight(36))));
  CHECK(status.size().width == doctest::Approx(static_cast<float>(lambda_shell::dockStatusGridWidth(36))));
  CHECK(status.size().height == doctest::Approx(static_cast<float>(lambda_shell::dockStatusGridHeight(36))));
  CHECK(lambda_shell::dockStatusGridRows(36) == 1);
  CHECK(lambda_shell::dockUsesSingleRowDocklets(39));
  CHECK_FALSE(lambda_shell::dockUsesSingleRowDocklets(40));
  CHECK(lambda_shell::dockStatusGridRows(40) == lambda_shell::kDockStatusRows);
  CHECK(lambda_shell::dockStatusCellSize(40) == 16);
  CHECK(lambda_shell::dockStatusIconSize(40) == 16);
  CHECK(lambda_shell::dockStatusGridGap(40) == 8);
  CHECK(lambda_shell::dockStatusGridHeight(40) == 40);
  CHECK(lambda_shell::dockStatusCellSize(48) == 19);
  CHECK(lambda_shell::dockStatusIconSize(48) == 19);
  CHECK(lambda_shell::dockStatusGridGap(48) == 10);
  CHECK(lambda_shell::dockStatusGridHeight(48) == 48);
  CHECK(lambda_shell::dockClockDateRowHeight(48) == doctest::Approx(19.f));
  CHECK(lambda_shell::dockClockTimeRowHeight(48) == doctest::Approx(19.f));
  CHECK(lambda_shell::dockClockRowGap(48) == doctest::Approx(10.f));
  CHECK(clock.size().width == doctest::Approx(148.f));
  CHECK(clock.position().x + clock.size().width <=
        static_cast<float>(lambda_shell::dockWidth(pinned, 148, 36)) -
            static_cast<float>(lambda_shell::kDockPaddingX) + 0.01f);
}

TEST_CASE("LambdaDock full-width layout reserves space before status docklets") {
  auto pinned = pinnedDockItems();

  lambda::Signal<std::vector<lambda_shell::DockItem>> items{pinned};
  lambda::Signal<std::string> timeText{"Sat 30 May, 18:31"};
  lambda::Signal<int> clockWidth{148};
  lambda::Signal<int> itemSize{48};

  FakeTextSystem textSystem;
  lambda::scenegraph::SceneGraph sceneGraph;
  lambda::MountRoot root{
      std::make_unique<lambda::TypedRootHolder<DockRoot>>(
          std::in_place, DockRoot{items, timeText, clockWidth, itemSize, true}),
      textSystem,
      testEnvironment(),
      lambda::Size{1100.f, static_cast<float>(lambda_shell::dockHeight(48))},
  };

  root.mount(sceneGraph);

  auto const* wrapper = &sceneGraph.root();
  if (wrapper->children().size() == 1) {
    wrapper = wrapper->children().front().get();
  }
  REQUIRE(wrapper->children().size() == 2);
  auto const& topBorder = *wrapper->children()[0];
  auto const* stack = wrapper->children()[1].get();
  while (stack->children().size() == 1) {
    stack = stack->children().front().get();
  }
  REQUIRE(stack->children().size() == 6);
  auto const& appSection = *stack->children()[0];
  auto const& spacer = *stack->children()[1];
  auto const& statusSeparator = *stack->children()[2];
  CHECK(wrapper->size().width == doctest::Approx(1100.f));
  CHECK(topBorder.size().width == doctest::Approx(1100.f));
  CHECK(stack->size().width == doctest::Approx(1100.f));
  CHECK(appSection.position().x == doctest::Approx(0.f));
  CHECK(spacer.size().width > 0.f);
  CHECK(statusSeparator.position().x >
        appSection.position().x + appSection.size().width + static_cast<float>(lambda_shell::kDockGap));
}

TEST_CASE("LambdaSessionMenu mounts all power and session actions") {
  int invoked = 0;
  std::string lastAction;
  lambda_shell::SessionMenuProps props{
      .surfaceWidth = static_cast<float>(lambda_shell::kSessionMenuSurfaceWidth),
      .surfaceHeight = static_cast<float>(lambda_shell::kSessionMenuSurfaceHeight),
      .menuX = 12.f,
      .menuY = 8.f,
      .onAction = [&](std::string const& action) {
        ++invoked;
        lastAction = action;
      },
      .onDismiss = [] {},
  };

  FakeTextSystem textSystem;
  lambda::scenegraph::SceneGraph sceneGraph;
  lambda::MountRoot root{
      std::make_unique<lambda::TypedRootHolder<lambda_shell::LambdaSessionMenu>>(
          std::in_place, lambda_shell::LambdaSessionMenu{props}),
      textSystem,
      testEnvironment(),
      lambda::Size{static_cast<float>(lambda_shell::kSessionMenuSurfaceWidth),
                   static_cast<float>(lambda_shell::kSessionMenuSurfaceHeight)},
  };

  root.mount(sceneGraph);
  CHECK(sceneGraph.root().size().width == doctest::Approx(static_cast<float>(lambda_shell::kSessionMenuSurfaceWidth)));
  CHECK(sceneGraph.root().size().height == doctest::Approx(static_cast<float>(lambda_shell::kSessionMenuSurfaceHeight)));
  CHECK(invoked == 0);
  CHECK(lastAction.empty());
}
