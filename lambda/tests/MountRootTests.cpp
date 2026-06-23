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

class MeasuringTextSystem final : public lambda::TextSystem {
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
    return {80.f, 16.f};
  }

  lambda::Size measure(std::string_view text, lambda::Font const&, lambda::Color const&, float,
                     lambda::TextLayoutOptions const&) override {
    return {std::max(24.f, static_cast<float>(text.size()) * 6.f), 16.f};
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

lambda::Color solidColor(lambda::scenegraph::RectNode const& rect) {
  lambda::Color color{};
  CHECK(rect.fill().solidColor(&color));
  return color;
}

struct IntrinsicBox {
  lambda::Size measure(lambda::MeasureContext& ctx, lambda::LayoutConstraints const&,
                     lambda::LayoutHints const&, lambda::TextSystem&) const {
    ctx.advanceChildSlot();
    return {24.f, 12.f};
  }

  std::unique_ptr<lambda::scenegraph::SceneNode> mount(lambda::MountContext&) const {
    return std::make_unique<lambda::scenegraph::RectNode>(
        lambda::Rect{0.f, 0.f, 24.f, 12.f});
  }
};

struct StretchBox {
  lambda::Size intrinsic{24.f, 12.f};

  lambda::Size measure(lambda::MeasureContext& ctx, lambda::LayoutConstraints const&,
                     lambda::LayoutHints const&, lambda::TextSystem&) const {
    ctx.advanceChildSlot();
    return intrinsic;
  }

  std::unique_ptr<lambda::scenegraph::SceneNode> mount(lambda::MountContext& ctx) const {
    auto sizeFor = [intrinsic = intrinsic](lambda::LayoutConstraints const& constraints) {
      lambda::Size size{
          std::isfinite(constraints.maxWidth) ? constraints.maxWidth : intrinsic.width,
          std::isfinite(constraints.maxHeight) ? constraints.maxHeight : intrinsic.height,
      };
      size.width = std::max(size.width, constraints.minWidth);
      size.height = std::max(size.height, constraints.minHeight);
      return size;
    };

    lambda::Size const initialSize = sizeFor(ctx.constraints());
    auto group = std::make_unique<lambda::scenegraph::SceneNode>(
        lambda::Rect{0.f, 0.f, initialSize.width, initialSize.height});
    auto* rawGroup = group.get();
    rawGroup->setRelayout([rawGroup, sizeFor = std::move(sizeFor)](
                              lambda::LayoutConstraints const& constraints) {
      rawGroup->setSize(sizeFor(constraints));
    });
    return group;
  }
};

lambda::LayoutConstraints fixedConstraints(lambda::Size size) {
  return lambda::LayoutConstraints{
      .maxWidth = std::max(0.f, size.width),
      .maxHeight = std::max(0.f, size.height),
      .minWidth = std::max(0.f, size.width),
      .minHeight = std::max(0.f, size.height),
  };
}

struct RelayoutProbeFrame {
  lambda::Element child;
  int* relayouts = nullptr;

  lambda::Size measure(lambda::MeasureContext& ctx, lambda::LayoutConstraints const&,
                     lambda::LayoutHints const&, lambda::TextSystem&) const {
    ctx.advanceChildSlot();
    return {100.f, 100.f};
  }

  std::unique_ptr<lambda::scenegraph::SceneNode> mount(lambda::MountContext& ctx) const {
    auto group = std::make_unique<lambda::scenegraph::SceneNode>(
        lambda::Rect{0.f, 0.f, 100.f, 100.f});
    lambda::MountContext childCtx = ctx.childWithSharedScope(fixedConstraints({100.f, 100.f}), ctx.hints());
    auto childNode = child.mount(childCtx);
    lambda::scenegraph::SceneNode* rawChild = childNode.get();
    if (childNode) {
      group->appendChild(std::move(childNode));
    }
    auto* rawGroup = group.get();
    rawGroup->setLayoutConstraints(ctx.constraints());
    rawGroup->setRelayout([rawGroup, rawChild, relayouts = relayouts](
                              lambda::LayoutConstraints const&) {
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
  lambda::Element child;

  lambda::Size measure(lambda::MeasureContext& ctx, lambda::LayoutConstraints const& constraints,
                     lambda::LayoutHints const& hints, lambda::TextSystem& textSystem) const {
    return child.measure(ctx, constraints, hints, textSystem);
  }

  std::unique_ptr<lambda::scenegraph::SceneNode> mount(lambda::MountContext& ctx) const {
    auto group = std::make_unique<lambda::scenegraph::SceneNode>();
    lambda::MountContext childCtx = ctx.childWithSharedScope(ctx.constraints(), ctx.hints());
    auto childNode = child.mount(childCtx);
    lambda::scenegraph::SceneNode* rawChild = childNode.get();
    if (childNode) {
      group->setSize(childNode->size());
      group->appendChild(std::move(childNode));
    }
    lambda::scenegraph::SceneNode* rawGroup = group.get();
    rawGroup->setLayoutConstraints(ctx.constraints());
    rawGroup->setRelayout([rawGroup, rawChild](lambda::LayoutConstraints const& constraints) {
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
  lambda::Reactive::Signal<float> width;

  lambda::Element body() const {
    if (depth <= 0) {
      return lambda::Element{lambda::Rectangle{}}
          .size([width = width] {
                  return width.get();
                },
                10.f);
    }
    return lambda::Element{RelayoutPassthroughFrame{
        .child = lambda::Element{DeepRelayoutNode{depth - 1, width}},
    }};
  }
};

} // namespace

TEST_CASE("MountRoot mounts a static root once") {
  int bodyCalls = 0;

  struct Root {
    int* bodyCalls = nullptr;

    lambda::Element body() const {
      ++*bodyCalls;
      return lambda::Element{lambda::Rectangle{}}
          .size(20.f, 30.f)
          .fill(lambda::Colors::red);
    }
  };

  FakeTextSystem textSystem;
  lambda::scenegraph::SceneGraph sceneGraph;
  lambda::MountRoot root{
      std::make_unique<lambda::TypedRootHolder<Root>>(std::in_place, Root{&bodyCalls}),
      textSystem,
      testEnvironment(),
      lambda::Size{200.f, 100.f},
  };

  root.mount(sceneGraph);

  CHECK(root.mounted());
  CHECK(bodyCalls == 1);
  REQUIRE(sceneGraph.root().kind() == lambda::scenegraph::SceneNodeKind::Rect);
  auto const& rect = static_cast<lambda::scenegraph::RectNode const&>(sceneGraph.root());
  CHECK(rect.size() == lambda::Size{20.f, 30.f});
  CHECK(solidColor(rect) == lambda::Colors::red);
}

TEST_CASE("Popover body mounts callout chrome and reserves arrow depth") {
  struct Root {
    lambda::Element body() const {
      return lambda::Popover{
          .content = lambda::Element{lambda::Rectangle{}}
                         .size(100.f, 20.f)
                         .fill(lambda::Colors::blue),
          .placement = lambda::PopoverPlacement::Below,
          .arrow = true,
      };
    }
  };

  lambda::Theme const theme = lambda::Theme::light();
  FakeTextSystem textSystem;
  lambda::scenegraph::SceneGraph sceneGraph;
  lambda::MountRoot root{
      std::make_unique<lambda::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      lambda::EnvironmentBinding{}.withValue<lambda::ThemeKey>(theme),
      lambda::Size{300.f, 200.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == lambda::scenegraph::SceneNodeKind::Group);
  CHECK(sceneGraph.root().size().width == doctest::Approx(100.f + 2.f * theme.space3));
  CHECK(sceneGraph.root().size().height ==
        doctest::Approx(20.f + 2.f * theme.space3 + lambda::PopoverCalloutShape::kArrowH));
  REQUIRE(sceneGraph.root().children().size() == 2);
  CHECK(sceneGraph.root().children()[0]->kind() == lambda::scenegraph::SceneNodeKind::Path);
  CHECK(sceneGraph.root().children()[1]->position().y ==
        doctest::Approx(lambda::PopoverCalloutShape::kArrowH + theme.space3));
}

TEST_CASE("Popover body follows resolved overlay placement from environment") {
  struct Root {
    lambda::Element body() const {
      return lambda::Popover{
          .content = lambda::Element{lambda::Rectangle{}}
                         .size(100.f, 20.f)
                         .fill(lambda::Colors::blue),
          .placement = lambda::PopoverPlacement::Below,
          .arrow = true,
      };
    }
  };

  lambda::Theme const theme = lambda::Theme::light();
  FakeTextSystem textSystem;
  lambda::scenegraph::SceneGraph sceneGraph;
  lambda::MountRoot root{
      std::make_unique<lambda::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      lambda::EnvironmentBinding{}
          .withValue<lambda::ThemeKey>(theme)
          .withValue<lambda::ResolvedOverlayPlacementKey>(
              std::optional<lambda::OverlayConfig::Placement>{lambda::OverlayConfig::Placement::Above}),
      lambda::Size{300.f, 200.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == lambda::scenegraph::SceneNodeKind::Group);
  REQUIRE(sceneGraph.root().children().size() == 2);
  CHECK(sceneGraph.root().children()[1]->position().y == doctest::Approx(theme.space3));
}

TEST_CASE("composite child body is materialized once across measure and mount") {
  int bodyCalls = 0;

  struct Probe {
    int* bodyCalls = nullptr;

    lambda::Element body() const {
      ++*bodyCalls;
      return lambda::Rectangle{}.size(20.f, 10.f);
    }
  };

  struct Root {
    int* bodyCalls = nullptr;

    lambda::Element body() const {
      return lambda::VStack{
          .children = lambda::children(Probe{bodyCalls}),
      };
    }
  };

  FakeTextSystem textSystem;
  lambda::scenegraph::SceneGraph sceneGraph;
  lambda::MountRoot root{
      std::make_unique<lambda::TypedRootHolder<Root>>(std::in_place, Root{&bodyCalls}),
      textSystem,
      testEnvironment(),
      lambda::Size{200.f, 100.f},
  };

  root.mount(sceneGraph);

  CHECK(bodyCalls == 1);
  REQUIRE(sceneGraph.root().kind() == lambda::scenegraph::SceneNodeKind::Group);
  REQUIRE(sceneGraph.root().children().size() == 1);
  CHECK(sceneGraph.root().children()[0]->size() == lambda::Size{20.f, 10.f});
}

TEST_CASE("interaction hooks attach reactive signals to mounted interaction data") {
  struct Root {
    lambda::Element body() const {
      lambda::Reactive::Signal<bool> hovered = lambda::useHover();
      lambda::Reactive::Signal<bool> pressed = lambda::usePress();
      lambda::Reactive::Signal<bool> focused = lambda::useFocus();
      lambda::Reactive::Signal<bool> keyboardFocused = lambda::useKeyboardFocus();
      return lambda::Rectangle{}
          .size(20.f, 10.f)
          .fill([hovered, pressed, focused, keyboardFocused] {
            if (keyboardFocused.get()) {
              return lambda::Colors::yellow;
            }
            if (focused.get()) {
              return lambda::Colors::blue;
            }
            if (pressed.get()) {
              return lambda::Colors::green;
            }
            if (hovered.get()) {
              return lambda::Colors::red;
            }
            return lambda::Colors::black;
          })
          .focusable(true)
          .onTap([] {});
    }
  };

  FakeTextSystem textSystem;
  lambda::scenegraph::SceneGraph sceneGraph;
  lambda::MountRoot root{
      std::make_unique<lambda::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      testEnvironment(),
      lambda::Size{200.f, 100.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == lambda::scenegraph::SceneNodeKind::Rect);
  auto const& rect = static_cast<lambda::scenegraph::RectNode const&>(sceneGraph.root());
  auto const* interaction = lambda::interactionData(rect);
  REQUIRE(interaction != nullptr);
  CHECK(solidColor(rect) == lambda::Colors::black);

  interaction->hoverSignal.set(true);
  CHECK(solidColor(rect) == lambda::Colors::red);
  interaction->hoverSignal.set(false);
  CHECK(solidColor(rect) == lambda::Colors::black);

  interaction->pressSignal.set(true);
  CHECK(solidColor(rect) == lambda::Colors::green);
  interaction->pressSignal.set(false);
  CHECK(solidColor(rect) == lambda::Colors::black);

  interaction->focusSignal.set(true);
  CHECK(solidColor(rect) == lambda::Colors::blue);
  interaction->focusSignal.set(false);
  CHECK(solidColor(rect) == lambda::Colors::black);

  interaction->keyboardFocusSignal.set(true);
  CHECK(solidColor(rect) == lambda::Colors::yellow);
  interaction->keyboardFocusSignal.set(false);
  CHECK(solidColor(rect) == lambda::Colors::black);
}

TEST_CASE("modifier envelopes honor fixed viewport constraints") {
  struct Root {
    lambda::Element body() const {
      return lambda::Element{IntrinsicBox{}}
          .onTap([] {});
    }
  };

  FakeTextSystem textSystem;
  lambda::scenegraph::SceneGraph sceneGraph;
  lambda::MountRoot root{
      std::make_unique<lambda::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      testEnvironment(),
      lambda::Size{200.f, 100.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == lambda::scenegraph::SceneNodeKind::Rect);
  CHECK(sceneGraph.root().size() == lambda::Size{200.f, 100.f});
  REQUIRE(sceneGraph.root().children().size() == 1);
  CHECK(sceneGraph.root().children()[0]->size() == lambda::Size{24.f, 12.f});
}

TEST_CASE("HStack flex children honor assigned main-axis size with explicit modifiers") {
  struct Root {
    lambda::Element body() const {
      return lambda::HStack{
          .spacing = 12.f,
          .alignment = lambda::Alignment::Center,
          .children = lambda::children(
              lambda::Element{lambda::Rectangle{}}
                  .size(56.f, 54.f)
                  .fill(lambda::Colors::red)
                  .flex(2.f, 1.f, 0.f),
              lambda::Rectangle{}.size(56.f, 76.f),
              lambda::Element{lambda::Rectangle{}}
                  .size(56.f, 40.f)
                  .fill(lambda::Colors::blue)
                  .flex(1.f, 1.f, 0.f),
              lambda::Rectangle{}.size(56.f, 54.f)),
      };
    }
  };

  FakeTextSystem textSystem;
  lambda::scenegraph::SceneGraph sceneGraph;
  lambda::MountRoot root{
      std::make_unique<lambda::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      testEnvironment(),
      lambda::Size{704.f, 100.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == lambda::scenegraph::SceneNodeKind::Group);
  auto const& group = sceneGraph.root();
  REQUIRE(group.children().size() == 4);
  CHECK(group.children()[0]->size().width == doctest::Approx(370.666f).epsilon(0.001));
  CHECK(group.children()[2]->size().width == doctest::Approx(185.333f).epsilon(0.001));
  CHECK(group.children()[1]->position().x == doctest::Approx(382.666f).epsilon(0.001));
  CHECK(group.children()[3]->position().x == doctest::Approx(648.f));
}

TEST_CASE("Spacer as composite expands to fill main axis in HStack") {
  struct Root {
    lambda::Element body() const {
      return lambda::HStack{
          .spacing = 0.f,
          .alignment = lambda::Alignment::Stretch,
          .children = lambda::children(
              lambda::Rectangle{}.size(20.f, 10.f),
              lambda::Spacer{},
              lambda::Rectangle{}.size(30.f, 10.f)),
      };
    }
  };

  FakeTextSystem textSystem;
  lambda::scenegraph::SceneGraph sceneGraph;
  lambda::MountRoot root{
      std::make_unique<lambda::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      testEnvironment(),
      lambda::Size{200.f, 40.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == lambda::scenegraph::SceneNodeKind::Group);
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
    lambda::Element body() const {
      return lambda::HStack{
          .spacing = 0.f,
          .alignment = lambda::Alignment::Stretch,
          .children = lambda::children(
              lambda::Spacer{}.minMainSize(40.f),
              lambda::Rectangle{}.size(10.f, 10.f)),
      };
    }
  };

  FakeTextSystem textSystem;
  lambda::scenegraph::SceneGraph sceneGraph;
  lambda::MountRoot root{
      std::make_unique<lambda::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      testEnvironment(),
      lambda::Size{30.f, 40.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == lambda::scenegraph::SceneNodeKind::Group);
  auto const& group = sceneGraph.root();
  REQUIRE(group.children().size() == 2);
  CHECK(group.children()[0]->size().width == doctest::Approx(40.f));
  CHECK(group.children()[1]->position().x == doctest::Approx(40.f));
}

TEST_CASE("Grid expands row tracks when flex assigns extra height") {
  struct Root {
    lambda::Element body() const {
      return lambda::VStack{
          .spacing = 0.f,
          .alignment = lambda::Alignment::Stretch,
          .children = lambda::children(
              lambda::Grid{
                  .columns = 1,
                  .horizontalSpacing = 0.f,
                  .verticalSpacing = 8.f,
                  .verticalAlignment = lambda::Alignment::Stretch,
                  .children = lambda::children(
                      StretchBox{{20.f, 10.f}},
                      StretchBox{{20.f, 160.f}},
                      StretchBox{{20.f, 10.f}},
                      StretchBox{{20.f, 10.f}}),
              }.flex(1.f, 1.f, 0.f)),
      };
    }
  };

  FakeTextSystem textSystem;
  lambda::scenegraph::SceneGraph sceneGraph;
  lambda::MountRoot root{
      std::make_unique<lambda::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      testEnvironment(),
      lambda::Size{100.f, 400.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == lambda::scenegraph::SceneNodeKind::Group);
  REQUIRE(sceneGraph.root().children().size() == 1);
  lambda::scenegraph::SceneNode const& gridNode = *sceneGraph.root().children()[0];
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
    lambda::Reactive::Signal<float> barHeight;

    lambda::Element body() const {
      return lambda::ZStack{
          .horizontalAlignment = lambda::Alignment::Center,
          .verticalAlignment = lambda::Alignment::Center,
          .children = lambda::children(
              lambda::Rectangle{}.size(100.f, 100.f),
              lambda::HStack{
                  .spacing = 8.f,
                  .alignment = lambda::Alignment::Center,
                  .children = lambda::children(
                      lambda::Rectangle{}.size(
                          20.f,
                          [barHeight = barHeight] {
                            return barHeight.get();
                          }),
                      lambda::Rectangle{}.size(20.f, 20.f)),
              }),
      };
    }
  };

  FakeTextSystem textSystem;
  lambda::scenegraph::SceneGraph sceneGraph;
  lambda::Reactive::Signal<float> barHeight{20.f};
  lambda::MountRoot root{
      std::make_unique<lambda::TypedRootHolder<Root>>(std::in_place, Root{barHeight}),
      textSystem,
      testEnvironment(),
      lambda::Size{100.f, 100.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == lambda::scenegraph::SceneNodeKind::Group);
  auto const& zstack = sceneGraph.root();
  REQUIRE(zstack.children().size() == 2);
  auto const& row = *zstack.children()[1];
  REQUIRE(row.kind() == lambda::scenegraph::SceneNodeKind::Group);
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
  lambda::Reactive::Scope scope;
  lambda::MeasureContext measureContext{textSystem, testEnvironment()};
  lambda::MountContext context{
      scope,
      textSystem,
      measureContext,
      lambda::LayoutConstraints{
          .maxWidth = std::numeric_limits<float>::infinity(),
          .maxHeight = std::numeric_limits<float>::infinity(),
          .minWidth = 0.f,
          .minHeight = 0.f,
      },
  };
  lambda::Reactive::Signal<float> width{20.f};
  lambda::Element root{DeepRelayoutNode{32, width}};

  std::unique_ptr<lambda::scenegraph::SceneNode> node = root.mount(context);

  REQUIRE(node);
  CHECK(node->size().width == doctest::Approx(20.f));

  width.set(48.f);

  CHECK(node->size().width == doctest::Approx(48.f));
}

TEST_CASE("reactive size relayout stops at unchanged ancestors") {
  struct Root {
    lambda::Reactive::Signal<float> barHeight;
    int* outerRelayouts = nullptr;

    lambda::Element body() const {
      return RelayoutProbeFrame{
          .child = lambda::Element{lambda::ZStack{
              .horizontalAlignment = lambda::Alignment::Center,
              .verticalAlignment = lambda::Alignment::Center,
              .children = lambda::children(
                  lambda::Rectangle{}.size(100.f, 100.f),
                  lambda::HStack{
                      .spacing = 8.f,
                      .alignment = lambda::Alignment::Center,
                      .children = lambda::children(
                          lambda::Rectangle{}.size(
                              20.f,
                              [barHeight = barHeight] {
                                return barHeight.get();
                              }),
                          lambda::Rectangle{}.size(20.f, 20.f)),
                  }),
          }},
          .relayouts = outerRelayouts,
      };
    }
  };

  FakeTextSystem textSystem;
  lambda::scenegraph::SceneGraph sceneGraph;
  lambda::Reactive::Signal<float> barHeight{20.f};
  int outerRelayouts = 0;
  lambda::MountRoot root{
      std::make_unique<lambda::TypedRootHolder<Root>>(
          std::in_place, Root{barHeight, &outerRelayouts}),
      textSystem,
      testEnvironment(),
      lambda::Size{100.f, 100.f},
  };

  root.mount(sceneGraph);
  barHeight.set(60.f);

  CHECK(outerRelayouts == 0);
}

TEST_CASE("MountContext childWithOwnScope creates a scoped owner") {
  lambda::Reactive::Scope rootScope;
  FakeTextSystem textSystem;
  lambda::MeasureContext measureContext{textSystem, testEnvironment()};
  lambda::MountContext rootContext{
      rootScope,
      textSystem,
      measureContext,
      lambda::LayoutConstraints{.maxWidth = 100.f, .maxHeight = 100.f},
  };

  int childCleanups = 0;
  {
    lambda::MountContext childContext =
        rootContext.childWithOwnScope(lambda::LayoutConstraints{.maxWidth = 40.f, .maxHeight = 20.f});
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
  lambda::Reactive::Scope rootScope;
  FakeTextSystem textSystem;
  lambda::MeasureContext measureContext{textSystem, testEnvironment()};
  lambda::MountContext rootContext{
      rootScope,
      textSystem,
      measureContext,
      lambda::LayoutConstraints{.maxWidth = 100.f, .maxHeight = 100.f},
  };

  lambda::MountContext childContext =
      rootContext.childWithSharedScope(lambda::LayoutConstraints{.maxWidth = 40.f, .maxHeight = 20.f});
  CHECK(&childContext.owner() == &rootContext.owner());
}

TEST_CASE("MountRoot resize relayouts without remounting root state") {
  int bodyCalls = 0;

  struct Root {
    int* bodyCalls = nullptr;

    lambda::Element body() const {
      ++*bodyCalls;
      auto width = lambda::useState(20.f);
      return lambda::Element{lambda::Rectangle{}}
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
  lambda::scenegraph::SceneGraph sceneGraph;
  lambda::MountRoot root{
      std::make_unique<lambda::TypedRootHolder<Root>>(std::in_place, Root{&bodyCalls}),
      textSystem,
      testEnvironment(),
      lambda::Size{200.f, 100.f},
  };

  root.mount(sceneGraph);
  auto const* interaction = lambda::interactionData(sceneGraph.root());
  REQUIRE(interaction != nullptr);
  REQUIRE(interaction->onTap);
  interaction->onTap(lambda::MouseButton::Left);
  CHECK(sceneGraph.root().size() == lambda::Size{64.f, 10.f});

  root.resize(lambda::Size{320.f, 180.f}, sceneGraph);

  CHECK(bodyCalls == 1);
  CHECK(sceneGraph.root().size() == lambda::Size{64.f, 10.f});
}

TEST_CASE("MountRoot resize updates viewport-sized root without remount") {
  int bodyCalls = 0;

  struct Root {
    int* bodyCalls = nullptr;

    lambda::Element body() const {
      ++*bodyCalls;
      return lambda::Rectangle{};
    }
  };

  FakeTextSystem textSystem;
  lambda::scenegraph::SceneGraph sceneGraph;
  lambda::MountRoot root{
      std::make_unique<lambda::TypedRootHolder<Root>>(std::in_place, Root{&bodyCalls}),
      textSystem,
      testEnvironment(),
      lambda::Size{200.f, 100.f},
  };

  root.mount(sceneGraph);
  CHECK(sceneGraph.root().size() == lambda::Size{200.f, 100.f});

  root.resize(lambda::Size{320.f, 180.f}, sceneGraph);

  CHECK(bodyCalls == 1);
  CHECK(sceneGraph.root().size() == lambda::Size{320.f, 180.f});
}

TEST_CASE("MountRoot repeated resize applies each viewport synchronously without remount") {
  int bodyCalls = 0;

  struct Root {
    int* bodyCalls = nullptr;

    lambda::Element body() const {
      ++*bodyCalls;
      return lambda::VStack{
          .children = lambda::children(lambda::Rectangle{}, lambda::Rectangle{}),
      };
    }
  };

  FakeTextSystem textSystem;
  lambda::scenegraph::SceneGraph sceneGraph;
  lambda::MountRoot root{
      std::make_unique<lambda::TypedRootHolder<Root>>(std::in_place, Root{&bodyCalls}),
      textSystem,
      testEnvironment(),
      lambda::Size{200.f, 100.f},
  };

  root.mount(sceneGraph);

  for (int i = 0; i < 64; ++i) {
    lambda::Size const next{200.f + static_cast<float>(i * 3),
                          100.f + static_cast<float>(i * 2)};
    root.resize(next, sceneGraph);
    CHECK(sceneGraph.root().size() == next);
  }

  CHECK(bodyCalls == 1);
}

TEST_CASE("centered stack text keeps intrinsic width while resizing") {
  struct Root {
    lambda::Element body() const {
      return lambda::VStack{
          .alignment = lambda::Alignment::Center,
          .children = lambda::children(lambda::Text{
              .text = "Title",
              .font = lambda::Font::largeTitle(),
          }),
      };
    }
  };

  MeasuringTextSystem textSystem;
  lambda::scenegraph::SceneGraph sceneGraph;
  lambda::MountRoot root{
      std::make_unique<lambda::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      testEnvironment(),
      lambda::Size{200.f, 100.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == lambda::scenegraph::SceneNodeKind::Group);
  REQUIRE(sceneGraph.root().children().size() == 1);
  auto const& title = *sceneGraph.root().children()[0];
  CHECK(title.size().width == doctest::Approx(30.f));
  CHECK(title.position().x == doctest::Approx(85.f));

  root.resize(lambda::Size{320.f, 100.f}, sceneGraph);

  CHECK(title.size().width == doctest::Approx(30.f));
  CHECK(title.position().x == doctest::Approx(145.f));
}

TEST_CASE("MountRoot resize preserves direct text positions in stacks") {
  struct Root {
    lambda::Element body() const {
      return lambda::ScrollView{
          .axis = lambda::ScrollAxis::Vertical,
          .children = lambda::children(
              lambda::Element{lambda::VStack{
                  .spacing = 16.f,
                  .alignment = lambda::Alignment::Stretch,
                  .children = lambda::children(
                      lambda::Text{.text = "Alert demo", .font = lambda::Font::largeTitle()},
                      lambda::Text{.text = "Modal alerts via useAlert().",
                                 .font = lambda::Font::body(),
                                 .wrapping = lambda::TextWrapping::Wrap},
                      lambda::Text{.text = "Tap a button to open an alert.",
                                 .font = lambda::Font::footnote(),
                                 .wrapping = lambda::TextWrapping::Wrap}),
              }}.padding(24.f)),
      };
    }
  };

  MeasuringTextSystem textSystem;
  lambda::scenegraph::SceneGraph sceneGraph;
  lambda::MountRoot root{
      std::make_unique<lambda::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      testEnvironment(),
      lambda::Size{800.f, 800.f},
  };

  root.mount(sceneGraph);

  auto const& viewport = static_cast<lambda::scenegraph::RectNode const&>(sceneGraph.root());
  auto const& content = *viewport.children()[0];
  REQUIRE(content.kind() == lambda::scenegraph::SceneNodeKind::Group);
  auto const& paddedStack = static_cast<lambda::scenegraph::RectNode const&>(*content.children()[0]);
  auto const& stack = *paddedStack.children()[0];
  REQUIRE(stack.kind() == lambda::scenegraph::SceneNodeKind::Group);
  REQUIRE(stack.children().size() == 3);
  float const firstY = stack.children()[0]->position().y;
  float const secondY = stack.children()[1]->position().y;
  float const thirdY = stack.children()[2]->position().y;
  CHECK(firstY == doctest::Approx(0.f));
  CHECK(secondY > firstY);
  CHECK(thirdY > secondY);

  root.resize(lambda::Size{900.f, 700.f}, sceneGraph);

  CHECK(stack.children()[0]->position().y == doctest::Approx(firstY));
  CHECK(stack.children()[1]->position().y == doctest::Approx(secondY));
  CHECK(stack.children()[2]->position().y == doctest::Approx(thirdY));
}

TEST_CASE("modifier-wrapped root ScrollView keeps viewport height after resize") {
  struct Root {
    lambda::Reactive::Signal<lambda::Point> offset;
    lambda::Reactive::Signal<lambda::Size> viewport;
    lambda::Reactive::Signal<lambda::Size> content;

    lambda::Element body() const {
      return lambda::ScrollView{
          .axis = lambda::ScrollAxis::Vertical,
          .scrollOffset = offset,
          .viewportSize = viewport,
          .contentSize = content,
          .children = lambda::children(
              lambda::Rectangle{}.size(80.f, 100.f),
              lambda::Rectangle{}.size(80.f, 100.f)),
      }.fill(lambda::Colors::red);
    }
  };

  FakeTextSystem textSystem;
  lambda::scenegraph::SceneGraph sceneGraph;
  lambda::Reactive::Signal<lambda::Point> offset{lambda::Point{0.f, 0.f}};
  lambda::Reactive::Signal<lambda::Size> viewport{lambda::Size{}};
  lambda::Reactive::Signal<lambda::Size> content{lambda::Size{}};
  lambda::MountRoot root{
      std::make_unique<lambda::TypedRootHolder<Root>>(
          std::in_place, Root{offset, viewport, content}),
      textSystem,
      testEnvironment(),
      lambda::Size{80.f, 80.f},
  };

  root.mount(sceneGraph);
  root.resize(lambda::Size{80.f, 60.f}, sceneGraph);

  REQUIRE(sceneGraph.root().kind() == lambda::scenegraph::SceneNodeKind::Rect);
  auto const& wrapper = static_cast<lambda::scenegraph::RectNode const&>(sceneGraph.root());
  REQUIRE(wrapper.children().size() == 1);
  auto const& scrollViewport = static_cast<lambda::scenegraph::RectNode const&>(*wrapper.children()[0]);
  CHECK(wrapper.size() == lambda::Size{80.f, 60.f});
  CHECK(scrollViewport.size() == lambda::Size{80.f, 60.f});
  CHECK(viewport.get().height == doctest::Approx(60.f));
  CHECK(content.get().height == doctest::Approx(200.f));

  auto const* scrollInteraction = lambda::interactionData(scrollViewport);
  REQUIRE(scrollInteraction != nullptr);
  REQUIRE(scrollInteraction->onScroll);
  scrollInteraction->onScroll(lambda::Vec2{0.f, -12.f});

  CHECK(offset.get().y == doctest::Approx(12.f));
  REQUIRE(scrollViewport.children().size() >= 1);
  CHECK(scrollViewport.children()[0]->position().y == doctest::Approx(-12.f));
}

TEST_CASE("ScrollView resize preserves child positions when already scrolled") {
  struct Root {
    lambda::Reactive::Signal<lambda::Point> offset;

    lambda::Element body() const {
      return lambda::ScrollView{
          .axis = lambda::ScrollAxis::Vertical,
          .scrollOffset = offset,
          .children = lambda::children(
              lambda::Rectangle{}.size(80.f, 80.f),
              lambda::Rectangle{}.size(80.f, 80.f)),
      };
    }
  };

  FakeTextSystem textSystem;
  lambda::scenegraph::SceneGraph sceneGraph;
  lambda::Reactive::Signal<lambda::Point> offset{lambda::Point{0.f, 20.f}};
  lambda::MountRoot root{
      std::make_unique<lambda::TypedRootHolder<Root>>(std::in_place, Root{offset}),
      textSystem,
      testEnvironment(),
      lambda::Size{80.f, 80.f},
  };

  root.mount(sceneGraph);

  auto const& viewport = static_cast<lambda::scenegraph::RectNode const&>(sceneGraph.root());
  REQUIRE(viewport.children().size() >= 1);
  auto const& content = *viewport.children()[0];
  REQUIRE(content.kind() == lambda::scenegraph::SceneNodeKind::Group);
  REQUIRE(content.children().size() == 2);
  CHECK(content.position().y == doctest::Approx(-20.f));
  CHECK(content.children()[0]->position().y == doctest::Approx(0.f));
  CHECK(content.children()[1]->position().y == doctest::Approx(80.f));

  root.resize(lambda::Size{80.f, 60.f}, sceneGraph);

  CHECK(content.position().y == doctest::Approx(-20.f));
  CHECK(content.children()[0]->position().y == doctest::Approx(0.f));
  CHECK(content.children()[1]->position().y == doctest::Approx(80.f));

  auto const* scrollInteraction = lambda::interactionData(viewport);
  REQUIRE(scrollInteraction != nullptr);
  REQUIRE(scrollInteraction->onScroll);
  scrollInteraction->onScroll(lambda::Vec2{0.f, -12.f});

  CHECK(content.position().y == doctest::Approx(-32.f));
  CHECK(content.children()[0]->position().y == doctest::Approx(0.f));
  CHECK(content.children()[1]->position().y == doctest::Approx(80.f));
}

TEST_CASE("ScaleAroundCenter relayout keeps reactive scale binding alive") {
  struct Root {
    lambda::Reactive::Signal<float> scale;

    lambda::Element body() const {
      return lambda::ScaleAroundCenter{
          .scale = lambda::Reactive::Bindable<float>{[scale = scale] {
            return scale.get();
          }},
          .child = lambda::Element{lambda::Rectangle{}}
                       .size(20.f, 10.f)
                       .fill(lambda::Colors::red),
      };
    }
  };

  FakeTextSystem textSystem;
  lambda::scenegraph::SceneGraph sceneGraph;
  lambda::Reactive::Signal<float> scale{0.96f};
  lambda::MountRoot root{
      std::make_unique<lambda::TypedRootHolder<Root>>(std::in_place, Root{scale}),
      textSystem,
      testEnvironment(),
      lambda::Size{200.f, 100.f},
  };

  root.mount(sceneGraph);
  scale.set(0.92f);

  root.resize(lambda::Size{320.f, 180.f}, sceneGraph);

  CHECK(sceneGraph.root().size().width >= 20.f);
  CHECK(sceneGraph.root().size().height >= 10.f);
}

TEST_CASE("element transform modifiers compose in call order") {
  struct TranslateThenRotate {
    lambda::Element body() const {
      return lambda::Rectangle{}
          .size(10.f, 10.f)
          .translate(10.f, 0.f)
          .rotate(1.5707963267948966f);
    }
  };
  struct RotateThenTranslate {
    lambda::Element body() const {
      return lambda::Rectangle{}
          .size(10.f, 10.f)
          .rotate(1.5707963267948966f)
          .translate(10.f, 0.f);
    }
  };
  struct RepeatedTranslate {
    lambda::Element body() const {
      return lambda::Rectangle{}
          .size(10.f, 10.f)
          .translate(10.f, 2.f)
          .translate(3.f, 4.f);
    }
  };

  FakeTextSystem textSystem;
  lambda::scenegraph::SceneGraph translateThenRotateGraph;
  lambda::scenegraph::SceneGraph rotateThenTranslateGraph;
  lambda::scenegraph::SceneGraph repeatedTranslateGraph;

  lambda::MountRoot translateThenRotateRoot{
      std::make_unique<lambda::TypedRootHolder<TranslateThenRotate>>(std::in_place),
      textSystem,
      testEnvironment(),
      lambda::Size{100.f, 100.f},
  };
  lambda::MountRoot rotateThenTranslateRoot{
      std::make_unique<lambda::TypedRootHolder<RotateThenTranslate>>(std::in_place),
      textSystem,
      testEnvironment(),
      lambda::Size{100.f, 100.f},
  };
  lambda::MountRoot repeatedTranslateRoot{
      std::make_unique<lambda::TypedRootHolder<RepeatedTranslate>>(std::in_place),
      textSystem,
      testEnvironment(),
      lambda::Size{100.f, 100.f},
  };

  translateThenRotateRoot.mount(translateThenRotateGraph);
  rotateThenTranslateRoot.mount(rotateThenTranslateGraph);
  repeatedTranslateRoot.mount(repeatedTranslateGraph);

  lambda::Point const sample{1.f, 0.f};
  lambda::Point const tr =
      translateThenRotateGraph.root().transform().apply(sample);
  lambda::Point const rt =
      rotateThenTranslateGraph.root().transform().apply(sample);
  lambda::Point const repeated =
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
    lambda::Reactive::Signal<float> dx;

    lambda::Element body() const {
      return lambda::Rectangle{}
          .size(10.f, 10.f)
          .translate(lambda::Reactive::Bindable<float>{[dx = dx] {
            return dx.get();
          }},
                     0.f);
    }
  };

  FakeTextSystem textSystem;
  lambda::scenegraph::SceneGraph sceneGraph;
  lambda::Reactive::Signal<float> dx{0.f};
  lambda::MountRoot root{
      std::make_unique<lambda::TypedRootHolder<Root>>(std::in_place, Root{dx}),
      textSystem,
      testEnvironment(),
      lambda::Size{100.f, 100.f},
  };

  root.mount(sceneGraph);
  CHECK(sceneGraph.root().transform().apply({0.f, 0.f}).x == doctest::Approx(0.f));

  dx.set(21.f);

  CHECK(sceneGraph.root().transform().apply({0.f, 0.f}).x == doctest::Approx(21.f));
}

TEST_CASE("TextInput fills finite assigned stack width") {
  struct Root {
    lambda::Element body() const {
      auto value = lambda::useState(std::string{"hello"});
      return lambda::VStack{
          .alignment = lambda::Alignment::Start,
          .children = lambda::children(lambda::TextInput{
              .value = value,
              .multiline = true,
              .multilineHeight = {.fixed = 40.f},
          }),
      };
    }
  };

  FakeTextSystem textSystem;
  lambda::scenegraph::SceneGraph sceneGraph;
  lambda::MountRoot root{
      std::make_unique<lambda::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      testEnvironment(),
      lambda::Size{180.f, 100.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == lambda::scenegraph::SceneNodeKind::Group);
  REQUIRE(sceneGraph.root().children().size() == 1);
  CHECK(sceneGraph.root().children()[0]->size().width == doctest::Approx(180.f));
}

TEST_CASE("ScrollView mount emits overlay indicators for overflowing content") {
  struct Root {
    lambda::Element body() const {
      return lambda::ScrollView{
          .axis = lambda::ScrollAxis::Vertical,
          .children = lambda::children(
              lambda::Rectangle{}.size(60.f, 30.f),
              lambda::Rectangle{}.size(60.f, 30.f)),
      };
    }
  };

  FakeTextSystem textSystem;
  lambda::scenegraph::SceneGraph sceneGraph;
  lambda::MountRoot root{
      std::make_unique<lambda::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      testEnvironment(),
      lambda::Size{80.f, 40.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == lambda::scenegraph::SceneNodeKind::Rect);
  auto const& viewport = static_cast<lambda::scenegraph::RectNode const&>(sceneGraph.root());
  CHECK(viewport.clipsContents());
  REQUIRE(viewport.children().size() == 2);
  REQUIRE(viewport.children()[0]->kind() == lambda::scenegraph::SceneNodeKind::Group);
  REQUIRE(viewport.children()[1]->kind() == lambda::scenegraph::SceneNodeKind::Rect);

  auto const& overlay = static_cast<lambda::scenegraph::RectNode const&>(*viewport.children()[1]);
  CHECK(overlay.opacity() == doctest::Approx(0.f));
  REQUIRE(overlay.children().size() == 1);
  CHECK(overlay.children()[0]->bounds().x == doctest::Approx(73.f));
  float const initialIndicatorY = overlay.children()[0]->bounds().y;

  auto const* scrollInteraction = lambda::interactionData(viewport);
  REQUIRE(scrollInteraction != nullptr);
  REQUIRE(scrollInteraction->onScroll);
  scrollInteraction->onScroll(lambda::Vec2{0.f, -12.f});

  CHECK(viewport.children()[0]->position().y == doctest::Approx(-12.f));
  CHECK(overlay.opacity() == doctest::Approx(0.f));
  CHECK(overlay.children()[0]->bounds().y > initialIndicatorY);
}

TEST_CASE("ScrollView updates content size when mounted content grows reactively") {
  struct Root {
    lambda::Reactive::Signal<float> childHeight;
    lambda::Reactive::Signal<lambda::Size> contentSize;

    lambda::Element body() const {
      return lambda::ScrollView{
          .axis = lambda::ScrollAxis::Vertical,
          .contentSize = contentSize,
          .children = lambda::children(
              lambda::Rectangle{}.size(60.f, [childHeight = childHeight] {
                return childHeight.get();
              })),
      };
    }
  };

  FakeTextSystem textSystem;
  lambda::scenegraph::SceneGraph sceneGraph;
  lambda::Reactive::Signal<float> childHeight{30.f};
  lambda::Reactive::Signal<lambda::Size> contentSize{};
  lambda::MountRoot root{
      std::make_unique<lambda::TypedRootHolder<Root>>(
          std::in_place, Root{.childHeight = childHeight, .contentSize = contentSize}),
      textSystem,
      testEnvironment(),
      lambda::Size{80.f, 40.f},
  };

  root.mount(sceneGraph);
  CHECK(contentSize.peek().height == doctest::Approx(30.f));

  childHeight = 90.f;

  CHECK(contentSize.peek().height == doctest::Approx(90.f));
  auto const* scrollInteraction = lambda::interactionData(sceneGraph.root());
  REQUIRE(scrollInteraction != nullptr);
  scrollInteraction->onScroll(lambda::Vec2{0.f, -50.f});
  REQUIRE(sceneGraph.root().children().size() >= 1);
  CHECK(sceneGraph.root().children()[0]->position().y == doctest::Approx(-50.f));
}

TEST_CASE("MountRoot keeps Bindable effects scoped to the mount") {
  struct Root {
    lambda::Reactive::Signal<bool> hot;

    lambda::Element body() const {
      return lambda::Element{lambda::Rectangle{}}
          .size(10.f, 10.f)
          .fill([hot = hot] {
            return hot() ? lambda::Colors::red : lambda::Colors::blue;
          });
    }
  };

  FakeTextSystem textSystem;
  lambda::scenegraph::SceneGraph sceneGraph;
  lambda::Reactive::Signal<bool> hot{true};
  lambda::MountRoot root{
      std::make_unique<lambda::TypedRootHolder<Root>>(std::in_place, Root{hot}),
      textSystem,
      testEnvironment(),
      lambda::Size{200.f, 100.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == lambda::scenegraph::SceneNodeKind::Rect);
  auto const& rect = static_cast<lambda::scenegraph::RectNode const&>(sceneGraph.root());
  CHECK(solidColor(rect) == lambda::Colors::red);

  hot.set(false);
  CHECK(solidColor(rect) == lambda::Colors::blue);

  root.unmount(sceneGraph);
  CHECK_FALSE(root.mounted());
  CHECK(sceneGraph.root().kind() == lambda::scenegraph::SceneNodeKind::Group);
  hot.set(true);
}

TEST_CASE("nested body component bindings inherit the root redraw callback") {
  struct Child {
    lambda::Reactive::Signal<bool> hot;

    lambda::Element body() const {
      return lambda::Element{lambda::Rectangle{}}
          .size(10.f, 10.f)
          .fill([hot = hot] {
            return hot() ? lambda::Colors::red : lambda::Colors::blue;
          });
    }
  };

  struct Root {
    lambda::Reactive::Signal<bool> hot;

    lambda::Element body() const {
      return lambda::Element{Child{hot}};
    }
  };

  FakeTextSystem textSystem;
  lambda::scenegraph::SceneGraph sceneGraph;
  lambda::Reactive::Signal<bool> hot{true};
  int redraws = 0;
  lambda::MountRoot root{
      std::make_unique<lambda::TypedRootHolder<Root>>(std::in_place, Root{hot}),
      textSystem,
      testEnvironment(),
      lambda::Size{200.f, 100.f},
      [&] { ++redraws; },
  };

  root.mount(sceneGraph);
  redraws = 0;
  hot.set(false);

  CHECK(redraws == 1);
}

TEST_CASE("container mounting composes slot origin with explicit child position") {
  struct Root {
    lambda::Element body() const {
      return lambda::ZStack{
          .horizontalAlignment = lambda::Alignment::Start,
          .verticalAlignment = lambda::Alignment::Start,
          .children = lambda::children(
              lambda::Rectangle{}
                  .size(44.f, 26.f)
                  .fill(lambda::Colors::blue),
              lambda::Rectangle{}
                  .size(18.f, 18.f)
                  .position(22.f, 4.f)
                  .fill(lambda::Colors::red)),
      };
    }
  };

  FakeTextSystem textSystem;
  lambda::scenegraph::SceneGraph sceneGraph;
  lambda::MountRoot root{
      std::make_unique<lambda::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      testEnvironment(),
      lambda::Size{44.f, 26.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == lambda::scenegraph::SceneNodeKind::Group);
  auto const& group = sceneGraph.root();
  REQUIRE(group.children().size() == 2);
  CHECK(group.children()[0]->position() == lambda::Point{0.f, 0.f});
  CHECK(group.children()[1]->position() == lambda::Point{22.f, 4.f});
}
