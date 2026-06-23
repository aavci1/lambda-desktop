#include <doctest/doctest.h>

#include <Lambda/UI/Detail/RootHolder.hpp>
#include <Lambda/Graphics/TextSystem.hpp>
#include <Lambda/Reactive/Signal.hpp>
#include <Lambda/SceneGraph/SceneNode.hpp>
#include <Lambda/UI/InteractionData.hpp>
#include <Lambda/SceneGraph/RectNode.hpp>
#include <Lambda/SceneGraph/SceneGraph.hpp>
#include <Lambda/UI/MeasureContext.hpp>
#include <Lambda/UI/MountRoot.hpp>
#include <Lambda/UI/MountContext.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/Views/Grid.hpp>
#include <Lambda/UI/Views/HStack.hpp>
#include <Lambda/UI/Views/Popover.hpp>
#include <Lambda/UI/Views/PopoverCalloutShape.hpp>
#include <Lambda/UI/Views/Rectangle.hpp>
#include <Lambda/UI/Views/ScaleAroundCenter.hpp>
#include <Lambda/UI/Views/ScrollView.hpp>
#include <Lambda/UI/Views/Spacer.hpp>
#include <Lambda/UI/Views/Text.hpp>
#include <Lambda/UI/Views/TextInput.hpp>
#include <Lambda/UI/Views/VStack.hpp>
#include <Lambda/UI/Views/ZStack.hpp>

#include <cmath>
#include <limits>
#include <memory>
#include <optional>
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

class MeasuringTextSystem final : public lambdaui::TextSystem {
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
    return {80.f, 16.f};
  }

  lambdaui::Size measure(std::string_view text, lambdaui::Font const&, lambdaui::Color const&, float,
                     lambdaui::TextLayoutOptions const&) override {
    return {std::max(24.f, static_cast<float>(text.size()) * 6.f), 16.f};
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

lambdaui::Color solidColor(lambdaui::scenegraph::RectNode const& rect) {
  lambdaui::Color color{};
  CHECK(rect.fill().solidColor(&color));
  return color;
}

struct IntrinsicBox {
  lambdaui::Size measure(lambdaui::MeasureContext& ctx, lambdaui::LayoutConstraints const&,
                     lambdaui::LayoutHints const&, lambdaui::TextSystem&) const {
    ctx.advanceChildSlot();
    return {24.f, 12.f};
  }

  std::unique_ptr<lambdaui::scenegraph::SceneNode> mount(lambdaui::MountContext&) const {
    return std::make_unique<lambdaui::scenegraph::RectNode>(
        lambdaui::Rect{0.f, 0.f, 24.f, 12.f});
  }
};

struct StretchBox {
  lambdaui::Size intrinsic{24.f, 12.f};

  lambdaui::Size measure(lambdaui::MeasureContext& ctx, lambdaui::LayoutConstraints const&,
                     lambdaui::LayoutHints const&, lambdaui::TextSystem&) const {
    ctx.advanceChildSlot();
    return intrinsic;
  }

  std::unique_ptr<lambdaui::scenegraph::SceneNode> mount(lambdaui::MountContext& ctx) const {
    auto sizeFor = [intrinsic = intrinsic](lambdaui::LayoutConstraints const& constraints) {
      lambdaui::Size size{
          std::isfinite(constraints.maxWidth) ? constraints.maxWidth : intrinsic.width,
          std::isfinite(constraints.maxHeight) ? constraints.maxHeight : intrinsic.height,
      };
      size.width = std::max(size.width, constraints.minWidth);
      size.height = std::max(size.height, constraints.minHeight);
      return size;
    };

    lambdaui::Size const initialSize = sizeFor(ctx.constraints());
    auto group = std::make_unique<lambdaui::scenegraph::SceneNode>(
        lambdaui::Rect{0.f, 0.f, initialSize.width, initialSize.height});
    auto* rawGroup = group.get();
    rawGroup->setRelayout([rawGroup, sizeFor = std::move(sizeFor)](
                              lambdaui::LayoutConstraints const& constraints) {
      rawGroup->setSize(sizeFor(constraints));
    });
    return group;
  }
};

lambdaui::LayoutConstraints fixedConstraints(lambdaui::Size size) {
  return lambdaui::LayoutConstraints{
      .maxWidth = std::max(0.f, size.width),
      .maxHeight = std::max(0.f, size.height),
      .minWidth = std::max(0.f, size.width),
      .minHeight = std::max(0.f, size.height),
  };
}

struct RelayoutProbeFrame {
  lambdaui::Element child;
  int* relayouts = nullptr;

  lambdaui::Size measure(lambdaui::MeasureContext& ctx, lambdaui::LayoutConstraints const&,
                     lambdaui::LayoutHints const&, lambdaui::TextSystem&) const {
    ctx.advanceChildSlot();
    return {100.f, 100.f};
  }

  std::unique_ptr<lambdaui::scenegraph::SceneNode> mount(lambdaui::MountContext& ctx) const {
    auto group = std::make_unique<lambdaui::scenegraph::SceneNode>(
        lambdaui::Rect{0.f, 0.f, 100.f, 100.f});
    lambdaui::MountContext childCtx = ctx.childWithSharedScope(fixedConstraints({100.f, 100.f}), ctx.hints());
    auto childNode = child.mount(childCtx);
    lambdaui::scenegraph::SceneNode* rawChild = childNode.get();
    if (childNode) {
      group->appendChild(std::move(childNode));
    }
    auto* rawGroup = group.get();
    rawGroup->setLayoutConstraints(ctx.constraints());
    rawGroup->setRelayout([rawGroup, rawChild, relayouts = relayouts](
                              lambdaui::LayoutConstraints const&) {
      if (relayouts) {
        ++*relayouts;
      }
      if (rawChild) {
        rawChild->relayout(fixedConstraints({100.f, 100.f}));
      }
      rawGroup->setSize({100.f, 100.f});
    });
    return group;
  }
};

struct RelayoutPassthroughFrame {
  lambdaui::Element child;

  lambdaui::Size measure(lambdaui::MeasureContext& ctx, lambdaui::LayoutConstraints const& constraints,
                     lambdaui::LayoutHints const& hints, lambdaui::TextSystem& textSystem) const {
    return child.measure(ctx, constraints, hints, textSystem);
  }

  std::unique_ptr<lambdaui::scenegraph::SceneNode> mount(lambdaui::MountContext& ctx) const {
    auto group = std::make_unique<lambdaui::scenegraph::SceneNode>();
    lambdaui::MountContext childCtx = ctx.childWithSharedScope(ctx.constraints(), ctx.hints());
    auto childNode = child.mount(childCtx);
    lambdaui::scenegraph::SceneNode* rawChild = childNode.get();
    if (childNode) {
      group->setSize(childNode->size());
      group->appendChild(std::move(childNode));
    }
    lambdaui::scenegraph::SceneNode* rawGroup = group.get();
    rawGroup->setLayoutConstraints(ctx.constraints());
    rawGroup->setRelayout([rawGroup, rawChild](lambdaui::LayoutConstraints const& constraints) {
      if (rawChild) {
        rawChild->relayout(constraints);
        rawGroup->setSize(rawChild->size());
      }
    });
    return group;
  }
};

struct DeepRelayoutNode {
  int depth = 0;
  lambdaui::Reactive::Signal<float> width;

  lambdaui::Element body() const {
    if (depth <= 0) {
      return lambdaui::Element{lambdaui::Rectangle{}}
          .size([width = width] {
                  return width.get();
                },
                10.f);
    }
    return lambdaui::Element{RelayoutPassthroughFrame{
        .child = lambdaui::Element{DeepRelayoutNode{depth - 1, width}},
    }};
  }
};

} // namespace

TEST_CASE("MountRoot mounts a static root once") {
  int bodyCalls = 0;

  struct Root {
    int* bodyCalls = nullptr;

    lambdaui::Element body() const {
      ++*bodyCalls;
      return lambdaui::Element{lambdaui::Rectangle{}}
          .size(20.f, 30.f)
          .fill(lambdaui::Colors::red);
    }
  };

  FakeTextSystem textSystem;
  lambdaui::scenegraph::SceneGraph sceneGraph;
  lambdaui::MountRoot root{
      std::make_unique<lambdaui::TypedRootHolder<Root>>(std::in_place, Root{&bodyCalls}),
      textSystem,
      testEnvironment(),
      lambdaui::Size{200.f, 100.f},
  };

  root.mount(sceneGraph);

  CHECK(root.mounted());
  CHECK(bodyCalls == 1);
  REQUIRE(sceneGraph.root().kind() == lambdaui::scenegraph::SceneNodeKind::Rect);
  auto const& rect = static_cast<lambdaui::scenegraph::RectNode const&>(sceneGraph.root());
  CHECK(rect.size() == lambdaui::Size{20.f, 30.f});
  CHECK(solidColor(rect) == lambdaui::Colors::red);
}

TEST_CASE("Popover body mounts callout chrome and reserves arrow depth") {
  struct Root {
    lambdaui::Element body() const {
      return lambdaui::Popover{
          .content = lambdaui::Element{lambdaui::Rectangle{}}
                         .size(100.f, 20.f)
                         .fill(lambdaui::Colors::blue),
          .placement = lambdaui::PopoverPlacement::Below,
          .arrow = true,
      };
    }
  };

  lambdaui::Theme const theme = lambdaui::Theme::light();
  FakeTextSystem textSystem;
  lambdaui::scenegraph::SceneGraph sceneGraph;
  lambdaui::MountRoot root{
      std::make_unique<lambdaui::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      lambdaui::EnvironmentBinding{}.withValue<lambdaui::ThemeKey>(theme),
      lambdaui::Size{300.f, 200.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == lambdaui::scenegraph::SceneNodeKind::Group);
  CHECK(sceneGraph.root().size().width == doctest::Approx(100.f + 2.f * theme.space3));
  CHECK(sceneGraph.root().size().height ==
        doctest::Approx(20.f + 2.f * theme.space3 + lambdaui::PopoverCalloutShape::kArrowH));
  REQUIRE(sceneGraph.root().children().size() == 2);
  CHECK(sceneGraph.root().children()[0]->kind() == lambdaui::scenegraph::SceneNodeKind::Path);
  CHECK(sceneGraph.root().children()[1]->position().y ==
        doctest::Approx(lambdaui::PopoverCalloutShape::kArrowH + theme.space3));
}

TEST_CASE("Popover body follows resolved overlay placement from environment") {
  struct Root {
    lambdaui::Element body() const {
      return lambdaui::Popover{
          .content = lambdaui::Element{lambdaui::Rectangle{}}
                         .size(100.f, 20.f)
                         .fill(lambdaui::Colors::blue),
          .placement = lambdaui::PopoverPlacement::Below,
          .arrow = true,
      };
    }
  };

  lambdaui::Theme const theme = lambdaui::Theme::light();
  FakeTextSystem textSystem;
  lambdaui::scenegraph::SceneGraph sceneGraph;
  lambdaui::MountRoot root{
      std::make_unique<lambdaui::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      lambdaui::EnvironmentBinding{}
          .withValue<lambdaui::ThemeKey>(theme)
          .withValue<lambdaui::ResolvedOverlayPlacementKey>(
              std::optional<lambdaui::OverlayConfig::Placement>{lambdaui::OverlayConfig::Placement::Above}),
      lambdaui::Size{300.f, 200.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == lambdaui::scenegraph::SceneNodeKind::Group);
  REQUIRE(sceneGraph.root().children().size() == 2);
  CHECK(sceneGraph.root().children()[1]->position().y == doctest::Approx(theme.space3));
}

TEST_CASE("composite child body is materialized once across measure and mount") {
  int bodyCalls = 0;

  struct Probe {
    int* bodyCalls = nullptr;

    lambdaui::Element body() const {
      ++*bodyCalls;
      return lambdaui::Rectangle{}.size(20.f, 10.f);
    }
  };

  struct Root {
    int* bodyCalls = nullptr;

    lambdaui::Element body() const {
      return lambdaui::VStack{
          .children = lambdaui::children(Probe{bodyCalls}),
      };
    }
  };

  FakeTextSystem textSystem;
  lambdaui::scenegraph::SceneGraph sceneGraph;
  lambdaui::MountRoot root{
      std::make_unique<lambdaui::TypedRootHolder<Root>>(std::in_place, Root{&bodyCalls}),
      textSystem,
      testEnvironment(),
      lambdaui::Size{200.f, 100.f},
  };

  root.mount(sceneGraph);

  CHECK(bodyCalls == 1);
  REQUIRE(sceneGraph.root().kind() == lambdaui::scenegraph::SceneNodeKind::Group);
  REQUIRE(sceneGraph.root().children().size() == 1);
  CHECK(sceneGraph.root().children()[0]->size() == lambdaui::Size{20.f, 10.f});
}

TEST_CASE("interaction hooks attach reactive signals to mounted interaction data") {
  struct Root {
    lambdaui::Element body() const {
      lambdaui::Reactive::Signal<bool> hovered = lambdaui::useHover();
      lambdaui::Reactive::Signal<bool> pressed = lambdaui::usePress();
      lambdaui::Reactive::Signal<bool> focused = lambdaui::useFocus();
      lambdaui::Reactive::Signal<bool> keyboardFocused = lambdaui::useKeyboardFocus();
      return lambdaui::Rectangle{}
          .size(20.f, 10.f)
          .fill([hovered, pressed, focused, keyboardFocused] {
            if (keyboardFocused.get()) {
              return lambdaui::Colors::yellow;
            }
            if (focused.get()) {
              return lambdaui::Colors::blue;
            }
            if (pressed.get()) {
              return lambdaui::Colors::green;
            }
            if (hovered.get()) {
              return lambdaui::Colors::red;
            }
            return lambdaui::Colors::black;
          })
          .focusable(true)
          .onTap([] {});
    }
  };

  FakeTextSystem textSystem;
  lambdaui::scenegraph::SceneGraph sceneGraph;
  lambdaui::MountRoot root{
      std::make_unique<lambdaui::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      testEnvironment(),
      lambdaui::Size{200.f, 100.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == lambdaui::scenegraph::SceneNodeKind::Rect);
  auto const& rect = static_cast<lambdaui::scenegraph::RectNode const&>(sceneGraph.root());
  auto const* interaction = lambdaui::interactionData(rect);
  REQUIRE(interaction != nullptr);
  CHECK(solidColor(rect) == lambdaui::Colors::black);

  interaction->hoverSignal.set(true);
  CHECK(solidColor(rect) == lambdaui::Colors::red);
  interaction->hoverSignal.set(false);
  CHECK(solidColor(rect) == lambdaui::Colors::black);

  interaction->pressSignal.set(true);
  CHECK(solidColor(rect) == lambdaui::Colors::green);
  interaction->pressSignal.set(false);
  CHECK(solidColor(rect) == lambdaui::Colors::black);

  interaction->focusSignal.set(true);
  CHECK(solidColor(rect) == lambdaui::Colors::blue);
  interaction->focusSignal.set(false);
  CHECK(solidColor(rect) == lambdaui::Colors::black);

  interaction->keyboardFocusSignal.set(true);
  CHECK(solidColor(rect) == lambdaui::Colors::yellow);
  interaction->keyboardFocusSignal.set(false);
  CHECK(solidColor(rect) == lambdaui::Colors::black);
}

TEST_CASE("modifier envelopes honor fixed viewport constraints") {
  struct Root {
    lambdaui::Element body() const {
      return lambdaui::Element{IntrinsicBox{}}
          .onTap([] {});
    }
  };

  FakeTextSystem textSystem;
  lambdaui::scenegraph::SceneGraph sceneGraph;
  lambdaui::MountRoot root{
      std::make_unique<lambdaui::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      testEnvironment(),
      lambdaui::Size{200.f, 100.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == lambdaui::scenegraph::SceneNodeKind::Rect);
  CHECK(sceneGraph.root().size() == lambdaui::Size{200.f, 100.f});
  REQUIRE(sceneGraph.root().children().size() == 1);
  CHECK(sceneGraph.root().children()[0]->size() == lambdaui::Size{24.f, 12.f});
}

TEST_CASE("HStack flex children honor assigned main-axis size with explicit modifiers") {
  struct Root {
    lambdaui::Element body() const {
      return lambdaui::HStack{
          .spacing = 12.f,
          .alignment = lambdaui::Alignment::Center,
          .children = lambdaui::children(
              lambdaui::Element{lambdaui::Rectangle{}}
                  .size(56.f, 54.f)
                  .fill(lambdaui::Colors::red)
                  .flex(2.f, 1.f, 0.f),
              lambdaui::Rectangle{}.size(56.f, 76.f),
              lambdaui::Element{lambdaui::Rectangle{}}
                  .size(56.f, 40.f)
                  .fill(lambdaui::Colors::blue)
                  .flex(1.f, 1.f, 0.f),
              lambdaui::Rectangle{}.size(56.f, 54.f)),
      };
    }
  };

  FakeTextSystem textSystem;
  lambdaui::scenegraph::SceneGraph sceneGraph;
  lambdaui::MountRoot root{
      std::make_unique<lambdaui::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      testEnvironment(),
      lambdaui::Size{704.f, 100.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == lambdaui::scenegraph::SceneNodeKind::Group);
  auto const& group = sceneGraph.root();
  REQUIRE(group.children().size() == 4);
  CHECK(group.children()[0]->size().width == doctest::Approx(370.666f).epsilon(0.001));
  CHECK(group.children()[2]->size().width == doctest::Approx(185.333f).epsilon(0.001));
  CHECK(group.children()[1]->position().x == doctest::Approx(382.666f).epsilon(0.001));
  CHECK(group.children()[3]->position().x == doctest::Approx(648.f));
}

TEST_CASE("Spacer as composite expands to fill main axis in HStack") {
  struct Root {
    lambdaui::Element body() const {
      return lambdaui::HStack{
          .spacing = 0.f,
          .alignment = lambdaui::Alignment::Stretch,
          .children = lambdaui::children(
              lambdaui::Rectangle{}.size(20.f, 10.f),
              lambdaui::Spacer{},
              lambdaui::Rectangle{}.size(30.f, 10.f)),
      };
    }
  };

  FakeTextSystem textSystem;
  lambdaui::scenegraph::SceneGraph sceneGraph;
  lambdaui::MountRoot root{
      std::make_unique<lambdaui::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      testEnvironment(),
      lambdaui::Size{200.f, 40.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == lambdaui::scenegraph::SceneNodeKind::Group);
  auto const& group = sceneGraph.root();
  REQUIRE(group.children().size() == 3);
  CHECK(group.children()[0]->size().width == doctest::Approx(20.f));
  CHECK(group.children()[1]->position().x == doctest::Approx(20.f));
  CHECK(group.children()[1]->size().width == doctest::Approx(150.f));
  CHECK(group.children()[2]->position().x == doctest::Approx(170.f));
  CHECK(group.children()[2]->size().width == doctest::Approx(30.f));
}

TEST_CASE("Spacer as composite respects user-set minMainSize override") {
  struct Root {
    lambdaui::Element body() const {
      return lambdaui::HStack{
          .spacing = 0.f,
          .alignment = lambdaui::Alignment::Stretch,
          .children = lambdaui::children(
              lambdaui::Spacer{}.minMainSize(40.f),
              lambdaui::Rectangle{}.size(10.f, 10.f)),
      };
    }
  };

  FakeTextSystem textSystem;
  lambdaui::scenegraph::SceneGraph sceneGraph;
  lambdaui::MountRoot root{
      std::make_unique<lambdaui::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      testEnvironment(),
      lambdaui::Size{30.f, 40.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == lambdaui::scenegraph::SceneNodeKind::Group);
  auto const& group = sceneGraph.root();
  REQUIRE(group.children().size() == 2);
  CHECK(group.children()[0]->size().width == doctest::Approx(40.f));
  CHECK(group.children()[1]->position().x == doctest::Approx(40.f));
}

TEST_CASE("Grid expands row tracks when flex assigns extra height") {
  struct Root {
    lambdaui::Element body() const {
      return lambdaui::VStack{
          .spacing = 0.f,
          .alignment = lambdaui::Alignment::Stretch,
          .children = lambdaui::children(
              lambdaui::Grid{
                  .columns = 1,
                  .horizontalSpacing = 0.f,
                  .verticalSpacing = 8.f,
                  .verticalAlignment = lambdaui::Alignment::Stretch,
                  .children = lambdaui::children(
                      StretchBox{{20.f, 10.f}},
                      StretchBox{{20.f, 160.f}},
                      StretchBox{{20.f, 10.f}},
                      StretchBox{{20.f, 10.f}}),
              }.flex(1.f, 1.f, 0.f)),
      };
    }
  };

  FakeTextSystem textSystem;
  lambdaui::scenegraph::SceneGraph sceneGraph;
  lambdaui::MountRoot root{
      std::make_unique<lambdaui::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      testEnvironment(),
      lambdaui::Size{100.f, 400.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == lambdaui::scenegraph::SceneNodeKind::Group);
  REQUIRE(sceneGraph.root().children().size() == 1);
  lambdaui::scenegraph::SceneNode const& gridNode = *sceneGraph.root().children()[0];
  CHECK(gridNode.size().height == doctest::Approx(400.f));
  REQUIRE(gridNode.children().size() == 4);

  CHECK(gridNode.children()[0]->position().y == doctest::Approx(0.f));
  CHECK(gridNode.children()[0]->size().height == doctest::Approx(72.f));
  CHECK(gridNode.children()[1]->position().y == doctest::Approx(80.f));
  CHECK(gridNode.children()[1]->size().height == doctest::Approx(160.f));
  CHECK(gridNode.children()[2]->position().y == doctest::Approx(248.f));
  CHECK(gridNode.children()[2]->size().height == doctest::Approx(72.f));
  CHECK(gridNode.children()[3]->position().y == doctest::Approx(328.f));
  CHECK(gridNode.children()[3]->size().height == doctest::Approx(72.f));
}

TEST_CASE("reactive size changes relayout ancestor stack alignment") {
  struct Root {
    lambdaui::Reactive::Signal<float> barHeight;

    lambdaui::Element body() const {
      return lambdaui::ZStack{
          .horizontalAlignment = lambdaui::Alignment::Center,
          .verticalAlignment = lambdaui::Alignment::Center,
          .children = lambdaui::children(
              lambdaui::Rectangle{}.size(100.f, 100.f),
              lambdaui::HStack{
                  .spacing = 8.f,
                  .alignment = lambdaui::Alignment::Center,
                  .children = lambdaui::children(
                      lambdaui::Rectangle{}.size(
                          20.f,
                          [barHeight = barHeight] {
                            return barHeight.get();
                          }),
                      lambdaui::Rectangle{}.size(20.f, 20.f)),
              }),
      };
    }
  };

  FakeTextSystem textSystem;
  lambdaui::scenegraph::SceneGraph sceneGraph;
  lambdaui::Reactive::Signal<float> barHeight{20.f};
  lambdaui::MountRoot root{
      std::make_unique<lambdaui::TypedRootHolder<Root>>(std::in_place, Root{barHeight}),
      textSystem,
      testEnvironment(),
      lambdaui::Size{100.f, 100.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == lambdaui::scenegraph::SceneNodeKind::Group);
  auto const& zstack = sceneGraph.root();
  REQUIRE(zstack.children().size() == 2);
  auto const& row = *zstack.children()[1];
  REQUIRE(row.kind() == lambdaui::scenegraph::SceneNodeKind::Group);
  REQUIRE(row.children().size() == 2);
  CHECK(row.position().y == doctest::Approx(40.f));
  CHECK(row.children()[1]->position().y == doctest::Approx(0.f));

  barHeight.set(60.f);

  CHECK(row.size().height == doctest::Approx(60.f));
  CHECK(row.position().y == doctest::Approx(20.f));
  CHECK(row.children()[1]->position().y == doctest::Approx(20.f));
}

TEST_CASE("reactive size relayout propagates through a 32-level scene tree") {
  FakeTextSystem textSystem;
  lambdaui::Reactive::Scope scope;
  lambdaui::MeasureContext measureContext{textSystem, testEnvironment()};
  lambdaui::MountContext context{
      scope,
      textSystem,
      measureContext,
      lambdaui::LayoutConstraints{
          .maxWidth = std::numeric_limits<float>::infinity(),
          .maxHeight = std::numeric_limits<float>::infinity(),
          .minWidth = 0.f,
          .minHeight = 0.f,
      },
  };
  lambdaui::Reactive::Signal<float> width{20.f};
  lambdaui::Element root{DeepRelayoutNode{32, width}};

  std::unique_ptr<lambdaui::scenegraph::SceneNode> node = root.mount(context);

  REQUIRE(node);
  CHECK(node->size().width == doctest::Approx(20.f));

  width.set(48.f);

  CHECK(node->size().width == doctest::Approx(48.f));
}

TEST_CASE("reactive size relayout stops at unchanged ancestors") {
  struct Root {
    lambdaui::Reactive::Signal<float> barHeight;
    int* outerRelayouts = nullptr;

    lambdaui::Element body() const {
      return RelayoutProbeFrame{
          .child = lambdaui::Element{lambdaui::ZStack{
              .horizontalAlignment = lambdaui::Alignment::Center,
              .verticalAlignment = lambdaui::Alignment::Center,
              .children = lambdaui::children(
                  lambdaui::Rectangle{}.size(100.f, 100.f),
                  lambdaui::HStack{
                      .spacing = 8.f,
                      .alignment = lambdaui::Alignment::Center,
                      .children = lambdaui::children(
                          lambdaui::Rectangle{}.size(
                              20.f,
                              [barHeight = barHeight] {
                                return barHeight.get();
                              }),
                          lambdaui::Rectangle{}.size(20.f, 20.f)),
                  }),
          }},
          .relayouts = outerRelayouts,
      };
    }
  };

  FakeTextSystem textSystem;
  lambdaui::scenegraph::SceneGraph sceneGraph;
  lambdaui::Reactive::Signal<float> barHeight{20.f};
  int outerRelayouts = 0;
  lambdaui::MountRoot root{
      std::make_unique<lambdaui::TypedRootHolder<Root>>(
          std::in_place, Root{barHeight, &outerRelayouts}),
      textSystem,
      testEnvironment(),
      lambdaui::Size{100.f, 100.f},
  };

  root.mount(sceneGraph);
  barHeight.set(60.f);

  CHECK(outerRelayouts == 0);
}

TEST_CASE("MountContext childWithOwnScope creates a scoped owner") {
  lambdaui::Reactive::Scope rootScope;
  FakeTextSystem textSystem;
  lambdaui::MeasureContext measureContext{textSystem, testEnvironment()};
  lambdaui::MountContext rootContext{
      rootScope,
      textSystem,
      measureContext,
      lambdaui::LayoutConstraints{.maxWidth = 100.f, .maxHeight = 100.f},
  };

  int childCleanups = 0;
  {
    lambdaui::MountContext childContext =
        rootContext.childWithOwnScope(lambdaui::LayoutConstraints{.maxWidth = 40.f, .maxHeight = 20.f});
    CHECK(&childContext.owner() != &rootContext.owner());
    childContext.owner().onCleanup([&childCleanups] {
      ++childCleanups;
    });
  }

  CHECK(childCleanups == 0);
  rootScope.dispose();
  CHECK(childCleanups == 1);
}

TEST_CASE("MountContext childWithSharedScope reuses parent owner") {
  lambdaui::Reactive::Scope rootScope;
  FakeTextSystem textSystem;
  lambdaui::MeasureContext measureContext{textSystem, testEnvironment()};
  lambdaui::MountContext rootContext{
      rootScope,
      textSystem,
      measureContext,
      lambdaui::LayoutConstraints{.maxWidth = 100.f, .maxHeight = 100.f},
  };

  lambdaui::MountContext childContext =
      rootContext.childWithSharedScope(lambdaui::LayoutConstraints{.maxWidth = 40.f, .maxHeight = 20.f});
  CHECK(&childContext.owner() == &rootContext.owner());
}

TEST_CASE("MountRoot resize relayouts without remounting root state") {
  int bodyCalls = 0;

  struct Root {
    int* bodyCalls = nullptr;

    lambdaui::Element body() const {
      ++*bodyCalls;
      auto width = lambdaui::useState(20.f);
      return lambdaui::Element{lambdaui::Rectangle{}}
          .width([width] {
            return width.get();
          })
          .height(10.f)
          .onTap([width] {
            width.set(64.f);
          });
    }
  };

  FakeTextSystem textSystem;
  lambdaui::scenegraph::SceneGraph sceneGraph;
  lambdaui::MountRoot root{
      std::make_unique<lambdaui::TypedRootHolder<Root>>(std::in_place, Root{&bodyCalls}),
      textSystem,
      testEnvironment(),
      lambdaui::Size{200.f, 100.f},
  };

  root.mount(sceneGraph);
  auto const* interaction = lambdaui::interactionData(sceneGraph.root());
  REQUIRE(interaction != nullptr);
  REQUIRE(interaction->onTap);
  interaction->onTap(lambdaui::MouseButton::Left);
  CHECK(sceneGraph.root().size() == lambdaui::Size{64.f, 10.f});

  root.resize(lambdaui::Size{320.f, 180.f}, sceneGraph);

  CHECK(bodyCalls == 1);
  CHECK(sceneGraph.root().size() == lambdaui::Size{64.f, 10.f});
}

TEST_CASE("MountRoot resize updates viewport-sized root without remount") {
  int bodyCalls = 0;

  struct Root {
    int* bodyCalls = nullptr;

    lambdaui::Element body() const {
      ++*bodyCalls;
      return lambdaui::Rectangle{};
    }
  };

  FakeTextSystem textSystem;
  lambdaui::scenegraph::SceneGraph sceneGraph;
  lambdaui::MountRoot root{
      std::make_unique<lambdaui::TypedRootHolder<Root>>(std::in_place, Root{&bodyCalls}),
      textSystem,
      testEnvironment(),
      lambdaui::Size{200.f, 100.f},
  };

  root.mount(sceneGraph);
  CHECK(sceneGraph.root().size() == lambdaui::Size{200.f, 100.f});

  root.resize(lambdaui::Size{320.f, 180.f}, sceneGraph);

  CHECK(bodyCalls == 1);
  CHECK(sceneGraph.root().size() == lambdaui::Size{320.f, 180.f});
}

TEST_CASE("MountRoot repeated resize applies each viewport synchronously without remount") {
  int bodyCalls = 0;

  struct Root {
    int* bodyCalls = nullptr;

    lambdaui::Element body() const {
      ++*bodyCalls;
      return lambdaui::VStack{
          .children = lambdaui::children(lambdaui::Rectangle{}, lambdaui::Rectangle{}),
      };
    }
  };

  FakeTextSystem textSystem;
  lambdaui::scenegraph::SceneGraph sceneGraph;
  lambdaui::MountRoot root{
      std::make_unique<lambdaui::TypedRootHolder<Root>>(std::in_place, Root{&bodyCalls}),
      textSystem,
      testEnvironment(),
      lambdaui::Size{200.f, 100.f},
  };

  root.mount(sceneGraph);

  for (int i = 0; i < 64; ++i) {
    lambdaui::Size const next{200.f + static_cast<float>(i * 3),
                          100.f + static_cast<float>(i * 2)};
    root.resize(next, sceneGraph);
    CHECK(sceneGraph.root().size() == next);
  }

  CHECK(bodyCalls == 1);
}

TEST_CASE("centered stack text keeps intrinsic width while resizing") {
  struct Root {
    lambdaui::Element body() const {
      return lambdaui::VStack{
          .alignment = lambdaui::Alignment::Center,
          .children = lambdaui::children(lambdaui::Text{
              .text = "Title",
              .font = lambdaui::Font::largeTitle(),
          }),
      };
    }
  };

  MeasuringTextSystem textSystem;
  lambdaui::scenegraph::SceneGraph sceneGraph;
  lambdaui::MountRoot root{
      std::make_unique<lambdaui::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      testEnvironment(),
      lambdaui::Size{200.f, 100.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == lambdaui::scenegraph::SceneNodeKind::Group);
  REQUIRE(sceneGraph.root().children().size() == 1);
  auto const& title = *sceneGraph.root().children()[0];
  CHECK(title.size().width == doctest::Approx(30.f));
  CHECK(title.position().x == doctest::Approx(85.f));

  root.resize(lambdaui::Size{320.f, 100.f}, sceneGraph);

  CHECK(title.size().width == doctest::Approx(30.f));
  CHECK(title.position().x == doctest::Approx(145.f));
}

TEST_CASE("MountRoot resize preserves direct text positions in stacks") {
  struct Root {
    lambdaui::Element body() const {
      return lambdaui::ScrollView{
          .axis = lambdaui::ScrollAxis::Vertical,
          .children = lambdaui::children(
              lambdaui::Element{lambdaui::VStack{
                  .spacing = 16.f,
                  .alignment = lambdaui::Alignment::Stretch,
                  .children = lambdaui::children(
                      lambdaui::Text{.text = "Alert demo", .font = lambdaui::Font::largeTitle()},
                      lambdaui::Text{.text = "Modal alerts via useAlert().",
                                 .font = lambdaui::Font::body(),
                                 .wrapping = lambdaui::TextWrapping::Wrap},
                      lambdaui::Text{.text = "Tap a button to open an alert.",
                                 .font = lambdaui::Font::footnote(),
                                 .wrapping = lambdaui::TextWrapping::Wrap}),
              }}.padding(24.f)),
      };
    }
  };

  MeasuringTextSystem textSystem;
  lambdaui::scenegraph::SceneGraph sceneGraph;
  lambdaui::MountRoot root{
      std::make_unique<lambdaui::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      testEnvironment(),
      lambdaui::Size{800.f, 800.f},
  };

  root.mount(sceneGraph);

  auto const& viewport = static_cast<lambdaui::scenegraph::RectNode const&>(sceneGraph.root());
  auto const& content = *viewport.children()[0];
  REQUIRE(content.kind() == lambdaui::scenegraph::SceneNodeKind::Group);
  auto const& paddedStack = static_cast<lambdaui::scenegraph::RectNode const&>(*content.children()[0]);
  auto const& stack = *paddedStack.children()[0];
  REQUIRE(stack.kind() == lambdaui::scenegraph::SceneNodeKind::Group);
  REQUIRE(stack.children().size() == 3);
  float const firstY = stack.children()[0]->position().y;
  float const secondY = stack.children()[1]->position().y;
  float const thirdY = stack.children()[2]->position().y;
  CHECK(firstY == doctest::Approx(0.f));
  CHECK(secondY > firstY);
  CHECK(thirdY > secondY);

  root.resize(lambdaui::Size{900.f, 700.f}, sceneGraph);

  CHECK(stack.children()[0]->position().y == doctest::Approx(firstY));
  CHECK(stack.children()[1]->position().y == doctest::Approx(secondY));
  CHECK(stack.children()[2]->position().y == doctest::Approx(thirdY));
}

TEST_CASE("modifier-wrapped root ScrollView keeps viewport height after resize") {
  struct Root {
    lambdaui::Reactive::Signal<lambdaui::Point> offset;
    lambdaui::Reactive::Signal<lambdaui::Size> viewport;
    lambdaui::Reactive::Signal<lambdaui::Size> content;

    lambdaui::Element body() const {
      return lambdaui::ScrollView{
          .axis = lambdaui::ScrollAxis::Vertical,
          .scrollOffset = offset,
          .viewportSize = viewport,
          .contentSize = content,
          .children = lambdaui::children(
              lambdaui::Rectangle{}.size(80.f, 100.f),
              lambdaui::Rectangle{}.size(80.f, 100.f)),
      }.fill(lambdaui::Colors::red);
    }
  };

  FakeTextSystem textSystem;
  lambdaui::scenegraph::SceneGraph sceneGraph;
  lambdaui::Reactive::Signal<lambdaui::Point> offset{lambdaui::Point{0.f, 0.f}};
  lambdaui::Reactive::Signal<lambdaui::Size> viewport{lambdaui::Size{}};
  lambdaui::Reactive::Signal<lambdaui::Size> content{lambdaui::Size{}};
  lambdaui::MountRoot root{
      std::make_unique<lambdaui::TypedRootHolder<Root>>(
          std::in_place, Root{offset, viewport, content}),
      textSystem,
      testEnvironment(),
      lambdaui::Size{80.f, 80.f},
  };

  root.mount(sceneGraph);
  root.resize(lambdaui::Size{80.f, 60.f}, sceneGraph);

  REQUIRE(sceneGraph.root().kind() == lambdaui::scenegraph::SceneNodeKind::Rect);
  auto const& wrapper = static_cast<lambdaui::scenegraph::RectNode const&>(sceneGraph.root());
  REQUIRE(wrapper.children().size() == 1);
  auto const& scrollViewport = static_cast<lambdaui::scenegraph::RectNode const&>(*wrapper.children()[0]);
  CHECK(wrapper.size() == lambdaui::Size{80.f, 60.f});
  CHECK(scrollViewport.size() == lambdaui::Size{80.f, 60.f});
  CHECK(viewport.get().height == doctest::Approx(60.f));
  CHECK(content.get().height == doctest::Approx(200.f));

  auto const* scrollInteraction = lambdaui::interactionData(scrollViewport);
  REQUIRE(scrollInteraction != nullptr);
  REQUIRE(scrollInteraction->onScroll);
  scrollInteraction->onScroll(lambdaui::Vec2{0.f, -12.f});

  CHECK(offset.get().y == doctest::Approx(12.f));
  REQUIRE(scrollViewport.children().size() >= 1);
  CHECK(scrollViewport.children()[0]->position().y == doctest::Approx(-12.f));
}

TEST_CASE("ScrollView resize preserves child positions when already scrolled") {
  struct Root {
    lambdaui::Reactive::Signal<lambdaui::Point> offset;

    lambdaui::Element body() const {
      return lambdaui::ScrollView{
          .axis = lambdaui::ScrollAxis::Vertical,
          .scrollOffset = offset,
          .children = lambdaui::children(
              lambdaui::Rectangle{}.size(80.f, 80.f),
              lambdaui::Rectangle{}.size(80.f, 80.f)),
      };
    }
  };

  FakeTextSystem textSystem;
  lambdaui::scenegraph::SceneGraph sceneGraph;
  lambdaui::Reactive::Signal<lambdaui::Point> offset{lambdaui::Point{0.f, 20.f}};
  lambdaui::MountRoot root{
      std::make_unique<lambdaui::TypedRootHolder<Root>>(std::in_place, Root{offset}),
      textSystem,
      testEnvironment(),
      lambdaui::Size{80.f, 80.f},
  };

  root.mount(sceneGraph);

  auto const& viewport = static_cast<lambdaui::scenegraph::RectNode const&>(sceneGraph.root());
  REQUIRE(viewport.children().size() >= 1);
  auto const& content = *viewport.children()[0];
  REQUIRE(content.kind() == lambdaui::scenegraph::SceneNodeKind::Group);
  REQUIRE(content.children().size() == 2);
  CHECK(content.position().y == doctest::Approx(-20.f));
  CHECK(content.children()[0]->position().y == doctest::Approx(0.f));
  CHECK(content.children()[1]->position().y == doctest::Approx(80.f));

  root.resize(lambdaui::Size{80.f, 60.f}, sceneGraph);

  CHECK(content.position().y == doctest::Approx(-20.f));
  CHECK(content.children()[0]->position().y == doctest::Approx(0.f));
  CHECK(content.children()[1]->position().y == doctest::Approx(80.f));

  auto const* scrollInteraction = lambdaui::interactionData(viewport);
  REQUIRE(scrollInteraction != nullptr);
  REQUIRE(scrollInteraction->onScroll);
  scrollInteraction->onScroll(lambdaui::Vec2{0.f, -12.f});

  CHECK(content.position().y == doctest::Approx(-32.f));
  CHECK(content.children()[0]->position().y == doctest::Approx(0.f));
  CHECK(content.children()[1]->position().y == doctest::Approx(80.f));
}

TEST_CASE("ScaleAroundCenter relayout keeps reactive scale binding alive") {
  struct Root {
    lambdaui::Reactive::Signal<float> scale;

    lambdaui::Element body() const {
      return lambdaui::ScaleAroundCenter{
          .scale = lambdaui::Reactive::Bindable<float>{[scale = scale] {
            return scale.get();
          }},
          .child = lambdaui::Element{lambdaui::Rectangle{}}
                       .size(20.f, 10.f)
                       .fill(lambdaui::Colors::red),
      };
    }
  };

  FakeTextSystem textSystem;
  lambdaui::scenegraph::SceneGraph sceneGraph;
  lambdaui::Reactive::Signal<float> scale{0.96f};
  lambdaui::MountRoot root{
      std::make_unique<lambdaui::TypedRootHolder<Root>>(std::in_place, Root{scale}),
      textSystem,
      testEnvironment(),
      lambdaui::Size{200.f, 100.f},
  };

  root.mount(sceneGraph);
  scale.set(0.92f);

  root.resize(lambdaui::Size{320.f, 180.f}, sceneGraph);

  CHECK(sceneGraph.root().size().width >= 20.f);
  CHECK(sceneGraph.root().size().height >= 10.f);
}

TEST_CASE("element transform modifiers compose in call order") {
  struct TranslateThenRotate {
    lambdaui::Element body() const {
      return lambdaui::Rectangle{}
          .size(10.f, 10.f)
          .translate(10.f, 0.f)
          .rotate(1.5707963267948966f);
    }
  };
  struct RotateThenTranslate {
    lambdaui::Element body() const {
      return lambdaui::Rectangle{}
          .size(10.f, 10.f)
          .rotate(1.5707963267948966f)
          .translate(10.f, 0.f);
    }
  };
  struct RepeatedTranslate {
    lambdaui::Element body() const {
      return lambdaui::Rectangle{}
          .size(10.f, 10.f)
          .translate(10.f, 2.f)
          .translate(3.f, 4.f);
    }
  };

  FakeTextSystem textSystem;
  lambdaui::scenegraph::SceneGraph translateThenRotateGraph;
  lambdaui::scenegraph::SceneGraph rotateThenTranslateGraph;
  lambdaui::scenegraph::SceneGraph repeatedTranslateGraph;

  lambdaui::MountRoot translateThenRotateRoot{
      std::make_unique<lambdaui::TypedRootHolder<TranslateThenRotate>>(std::in_place),
      textSystem,
      testEnvironment(),
      lambdaui::Size{100.f, 100.f},
  };
  lambdaui::MountRoot rotateThenTranslateRoot{
      std::make_unique<lambdaui::TypedRootHolder<RotateThenTranslate>>(std::in_place),
      textSystem,
      testEnvironment(),
      lambdaui::Size{100.f, 100.f},
  };
  lambdaui::MountRoot repeatedTranslateRoot{
      std::make_unique<lambdaui::TypedRootHolder<RepeatedTranslate>>(std::in_place),
      textSystem,
      testEnvironment(),
      lambdaui::Size{100.f, 100.f},
  };

  translateThenRotateRoot.mount(translateThenRotateGraph);
  rotateThenTranslateRoot.mount(rotateThenTranslateGraph);
  repeatedTranslateRoot.mount(repeatedTranslateGraph);

  lambdaui::Point const sample{1.f, 0.f};
  lambdaui::Point const tr =
      translateThenRotateGraph.root().transform().apply(sample);
  lambdaui::Point const rt =
      rotateThenTranslateGraph.root().transform().apply(sample);
  lambdaui::Point const repeated =
      repeatedTranslateGraph.root().transform().apply({0.f, 0.f});

  CHECK(tr.x == doctest::Approx(10.f).epsilon(0.001));
  CHECK(tr.y == doctest::Approx(1.f).epsilon(0.001));
  CHECK(rt.x == doctest::Approx(0.f).epsilon(0.001));
  CHECK(rt.y == doctest::Approx(11.f).epsilon(0.001));
  CHECK(repeated.x == doctest::Approx(13.f).epsilon(0.001));
  CHECK(repeated.y == doctest::Approx(6.f).epsilon(0.001));
}

TEST_CASE("reactive element transform modifiers update mounted node") {
  struct Root {
    lambdaui::Reactive::Signal<float> dx;

    lambdaui::Element body() const {
      return lambdaui::Rectangle{}
          .size(10.f, 10.f)
          .translate(lambdaui::Reactive::Bindable<float>{[dx = dx] {
            return dx.get();
          }},
                     0.f);
    }
  };

  FakeTextSystem textSystem;
  lambdaui::scenegraph::SceneGraph sceneGraph;
  lambdaui::Reactive::Signal<float> dx{0.f};
  lambdaui::MountRoot root{
      std::make_unique<lambdaui::TypedRootHolder<Root>>(std::in_place, Root{dx}),
      textSystem,
      testEnvironment(),
      lambdaui::Size{100.f, 100.f},
  };

  root.mount(sceneGraph);
  CHECK(sceneGraph.root().transform().apply({0.f, 0.f}).x == doctest::Approx(0.f));

  dx.set(21.f);

  CHECK(sceneGraph.root().transform().apply({0.f, 0.f}).x == doctest::Approx(21.f));
}

TEST_CASE("TextInput fills finite assigned stack width") {
  struct Root {
    lambdaui::Element body() const {
      auto value = lambdaui::useState(std::string{"hello"});
      return lambdaui::VStack{
          .alignment = lambdaui::Alignment::Start,
          .children = lambdaui::children(lambdaui::TextInput{
              .value = value,
              .multiline = true,
              .multilineHeight = {.fixed = 40.f},
          }),
      };
    }
  };

  FakeTextSystem textSystem;
  lambdaui::scenegraph::SceneGraph sceneGraph;
  lambdaui::MountRoot root{
      std::make_unique<lambdaui::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      testEnvironment(),
      lambdaui::Size{180.f, 100.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == lambdaui::scenegraph::SceneNodeKind::Group);
  REQUIRE(sceneGraph.root().children().size() == 1);
  CHECK(sceneGraph.root().children()[0]->size().width == doctest::Approx(180.f));
}

TEST_CASE("ScrollView mount emits overlay indicators for overflowing content") {
  struct Root {
    lambdaui::Element body() const {
      return lambdaui::ScrollView{
          .axis = lambdaui::ScrollAxis::Vertical,
          .children = lambdaui::children(
              lambdaui::Rectangle{}.size(60.f, 30.f),
              lambdaui::Rectangle{}.size(60.f, 30.f)),
      };
    }
  };

  FakeTextSystem textSystem;
  lambdaui::scenegraph::SceneGraph sceneGraph;
  lambdaui::MountRoot root{
      std::make_unique<lambdaui::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      testEnvironment(),
      lambdaui::Size{80.f, 40.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == lambdaui::scenegraph::SceneNodeKind::Rect);
  auto const& viewport = static_cast<lambdaui::scenegraph::RectNode const&>(sceneGraph.root());
  CHECK(viewport.clipsContents());
  REQUIRE(viewport.children().size() == 2);
  REQUIRE(viewport.children()[0]->kind() == lambdaui::scenegraph::SceneNodeKind::Group);
  REQUIRE(viewport.children()[1]->kind() == lambdaui::scenegraph::SceneNodeKind::Rect);

  auto const& overlay = static_cast<lambdaui::scenegraph::RectNode const&>(*viewport.children()[1]);
  CHECK(overlay.opacity() == doctest::Approx(0.f));
  REQUIRE(overlay.children().size() == 1);
  CHECK(overlay.children()[0]->bounds().x == doctest::Approx(73.f));
  float const initialIndicatorY = overlay.children()[0]->bounds().y;

  auto const* scrollInteraction = lambdaui::interactionData(viewport);
  REQUIRE(scrollInteraction != nullptr);
  REQUIRE(scrollInteraction->onScroll);
  scrollInteraction->onScroll(lambdaui::Vec2{0.f, -12.f});

  CHECK(viewport.children()[0]->position().y == doctest::Approx(-12.f));
  CHECK(overlay.opacity() == doctest::Approx(0.f));
  CHECK(overlay.children()[0]->bounds().y > initialIndicatorY);
}

TEST_CASE("ScrollView updates content size when mounted content grows reactively") {
  struct Root {
    lambdaui::Reactive::Signal<float> childHeight;
    lambdaui::Reactive::Signal<lambdaui::Size> contentSize;

    lambdaui::Element body() const {
      return lambdaui::ScrollView{
          .axis = lambdaui::ScrollAxis::Vertical,
          .contentSize = contentSize,
          .children = lambdaui::children(
              lambdaui::Rectangle{}.size(60.f, [childHeight = childHeight] {
                return childHeight.get();
              })),
      };
    }
  };

  FakeTextSystem textSystem;
  lambdaui::scenegraph::SceneGraph sceneGraph;
  lambdaui::Reactive::Signal<float> childHeight{30.f};
  lambdaui::Reactive::Signal<lambdaui::Size> contentSize{};
  lambdaui::MountRoot root{
      std::make_unique<lambdaui::TypedRootHolder<Root>>(
          std::in_place, Root{.childHeight = childHeight, .contentSize = contentSize}),
      textSystem,
      testEnvironment(),
      lambdaui::Size{80.f, 40.f},
  };

  root.mount(sceneGraph);
  CHECK(contentSize.peek().height == doctest::Approx(30.f));

  childHeight = 90.f;

  CHECK(contentSize.peek().height == doctest::Approx(90.f));
  auto const* scrollInteraction = lambdaui::interactionData(sceneGraph.root());
  REQUIRE(scrollInteraction != nullptr);
  scrollInteraction->onScroll(lambdaui::Vec2{0.f, -50.f});
  REQUIRE(sceneGraph.root().children().size() >= 1);
  CHECK(sceneGraph.root().children()[0]->position().y == doctest::Approx(-50.f));
}

TEST_CASE("MountRoot keeps Bindable effects scoped to the mount") {
  struct Root {
    lambdaui::Reactive::Signal<bool> hot;

    lambdaui::Element body() const {
      return lambdaui::Element{lambdaui::Rectangle{}}
          .size(10.f, 10.f)
          .fill([hot = hot] {
            return hot() ? lambdaui::Colors::red : lambdaui::Colors::blue;
          });
    }
  };

  FakeTextSystem textSystem;
  lambdaui::scenegraph::SceneGraph sceneGraph;
  lambdaui::Reactive::Signal<bool> hot{true};
  lambdaui::MountRoot root{
      std::make_unique<lambdaui::TypedRootHolder<Root>>(std::in_place, Root{hot}),
      textSystem,
      testEnvironment(),
      lambdaui::Size{200.f, 100.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == lambdaui::scenegraph::SceneNodeKind::Rect);
  auto const& rect = static_cast<lambdaui::scenegraph::RectNode const&>(sceneGraph.root());
  CHECK(solidColor(rect) == lambdaui::Colors::red);

  hot.set(false);
  CHECK(solidColor(rect) == lambdaui::Colors::blue);

  root.unmount(sceneGraph);
  CHECK_FALSE(root.mounted());
  CHECK(sceneGraph.root().kind() == lambdaui::scenegraph::SceneNodeKind::Group);
  hot.set(true);
}

TEST_CASE("nested body component bindings inherit the root redraw callback") {
  struct Child {
    lambdaui::Reactive::Signal<bool> hot;

    lambdaui::Element body() const {
      return lambdaui::Element{lambdaui::Rectangle{}}
          .size(10.f, 10.f)
          .fill([hot = hot] {
            return hot() ? lambdaui::Colors::red : lambdaui::Colors::blue;
          });
    }
  };

  struct Root {
    lambdaui::Reactive::Signal<bool> hot;

    lambdaui::Element body() const {
      return lambdaui::Element{Child{hot}};
    }
  };

  FakeTextSystem textSystem;
  lambdaui::scenegraph::SceneGraph sceneGraph;
  lambdaui::Reactive::Signal<bool> hot{true};
  int redraws = 0;
  lambdaui::MountRoot root{
      std::make_unique<lambdaui::TypedRootHolder<Root>>(std::in_place, Root{hot}),
      textSystem,
      testEnvironment(),
      lambdaui::Size{200.f, 100.f},
      [&] { ++redraws; },
  };

  root.mount(sceneGraph);
  redraws = 0;
  hot.set(false);

  CHECK(redraws == 1);
}

TEST_CASE("container mounting composes slot origin with explicit child position") {
  struct Root {
    lambdaui::Element body() const {
      return lambdaui::ZStack{
          .horizontalAlignment = lambdaui::Alignment::Start,
          .verticalAlignment = lambdaui::Alignment::Start,
          .children = lambdaui::children(
              lambdaui::Rectangle{}
                  .size(44.f, 26.f)
                  .fill(lambdaui::Colors::blue),
              lambdaui::Rectangle{}
                  .size(18.f, 18.f)
                  .position(22.f, 4.f)
                  .fill(lambdaui::Colors::red)),
      };
    }
  };

  FakeTextSystem textSystem;
  lambdaui::scenegraph::SceneGraph sceneGraph;
  lambdaui::MountRoot root{
      std::make_unique<lambdaui::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      testEnvironment(),
      lambdaui::Size{44.f, 26.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == lambdaui::scenegraph::SceneNodeKind::Group);
  auto const& group = sceneGraph.root();
  REQUIRE(group.children().size() == 2);
  CHECK(group.children()[0]->position() == lambdaui::Point{0.f, 0.f});
  CHECK(group.children()[1]->position() == lambdaui::Point{22.f, 4.f});
}
