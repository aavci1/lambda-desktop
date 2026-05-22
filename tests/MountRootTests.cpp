#include <doctest/doctest.h>

#include <Flux/UI/Detail/RootHolder.hpp>
#include <Flux/Graphics/TextSystem.hpp>
#include <Flux/Reactive/Signal.hpp>
#include <Flux/SceneGraph/SceneNode.hpp>
#include <Flux/UI/InteractionData.hpp>
#include <Flux/SceneGraph/RectNode.hpp>
#include <Flux/SceneGraph/SceneGraph.hpp>
#include <Flux/UI/MeasureContext.hpp>
#include <Flux/UI/MountRoot.hpp>
#include <Flux/UI/MountContext.hpp>
#include <Flux/UI/Theme.hpp>
#include <Flux/UI/Views/Grid.hpp>
#include <Flux/UI/Views/HStack.hpp>
#include <Flux/UI/Views/Popover.hpp>
#include <Flux/UI/Views/PopoverCalloutShape.hpp>
#include <Flux/UI/Views/Rectangle.hpp>
#include <Flux/UI/Views/ScaleAroundCenter.hpp>
#include <Flux/UI/Views/ScrollView.hpp>
#include <Flux/UI/Views/Spacer.hpp>
#include <Flux/UI/Views/Text.hpp>
#include <Flux/UI/Views/TextInput.hpp>
#include <Flux/UI/Views/VStack.hpp>
#include <Flux/UI/Views/ZStack.hpp>

#include <cmath>
#include <limits>
#include <memory>
#include <optional>
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

class MeasuringTextSystem final : public flux::TextSystem {
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
    return {80.f, 16.f};
  }

  flux::Size measure(std::string_view text, flux::Font const&, flux::Color const&, float,
                     flux::TextLayoutOptions const&) override {
    return {std::max(24.f, static_cast<float>(text.size()) * 6.f), 16.f};
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

flux::EnvironmentBinding testEnvironment() {
  return flux::EnvironmentBinding{}.withValue<flux::ThemeKey>(flux::Theme::light());
}

flux::Color solidColor(flux::scenegraph::RectNode const& rect) {
  flux::Color color{};
  CHECK(rect.fill().solidColor(&color));
  return color;
}

struct IntrinsicBox {
  flux::Size measure(flux::MeasureContext& ctx, flux::LayoutConstraints const&,
                     flux::LayoutHints const&, flux::TextSystem&) const {
    ctx.advanceChildSlot();
    return {24.f, 12.f};
  }

  std::unique_ptr<flux::scenegraph::SceneNode> mount(flux::MountContext&) const {
    return std::make_unique<flux::scenegraph::RectNode>(
        flux::Rect{0.f, 0.f, 24.f, 12.f});
  }
};

struct StretchBox {
  flux::Size intrinsic{24.f, 12.f};

  flux::Size measure(flux::MeasureContext& ctx, flux::LayoutConstraints const&,
                     flux::LayoutHints const&, flux::TextSystem&) const {
    ctx.advanceChildSlot();
    return intrinsic;
  }

  std::unique_ptr<flux::scenegraph::SceneNode> mount(flux::MountContext& ctx) const {
    auto sizeFor = [intrinsic = intrinsic](flux::LayoutConstraints const& constraints) {
      flux::Size size{
          std::isfinite(constraints.maxWidth) ? constraints.maxWidth : intrinsic.width,
          std::isfinite(constraints.maxHeight) ? constraints.maxHeight : intrinsic.height,
      };
      size.width = std::max(size.width, constraints.minWidth);
      size.height = std::max(size.height, constraints.minHeight);
      return size;
    };

    flux::Size const initialSize = sizeFor(ctx.constraints());
    auto group = std::make_unique<flux::scenegraph::SceneNode>(
        flux::Rect{0.f, 0.f, initialSize.width, initialSize.height});
    auto* rawGroup = group.get();
    rawGroup->setRelayout([rawGroup, sizeFor = std::move(sizeFor)](
                              flux::LayoutConstraints const& constraints) {
      rawGroup->setSize(sizeFor(constraints));
    });
    return group;
  }
};

flux::LayoutConstraints fixedConstraints(flux::Size size) {
  return flux::LayoutConstraints{
      .maxWidth = std::max(0.f, size.width),
      .maxHeight = std::max(0.f, size.height),
      .minWidth = std::max(0.f, size.width),
      .minHeight = std::max(0.f, size.height),
  };
}

struct RelayoutProbeFrame {
  flux::Element child;
  int* relayouts = nullptr;

  flux::Size measure(flux::MeasureContext& ctx, flux::LayoutConstraints const&,
                     flux::LayoutHints const&, flux::TextSystem&) const {
    ctx.advanceChildSlot();
    return {100.f, 100.f};
  }

  std::unique_ptr<flux::scenegraph::SceneNode> mount(flux::MountContext& ctx) const {
    auto group = std::make_unique<flux::scenegraph::SceneNode>(
        flux::Rect{0.f, 0.f, 100.f, 100.f});
    flux::MountContext childCtx = ctx.childWithSharedScope(fixedConstraints({100.f, 100.f}), ctx.hints());
    auto childNode = child.mount(childCtx);
    flux::scenegraph::SceneNode* rawChild = childNode.get();
    if (childNode) {
      group->appendChild(std::move(childNode));
    }
    auto* rawGroup = group.get();
    rawGroup->setLayoutConstraints(ctx.constraints());
    rawGroup->setRelayout([rawGroup, rawChild, relayouts = relayouts](
                              flux::LayoutConstraints const&) {
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
  flux::Element child;

  flux::Size measure(flux::MeasureContext& ctx, flux::LayoutConstraints const& constraints,
                     flux::LayoutHints const& hints, flux::TextSystem& textSystem) const {
    return child.measure(ctx, constraints, hints, textSystem);
  }

  std::unique_ptr<flux::scenegraph::SceneNode> mount(flux::MountContext& ctx) const {
    auto group = std::make_unique<flux::scenegraph::SceneNode>();
    flux::MountContext childCtx = ctx.childWithSharedScope(ctx.constraints(), ctx.hints());
    auto childNode = child.mount(childCtx);
    flux::scenegraph::SceneNode* rawChild = childNode.get();
    if (childNode) {
      group->setSize(childNode->size());
      group->appendChild(std::move(childNode));
    }
    flux::scenegraph::SceneNode* rawGroup = group.get();
    rawGroup->setLayoutConstraints(ctx.constraints());
    rawGroup->setRelayout([rawGroup, rawChild](flux::LayoutConstraints const& constraints) {
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
  flux::Reactive::Signal<float> width;

  flux::Element body() const {
    if (depth <= 0) {
      return flux::Element{flux::Rectangle{}}
          .size([width = width] {
                  return width.get();
                },
                10.f);
    }
    return flux::Element{RelayoutPassthroughFrame{
        .child = flux::Element{DeepRelayoutNode{depth - 1, width}},
    }};
  }
};

} // namespace

TEST_CASE("MountRoot mounts a static root once") {
  int bodyCalls = 0;

  struct Root {
    int* bodyCalls = nullptr;

    flux::Element body() const {
      ++*bodyCalls;
      return flux::Element{flux::Rectangle{}}
          .size(20.f, 30.f)
          .fill(flux::Colors::red);
    }
  };

  FakeTextSystem textSystem;
  flux::scenegraph::SceneGraph sceneGraph;
  flux::MountRoot root{
      std::make_unique<flux::TypedRootHolder<Root>>(std::in_place, Root{&bodyCalls}),
      textSystem,
      testEnvironment(),
      flux::Size{200.f, 100.f},
  };

  root.mount(sceneGraph);

  CHECK(root.mounted());
  CHECK(bodyCalls == 1);
  REQUIRE(sceneGraph.root().kind() == flux::scenegraph::SceneNodeKind::Rect);
  auto const& rect = static_cast<flux::scenegraph::RectNode const&>(sceneGraph.root());
  CHECK(rect.size() == flux::Size{20.f, 30.f});
  CHECK(solidColor(rect) == flux::Colors::red);
}

TEST_CASE("Popover body mounts callout chrome and reserves arrow depth") {
  struct Root {
    flux::Element body() const {
      return flux::Popover{
          .content = flux::Element{flux::Rectangle{}}
                         .size(100.f, 20.f)
                         .fill(flux::Colors::blue),
          .placement = flux::PopoverPlacement::Below,
          .arrow = true,
      };
    }
  };

  flux::Theme const theme = flux::Theme::light();
  FakeTextSystem textSystem;
  flux::scenegraph::SceneGraph sceneGraph;
  flux::MountRoot root{
      std::make_unique<flux::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      flux::EnvironmentBinding{}.withValue<flux::ThemeKey>(theme),
      flux::Size{300.f, 200.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == flux::scenegraph::SceneNodeKind::Group);
  CHECK(sceneGraph.root().size().width == doctest::Approx(100.f + 2.f * theme.space3));
  CHECK(sceneGraph.root().size().height ==
        doctest::Approx(20.f + 2.f * theme.space3 + flux::PopoverCalloutShape::kArrowH));
  REQUIRE(sceneGraph.root().children().size() == 2);
  CHECK(sceneGraph.root().children()[0]->kind() == flux::scenegraph::SceneNodeKind::Path);
  CHECK(sceneGraph.root().children()[1]->position().y ==
        doctest::Approx(flux::PopoverCalloutShape::kArrowH + theme.space3));
}

TEST_CASE("Popover body follows resolved overlay placement from environment") {
  struct Root {
    flux::Element body() const {
      return flux::Popover{
          .content = flux::Element{flux::Rectangle{}}
                         .size(100.f, 20.f)
                         .fill(flux::Colors::blue),
          .placement = flux::PopoverPlacement::Below,
          .arrow = true,
      };
    }
  };

  flux::Theme const theme = flux::Theme::light();
  FakeTextSystem textSystem;
  flux::scenegraph::SceneGraph sceneGraph;
  flux::MountRoot root{
      std::make_unique<flux::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      flux::EnvironmentBinding{}
          .withValue<flux::ThemeKey>(theme)
          .withValue<flux::ResolvedOverlayPlacementKey>(
              std::optional<flux::OverlayConfig::Placement>{flux::OverlayConfig::Placement::Above}),
      flux::Size{300.f, 200.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == flux::scenegraph::SceneNodeKind::Group);
  REQUIRE(sceneGraph.root().children().size() == 2);
  CHECK(sceneGraph.root().children()[1]->position().y == doctest::Approx(theme.space3));
}

TEST_CASE("composite child body is materialized once across measure and mount") {
  int bodyCalls = 0;

  struct Probe {
    int* bodyCalls = nullptr;

    flux::Element body() const {
      ++*bodyCalls;
      return flux::Rectangle{}.size(20.f, 10.f);
    }
  };

  struct Root {
    int* bodyCalls = nullptr;

    flux::Element body() const {
      return flux::VStack{
          .children = flux::children(Probe{bodyCalls}),
      };
    }
  };

  FakeTextSystem textSystem;
  flux::scenegraph::SceneGraph sceneGraph;
  flux::MountRoot root{
      std::make_unique<flux::TypedRootHolder<Root>>(std::in_place, Root{&bodyCalls}),
      textSystem,
      testEnvironment(),
      flux::Size{200.f, 100.f},
  };

  root.mount(sceneGraph);

  CHECK(bodyCalls == 1);
  REQUIRE(sceneGraph.root().kind() == flux::scenegraph::SceneNodeKind::Group);
  REQUIRE(sceneGraph.root().children().size() == 1);
  CHECK(sceneGraph.root().children()[0]->size() == flux::Size{20.f, 10.f});
}

TEST_CASE("interaction hooks attach reactive signals to mounted interaction data") {
  struct Root {
    flux::Element body() const {
      flux::Reactive::Signal<bool> hovered = flux::useHover();
      flux::Reactive::Signal<bool> pressed = flux::usePress();
      flux::Reactive::Signal<bool> focused = flux::useFocus();
      flux::Reactive::Signal<bool> keyboardFocused = flux::useKeyboardFocus();
      return flux::Rectangle{}
          .size(20.f, 10.f)
          .fill([hovered, pressed, focused, keyboardFocused] {
            if (keyboardFocused.get()) {
              return flux::Colors::yellow;
            }
            if (focused.get()) {
              return flux::Colors::blue;
            }
            if (pressed.get()) {
              return flux::Colors::green;
            }
            if (hovered.get()) {
              return flux::Colors::red;
            }
            return flux::Colors::black;
          })
          .focusable(true)
          .onTap([] {});
    }
  };

  FakeTextSystem textSystem;
  flux::scenegraph::SceneGraph sceneGraph;
  flux::MountRoot root{
      std::make_unique<flux::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      testEnvironment(),
      flux::Size{200.f, 100.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == flux::scenegraph::SceneNodeKind::Rect);
  auto const& rect = static_cast<flux::scenegraph::RectNode const&>(sceneGraph.root());
  auto const* interaction = flux::interactionData(rect);
  REQUIRE(interaction != nullptr);
  CHECK(solidColor(rect) == flux::Colors::black);

  interaction->hoverSignal.set(true);
  CHECK(solidColor(rect) == flux::Colors::red);
  interaction->hoverSignal.set(false);
  CHECK(solidColor(rect) == flux::Colors::black);

  interaction->pressSignal.set(true);
  CHECK(solidColor(rect) == flux::Colors::green);
  interaction->pressSignal.set(false);
  CHECK(solidColor(rect) == flux::Colors::black);

  interaction->focusSignal.set(true);
  CHECK(solidColor(rect) == flux::Colors::blue);
  interaction->focusSignal.set(false);
  CHECK(solidColor(rect) == flux::Colors::black);

  interaction->keyboardFocusSignal.set(true);
  CHECK(solidColor(rect) == flux::Colors::yellow);
  interaction->keyboardFocusSignal.set(false);
  CHECK(solidColor(rect) == flux::Colors::black);
}

TEST_CASE("modifier envelopes honor fixed viewport constraints") {
  struct Root {
    flux::Element body() const {
      return flux::Element{IntrinsicBox{}}
          .onTap([] {});
    }
  };

  FakeTextSystem textSystem;
  flux::scenegraph::SceneGraph sceneGraph;
  flux::MountRoot root{
      std::make_unique<flux::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      testEnvironment(),
      flux::Size{200.f, 100.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == flux::scenegraph::SceneNodeKind::Rect);
  CHECK(sceneGraph.root().size() == flux::Size{200.f, 100.f});
  REQUIRE(sceneGraph.root().children().size() == 1);
  CHECK(sceneGraph.root().children()[0]->size() == flux::Size{24.f, 12.f});
}

TEST_CASE("HStack flex children honor assigned main-axis size with explicit modifiers") {
  struct Root {
    flux::Element body() const {
      return flux::HStack{
          .spacing = 12.f,
          .alignment = flux::Alignment::Center,
          .children = flux::children(
              flux::Element{flux::Rectangle{}}
                  .size(56.f, 54.f)
                  .fill(flux::Colors::red)
                  .flex(2.f, 1.f, 0.f),
              flux::Rectangle{}.size(56.f, 76.f),
              flux::Element{flux::Rectangle{}}
                  .size(56.f, 40.f)
                  .fill(flux::Colors::blue)
                  .flex(1.f, 1.f, 0.f),
              flux::Rectangle{}.size(56.f, 54.f)),
      };
    }
  };

  FakeTextSystem textSystem;
  flux::scenegraph::SceneGraph sceneGraph;
  flux::MountRoot root{
      std::make_unique<flux::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      testEnvironment(),
      flux::Size{704.f, 100.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == flux::scenegraph::SceneNodeKind::Group);
  auto const& group = sceneGraph.root();
  REQUIRE(group.children().size() == 4);
  CHECK(group.children()[0]->size().width == doctest::Approx(370.666f).epsilon(0.001));
  CHECK(group.children()[2]->size().width == doctest::Approx(185.333f).epsilon(0.001));
  CHECK(group.children()[1]->position().x == doctest::Approx(382.666f).epsilon(0.001));
  CHECK(group.children()[3]->position().x == doctest::Approx(648.f));
}

TEST_CASE("Spacer as composite expands to fill main axis in HStack") {
  struct Root {
    flux::Element body() const {
      return flux::HStack{
          .spacing = 0.f,
          .alignment = flux::Alignment::Stretch,
          .children = flux::children(
              flux::Rectangle{}.size(20.f, 10.f),
              flux::Spacer{},
              flux::Rectangle{}.size(30.f, 10.f)),
      };
    }
  };

  FakeTextSystem textSystem;
  flux::scenegraph::SceneGraph sceneGraph;
  flux::MountRoot root{
      std::make_unique<flux::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      testEnvironment(),
      flux::Size{200.f, 40.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == flux::scenegraph::SceneNodeKind::Group);
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
    flux::Element body() const {
      return flux::HStack{
          .spacing = 0.f,
          .alignment = flux::Alignment::Stretch,
          .children = flux::children(
              flux::Spacer{}.minMainSize(40.f),
              flux::Rectangle{}.size(10.f, 10.f)),
      };
    }
  };

  FakeTextSystem textSystem;
  flux::scenegraph::SceneGraph sceneGraph;
  flux::MountRoot root{
      std::make_unique<flux::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      testEnvironment(),
      flux::Size{30.f, 40.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == flux::scenegraph::SceneNodeKind::Group);
  auto const& group = sceneGraph.root();
  REQUIRE(group.children().size() == 2);
  CHECK(group.children()[0]->size().width == doctest::Approx(40.f));
  CHECK(group.children()[1]->position().x == doctest::Approx(40.f));
}

TEST_CASE("Grid expands row tracks when flex assigns extra height") {
  struct Root {
    flux::Element body() const {
      return flux::VStack{
          .spacing = 0.f,
          .alignment = flux::Alignment::Stretch,
          .children = flux::children(
              flux::Grid{
                  .columns = 1,
                  .horizontalSpacing = 0.f,
                  .verticalSpacing = 8.f,
                  .verticalAlignment = flux::Alignment::Stretch,
                  .children = flux::children(
                      StretchBox{{20.f, 10.f}},
                      StretchBox{{20.f, 160.f}},
                      StretchBox{{20.f, 10.f}},
                      StretchBox{{20.f, 10.f}}),
              }.flex(1.f, 1.f, 0.f)),
      };
    }
  };

  FakeTextSystem textSystem;
  flux::scenegraph::SceneGraph sceneGraph;
  flux::MountRoot root{
      std::make_unique<flux::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      testEnvironment(),
      flux::Size{100.f, 400.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == flux::scenegraph::SceneNodeKind::Group);
  REQUIRE(sceneGraph.root().children().size() == 1);
  flux::scenegraph::SceneNode const& gridNode = *sceneGraph.root().children()[0];
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
    flux::Reactive::Signal<float> barHeight;

    flux::Element body() const {
      return flux::ZStack{
          .horizontalAlignment = flux::Alignment::Center,
          .verticalAlignment = flux::Alignment::Center,
          .children = flux::children(
              flux::Rectangle{}.size(100.f, 100.f),
              flux::HStack{
                  .spacing = 8.f,
                  .alignment = flux::Alignment::Center,
                  .children = flux::children(
                      flux::Rectangle{}.size(
                          20.f,
                          [barHeight = barHeight] {
                            return barHeight.get();
                          }),
                      flux::Rectangle{}.size(20.f, 20.f)),
              }),
      };
    }
  };

  FakeTextSystem textSystem;
  flux::scenegraph::SceneGraph sceneGraph;
  flux::Reactive::Signal<float> barHeight{20.f};
  flux::MountRoot root{
      std::make_unique<flux::TypedRootHolder<Root>>(std::in_place, Root{barHeight}),
      textSystem,
      testEnvironment(),
      flux::Size{100.f, 100.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == flux::scenegraph::SceneNodeKind::Group);
  auto const& zstack = sceneGraph.root();
  REQUIRE(zstack.children().size() == 2);
  auto const& row = *zstack.children()[1];
  REQUIRE(row.kind() == flux::scenegraph::SceneNodeKind::Group);
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
  flux::Reactive::Scope scope;
  flux::MeasureContext measureContext{textSystem, testEnvironment()};
  flux::MountContext context{
      scope,
      textSystem,
      measureContext,
      flux::LayoutConstraints{
          .maxWidth = std::numeric_limits<float>::infinity(),
          .maxHeight = std::numeric_limits<float>::infinity(),
          .minWidth = 0.f,
          .minHeight = 0.f,
      },
  };
  flux::Reactive::Signal<float> width{20.f};
  flux::Element root{DeepRelayoutNode{32, width}};

  std::unique_ptr<flux::scenegraph::SceneNode> node = root.mount(context);

  REQUIRE(node);
  CHECK(node->size().width == doctest::Approx(20.f));

  width.set(48.f);

  CHECK(node->size().width == doctest::Approx(48.f));
}

TEST_CASE("reactive size relayout stops at unchanged ancestors") {
  struct Root {
    flux::Reactive::Signal<float> barHeight;
    int* outerRelayouts = nullptr;

    flux::Element body() const {
      return RelayoutProbeFrame{
          .child = flux::Element{flux::ZStack{
              .horizontalAlignment = flux::Alignment::Center,
              .verticalAlignment = flux::Alignment::Center,
              .children = flux::children(
                  flux::Rectangle{}.size(100.f, 100.f),
                  flux::HStack{
                      .spacing = 8.f,
                      .alignment = flux::Alignment::Center,
                      .children = flux::children(
                          flux::Rectangle{}.size(
                              20.f,
                              [barHeight = barHeight] {
                                return barHeight.get();
                              }),
                          flux::Rectangle{}.size(20.f, 20.f)),
                  }),
          }},
          .relayouts = outerRelayouts,
      };
    }
  };

  FakeTextSystem textSystem;
  flux::scenegraph::SceneGraph sceneGraph;
  flux::Reactive::Signal<float> barHeight{20.f};
  int outerRelayouts = 0;
  flux::MountRoot root{
      std::make_unique<flux::TypedRootHolder<Root>>(
          std::in_place, Root{barHeight, &outerRelayouts}),
      textSystem,
      testEnvironment(),
      flux::Size{100.f, 100.f},
  };

  root.mount(sceneGraph);
  barHeight.set(60.f);

  CHECK(outerRelayouts == 0);
}

TEST_CASE("MountContext childWithOwnScope creates a scoped owner") {
  flux::Reactive::Scope rootScope;
  FakeTextSystem textSystem;
  flux::MeasureContext measureContext{textSystem, testEnvironment()};
  flux::MountContext rootContext{
      rootScope,
      textSystem,
      measureContext,
      flux::LayoutConstraints{.maxWidth = 100.f, .maxHeight = 100.f},
  };

  int childCleanups = 0;
  {
    flux::MountContext childContext =
        rootContext.childWithOwnScope(flux::LayoutConstraints{.maxWidth = 40.f, .maxHeight = 20.f});
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
  flux::Reactive::Scope rootScope;
  FakeTextSystem textSystem;
  flux::MeasureContext measureContext{textSystem, testEnvironment()};
  flux::MountContext rootContext{
      rootScope,
      textSystem,
      measureContext,
      flux::LayoutConstraints{.maxWidth = 100.f, .maxHeight = 100.f},
  };

  flux::MountContext childContext =
      rootContext.childWithSharedScope(flux::LayoutConstraints{.maxWidth = 40.f, .maxHeight = 20.f});
  CHECK(&childContext.owner() == &rootContext.owner());
}

TEST_CASE("MountRoot resize relayouts without remounting root state") {
  int bodyCalls = 0;

  struct Root {
    int* bodyCalls = nullptr;

    flux::Element body() const {
      ++*bodyCalls;
      auto width = flux::useState(20.f);
      return flux::Element{flux::Rectangle{}}
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
  flux::scenegraph::SceneGraph sceneGraph;
  flux::MountRoot root{
      std::make_unique<flux::TypedRootHolder<Root>>(std::in_place, Root{&bodyCalls}),
      textSystem,
      testEnvironment(),
      flux::Size{200.f, 100.f},
  };

  root.mount(sceneGraph);
  auto const* interaction = flux::interactionData(sceneGraph.root());
  REQUIRE(interaction != nullptr);
  REQUIRE(interaction->onTap);
  interaction->onTap(flux::MouseButton::Left);
  CHECK(sceneGraph.root().size() == flux::Size{64.f, 10.f});

  root.resize(flux::Size{320.f, 180.f}, sceneGraph);

  CHECK(bodyCalls == 1);
  CHECK(sceneGraph.root().size() == flux::Size{64.f, 10.f});
}

TEST_CASE("MountRoot resize updates viewport-sized root without remount") {
  int bodyCalls = 0;

  struct Root {
    int* bodyCalls = nullptr;

    flux::Element body() const {
      ++*bodyCalls;
      return flux::Rectangle{};
    }
  };

  FakeTextSystem textSystem;
  flux::scenegraph::SceneGraph sceneGraph;
  flux::MountRoot root{
      std::make_unique<flux::TypedRootHolder<Root>>(std::in_place, Root{&bodyCalls}),
      textSystem,
      testEnvironment(),
      flux::Size{200.f, 100.f},
  };

  root.mount(sceneGraph);
  CHECK(sceneGraph.root().size() == flux::Size{200.f, 100.f});

  root.resize(flux::Size{320.f, 180.f}, sceneGraph);

  CHECK(bodyCalls == 1);
  CHECK(sceneGraph.root().size() == flux::Size{320.f, 180.f});
}

TEST_CASE("MountRoot repeated resize applies each viewport synchronously without remount") {
  int bodyCalls = 0;

  struct Root {
    int* bodyCalls = nullptr;

    flux::Element body() const {
      ++*bodyCalls;
      return flux::VStack{
          .children = flux::children(flux::Rectangle{}, flux::Rectangle{}),
      };
    }
  };

  FakeTextSystem textSystem;
  flux::scenegraph::SceneGraph sceneGraph;
  flux::MountRoot root{
      std::make_unique<flux::TypedRootHolder<Root>>(std::in_place, Root{&bodyCalls}),
      textSystem,
      testEnvironment(),
      flux::Size{200.f, 100.f},
  };

  root.mount(sceneGraph);

  for (int i = 0; i < 64; ++i) {
    flux::Size const next{200.f + static_cast<float>(i * 3),
                          100.f + static_cast<float>(i * 2)};
    root.resize(next, sceneGraph);
    CHECK(sceneGraph.root().size() == next);
  }

  CHECK(bodyCalls == 1);
}

TEST_CASE("MountRoot resize preserves direct text positions in stacks") {
  struct Root {
    flux::Element body() const {
      return flux::ScrollView{
          .axis = flux::ScrollAxis::Vertical,
          .children = flux::children(
              flux::Element{flux::VStack{
                  .spacing = 16.f,
                  .alignment = flux::Alignment::Stretch,
                  .children = flux::children(
                      flux::Text{.text = "Alert demo", .font = flux::Font::largeTitle()},
                      flux::Text{.text = "Modal alerts via useAlert().",
                                 .font = flux::Font::body(),
                                 .wrapping = flux::TextWrapping::Wrap},
                      flux::Text{.text = "Tap a button to open an alert.",
                                 .font = flux::Font::footnote(),
                                 .wrapping = flux::TextWrapping::Wrap}),
              }}.padding(24.f)),
      };
    }
  };

  MeasuringTextSystem textSystem;
  flux::scenegraph::SceneGraph sceneGraph;
  flux::MountRoot root{
      std::make_unique<flux::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      testEnvironment(),
      flux::Size{800.f, 800.f},
  };

  root.mount(sceneGraph);

  auto const& viewport = static_cast<flux::scenegraph::RectNode const&>(sceneGraph.root());
  auto const& content = *viewport.children()[0];
  REQUIRE(content.kind() == flux::scenegraph::SceneNodeKind::Group);
  auto const& paddedStack = static_cast<flux::scenegraph::RectNode const&>(*content.children()[0]);
  auto const& stack = *paddedStack.children()[0];
  REQUIRE(stack.kind() == flux::scenegraph::SceneNodeKind::Group);
  REQUIRE(stack.children().size() == 3);
  float const firstY = stack.children()[0]->position().y;
  float const secondY = stack.children()[1]->position().y;
  float const thirdY = stack.children()[2]->position().y;
  CHECK(firstY == doctest::Approx(0.f));
  CHECK(secondY > firstY);
  CHECK(thirdY > secondY);

  root.resize(flux::Size{900.f, 700.f}, sceneGraph);

  CHECK(stack.children()[0]->position().y == doctest::Approx(firstY));
  CHECK(stack.children()[1]->position().y == doctest::Approx(secondY));
  CHECK(stack.children()[2]->position().y == doctest::Approx(thirdY));
}

TEST_CASE("modifier-wrapped root ScrollView keeps viewport height after resize") {
  struct Root {
    flux::Reactive::Signal<flux::Point> offset;
    flux::Reactive::Signal<flux::Size> viewport;
    flux::Reactive::Signal<flux::Size> content;

    flux::Element body() const {
      return flux::ScrollView{
          .axis = flux::ScrollAxis::Vertical,
          .scrollOffset = offset,
          .viewportSize = viewport,
          .contentSize = content,
          .children = flux::children(
              flux::Rectangle{}.size(80.f, 100.f),
              flux::Rectangle{}.size(80.f, 100.f)),
      }.fill(flux::Colors::red);
    }
  };

  FakeTextSystem textSystem;
  flux::scenegraph::SceneGraph sceneGraph;
  flux::Reactive::Signal<flux::Point> offset{flux::Point{0.f, 0.f}};
  flux::Reactive::Signal<flux::Size> viewport{flux::Size{}};
  flux::Reactive::Signal<flux::Size> content{flux::Size{}};
  flux::MountRoot root{
      std::make_unique<flux::TypedRootHolder<Root>>(
          std::in_place, Root{offset, viewport, content}),
      textSystem,
      testEnvironment(),
      flux::Size{80.f, 80.f},
  };

  root.mount(sceneGraph);
  root.resize(flux::Size{80.f, 60.f}, sceneGraph);

  REQUIRE(sceneGraph.root().kind() == flux::scenegraph::SceneNodeKind::Rect);
  auto const& wrapper = static_cast<flux::scenegraph::RectNode const&>(sceneGraph.root());
  REQUIRE(wrapper.children().size() == 1);
  auto const& scrollViewport = static_cast<flux::scenegraph::RectNode const&>(*wrapper.children()[0]);
  CHECK(wrapper.size() == flux::Size{80.f, 60.f});
  CHECK(scrollViewport.size() == flux::Size{80.f, 60.f});
  CHECK(viewport.get().height == doctest::Approx(60.f));
  CHECK(content.get().height == doctest::Approx(200.f));

  auto const* scrollInteraction = flux::interactionData(scrollViewport);
  REQUIRE(scrollInteraction != nullptr);
  REQUIRE(scrollInteraction->onScroll);
  scrollInteraction->onScroll(flux::Vec2{0.f, -12.f});

  CHECK(offset.get().y == doctest::Approx(12.f));
  REQUIRE(scrollViewport.children().size() >= 1);
  CHECK(scrollViewport.children()[0]->position().y == doctest::Approx(-12.f));
}

TEST_CASE("ScrollView resize preserves child positions when already scrolled") {
  struct Root {
    flux::Reactive::Signal<flux::Point> offset;

    flux::Element body() const {
      return flux::ScrollView{
          .axis = flux::ScrollAxis::Vertical,
          .scrollOffset = offset,
          .children = flux::children(
              flux::Rectangle{}.size(80.f, 80.f),
              flux::Rectangle{}.size(80.f, 80.f)),
      };
    }
  };

  FakeTextSystem textSystem;
  flux::scenegraph::SceneGraph sceneGraph;
  flux::Reactive::Signal<flux::Point> offset{flux::Point{0.f, 20.f}};
  flux::MountRoot root{
      std::make_unique<flux::TypedRootHolder<Root>>(std::in_place, Root{offset}),
      textSystem,
      testEnvironment(),
      flux::Size{80.f, 80.f},
  };

  root.mount(sceneGraph);

  auto const& viewport = static_cast<flux::scenegraph::RectNode const&>(sceneGraph.root());
  REQUIRE(viewport.children().size() >= 1);
  auto const& content = *viewport.children()[0];
  REQUIRE(content.kind() == flux::scenegraph::SceneNodeKind::Group);
  REQUIRE(content.children().size() == 2);
  CHECK(content.position().y == doctest::Approx(-20.f));
  CHECK(content.children()[0]->position().y == doctest::Approx(0.f));
  CHECK(content.children()[1]->position().y == doctest::Approx(80.f));

  root.resize(flux::Size{80.f, 60.f}, sceneGraph);

  CHECK(content.position().y == doctest::Approx(-20.f));
  CHECK(content.children()[0]->position().y == doctest::Approx(0.f));
  CHECK(content.children()[1]->position().y == doctest::Approx(80.f));

  auto const* scrollInteraction = flux::interactionData(viewport);
  REQUIRE(scrollInteraction != nullptr);
  REQUIRE(scrollInteraction->onScroll);
  scrollInteraction->onScroll(flux::Vec2{0.f, -12.f});

  CHECK(content.position().y == doctest::Approx(-32.f));
  CHECK(content.children()[0]->position().y == doctest::Approx(0.f));
  CHECK(content.children()[1]->position().y == doctest::Approx(80.f));
}

TEST_CASE("ScaleAroundCenter relayout keeps reactive scale binding alive") {
  struct Root {
    flux::Reactive::Signal<float> scale;

    flux::Element body() const {
      return flux::ScaleAroundCenter{
          .scale = flux::Reactive::Bindable<float>{[scale = scale] {
            return scale.get();
          }},
          .child = flux::Element{flux::Rectangle{}}
                       .size(20.f, 10.f)
                       .fill(flux::Colors::red),
      };
    }
  };

  FakeTextSystem textSystem;
  flux::scenegraph::SceneGraph sceneGraph;
  flux::Reactive::Signal<float> scale{0.96f};
  flux::MountRoot root{
      std::make_unique<flux::TypedRootHolder<Root>>(std::in_place, Root{scale}),
      textSystem,
      testEnvironment(),
      flux::Size{200.f, 100.f},
  };

  root.mount(sceneGraph);
  scale.set(0.92f);

  root.resize(flux::Size{320.f, 180.f}, sceneGraph);

  CHECK(sceneGraph.root().size().width >= 20.f);
  CHECK(sceneGraph.root().size().height >= 10.f);
}

TEST_CASE("element transform modifiers compose in call order") {
  struct TranslateThenRotate {
    flux::Element body() const {
      return flux::Rectangle{}
          .size(10.f, 10.f)
          .translate(10.f, 0.f)
          .rotate(1.5707963267948966f);
    }
  };
  struct RotateThenTranslate {
    flux::Element body() const {
      return flux::Rectangle{}
          .size(10.f, 10.f)
          .rotate(1.5707963267948966f)
          .translate(10.f, 0.f);
    }
  };
  struct RepeatedTranslate {
    flux::Element body() const {
      return flux::Rectangle{}
          .size(10.f, 10.f)
          .translate(10.f, 2.f)
          .translate(3.f, 4.f);
    }
  };

  FakeTextSystem textSystem;
  flux::scenegraph::SceneGraph translateThenRotateGraph;
  flux::scenegraph::SceneGraph rotateThenTranslateGraph;
  flux::scenegraph::SceneGraph repeatedTranslateGraph;

  flux::MountRoot translateThenRotateRoot{
      std::make_unique<flux::TypedRootHolder<TranslateThenRotate>>(std::in_place),
      textSystem,
      testEnvironment(),
      flux::Size{100.f, 100.f},
  };
  flux::MountRoot rotateThenTranslateRoot{
      std::make_unique<flux::TypedRootHolder<RotateThenTranslate>>(std::in_place),
      textSystem,
      testEnvironment(),
      flux::Size{100.f, 100.f},
  };
  flux::MountRoot repeatedTranslateRoot{
      std::make_unique<flux::TypedRootHolder<RepeatedTranslate>>(std::in_place),
      textSystem,
      testEnvironment(),
      flux::Size{100.f, 100.f},
  };

  translateThenRotateRoot.mount(translateThenRotateGraph);
  rotateThenTranslateRoot.mount(rotateThenTranslateGraph);
  repeatedTranslateRoot.mount(repeatedTranslateGraph);

  flux::Point const sample{1.f, 0.f};
  flux::Point const tr =
      translateThenRotateGraph.root().transform().apply(sample);
  flux::Point const rt =
      rotateThenTranslateGraph.root().transform().apply(sample);
  flux::Point const repeated =
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
    flux::Reactive::Signal<float> dx;

    flux::Element body() const {
      return flux::Rectangle{}
          .size(10.f, 10.f)
          .translate(flux::Reactive::Bindable<float>{[dx = dx] {
            return dx.get();
          }},
                     0.f);
    }
  };

  FakeTextSystem textSystem;
  flux::scenegraph::SceneGraph sceneGraph;
  flux::Reactive::Signal<float> dx{0.f};
  flux::MountRoot root{
      std::make_unique<flux::TypedRootHolder<Root>>(std::in_place, Root{dx}),
      textSystem,
      testEnvironment(),
      flux::Size{100.f, 100.f},
  };

  root.mount(sceneGraph);
  CHECK(sceneGraph.root().transform().apply({0.f, 0.f}).x == doctest::Approx(0.f));

  dx.set(21.f);

  CHECK(sceneGraph.root().transform().apply({0.f, 0.f}).x == doctest::Approx(21.f));
}

TEST_CASE("TextInput fills finite assigned stack width") {
  struct Root {
    flux::Element body() const {
      auto value = flux::useState(std::string{"hello"});
      return flux::VStack{
          .alignment = flux::Alignment::Start,
          .children = flux::children(flux::TextInput{
              .value = value,
              .multiline = true,
              .multilineHeight = {.fixed = 40.f},
          }),
      };
    }
  };

  FakeTextSystem textSystem;
  flux::scenegraph::SceneGraph sceneGraph;
  flux::MountRoot root{
      std::make_unique<flux::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      testEnvironment(),
      flux::Size{180.f, 100.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == flux::scenegraph::SceneNodeKind::Group);
  REQUIRE(sceneGraph.root().children().size() == 1);
  CHECK(sceneGraph.root().children()[0]->size().width == doctest::Approx(180.f));
}

TEST_CASE("ScrollView mount emits overlay indicators for overflowing content") {
  struct Root {
    flux::Element body() const {
      return flux::ScrollView{
          .axis = flux::ScrollAxis::Vertical,
          .children = flux::children(
              flux::Rectangle{}.size(60.f, 30.f),
              flux::Rectangle{}.size(60.f, 30.f)),
      };
    }
  };

  FakeTextSystem textSystem;
  flux::scenegraph::SceneGraph sceneGraph;
  flux::MountRoot root{
      std::make_unique<flux::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      testEnvironment(),
      flux::Size{80.f, 40.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == flux::scenegraph::SceneNodeKind::Rect);
  auto const& viewport = static_cast<flux::scenegraph::RectNode const&>(sceneGraph.root());
  CHECK(viewport.clipsContents());
  REQUIRE(viewport.children().size() == 2);
  REQUIRE(viewport.children()[0]->kind() == flux::scenegraph::SceneNodeKind::Group);
  REQUIRE(viewport.children()[1]->kind() == flux::scenegraph::SceneNodeKind::Rect);

  auto const& overlay = static_cast<flux::scenegraph::RectNode const&>(*viewport.children()[1]);
  CHECK(overlay.opacity() == doctest::Approx(0.f));
  REQUIRE(overlay.children().size() == 1);
  CHECK(overlay.children()[0]->bounds().x == doctest::Approx(73.f));
  float const initialIndicatorY = overlay.children()[0]->bounds().y;

  auto const* scrollInteraction = flux::interactionData(viewport);
  REQUIRE(scrollInteraction != nullptr);
  REQUIRE(scrollInteraction->onScroll);
  scrollInteraction->onScroll(flux::Vec2{0.f, -12.f});

  CHECK(viewport.children()[0]->position().y == doctest::Approx(-12.f));
  CHECK(overlay.opacity() == doctest::Approx(0.f));
  CHECK(overlay.children()[0]->bounds().y > initialIndicatorY);
}

TEST_CASE("ScrollView updates content size when mounted content grows reactively") {
  struct Root {
    flux::Reactive::Signal<float> childHeight;
    flux::Reactive::Signal<flux::Size> contentSize;

    flux::Element body() const {
      return flux::ScrollView{
          .axis = flux::ScrollAxis::Vertical,
          .contentSize = contentSize,
          .children = flux::children(
              flux::Rectangle{}.size(60.f, [childHeight = childHeight] {
                return childHeight.get();
              })),
      };
    }
  };

  FakeTextSystem textSystem;
  flux::scenegraph::SceneGraph sceneGraph;
  flux::Reactive::Signal<float> childHeight{30.f};
  flux::Reactive::Signal<flux::Size> contentSize{};
  flux::MountRoot root{
      std::make_unique<flux::TypedRootHolder<Root>>(
          std::in_place, Root{.childHeight = childHeight, .contentSize = contentSize}),
      textSystem,
      testEnvironment(),
      flux::Size{80.f, 40.f},
  };

  root.mount(sceneGraph);
  CHECK(contentSize.peek().height == doctest::Approx(30.f));

  childHeight = 90.f;

  CHECK(contentSize.peek().height == doctest::Approx(90.f));
  auto const* scrollInteraction = flux::interactionData(sceneGraph.root());
  REQUIRE(scrollInteraction != nullptr);
  scrollInteraction->onScroll(flux::Vec2{0.f, -50.f});
  REQUIRE(sceneGraph.root().children().size() >= 1);
  CHECK(sceneGraph.root().children()[0]->position().y == doctest::Approx(-50.f));
}

TEST_CASE("MountRoot keeps Bindable effects scoped to the mount") {
  struct Root {
    flux::Reactive::Signal<bool> hot;

    flux::Element body() const {
      return flux::Element{flux::Rectangle{}}
          .size(10.f, 10.f)
          .fill([hot = hot] {
            return hot() ? flux::Colors::red : flux::Colors::blue;
          });
    }
  };

  FakeTextSystem textSystem;
  flux::scenegraph::SceneGraph sceneGraph;
  flux::Reactive::Signal<bool> hot{true};
  flux::MountRoot root{
      std::make_unique<flux::TypedRootHolder<Root>>(std::in_place, Root{hot}),
      textSystem,
      testEnvironment(),
      flux::Size{200.f, 100.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == flux::scenegraph::SceneNodeKind::Rect);
  auto const& rect = static_cast<flux::scenegraph::RectNode const&>(sceneGraph.root());
  CHECK(solidColor(rect) == flux::Colors::red);

  hot.set(false);
  CHECK(solidColor(rect) == flux::Colors::blue);

  root.unmount(sceneGraph);
  CHECK_FALSE(root.mounted());
  CHECK(sceneGraph.root().kind() == flux::scenegraph::SceneNodeKind::Group);
  hot.set(true);
}

TEST_CASE("nested body component bindings inherit the root redraw callback") {
  struct Child {
    flux::Reactive::Signal<bool> hot;

    flux::Element body() const {
      return flux::Element{flux::Rectangle{}}
          .size(10.f, 10.f)
          .fill([hot = hot] {
            return hot() ? flux::Colors::red : flux::Colors::blue;
          });
    }
  };

  struct Root {
    flux::Reactive::Signal<bool> hot;

    flux::Element body() const {
      return flux::Element{Child{hot}};
    }
  };

  FakeTextSystem textSystem;
  flux::scenegraph::SceneGraph sceneGraph;
  flux::Reactive::Signal<bool> hot{true};
  int redraws = 0;
  flux::MountRoot root{
      std::make_unique<flux::TypedRootHolder<Root>>(std::in_place, Root{hot}),
      textSystem,
      testEnvironment(),
      flux::Size{200.f, 100.f},
      [&] { ++redraws; },
  };

  root.mount(sceneGraph);
  redraws = 0;
  hot.set(false);

  CHECK(redraws == 1);
}

TEST_CASE("container mounting composes slot origin with explicit child position") {
  struct Root {
    flux::Element body() const {
      return flux::ZStack{
          .horizontalAlignment = flux::Alignment::Start,
          .verticalAlignment = flux::Alignment::Start,
          .children = flux::children(
              flux::Rectangle{}
                  .size(44.f, 26.f)
                  .fill(flux::Colors::blue),
              flux::Rectangle{}
                  .size(18.f, 18.f)
                  .position(22.f, 4.f)
                  .fill(flux::Colors::red)),
      };
    }
  };

  FakeTextSystem textSystem;
  flux::scenegraph::SceneGraph sceneGraph;
  flux::MountRoot root{
      std::make_unique<flux::TypedRootHolder<Root>>(std::in_place, Root{}),
      textSystem,
      testEnvironment(),
      flux::Size{44.f, 26.f},
  };

  root.mount(sceneGraph);

  REQUIRE(sceneGraph.root().kind() == flux::scenegraph::SceneNodeKind::Group);
  auto const& group = sceneGraph.root();
  REQUIRE(group.children().size() == 2);
  CHECK(group.children()[0]->position() == flux::Point{0.f, 0.f});
  CHECK(group.children()[1]->position() == flux::Point{22.f, 4.f});
}
