#include <doctest/doctest.h>

#include <Flux/UI/Application.hpp>
#include <Flux/UI/Events.hpp>
#include <Flux/UI/KeyCodes.hpp>
#include <Flux/UI/Shortcut.hpp>
#include <Flux/UI/Window.hpp>
#include <Flux/UI/Detail/RootHolder.hpp>
#include <Flux/UI/Detail/Runtime.hpp>
#include <Flux/Reactive/Signal.hpp>
#include <Flux/UI/InteractionData.hpp>
#include <Flux/SceneGraph/RectNode.hpp>
#include <Flux/UI/Hooks.hpp>
#include <Flux/UI/Overlay.hpp>
#include <Flux/UI/Views/HStack.hpp>
#include <Flux/UI/Views/Popover.hpp>
#include <Flux/UI/Views/PopoverCalloutShape.hpp>
#include <Flux/UI/Views/Rectangle.hpp>
#include <Flux/UI/Views/ScrollView.hpp>
#include <Flux/UI/Views/Select.hpp>
#include <Flux/UI/Views/Show.hpp>
#include <Flux/UI/Views/TextInput.hpp>
#include <Flux/UI/Views/VStack.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace {

struct RuntimeHarness {
  flux::Application app;
  flux::Window& window;
  flux::Runtime runtime;

  RuntimeHarness()
      : app()
      , window(app.createWindow(flux::WindowConfig{
            .size = {240.f, 140.f},
            .title = "Flux Runtime Input Tests",
            .fullscreen = false,
            .resizable = false,
        }))
      , runtime(window) {}

  ~RuntimeHarness() {
    runtime.beginShutdown();
  }

  template<typename Root>
  void setRoot(Root root) {
    runtime.setRoot(std::make_unique<flux::TypedRootHolder<Root>>(
        std::in_place, std::move(root)));
  }

  void pointerMove(flux::Point point) {
    dispatchPointer(flux::InputEvent::Kind::PointerMove, point);
  }

  void pointerDown(flux::Point point) {
    dispatchPointer(flux::InputEvent::Kind::PointerDown, point);
  }

  void pointerUp(flux::Point point) {
    dispatchPointer(flux::InputEvent::Kind::PointerUp, point);
  }

  void keyDown(flux::KeyCode key, flux::Modifiers modifiers = flux::Modifiers::None) {
    flux::InputEvent event{};
    event.kind = flux::InputEvent::Kind::KeyDown;
    event.handle = window.handle();
    event.key = key;
    event.modifiers = modifiers;
    runtime.handleInput(event);
  }

  void textInput(std::string text) {
    flux::InputEvent event{};
    event.kind = flux::InputEvent::Kind::TextInput;
    event.handle = window.handle();
    event.text = std::move(text);
    runtime.handleInput(event);
  }

  void scroll(flux::Point point, flux::Vec2 delta, bool precise = true) {
    flux::InputEvent event{};
    event.kind = flux::InputEvent::Kind::Scroll;
    event.handle = window.handle();
    event.position = {point.x, point.y};
    event.scrollDelta = delta;
    event.preciseScrollDelta = precise;
    runtime.handleInput(event);
  }

private:
  void dispatchPointer(flux::InputEvent::Kind kind, flux::Point point) {
    flux::InputEvent event{};
    event.kind = kind;
    event.handle = window.handle();
    event.position = {point.x, point.y};
    event.button = flux::MouseButton::Left;
    event.pressedButtons =
        kind == flux::InputEvent::Kind::PointerUp ? 0u : static_cast<std::uint8_t>(1u);
    runtime.handleInput(event);
  }
};

struct ProbeView {
  flux::Reactive::Signal<bool>* hover = nullptr;
  flux::Reactive::Signal<bool>* press = nullptr;
  flux::Reactive::Signal<bool>* focus = nullptr;
  flux::Reactive::Signal<bool>* keyboardFocus = nullptr;

  flux::Element body() const {
    if (hover) {
      *hover = flux::useHover();
    }
    if (press) {
      *press = flux::usePress();
    }
    if (focus) {
      *focus = flux::useFocus();
    }
    if (keyboardFocus) {
      *keyboardFocus = flux::useKeyboardFocus();
    }
    return flux::Element{flux::Rectangle{}}
        .size(20.f, 20.f)
        .focusable(true)
        .onTap([] {});
  }
};

struct SingleProbeRoot {
  flux::Reactive::Signal<bool>* hover = nullptr;
  flux::Reactive::Signal<bool>* press = nullptr;
  flux::Reactive::Signal<bool>* focus = nullptr;
  flux::Reactive::Signal<bool>* keyboardFocus = nullptr;

  flux::Element body() const {
    return ProbeView{hover, press, focus, keyboardFocus};
  }
};

struct TextInputFocusRoot {
  flux::Reactive::Signal<std::string>* first = nullptr;
  flux::Reactive::Signal<std::string>* second = nullptr;

  flux::Element body() const {
    return flux::VStack{
        .spacing = 8.f,
        .children = flux::children(
            flux::TextInput{
                .value = *first,
                .placeholder = "First",
            },
            flux::TextInput{
                .value = *second,
                .placeholder = "Second",
            }),
    };
  }
};

struct StatefulOverlayProbe {
  int* bodyCalls = nullptr;
  int* cleanups = nullptr;
  flux::Reactive::Signal<int>* state = nullptr;

  flux::Element body() const {
    ++*bodyCalls;
    auto localState = flux::useState(1);
    *state = localState;
    flux::Reactive::onCleanup([cleanups = cleanups] {
      ++*cleanups;
    });
    return flux::Element{flux::Rectangle{}}
        .size([localState] {
          return 20.f + static_cast<float>(localState.get());
        }, 12.f);
  }
};

struct TwoProbeRoot {
  flux::Reactive::Signal<bool>* firstFocus = nullptr;
  flux::Reactive::Signal<bool>* secondFocus = nullptr;

  flux::Element body() const {
    return flux::HStack{
        .spacing = 8.f,
        .children = flux::children(
            ProbeView{nullptr, nullptr, firstFocus, nullptr},
            ProbeView{nullptr, nullptr, secondFocus, nullptr}),
    };
  }
};

struct ActionProbeView {
  int* fired = nullptr;

  flux::Element body() const {
    flux::useFocus();
    flux::useViewAction("demo.save", [fired = fired] {
      ++*fired;
    });
    return flux::Element{flux::Rectangle{}}
        .size(20.f, 20.f)
        .focusable(true)
        .onTap([] {});
  }
};

struct TwoActionRoot {
  int* firstFired = nullptr;
  int* secondFired = nullptr;

  flux::Element body() const {
    return flux::HStack{
        .spacing = 8.f,
        .children = flux::children(
            ActionProbeView{firstFired},
            ActionProbeView{secondFired}),
    };
  }
};

struct ConditionalActionRoot {
  flux::Reactive::Signal<bool> visible;
  int* fired = nullptr;

  flux::Element body() const {
    return flux::Show(visible, [fired = fired] {
      return flux::Element{ActionProbeView{fired}};
    });
  }
};

struct ConditionalHoverRoot {
  flux::Reactive::Signal<bool> visible;
  flux::Reactive::Signal<bool>* hover = nullptr;

  flux::Element body() const {
    return flux::Show(visible, [hover = hover] {
      return flux::Element{ProbeView{hover, nullptr, nullptr, nullptr}};
    });
  }
};

struct ScrollProbeRoot {
  flux::Reactive::Signal<flux::Point> offset;

  flux::Element body() const {
    return flux::ScrollView{
        .axis = flux::ScrollAxis::Vertical,
        .scrollOffset = offset,
        .children = flux::children(
            flux::Rectangle{}.size(100.f, 100.f),
            flux::Rectangle{}.size(100.f, 100.f)),
    };
  }
};

struct ScrollAnchoredProbeRoot {
  flux::Reactive::Signal<flux::Point> offset;

  flux::Element body() const {
    return flux::ScrollView{
        .axis = flux::ScrollAxis::Vertical,
        .scrollOffset = offset,
        .children = flux::children(
            flux::Rectangle{}.size(100.f, 80.f),
            ProbeView{},
            flux::Rectangle{}.size(100.f, 100.f)),
    };
  }
};

struct SelectKeyboardProbeRoot {
  flux::Element body() const {
    return flux::Element{flux::Select{
        .options = {
            flux::SelectOption{.label = "First"},
            flux::SelectOption{.label = "Second"},
        },
    }}.width(120.f);
  }
};

struct SelectCommitProbeRoot {
  flux::Reactive::Signal<int> selected;
  flux::Reactive::Signal<bool>* nextFocus = nullptr;
  int* changeCount = nullptr;

  flux::Element body() const {
    return flux::VStack{
        .spacing = 8.f,
        .alignment = flux::Alignment::Stretch,
        .children = flux::children(
            flux::Element{flux::Select{
                .selectedIndex = selected,
                .options = {
                    flux::SelectOption{.label = "First"},
                    flux::SelectOption{.label = "Second"},
                },
                .onChange = [changeCount = changeCount](int) {
                  if (changeCount) {
                    ++*changeCount;
                  }
                },
            }}.width(120.f),
            ProbeView{nullptr, nullptr, nextFocus, nullptr}),
    };
  }
};

struct LongSelectProbeRoot {
  flux::Element body() const {
    return flux::Element{flux::Select{
        .options = {
            flux::SelectOption{.label = "Option 1"},
            flux::SelectOption{.label = "Option 2"},
            flux::SelectOption{.label = "Option 3"},
            flux::SelectOption{.label = "Option 4"},
            flux::SelectOption{.label = "Option 5"},
            flux::SelectOption{.label = "Option 6"},
            flux::SelectOption{.label = "Option 7"},
            flux::SelectOption{.label = "Option 8"},
            flux::SelectOption{.label = "Option 9"},
            flux::SelectOption{.label = "Option 10"},
            flux::SelectOption{.label = "Option 11"},
            flux::SelectOption{.label = "Option 12"},
        },
    }}.width(160.f);
  }
};

struct HoverPopoverProbeRoot {
  flux::Element body() const {
    auto [showPopover, hidePopover, presented] = flux::usePopover();
    (void)presented;
    return flux::Element{flux::Rectangle{}}
        .size(20.f, 20.f)
        .onPointerEnter([showPopover] {
          showPopover(flux::Popover{
              .content = flux::Element{flux::Rectangle{}}.size(30.f, 10.f),
              .placement = flux::PopoverPlacement::Below,
              .gap = 0.f,
              .arrow = false,
              .contentPadding = 0.f,
              .backdropColor = flux::Colors::transparent,
              .dismissOnOutsideTap = false,
              .useTapAnchor = false,
              .useHoverLeafAnchor = true,
          });
        })
        .onPointerExit([hidePopover] {
          hidePopover();
        });
  }
};

struct WrappedScrollProbeRoot {
  flux::Reactive::Signal<flux::Point> offset;
  flux::Reactive::Signal<flux::Size> viewport;
  flux::Reactive::Signal<flux::Size> content;

  flux::Element body() const {
    return flux::VStack{
        .spacing = 0.f,
        .alignment = flux::Alignment::Stretch,
        .children = flux::children(
            flux::Rectangle{}.height(20.f),
            flux::ScrollView{
                .axis = flux::ScrollAxis::Vertical,
                .scrollOffset = offset,
                .viewportSize = viewport,
                .contentSize = content,
                .children = flux::children(
                    flux::Rectangle{}.size(100.f, 100.f),
                    flux::Rectangle{}.size(100.f, 100.f)),
            }
                .flex(1.f, 1.f, 0.f)
                .fill(flux::Color::windowBackground())),
    };
  }
};

void checkSameColor(flux::Color actual, flux::Color expected) {
  CHECK(actual.r == doctest::Approx(expected.r));
  CHECK(actual.g == doctest::Approx(expected.g));
  CHECK(actual.b == doctest::Approx(expected.b));
  CHECK(actual.a == doctest::Approx(expected.a));
}

flux::Color solidWindowBackground(flux::Window const& window) {
  flux::Color color{};
  REQUIRE(window.background().kind == flux::WindowBackgroundKind::Fill);
  REQUIRE(window.background().fill.solidColor(&color));
  return color;
}

void registerSaveAction(flux::Window& window) {
  window.registerAction("demo.save", flux::ActionDescriptor{
      .label = "Save",
      .shortcut = flux::shortcuts::Save,
  });
}

flux::scenegraph::SceneNode const* findScrollViewport(flux::scenegraph::SceneNode const& node) {
  auto const* interaction = flux::interactionData(node);
  if (interaction && interaction->onScroll && node.children().size() >= 2) {
    return &node;
  }
  for (auto const& child : node.children()) {
    if (child) {
      if (auto* found = findScrollViewport(*child)) {
        return found;
      }
    }
  }
  return nullptr;
}

void collectTapRects(flux::scenegraph::SceneNode const& node,
                     std::vector<flux::scenegraph::RectNode const*>& out) {
  auto const* interaction = flux::interactionData(node);
  if (node.kind() == flux::scenegraph::SceneNodeKind::Rect &&
      interaction && interaction->onTap) {
    out.push_back(static_cast<flux::scenegraph::RectNode const*>(&node));
  }
  for (auto const& child : node.children()) {
    if (child) {
      collectTapRects(*child, out);
    }
  }
}

flux::Point windowPointInside(flux::OverlayEntry const& entry,
                              flux::scenegraph::SceneNode const& node) {
  flux::Point origin{entry.resolvedFrame.x, entry.resolvedFrame.y};
  flux::scenegraph::SceneNode const* current = &node;
  while (current) {
    origin.x += current->position().x;
    origin.y += current->position().y;
    current = current->parent();
  }
  flux::Size const size = node.size();
  return flux::Point{origin.x + std::min(12.f, std::max(1.f, size.width * 0.5f)),
                     origin.y + std::min(12.f, std::max(1.f, size.height * 0.5f))};
}

float solidFillAlpha(flux::scenegraph::RectNode const& node) {
  flux::Color color{};
  if (node.fill().solidColor(&color)) {
    return color.a;
  }
  return 0.f;
}

flux::Color solidFillColor(flux::scenegraph::RectNode const& node) {
  flux::Color color{};
  if (node.fill().solidColor(&color)) {
    return color;
  }
  return {};
}

} // namespace

TEST_CASE("pointer move into element flips hover signal") {
  RuntimeHarness harness;
  flux::Reactive::Signal<bool> hover;
  harness.setRoot(SingleProbeRoot{.hover = &hover});

  harness.pointerMove({10.f, 10.f});
  CHECK(hover.get());

  harness.pointerMove({100.f, 100.f});
  CHECK_FALSE(hover.get());
}

TEST_CASE("pointer down and up flip press signal") {
  RuntimeHarness harness;
  flux::Reactive::Signal<bool> press;
  harness.setRoot(SingleProbeRoot{.press = &press});

  harness.pointerDown({10.f, 10.f});
  CHECK(press.get());

  harness.pointerUp({10.f, 10.f});
  CHECK_FALSE(press.get());
}

TEST_CASE("pointer down then drag out keeps press until release") {
  RuntimeHarness harness;
  flux::Reactive::Signal<bool> press;
  harness.setRoot(SingleProbeRoot{.press = &press});

  harness.pointerDown({10.f, 10.f});
  REQUIRE(press.get());

  harness.pointerMove({100.f, 100.f});
  CHECK(press.get());

  harness.pointerUp({100.f, 100.f});
  CHECK_FALSE(press.get());
}

TEST_CASE("focus moves between elements") {
  RuntimeHarness harness;
  flux::Reactive::Signal<bool> firstFocus;
  flux::Reactive::Signal<bool> secondFocus;
  harness.setRoot(TwoProbeRoot{.firstFocus = &firstFocus, .secondFocus = &secondFocus});

  harness.keyDown(flux::keys::Tab);
  CHECK(firstFocus.get());
  CHECK_FALSE(secondFocus.get());

  harness.keyDown(flux::keys::Tab);
  CHECK_FALSE(firstFocus.get());
  CHECK(secondFocus.get());
}

TEST_CASE("keyboard focus signal differs from pointer focus") {
  RuntimeHarness harness;
  flux::Reactive::Signal<bool> focus;
  flux::Reactive::Signal<bool> keyboardFocus;
  harness.setRoot(SingleProbeRoot{.focus = &focus, .keyboardFocus = &keyboardFocus});

  harness.pointerDown({10.f, 10.f});
  CHECK(focus.get());
  CHECK_FALSE(keyboardFocus.get());

  harness.pointerDown({100.f, 100.f});
  REQUIRE_FALSE(focus.get());

  harness.keyDown(flux::keys::Tab);
  CHECK(focus.get());
  CHECK(keyboardFocus.get());
}

TEST_CASE("text input participates in keyboard focus traversal") {
  RuntimeHarness harness;
  flux::Reactive::Signal<std::string> first{""};
  flux::Reactive::Signal<std::string> second{""};
  harness.setRoot(TextInputFocusRoot{.first = &first, .second = &second});

  harness.keyDown(flux::keys::Tab);
  CHECK(harness.runtime.focusTargetKey().has_value());
  harness.textInput("A");

  harness.keyDown(flux::keys::Tab);
  harness.textInput("B");

  CHECK(first.get() == "A");
  CHECK(second.get() == "B");
}

TEST_CASE("view action fires only for the focused view") {
  RuntimeHarness harness;
  registerSaveAction(harness.window);
  int firstFired = 0;
  int secondFired = 0;
  harness.setRoot(TwoActionRoot{.firstFired = &firstFired, .secondFired = &secondFired});

  harness.keyDown(flux::keys::Tab);
  harness.keyDown(flux::keys::S, flux::Modifiers::Meta);
  CHECK(firstFired == 1);
  CHECK(secondFired == 0);

  harness.keyDown(flux::keys::Tab);
  harness.keyDown(flux::keys::S, flux::Modifiers::Meta);
  CHECK(firstFired == 1);
  CHECK(secondFired == 1);
}

TEST_CASE("view action deregisters on view unmount") {
  RuntimeHarness harness;
  registerSaveAction(harness.window);
  flux::Reactive::Signal<bool> visible{true};
  int fired = 0;
  harness.setRoot(ConditionalActionRoot{.visible = visible, .fired = &fired});

  harness.pointerDown({10.f, 10.f});
  harness.keyDown(flux::keys::S, flux::Modifiers::Meta);
  REQUIRE(fired == 1);

  visible.set(false);
  harness.keyDown(flux::keys::S, flux::Modifiers::Meta);
  CHECK(fired == 1);
}

TEST_CASE("hover chain disposes signals on subtree unmount") {
  RuntimeHarness harness;
  flux::Reactive::Signal<bool> visible{true};
  flux::Reactive::Signal<bool> hover;
  harness.setRoot(ConditionalHoverRoot{.visible = visible, .hover = &hover});

  harness.pointerMove({10.f, 10.f});
  REQUIRE(hover.get());

  visible.set(false);
  CHECK(hover.disposed());

  harness.pointerMove({100.f, 100.f});
  CHECK(hover.disposed());
}

TEST_CASE("runtime scroll dispatch reaches scroll view") {
  RuntimeHarness harness;
  flux::Reactive::Signal<flux::Point> offset{flux::Point{0.f, 0.f}};
  harness.setRoot(ScrollProbeRoot{.offset = offset});

  harness.scroll({10.f, 10.f}, {0.f, -12.f});

  CHECK(offset.get().x == doctest::Approx(0.f));
  CHECK(offset.get().y == doctest::Approx(12.f));
}

TEST_CASE("scroll view measurement does not overwrite mounted scroll range") {
  RuntimeHarness harness;
  flux::Reactive::Signal<flux::Point> offset{flux::Point{0.f, 0.f}};
  flux::Reactive::Signal<flux::Size> viewport{flux::Size{0.f, 0.f}};
  flux::Reactive::Signal<flux::Size> content{flux::Size{0.f, 0.f}};
  harness.setRoot(WrappedScrollProbeRoot{
      .offset = offset,
      .viewport = viewport,
      .content = content,
  });

  CHECK(content.get().height == doctest::Approx(200.f));
  CHECK(viewport.get().height < content.get().height);

  harness.scroll({10.f, 40.f}, {0.f, -12.f});

  CHECK(offset.get().y == doctest::Approx(12.f));
}

TEST_CASE("runtime exposes tap and hover anchors for overlay placement") {
  RuntimeHarness harness;
  harness.setRoot(SingleProbeRoot{});

  harness.pointerMove({10.f, 10.f});
  std::optional<flux::Rect> hoverAnchor = harness.runtime.hoverAnchor();
  REQUIRE(hoverAnchor.has_value());
  CHECK(harness.runtime.hoverTargetKey().has_value());
  CHECK(hoverAnchor->x == doctest::Approx(0.f));
  CHECK(hoverAnchor->y == doctest::Approx(0.f));
  CHECK(hoverAnchor->width == doctest::Approx(20.f));
  CHECK(hoverAnchor->height == doctest::Approx(20.f));

  harness.pointerDown({10.f, 10.f});
  std::optional<flux::Rect> tapAnchor = harness.runtime.lastTapAnchor();
  REQUIRE(tapAnchor.has_value());
  CHECK(harness.runtime.lastTapTargetKey().has_value());
  CHECK(tapAnchor->x == doctest::Approx(0.f));
  CHECK(tapAnchor->y == doctest::Approx(0.f));
  CHECK(tapAnchor->width == doctest::Approx(20.f));
  CHECK(tapAnchor->height == doctest::Approx(20.f));

  harness.keyDown(flux::keys::Tab);
  std::optional<flux::Rect> focusAnchor = harness.runtime.focusAnchor();
  REQUIRE(focusAnchor.has_value());
  CHECK(harness.runtime.focusTargetKey().has_value());
  CHECK(focusAnchor->x == doctest::Approx(0.f));
  CHECK(focusAnchor->y == doctest::Approx(0.f));
  CHECK(focusAnchor->width == doctest::Approx(20.f));
  CHECK(focusAnchor->height == doctest::Approx(20.f));
}

TEST_CASE("hover popovers keep the exact hover anchor instead of tracking component wrappers") {
  RuntimeHarness harness;
  harness.setRoot(HoverPopoverProbeRoot{});

  harness.pointerMove({10.f, 10.f});

  flux::OverlayEntry const* entry = harness.window.overlayManager().top();
  REQUIRE(entry != nullptr);
  REQUIRE(entry->config.anchor.has_value());
  CHECK(entry->config.anchor->x == doctest::Approx(0.f));
  CHECK(entry->config.anchor->y == doctest::Approx(0.f));
  CHECK(entry->config.anchor->width == doctest::Approx(20.f));
  CHECK(entry->config.anchor->height == doctest::Approx(20.f));
  CHECK_FALSE(entry->config.anchorTrackComponentKey.has_value());
}

TEST_CASE("tracked overlay anchors follow moved trigger nodes") {
  RuntimeHarness harness;
  flux::Reactive::Signal<flux::Point> offset{flux::Point{0.f, 0.f}};
  harness.setRoot(ScrollAnchoredProbeRoot{.offset = offset});

  harness.pointerDown({10.f, 90.f});
  std::optional<flux::ComponentKey> key = harness.runtime.lastTapTargetKey();
  REQUIRE(key.has_value());

  flux::OverlayConfig config{};
  config.anchor = harness.runtime.lastTapAnchor();
  config.anchorTrackComponentKey = key;
  config.placement = flux::OverlayConfig::Placement::Below;
  flux::OverlayId const id = harness.window.overlayManager().push(
      flux::Element{flux::Rectangle{}}.size(30.f, 10.f),
      std::move(config), &harness.runtime);

  flux::OverlayEntry const* entry = harness.window.overlayManager().find(id);
  REQUIRE(entry != nullptr);
  CHECK(entry->resolvedFrame.y == doctest::Approx(100.f));

  harness.scroll({10.f, 90.f}, {0.f, -12.f});
  harness.window.overlayManager().rebuild(harness.window.getSize(), harness.runtime);

  entry = harness.window.overlayManager().find(id);
  REQUIRE(entry != nullptr);
  CHECK(entry->resolvedFrame.y == doctest::Approx(88.f));
}

TEST_CASE("tracked overlay placement flips after scroll changes available space") {
  RuntimeHarness harness;
  flux::Reactive::Signal<flux::Point> offset{flux::Point{0.f, 0.f}};
  harness.setRoot(ScrollAnchoredProbeRoot{.offset = offset});

  harness.pointerDown({10.f, 90.f});
  std::optional<flux::ComponentKey> key = harness.runtime.lastTapTargetKey();
  REQUIRE(key.has_value());

  flux::OverlayConfig config{};
  config.anchor = harness.runtime.lastTapAnchor();
  config.anchorTrackComponentKey = key;
  config.placement = flux::OverlayConfig::Placement::Below;
  config.autoFlipPreferredPlacement = flux::OverlayConfig::Placement::Below;
  flux::OverlayId const id = harness.window.overlayManager().push(
      flux::Element{flux::Rectangle{}}.size(30.f, 50.f),
      std::move(config), &harness.runtime);

  flux::OverlayEntry const* entry = harness.window.overlayManager().find(id);
  REQUIRE(entry != nullptr);
  CHECK(entry->config.placement == flux::OverlayConfig::Placement::Above);
  CHECK(entry->resolvedFrame.y == doctest::Approx(30.f));

  harness.scroll({10.f, 90.f}, {0.f, -60.f});
  harness.window.overlayManager().rebuild(harness.window.getSize(), harness.runtime);

  entry = harness.window.overlayManager().find(id);
  REQUIRE(entry != nullptr);
  CHECK(entry->config.placement == flux::OverlayConfig::Placement::Below);
  CHECK(entry->resolvedFrame.y == doctest::Approx(40.f));
}

TEST_CASE("tracked popover callout arrow follows flipped overlay placement") {
  RuntimeHarness harness;
  flux::Reactive::Signal<flux::Point> offset{flux::Point{0.f, 60.f}};
  harness.setRoot(ScrollAnchoredProbeRoot{.offset = offset});

  harness.pointerDown({10.f, 30.f});
  std::optional<flux::ComponentKey> key = harness.runtime.lastTapTargetKey();
  REQUIRE(key.has_value());

  flux::OverlayConfig config{};
  config.anchor = harness.runtime.lastTapAnchor();
  config.anchorTrackComponentKey = key;
  config.placement = flux::OverlayConfig::Placement::Below;
  config.autoFlipPreferredPlacement = flux::OverlayConfig::Placement::Below;

  flux::Theme const theme = flux::Theme::light();
  flux::OverlayId const id = harness.window.overlayManager().push(
      flux::Popover{
          .content = flux::Element{flux::Rectangle{}}.size(30.f, 10.f),
          .placement = flux::PopoverPlacement::Below,
          .arrow = true,
      },
      std::move(config), &harness.runtime);

  auto calloutContentY = [&]() {
    flux::OverlayEntry const* entry = harness.window.overlayManager().find(id);
    REQUIRE(entry != nullptr);
    REQUIRE(entry->sceneGraph.root().children().size() >= 1);
    flux::scenegraph::SceneNode const* callout =
        entry->sceneGraph.root().children().back().get();
    REQUIRE(callout != nullptr);
    REQUIRE(callout->children().size() == 2);
    return callout->children()[1]->position().y;
  };

  flux::OverlayEntry const* entry = harness.window.overlayManager().find(id);
  REQUIRE(entry != nullptr);
  CHECK(entry->config.placement == flux::OverlayConfig::Placement::Below);
  CHECK(calloutContentY() == doctest::Approx(theme.space3 + flux::PopoverCalloutShape::kArrowH));

  offset = flux::Point{0.f, 0.f};
  harness.window.overlayManager().rebuild(harness.window.getSize(), harness.runtime);

  entry = harness.window.overlayManager().find(id);
  REQUIRE(entry != nullptr);
  CHECK(entry->config.placement == flux::OverlayConfig::Placement::Above);
  CHECK(calloutContentY() == doctest::Approx(theme.space3));
}

TEST_CASE("transparent overlay backdrop still dismisses on outside tap") {
  RuntimeHarness harness;
  harness.setRoot(SingleProbeRoot{});

  flux::OverlayConfig config{};
  config.backdropColor = flux::Colors::transparent;
  config.dismissOnOutsideTap = true;
  flux::OverlayId const id = harness.window.overlayManager().push(
      flux::Element{flux::Rectangle{}}.size(30.f, 10.f),
      std::move(config), &harness.runtime);

  REQUIRE(harness.window.overlayManager().find(id) != nullptr);

  harness.pointerDown({1.f, 1.f});
  harness.pointerUp({1.f, 1.f});

  CHECK(harness.window.overlayManager().find(id) == nullptr);
}

TEST_CASE("transparent overlay backdrop ignores outside tap when dismissal is disabled") {
  RuntimeHarness harness;
  harness.setRoot(SingleProbeRoot{});

  flux::OverlayConfig config{};
  config.backdropColor = flux::Colors::transparent;
  config.dismissOnOutsideTap = false;
  flux::OverlayId const id = harness.window.overlayManager().push(
      flux::Element{flux::Rectangle{}}.size(30.f, 10.f),
      std::move(config), &harness.runtime);

  REQUIRE(harness.window.overlayManager().find(id) != nullptr);

  harness.pointerDown({1.f, 1.f});
  harness.pointerUp({1.f, 1.f});

  CHECK(harness.window.overlayManager().find(id) != nullptr);
}

TEST_CASE("select popover anchors to focused trigger when opened from keyboard") {
  RuntimeHarness harness;
  harness.setRoot(SelectKeyboardProbeRoot{});

  harness.keyDown(flux::keys::Tab);
  std::optional<flux::Rect> focusAnchor = harness.runtime.focusAnchor();
  REQUIRE(focusAnchor.has_value());
  std::optional<flux::ComponentKey> focusKey = harness.runtime.focusTargetKey();
  REQUIRE(focusKey.has_value());

  harness.keyDown(flux::keys::Return);

  flux::OverlayEntry const* entry = harness.window.overlayManager().top();
  REQUIRE(entry != nullptr);
  REQUIRE(entry->config.anchor.has_value());
  CHECK(entry->config.anchor->x == doctest::Approx(focusAnchor->x));
  CHECK(entry->config.anchor->y == doctest::Approx(focusAnchor->y));
  CHECK(entry->config.anchor->width == doctest::Approx(focusAnchor->width));
  CHECK(entry->config.anchor->height == doctest::Approx(focusAnchor->height));
  REQUIRE(entry->config.anchorTrackComponentKey.has_value());
  CHECK(*entry->config.anchorTrackComponentKey == *focusKey);
}

TEST_CASE("select option Enter commits and closes keyboard-opened popover") {
  RuntimeHarness harness;
  flux::Reactive::Signal<int> selected{-1};
  int changes = 0;
  harness.setRoot(SelectCommitProbeRoot{
      .selected = selected,
      .changeCount = &changes,
  });

  harness.keyDown(flux::keys::Tab);
  harness.keyDown(flux::keys::Return);
  REQUIRE(harness.window.overlayManager().top() != nullptr);

  harness.keyDown(flux::keys::DownArrow);
  CHECK(selected.get() == -1);

  harness.keyDown(flux::keys::Return);

  CHECK(harness.window.overlayManager().top() == nullptr);
  CHECK(selected.get() == 1);
  CHECK(changes == 1);
}

TEST_CASE("select option Tab commits closes popover and advances focus past trigger") {
  RuntimeHarness harness;
  flux::Reactive::Signal<int> selected{-1};
  flux::Reactive::Signal<bool> nextFocus;
  int changes = 0;
  harness.setRoot(SelectCommitProbeRoot{
      .selected = selected,
      .nextFocus = &nextFocus,
      .changeCount = &changes,
  });

  harness.keyDown(flux::keys::Tab);
  harness.keyDown(flux::keys::Return);
  REQUIRE(harness.window.overlayManager().top() != nullptr);

  harness.keyDown(flux::keys::DownArrow);
  CHECK(selected.get() == -1);
  CHECK_FALSE(nextFocus.get());

  harness.keyDown(flux::keys::Tab);

  CHECK(harness.window.overlayManager().top() == nullptr);
  CHECK(selected.get() == 1);
  CHECK(changes == 1);
  CHECK(nextFocus.get());
}

TEST_CASE("select popover scroll moves menu content") {
  RuntimeHarness harness;
  harness.setRoot(LongSelectProbeRoot{});

  harness.pointerDown({20.f, 20.f});
  harness.pointerUp({20.f, 20.f});
  flux::OverlayEntry const* entry = harness.window.overlayManager().top();
  REQUIRE(entry != nullptr);
  harness.window.overlayManager().rebuild(harness.window.getSize(), harness.runtime);

  flux::scenegraph::SceneNode const* viewport = findScrollViewport(entry->sceneGraph.root());
  REQUIRE(viewport != nullptr);
  REQUIRE(viewport->children().size() >= 1);
  flux::scenegraph::SceneNode const* content = viewport->children()[0].get();
  REQUIRE(content != nullptr);
  CHECK(content->position().y == doctest::Approx(0.f));

  harness.scroll({40.f, 70.f}, {0.f, -48.f});
  entry = harness.window.overlayManager().top();
  REQUIRE(entry != nullptr);
  viewport = findScrollViewport(entry->sceneGraph.root());
  REQUIRE(viewport != nullptr);
  REQUIRE(viewport->children().size() >= 1);
  content = viewport->children()[0].get();
  REQUIRE(content != nullptr);
  CHECK(content->position().y < 0.f);

  harness.window.overlayManager().rebuild(harness.window.getSize(), harness.runtime);

  entry = harness.window.overlayManager().top();
  REQUIRE(entry != nullptr);
  viewport = findScrollViewport(entry->sceneGraph.root());
  REQUIRE(viewport != nullptr);
  REQUIRE(viewport->children().size() >= 1);
  content = viewport->children()[0].get();
  REQUIRE(content != nullptr);
  CHECK(content->position().y < 0.f);
}

TEST_CASE("select mouse hover drives stable active option highlight") {
  RuntimeHarness harness;
  flux::Theme theme = flux::Theme::light();
  theme.durationFast = 0.f;
  harness.window.setTheme(theme);
  harness.setRoot(LongSelectProbeRoot{});

  harness.pointerDown({20.f, 20.f});
  harness.pointerUp({20.f, 20.f});
  flux::OverlayEntry const* entry = harness.window.overlayManager().top();
  REQUIRE(entry != nullptr);
  harness.window.overlayManager().rebuild(harness.window.getSize(), harness.runtime);

  std::vector<flux::scenegraph::RectNode const*> rows;
  collectTapRects(entry->sceneGraph.root(), rows);
  REQUIRE(rows.size() >= 2);
  flux::Color idleFill = solidFillColor(*rows[1]);
  CHECK(idleFill.r == doctest::Approx(theme.rowHoverBackgroundColor.r));
  CHECK(idleFill.g == doctest::Approx(theme.rowHoverBackgroundColor.g));
  CHECK(idleFill.b == doctest::Approx(theme.rowHoverBackgroundColor.b));
  CHECK(idleFill.a == doctest::Approx(0.f));

  harness.pointerMove(windowPointInside(*entry, *rows[1]));
  harness.window.overlayManager().rebuild(harness.window.getSize(), harness.runtime);

  entry = harness.window.overlayManager().top();
  REQUIRE(entry != nullptr);
  rows.clear();
  collectTapRects(entry->sceneGraph.root(), rows);
  REQUIRE(rows.size() >= 2);

  CHECK(solidFillAlpha(*rows[0]) == doctest::Approx(0.f));
  CHECK(solidFillAlpha(*rows[1]) > 0.f);

  harness.pointerMove({230.f, 130.f});
  harness.window.overlayManager().rebuild(harness.window.getSize(), harness.runtime);

  entry = harness.window.overlayManager().top();
  REQUIRE(entry != nullptr);
  rows.clear();
  collectTapRects(entry->sceneGraph.root(), rows);
  REQUIRE(rows.size() >= 2);
  CHECK(solidFillAlpha(*rows[1]) == doctest::Approx(0.f));
}

TEST_CASE("overlay rebuild relayouts mounted content without remounting state") {
  RuntimeHarness harness;
  int bodyCalls = 0;
  int cleanups = 0;
  flux::Reactive::Signal<int> state;

  flux::OverlayId const id = harness.window.overlayManager().push(
      StatefulOverlayProbe{.bodyCalls = &bodyCalls, .cleanups = &cleanups, .state = &state},
      flux::OverlayConfig{}, &harness.runtime);

  REQUIRE(id.isValid());
  REQUIRE(harness.window.overlayManager().find(id) != nullptr);
  CHECK(bodyCalls == 1);
  CHECK(cleanups == 0);

  state.set(9);
  harness.window.overlayManager().rebuild({320.f, 180.f}, harness.runtime);

  CHECK(bodyCalls == 1);
  CHECK(cleanups == 0);
  CHECK(state.get() == 9);
}

TEST_CASE("window background follows theme unless overridden") {
  RuntimeHarness harness;
  checkSameColor(solidWindowBackground(harness.window), flux::Theme::light().windowBackgroundColor);

  harness.window.setTheme(flux::Theme::dark());
  checkSameColor(solidWindowBackground(harness.window), flux::Theme::dark().windowBackgroundColor);

  flux::Color const custom = flux::Color::hex(0x123456);
  harness.window.setBackground(flux::WindowBackground::solid(custom));
  harness.window.setTheme(flux::Theme::light());
  checkSameColor(solidWindowBackground(harness.window), custom);
}
