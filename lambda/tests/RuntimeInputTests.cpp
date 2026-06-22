#include <doctest/doctest.h>

#include <Lambda/UI/Application.hpp>
#include <Lambda/UI/EventQueue.hpp>
#include <Lambda/UI/Events.hpp>
#include <Lambda/UI/KeyCodes.hpp>
#include <Lambda/UI/Shortcut.hpp>
#include <Lambda/UI/Window.hpp>
#include <Lambda/UI/Detail/RootHolder.hpp>
#include <Lambda/UI/Detail/Runtime.hpp>
#include <Lambda/Reactive/Signal.hpp>
#include <Lambda/UI/InteractionData.hpp>
#include <Lambda/SceneGraph/RectNode.hpp>
#include <Lambda/SceneGraph/SceneInteraction.hpp>
#include <Lambda/UI/Hooks.hpp>
#include <Lambda/UI/Overlay.hpp>
#include <Lambda/UI/Views/Button.hpp>
#include <Lambda/UI/Views/For.hpp>
#include <Lambda/UI/Views/HStack.hpp>
#include <Lambda/UI/Views/Popover.hpp>
#include <Lambda/UI/Views/PopoverCalloutShape.hpp>
#include <Lambda/UI/Views/Rectangle.hpp>
#include <Lambda/UI/Views/ScrollView.hpp>
#include <Lambda/UI/Views/Select.hpp>
#include <Lambda/UI/Views/Show.hpp>
#include <Lambda/UI/Views/TextInput.hpp>
#include <Lambda/UI/Views/Tooltip.hpp>
#include <Lambda/UI/Views/VStack.hpp>

#include <cstdlib>
#include <cstdint>
#include <memory>
#include <string>
#include <functional>
#include <utility>
#include <vector>

namespace {

struct ScopedEnvOverride {
  char const* name = nullptr;
  std::string previous;
  bool hadPrevious = false;

  ScopedEnvOverride(char const* envName, char const* value)
      : name(envName) {
    if (char const* current = std::getenv(name)) {
      previous = current;
      hadPrevious = true;
    }
    setenv(name, value, 1);
  }

  ~ScopedEnvOverride() {
    if (hadPrevious) {
      setenv(name, previous.c_str(), 1);
    } else {
      unsetenv(name);
    }
  }
};

struct RuntimeHarness {
  ScopedEnvOverride disableNativePopovers;
  lambda::Application app;
  lambda::Window& window;
  lambda::Runtime runtime;

  RuntimeHarness()
      : disableNativePopovers("LAMBDA_DISABLE_NATIVE_POPOVERS", "1")
      , app()
      , window(app.createWindow(lambda::WindowConfig{
            .size = {240.f, 140.f},
            .title = "Lambda Runtime Input Tests",
            .fullscreen = false,
            .resizable = false,
        }))
      , runtime(window) {}

  ~RuntimeHarness() {
    runtime.beginShutdown();
  }

  template<typename Root>
  void setRoot(Root root) {
    runtime.setRoot(std::make_unique<lambda::TypedRootHolder<Root>>(
        std::in_place, std::move(root)));
  }

  void pointerMove(lambda::Point point) {
    dispatchPointer(lambda::InputEvent::Kind::PointerMove, point);
  }

  void pointerEnter(lambda::Point point) {
    dispatchPointer(lambda::InputEvent::Kind::PointerEnter, point, lambda::Modifiers::None, 0u);
  }

  void pointerLeave(lambda::Point point = {}) {
    dispatchPointer(lambda::InputEvent::Kind::PointerLeave, point, lambda::Modifiers::None, 0u);
  }

  void pointerDown(lambda::Point point, lambda::Modifiers modifiers = lambda::Modifiers::None) {
    dispatchPointer(lambda::InputEvent::Kind::PointerDown, point, modifiers);
  }

  void pointerUp(lambda::Point point, lambda::Modifiers modifiers = lambda::Modifiers::None) {
    dispatchPointer(lambda::InputEvent::Kind::PointerUp, point, modifiers);
  }

  void keyDown(lambda::KeyCode key, lambda::Modifiers modifiers = lambda::Modifiers::None) {
    lambda::InputEvent event{};
    event.kind = lambda::InputEvent::Kind::KeyDown;
    event.handle = window.handle();
    event.key = key;
    event.modifiers = modifiers;
    runtime.handleInput(event);
  }

  void textInput(std::string text) {
    lambda::InputEvent event{};
    event.kind = lambda::InputEvent::Kind::TextInput;
    event.handle = window.handle();
    event.text = std::move(text);
    runtime.handleInput(event);
  }

  void windowEvent(lambda::WindowEvent::Kind kind) {
    lambda::WindowEvent event{};
    event.kind = kind;
    event.handle = window.handle();
    runtime.handleWindowEvent(event);
  }

  void resize(lambda::Size size) {
    lambda::WindowEvent event{};
    event.kind = lambda::WindowEvent::Kind::Resize;
    event.handle = window.handle();
    event.size = size;
    runtime.handleWindowEvent(event);
  }

  void scroll(lambda::Point point, lambda::Vec2 delta, bool precise = true) {
    lambda::InputEvent event{};
    event.kind = lambda::InputEvent::Kind::Scroll;
    event.handle = window.handle();
    event.position = {point.x, point.y};
    event.scrollDelta = delta;
    event.preciseScrollDelta = precise;
    runtime.handleInput(event);
  }

private:
  void dispatchPointer(lambda::InputEvent::Kind kind, lambda::Point point,
                       lambda::Modifiers modifiers = lambda::Modifiers::None,
                       std::uint8_t pressedButtons = 1u) {
    lambda::InputEvent event{};
    event.kind = kind;
    event.handle = window.handle();
    event.position = {point.x, point.y};
    event.button = pressedButtons == 0u ? lambda::MouseButton::None : lambda::MouseButton::Left;
    event.modifiers = modifiers;
    event.pressedButtons =
        kind == lambda::InputEvent::Kind::PointerUp ? 0u : pressedButtons;
    runtime.handleInput(event);
  }
};

struct ProbeView {
  lambda::Reactive::Signal<bool>* hover = nullptr;
  lambda::Reactive::Signal<bool>* press = nullptr;
  lambda::Reactive::Signal<bool>* focus = nullptr;
  lambda::Reactive::Signal<bool>* keyboardFocus = nullptr;
  int* pointerEnterCount = nullptr;
  int* pointerExitCount = nullptr;
  lambda::Cursor cursor = lambda::Cursor::Inherit;

  lambda::Element body() const {
    if (hover) {
      *hover = lambda::useHover();
    }
    if (press) {
      *press = lambda::usePress();
    }
    if (focus) {
      *focus = lambda::useFocus();
    }
    if (keyboardFocus) {
      *keyboardFocus = lambda::useKeyboardFocus();
    }
    auto element = lambda::Element{lambda::Rectangle{}}
        .size(20.f, 20.f)
        .focusable(true)
        .onTap([] {});
    if (pointerEnterCount) {
      element = std::move(element).onPointerEnter([pointerEnterCount = pointerEnterCount] {
        ++*pointerEnterCount;
      });
    }
    if (pointerExitCount) {
      element = std::move(element).onPointerExit([pointerExitCount = pointerExitCount] {
        ++*pointerExitCount;
      });
    }
    if (cursor != lambda::Cursor::Inherit) {
      element = std::move(element).cursor(cursor);
    }
    return element;
  }
};

struct SingleProbeRoot {
  lambda::Reactive::Signal<bool>* hover = nullptr;
  lambda::Reactive::Signal<bool>* press = nullptr;
  lambda::Reactive::Signal<bool>* focus = nullptr;
  lambda::Reactive::Signal<bool>* keyboardFocus = nullptr;
  int* pointerEnterCount = nullptr;
  int* pointerExitCount = nullptr;
  lambda::Cursor cursor = lambda::Cursor::Inherit;

  lambda::Element body() const {
    return ProbeView{hover, press, focus, keyboardFocus, pointerEnterCount, pointerExitCount, cursor};
  }
};

struct CapturedMoveUnmountRoot {
  lambda::Reactive::Signal<bool>* visible = nullptr;
  int* moveCount = nullptr;

  lambda::Element body() const {
    return lambda::Show(
        *visible,
        [visible = visible, moveCount = moveCount] {
          return lambda::Element{lambda::Rectangle{}}
              .size(40.f, 40.f)
              .onPointerMove([visible, moveCount](lambda::Point) {
                ++*moveCount;
                visible->set(false);
              });
        });
  }
};

struct TextInputFocusRoot {
  lambda::Reactive::Signal<std::string>* first = nullptr;
  lambda::Reactive::Signal<std::string>* second = nullptr;

  lambda::Element body() const {
    return lambda::VStack{
        .spacing = 8.f,
        .children = lambda::children(
            lambda::TextInput{
                .value = *first,
                .placeholder = "First",
            },
            lambda::TextInput{
                .value = *second,
                .placeholder = "Second",
            }),
    };
  }
};

struct ControlledTextInputRoot {
  lambda::Reactive::Signal<std::string>* text = nullptr;
  lambda::Reactive::Signal<lambda::detail::TextEditSelection>* selection = nullptr;
  int* editCount = nullptr;
  bool multiline = false;
  std::function<bool(lambda::KeyCode, lambda::Modifiers)> onPreviewKeyDown;
  std::function<bool(std::string const&)> onPreviewCommand;

  lambda::Element body() const {
    return lambda::TextInput{
        .value = *text,
        .selection = *selection,
        .placeholder = "Controlled",
        .multiline = multiline,
        .onEdit = [editCount = editCount](
                      std::string const&, lambda::detail::TextEditSelection) {
          ++*editCount;
        },
        .onPreviewKeyDown = onPreviewKeyDown,
        .onPreviewCommand = onPreviewCommand,
    };
  }
};

struct StatefulOverlayProbe {
  std::shared_ptr<int> bodyCalls;
  std::shared_ptr<int> cleanups;
  lambda::Reactive::Signal<int>* state = nullptr;

  lambda::Element body() const {
    ++*bodyCalls;
    auto localState = lambda::useState(1);
    *state = localState;
    lambda::Reactive::onCleanup([cleanups = cleanups] {
      ++*cleanups;
    });
    return lambda::Element{lambda::Rectangle{}}
        .size([localState] {
          return 20.f + static_cast<float>(localState.get());
        }, 12.f);
  }
};

struct TwoProbeRoot {
  lambda::Reactive::Signal<bool>* firstFocus = nullptr;
  lambda::Reactive::Signal<bool>* secondFocus = nullptr;

  lambda::Element body() const {
    return lambda::HStack{
        .spacing = 8.f,
        .children = lambda::children(
            ProbeView{nullptr, nullptr, firstFocus, nullptr},
            ProbeView{nullptr, nullptr, secondFocus, nullptr}),
    };
  }
};

struct ScopedAutoFocusProbe {
  lambda::Reactive::Signal<int> focusGeneration;
  lambda::Reactive::Signal<bool>* firstFocus = nullptr;
  lambda::Reactive::Signal<bool>* secondFocus = nullptr;

  lambda::Element body() const {
    lambda::useAutoFocus(focusGeneration);
    return lambda::HStack{
        .spacing = 8.f,
        .children = lambda::children(
            ProbeView{nullptr, nullptr, firstFocus, nullptr},
            ProbeView{nullptr, nullptr, secondFocus, nullptr}),
    };
  }
};

struct AutoFocusScopeRoot {
  lambda::Reactive::Signal<int> focusGeneration;
  lambda::Reactive::Signal<bool>* outsideFocus = nullptr;
  lambda::Reactive::Signal<bool>* firstInsideFocus = nullptr;
  lambda::Reactive::Signal<bool>* secondInsideFocus = nullptr;

  lambda::Element body() const {
    return lambda::HStack{
        .spacing = 8.f,
        .children = lambda::children(
            ProbeView{nullptr, nullptr, outsideFocus, nullptr},
            ScopedAutoFocusProbe{
                .focusGeneration = focusGeneration,
                .firstFocus = firstInsideFocus,
                .secondFocus = secondInsideFocus,
            }),
    };
  }
};

struct ActionProbeView {
  int* fired = nullptr;

  lambda::Element body() const {
    lambda::useFocus();
    lambda::useViewAction("demo.save", [fired = fired] {
      ++*fired;
    });
    return lambda::Element{lambda::Rectangle{}}
        .size(20.f, 20.f)
        .focusable(true)
        .onTap([] {});
  }
};

struct TwoActionRoot {
  int* firstFired = nullptr;
  int* secondFired = nullptr;

  lambda::Element body() const {
    return lambda::HStack{
        .spacing = 8.f,
        .children = lambda::children(
            ActionProbeView{firstFired},
            ActionProbeView{secondFired}),
    };
  }
};

struct ConditionalActionRoot {
  lambda::Reactive::Signal<bool> visible;
  int* fired = nullptr;

  lambda::Element body() const {
    return lambda::Show(visible, [fired = fired] {
      return lambda::Element{ActionProbeView{fired}};
    });
  }
};

struct AncestorActionShowRoot {
  lambda::Reactive::Signal<bool> visible;
  int* fired = nullptr;

  lambda::Element body() const {
    lambda::useViewAction("demo.save", [fired = fired] {
      ++*fired;
    });
    return lambda::Show(visible, [] {
      return lambda::Element{ProbeView{}};
    });
  }
};

struct AncestorActionForRoot {
  lambda::Reactive::Signal<std::vector<int>> items;
  int* fired = nullptr;

  lambda::Element body() const {
    lambda::useViewAction("demo.save", [fired = fired] {
      ++*fired;
    });
    return lambda::For(
        items,
        [](int value) {
          return value;
        },
        [](int, lambda::Reactive::Signal<std::size_t>) {
          return lambda::Element{ProbeView{}};
        });
  }
};

struct ConditionalHoverRoot {
  lambda::Reactive::Signal<bool> visible;
  lambda::Reactive::Signal<bool>* hover = nullptr;

  lambda::Element body() const {
    return lambda::Show(visible, [hover = hover] {
      return lambda::Element{ProbeView{hover, nullptr, nullptr, nullptr}};
    });
  }
};

struct ScrollProbeRoot {
  lambda::Reactive::Signal<lambda::Point> offset;

  lambda::Element body() const {
    return lambda::ScrollView{
        .axis = lambda::ScrollAxis::Vertical,
        .scrollOffset = offset,
        .children = lambda::children(
            lambda::Rectangle{}.size(100.f, 100.f),
            lambda::Rectangle{}.size(100.f, 100.f)),
    };
  }
};

struct ScrollAnchoredProbeRoot {
  lambda::Reactive::Signal<lambda::Point> offset;

  lambda::Element body() const {
    return lambda::ScrollView{
        .axis = lambda::ScrollAxis::Vertical,
        .scrollOffset = offset,
        .children = lambda::children(
            lambda::Rectangle{}.size(100.f, 80.f),
            ProbeView{},
            lambda::Rectangle{}.size(100.f, 100.f)),
    };
  }
};

struct SelectKeyboardProbeRoot {
  lambda::Element body() const {
    return lambda::Element{lambda::Select{
        .options = {
            lambda::SelectOption{.label = "First"},
            lambda::SelectOption{.label = "Second"},
        },
    }}.width(120.f);
  }
};

struct SelectCommitProbeRoot {
  lambda::Reactive::Signal<int> selected;
  lambda::Reactive::Signal<bool>* nextFocus = nullptr;
  int* changeCount = nullptr;

  lambda::Element body() const {
    return lambda::VStack{
        .spacing = 8.f,
        .alignment = lambda::Alignment::Stretch,
        .children = lambda::children(
            lambda::Element{lambda::Select{
                .selectedIndex = selected,
                .options = {
                    lambda::SelectOption{.label = "First"},
                    lambda::SelectOption{.label = "Second"},
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
  lambda::Element body() const {
    return lambda::Element{lambda::Select{
        .options = {
            lambda::SelectOption{.label = "Option 1"},
            lambda::SelectOption{.label = "Option 2"},
            lambda::SelectOption{.label = "Option 3"},
            lambda::SelectOption{.label = "Option 4"},
            lambda::SelectOption{.label = "Option 5"},
            lambda::SelectOption{.label = "Option 6"},
            lambda::SelectOption{.label = "Option 7"},
            lambda::SelectOption{.label = "Option 8"},
            lambda::SelectOption{.label = "Option 9"},
            lambda::SelectOption{.label = "Option 10"},
            lambda::SelectOption{.label = "Option 11"},
            lambda::SelectOption{.label = "Option 12"},
        },
    }}.width(160.f);
  }
};

struct HoverPopoverProbeRoot {
  lambda::Element body() const {
    auto [showPopover, hidePopover, presented] = lambda::usePopover();
    (void)presented;
    return lambda::Element{lambda::Rectangle{}}
        .size(20.f, 20.f)
        .onPointerEnter([showPopover] {
          showPopover(lambda::Popover{
              .content = lambda::Element{lambda::Rectangle{}}.size(30.f, 10.f),
              .placement = lambda::PopoverPlacement::Below,
              .gap = 0.f,
              .arrow = false,
              .contentPadding = 0.f,
              .backdropColor = lambda::Colors::transparent,
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

struct NestedButtonTooltipProbeRoot {
  lambda::Element body() const {
    lambda::useTooltip(lambda::TooltipConfig{
        .text = "Nested button tooltip",
        .placement = lambda::PopoverPlacement::Below,
        .delayMs = 1,
    });
    return lambda::Element{lambda::Button{
        .label = "Hover",
        .variant = lambda::ButtonVariant::Secondary,
    }}.size(80.f, 36.f);
  }
};

struct WrappedScrollProbeRoot {
  lambda::Reactive::Signal<lambda::Point> offset;
  lambda::Reactive::Signal<lambda::Size> viewport;
  lambda::Reactive::Signal<lambda::Size> content;

  lambda::Element body() const {
    return lambda::VStack{
        .spacing = 0.f,
        .alignment = lambda::Alignment::Stretch,
        .children = lambda::children(
            lambda::Rectangle{}.height(20.f),
            lambda::ScrollView{
                .axis = lambda::ScrollAxis::Vertical,
                .scrollOffset = offset,
                .viewportSize = viewport,
                .contentSize = content,
                .children = lambda::children(
                    lambda::Rectangle{}.size(100.f, 100.f),
                    lambda::Rectangle{}.size(100.f, 100.f)),
            }
                .flex(1.f, 1.f, 0.f)
                .fill(lambda::Color::windowBackground())),
    };
  }
};

void checkSameColor(lambda::Color actual, lambda::Color expected) {
  CHECK(actual.r == doctest::Approx(expected.r));
  CHECK(actual.g == doctest::Approx(expected.g));
  CHECK(actual.b == doctest::Approx(expected.b));
  CHECK(actual.a == doctest::Approx(expected.a));
}

lambda::Color solidWindowBackground(lambda::Window const& window) {
  lambda::Color color{};
  REQUIRE(window.background().kind == lambda::WindowBackgroundKind::Fill);
  REQUIRE(window.background().fill.solidColor(&color));
  return color;
}

void registerSaveAction(lambda::Window& window) {
  window.registerAction("demo.save", lambda::ActionDescriptor{
      .label = "Save",
      .shortcut = lambda::shortcuts::Save,
  });
}

lambda::scenegraph::SceneNode const* findScrollViewport(lambda::scenegraph::SceneNode const& node) {
  auto const* interaction = lambda::interactionData(node);
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

void collectTapRects(lambda::scenegraph::SceneNode const& node,
                     std::vector<lambda::scenegraph::RectNode const*>& out) {
  auto const* interaction = lambda::interactionData(node);
  if (node.kind() == lambda::scenegraph::SceneNodeKind::Rect &&
      interaction && interaction->onTap) {
    out.push_back(static_cast<lambda::scenegraph::RectNode const*>(&node));
  }
  for (auto const& child : node.children()) {
    if (child) {
      collectTapRects(*child, out);
    }
  }
}

lambda::Point windowPointInside(lambda::OverlayEntry const& entry,
                              lambda::scenegraph::SceneNode const& node) {
  lambda::Point origin{entry.resolvedFrame.x, entry.resolvedFrame.y};
  lambda::scenegraph::SceneNode const* current = &node;
  while (current) {
    origin.x += current->position().x;
    origin.y += current->position().y;
    current = current->parent();
  }
  lambda::Size const size = node.size();
  return lambda::Point{origin.x + std::min(12.f, std::max(1.f, size.width * 0.5f)),
                     origin.y + std::min(12.f, std::max(1.f, size.height * 0.5f))};
}

float solidFillAlpha(lambda::scenegraph::RectNode const& node) {
  lambda::Color color{};
  if (node.fill().solidColor(&color)) {
    return color.a;
  }
  return 0.f;
}

lambda::Color solidFillColor(lambda::scenegraph::RectNode const& node) {
  lambda::Color color{};
  if (node.fill().solidColor(&color)) {
    return color;
  }
  return {};
}

} // namespace

TEST_CASE("pointer move into element flips hover signal") {
  RuntimeHarness harness;
  lambda::Reactive::Signal<bool> hover;
  harness.setRoot(SingleProbeRoot{.hover = &hover});

  harness.pointerMove({10.f, 10.f});
  CHECK(hover.get());

  harness.pointerMove({100.f, 100.f});
  CHECK_FALSE(hover.get());
}

TEST_CASE("pointer move performs one scene hit traversal") {
  RuntimeHarness harness;
  lambda::Reactive::Signal<bool> hover;
  harness.setRoot(SingleProbeRoot{
      .hover = &hover,
      .cursor = lambda::Cursor::Hand,
  });

  lambda::scenegraph::detail::resetHitTestTraversalCountForTesting();

  harness.pointerMove({10.f, 10.f});

  CHECK(lambda::scenegraph::detail::hitTestTraversalCountForTesting() == 1);
  CHECK(hover.get());
}

TEST_CASE("pointer leave clears hover without blurring keyboard focus") {
  RuntimeHarness harness;
  lambda::Reactive::Signal<bool> hover;
  lambda::Reactive::Signal<bool> focus;
  lambda::Reactive::Signal<bool> keyboardFocus;
  int pointerExitCount = 0;
  harness.setRoot(SingleProbeRoot{
      .hover = &hover,
      .focus = &focus,
      .keyboardFocus = &keyboardFocus,
      .pointerExitCount = &pointerExitCount,
      .cursor = lambda::Cursor::Hand,
  });

  REQUIRE(focus.get());
  REQUIRE(keyboardFocus.get());

  harness.pointerMove({10.f, 10.f});
  REQUIRE(hover.get());

  harness.pointerLeave();

  CHECK_FALSE(hover.get());
  CHECK(pointerExitCount == 1);
  CHECK(focus.get());
  CHECK(keyboardFocus.get());
}

TEST_CASE("pointer down and up flip press signal") {
  RuntimeHarness harness;
  lambda::Reactive::Signal<bool> press;
  harness.setRoot(SingleProbeRoot{.press = &press});

  harness.pointerDown({10.f, 10.f});
  CHECK(press.get());

  harness.pointerUp({10.f, 10.f});
  CHECK_FALSE(press.get());
}

TEST_CASE("pointer down then drag out keeps press until release") {
  RuntimeHarness harness;
  lambda::Reactive::Signal<bool> press;
  harness.setRoot(SingleProbeRoot{.press = &press});

  harness.pointerDown({10.f, 10.f});
  REQUIRE(press.get());

  harness.pointerMove({100.f, 100.f});
  CHECK(press.get());

  harness.pointerUp({100.f, 100.f});
  CHECK_FALSE(press.get());
}

TEST_CASE("captured pointer move survives handler unmounting its target") {
  RuntimeHarness harness;
  lambda::Reactive::Signal<bool> visible{true};
  int moveCount = 0;
  harness.setRoot(CapturedMoveUnmountRoot{&visible, &moveCount});

  harness.pointerDown({5.f, 5.f});
  harness.pointerMove({16.f, 5.f});

  CHECK(moveCount == 1);
  CHECK_FALSE(visible.get());
}

TEST_CASE("focus moves between elements") {
  RuntimeHarness harness;
  lambda::Reactive::Signal<bool> firstFocus;
  lambda::Reactive::Signal<bool> secondFocus;
  harness.setRoot(TwoProbeRoot{.firstFocus = &firstFocus, .secondFocus = &secondFocus});

  harness.keyDown(lambda::keys::Tab);
  CHECK(firstFocus.get());
  CHECK_FALSE(secondFocus.get());

  harness.keyDown(lambda::keys::Tab);
  CHECK_FALSE(firstFocus.get());
  CHECK(secondFocus.get());
}

TEST_CASE("keyboard focus signal differs from pointer focus") {
  RuntimeHarness harness;
  lambda::Reactive::Signal<bool> focus;
  lambda::Reactive::Signal<bool> keyboardFocus;
  harness.setRoot(SingleProbeRoot{.focus = &focus, .keyboardFocus = &keyboardFocus});

  harness.pointerDown({10.f, 10.f});
  CHECK(focus.get());
  CHECK_FALSE(keyboardFocus.get());

  harness.pointerDown({100.f, 100.f});
  REQUIRE_FALSE(focus.get());

  harness.keyDown(lambda::keys::Tab);
  CHECK(focus.get());
  CHECK(keyboardFocus.get());
}

TEST_CASE("root mount selects the only focusable target") {
  RuntimeHarness harness;
  lambda::Reactive::Signal<bool> focus;
  lambda::Reactive::Signal<bool> keyboardFocus;
  harness.setRoot(SingleProbeRoot{.focus = &focus, .keyboardFocus = &keyboardFocus});

  CHECK(focus.get());
  CHECK(keyboardFocus.get());
}

TEST_CASE("window focus reselects the only focusable target") {
  RuntimeHarness harness;
  lambda::Reactive::Signal<bool> focus;
  lambda::Reactive::Signal<bool> keyboardFocus;
  harness.setRoot(SingleProbeRoot{.focus = &focus, .keyboardFocus = &keyboardFocus});

  harness.windowEvent(lambda::WindowEvent::Kind::FocusLost);
  REQUIRE_FALSE(focus.get());
  REQUIRE_FALSE(keyboardFocus.get());

  harness.windowEvent(lambda::WindowEvent::Kind::FocusGained);

  CHECK(focus.get());
  CHECK(keyboardFocus.get());
}

TEST_CASE("root mount does not select among multiple focusable targets") {
  RuntimeHarness harness;
  lambda::Reactive::Signal<bool> firstFocus;
  lambda::Reactive::Signal<bool> secondFocus;
  harness.setRoot(TwoProbeRoot{.firstFocus = &firstFocus, .secondFocus = &secondFocus});

  CHECK_FALSE(firstFocus.get());
  CHECK_FALSE(secondFocus.get());
}

TEST_CASE("auto focus requests first focusable target inside hook scope") {
  RuntimeHarness harness;
  lambda::Reactive::Signal<int> focusGeneration{0};
  lambda::Reactive::Signal<bool> outsideFocus;
  lambda::Reactive::Signal<bool> firstInsideFocus;
  lambda::Reactive::Signal<bool> secondInsideFocus;
  harness.setRoot(AutoFocusScopeRoot{
      .focusGeneration = focusGeneration,
      .outsideFocus = &outsideFocus,
      .firstInsideFocus = &firstInsideFocus,
      .secondInsideFocus = &secondInsideFocus,
  });

  focusGeneration.set(1);
  harness.app.eventQueue().dispatch();

  CHECK_FALSE(outsideFocus.get());
  CHECK(firstInsideFocus.get());
  CHECK_FALSE(secondInsideFocus.get());

  harness.keyDown(lambda::keys::Tab);
  harness.keyDown(lambda::keys::Tab);
  REQUIRE(outsideFocus.get());
  REQUIRE_FALSE(firstInsideFocus.get());

  focusGeneration.set(2);
  harness.app.eventQueue().dispatch();

  CHECK_FALSE(outsideFocus.get());
  CHECK(firstInsideFocus.get());
  CHECK_FALSE(secondInsideFocus.get());
}

TEST_CASE("text input participates in keyboard focus traversal") {
  RuntimeHarness harness;
  lambda::Reactive::Signal<std::string> first{""};
  lambda::Reactive::Signal<std::string> second{""};
  harness.setRoot(TextInputFocusRoot{.first = &first, .second = &second});

  harness.keyDown(lambda::keys::Tab);
  CHECK(harness.runtime.focusTargetKey().has_value());
  harness.textInput("A");

  harness.keyDown(lambda::keys::Tab);
  harness.textInput("B");

  CHECK(first.get() == "A");
  CHECK(second.get() == "B");
}

TEST_CASE("resize preserves focused text input target") {
  RuntimeHarness harness;
  lambda::Reactive::Signal<std::string> first{""};
  lambda::Reactive::Signal<std::string> second{""};
  harness.setRoot(TextInputFocusRoot{.first = &first, .second = &second});

  harness.keyDown(lambda::keys::Tab);
  REQUIRE(harness.runtime.focusTargetKey().has_value());
  harness.textInput("A");

  harness.resize({320.f, 180.f});
  harness.textInput("B");

  CHECK(first.get() == "AB");
  CHECK(second.get().empty());
}

TEST_CASE("controlled text input handles Ctrl+A and reports edit selection") {
  RuntimeHarness harness;
  lambda::Reactive::Signal<std::string> text{"abc"};
  lambda::Reactive::Signal<lambda::detail::TextEditSelection> selection{
      lambda::detail::TextEditSelection{.caretByte = 3, .anchorByte = 3}};
  int editCount = 0;
  harness.setRoot(ControlledTextInputRoot{
      .text = &text,
      .selection = &selection,
      .editCount = &editCount,
  });

  harness.keyDown(lambda::keys::A, lambda::Modifiers::Ctrl);

  CHECK(selection.get().anchorByte == 0);
  CHECK(selection.get().caretByte == 3);
  CHECK(selection.get().hasSelection());

  harness.textInput("x");

  CHECK(text.get() == "x");
  CHECK(selection.get().anchorByte == 1);
  CHECK(selection.get().caretByte == 1);
  CHECK(editCount == 1);
}

TEST_CASE("controlled text input uses Ctrl clipboard shortcuts without Super fallback") {
  RuntimeHarness harness;
  harness.app.clipboard().writeText("");

  lambda::Reactive::Signal<std::string> text{"hello world"};
  lambda::Reactive::Signal<lambda::detail::TextEditSelection> selection{
      lambda::detail::TextEditSelection{.caretByte = 11, .anchorByte = 6}};
  int editCount = 0;
  harness.setRoot(ControlledTextInputRoot{
      .text = &text,
      .selection = &selection,
      .editCount = &editCount,
  });

  harness.keyDown(lambda::keys::C, lambda::Modifiers::Ctrl);
  REQUIRE(harness.app.clipboard().readText().has_value());
  CHECK(*harness.app.clipboard().readText() == "world");
  CHECK(text.get() == "hello world");

  harness.app.clipboard().writeText("");
  harness.keyDown(lambda::keys::C, lambda::Modifiers::Meta);
  CHECK_FALSE(harness.app.clipboard().readText().has_value());

  harness.app.clipboard().writeText("Flux ");
  selection.set(lambda::detail::TextEditSelection{.caretByte = 0, .anchorByte = 0});
  harness.keyDown(lambda::keys::V, lambda::Modifiers::Ctrl);

  CHECK(text.get() == "Flux hello world");
  CHECK(selection.get().caretByte == 5);
  CHECK(selection.get().anchorByte == 5);
  CHECK(editCount == 1);
}

TEST_CASE("controlled text input handles registered semantic edit commands") {
  RuntimeHarness harness;
  harness.window.registerCommand("edit.deleteWordBackward", lambda::CommandDescriptor{
      .title = "Delete Word Backward",
      .shortcut = lambda::Shortcut{lambda::keys::Delete, lambda::Modifiers::Ctrl},
  });
  harness.window.registerCommand("selection.lineStart", lambda::CommandDescriptor{
      .title = "Select to Line Start",
      .shortcut = lambda::Shortcut{lambda::keys::Home, lambda::Modifiers::Shift},
  });

  lambda::Reactive::Signal<std::string> text{"hello world"};
  lambda::Reactive::Signal<lambda::detail::TextEditSelection> selection{
      lambda::detail::TextEditSelection{.caretByte = 11, .anchorByte = 11}};
  int editCount = 0;
  harness.setRoot(ControlledTextInputRoot{
      .text = &text,
      .selection = &selection,
      .editCount = &editCount,
  });

  harness.keyDown(lambda::keys::Tab);
  harness.keyDown(lambda::keys::Delete, lambda::Modifiers::Ctrl);

  CHECK(text.get() == "hello ");
  CHECK(selection.get().caretByte == 6);
  CHECK(selection.get().anchorByte == 6);
  CHECK(editCount == 1);

  harness.keyDown(lambda::keys::Home, lambda::Modifiers::Shift);

  CHECK(selection.get().caretByte == 0);
  CHECK(selection.get().anchorByte == 6);
}

TEST_CASE("controlled text input preview hooks consume raw keys and semantic commands") {
  RuntimeHarness harness;
  harness.window.registerCommand("cursor.right", lambda::CommandDescriptor{
      .title = "Move Cursor Right",
      .shortcut = lambda::Shortcut{lambda::keys::RightArrow, lambda::Modifiers::None},
  });

  lambda::Reactive::Signal<std::string> text{"abc"};
  lambda::Reactive::Signal<lambda::detail::TextEditSelection> selection{
      lambda::detail::TextEditSelection{.caretByte = 0, .anchorByte = 0}};
  int editCount = 0;
  int previewKeyCount = 0;
  int previewCommandCount = 0;
  harness.setRoot(ControlledTextInputRoot{
      .text = &text,
      .selection = &selection,
      .editCount = &editCount,
      .onPreviewKeyDown = [&previewKeyCount](lambda::KeyCode key, lambda::Modifiers) {
        if (key == lambda::keys::PageDown) {
          ++previewKeyCount;
          return true;
        }
        return false;
      },
      .onPreviewCommand = [&previewCommandCount](std::string const& commandId) {
        if (commandId == "cursor.right") {
          ++previewCommandCount;
          return true;
        }
        return false;
      },
  });

  harness.keyDown(lambda::keys::Tab);
  harness.keyDown(lambda::keys::RightArrow);
  harness.keyDown(lambda::keys::PageDown);

  CHECK(previewCommandCount == 1);
  CHECK(previewKeyCount == 1);
  CHECK(selection.get().caretByte == 0);
  CHECK(selection.get().anchorByte == 0);
  CHECK(editCount == 0);
}

TEST_CASE("multiline text input handles registered line edit commands") {
  RuntimeHarness harness;
  harness.window.registerCommand("edit.deleteLine", lambda::CommandDescriptor{
      .title = "Delete Line",
      .shortcut = lambda::Shortcut{lambda::keys::K,
                                   lambda::Modifiers::Ctrl | lambda::Modifiers::Shift},
  });

  lambda::Reactive::Signal<std::string> text{"one\ntwo\nthree"};
  lambda::Reactive::Signal<lambda::detail::TextEditSelection> selection{
      lambda::detail::TextEditSelection{.caretByte = 5, .anchorByte = 5}};
  int editCount = 0;
  harness.setRoot(ControlledTextInputRoot{
      .text = &text,
      .selection = &selection,
      .editCount = &editCount,
      .multiline = true,
  });

  harness.keyDown(lambda::keys::Tab);
  harness.keyDown(lambda::keys::K, lambda::Modifiers::Ctrl | lambda::Modifiers::Shift);

  CHECK(text.get() == "one\nthree");
  CHECK(selection.get().caretByte == 4);
  CHECK(selection.get().anchorByte == 4);
  CHECK(editCount == 1);
}

TEST_CASE("view action fires only for the focused view") {
  RuntimeHarness harness;
  registerSaveAction(harness.window);
  int firstFired = 0;
  int secondFired = 0;
  harness.setRoot(TwoActionRoot{.firstFired = &firstFired, .secondFired = &secondFired});

  harness.keyDown(lambda::keys::Tab);
  harness.keyDown(lambda::keys::S, lambda::Modifiers::Meta);
  CHECK(firstFired == 1);
  CHECK(secondFired == 0);

  harness.keyDown(lambda::keys::Tab);
  harness.keyDown(lambda::keys::S, lambda::Modifiers::Meta);
  CHECK(firstFired == 1);
  CHECK(secondFired == 1);
}

TEST_CASE("view action deregisters on view unmount") {
  RuntimeHarness harness;
  registerSaveAction(harness.window);
  lambda::Reactive::Signal<bool> visible{true};
  int fired = 0;
  harness.setRoot(ConditionalActionRoot{.visible = visible, .fired = &fired});

  harness.pointerDown({10.f, 10.f});
  harness.keyDown(lambda::keys::S, lambda::Modifiers::Meta);
  REQUIRE(fired == 1);

  visible.set(false);
  harness.keyDown(lambda::keys::S, lambda::Modifiers::Meta);
  CHECK(fired == 1);
}

TEST_CASE("view action on ancestor resolves through Show branch remount") {
  RuntimeHarness harness;
  registerSaveAction(harness.window);
  lambda::Reactive::Signal<bool> visible{true};
  int fired = 0;
  harness.setRoot(AncestorActionShowRoot{.visible = visible, .fired = &fired});

  harness.pointerDown({10.f, 10.f});
  harness.keyDown(lambda::keys::S, lambda::Modifiers::Meta);
  REQUIRE(fired == 1);

  visible.set(false);
  harness.app.eventQueue().dispatch();
  visible.set(true);
  harness.app.eventQueue().dispatch();

  harness.pointerDown({10.f, 10.f});
  harness.keyDown(lambda::keys::S, lambda::Modifiers::Meta);
  CHECK(fired == 2);
}

TEST_CASE("view action on ancestor resolves through For row replacement") {
  RuntimeHarness harness;
  registerSaveAction(harness.window);
  lambda::Reactive::Signal<std::vector<int>> items{std::vector<int>{1}};
  int fired = 0;
  harness.setRoot(AncestorActionForRoot{.items = items, .fired = &fired});

  harness.pointerDown({10.f, 10.f});
  harness.keyDown(lambda::keys::S, lambda::Modifiers::Meta);
  REQUIRE(fired == 1);

  items.set(std::vector<int>{2});
  harness.app.eventQueue().dispatch();

  harness.pointerDown({10.f, 10.f});
  harness.keyDown(lambda::keys::S, lambda::Modifiers::Meta);
  CHECK(fired == 2);
}

TEST_CASE("hover chain disposes signals on subtree unmount") {
  RuntimeHarness harness;
  lambda::Reactive::Signal<bool> visible{true};
  lambda::Reactive::Signal<bool> hover;
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
  lambda::Reactive::Signal<lambda::Point> offset{lambda::Point{0.f, 0.f}};
  harness.setRoot(ScrollProbeRoot{.offset = offset});

  harness.scroll({10.f, 10.f}, {0.f, -12.f});

  CHECK(offset.get().x == doctest::Approx(0.f));
  CHECK(offset.get().y == doctest::Approx(12.f));
}

TEST_CASE("scroll view measurement does not overwrite mounted scroll range") {
  RuntimeHarness harness;
  lambda::Reactive::Signal<lambda::Point> offset{lambda::Point{0.f, 0.f}};
  lambda::Reactive::Signal<lambda::Size> viewport{lambda::Size{0.f, 0.f}};
  lambda::Reactive::Signal<lambda::Size> content{lambda::Size{0.f, 0.f}};
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
  std::optional<lambda::Rect> hoverAnchor = harness.runtime.hoverAnchor();
  REQUIRE(hoverAnchor.has_value());
  CHECK(harness.runtime.hoverTargetKey().has_value());
  CHECK(hoverAnchor->x == doctest::Approx(0.f));
  CHECK(hoverAnchor->y == doctest::Approx(0.f));
  CHECK(hoverAnchor->width == doctest::Approx(20.f));
  CHECK(hoverAnchor->height == doctest::Approx(20.f));

  harness.pointerDown({10.f, 10.f});
  std::optional<lambda::Rect> tapAnchor = harness.runtime.lastTapAnchor();
  REQUIRE(tapAnchor.has_value());
  CHECK(harness.runtime.lastTapTargetKey().has_value());
  CHECK(tapAnchor->x == doctest::Approx(0.f));
  CHECK(tapAnchor->y == doctest::Approx(0.f));
  CHECK(tapAnchor->width == doctest::Approx(20.f));
  CHECK(tapAnchor->height == doctest::Approx(20.f));

  harness.keyDown(lambda::keys::Tab);
  std::optional<lambda::Rect> focusAnchor = harness.runtime.focusAnchor();
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

  lambda::OverlayEntry const* entry = harness.window.overlayManager().top();
  REQUIRE(entry != nullptr);
  REQUIRE(entry->config.anchor.has_value());
  CHECK(entry->config.anchor->x == doctest::Approx(0.f));
  CHECK(entry->config.anchor->y == doctest::Approx(0.f));
  CHECK(entry->config.anchor->width == doctest::Approx(20.f));
  CHECK(entry->config.anchor->height == doctest::Approx(20.f));
  CHECK_FALSE(entry->config.anchorTrackComponentKey.has_value());
}

TEST_CASE("tooltips observe hover on nested child components") {
  RuntimeHarness harness;
  harness.setRoot(NestedButtonTooltipProbeRoot{});

  harness.pointerMove({10.f, 10.f});
  CHECK(harness.window.overlayManager().top() == nullptr);

  harness.app.eventQueue().post(lambda::TimerEvent{.timerId = 1});
  harness.app.eventQueue().dispatch();

  lambda::OverlayEntry const* entry = harness.window.overlayManager().top();
  REQUIRE(entry != nullptr);
  CHECK(entry->config.debugName == "tooltip");
  REQUIRE(entry->config.anchor.has_value());
  CHECK(entry->config.anchor->x == doctest::Approx(0.f));
  CHECK(entry->config.anchor->y == doctest::Approx(0.f));
  CHECK(entry->config.anchor->width == doctest::Approx(80.f));
  CHECK(entry->config.anchor->height == doctest::Approx(36.f));

  harness.pointerMove({120.f, 90.f});
  CHECK(harness.window.overlayManager().top() == nullptr);
}

TEST_CASE("tracked overlay anchors follow moved trigger nodes") {
  RuntimeHarness harness;
  lambda::Reactive::Signal<lambda::Point> offset{lambda::Point{0.f, 0.f}};
  harness.setRoot(ScrollAnchoredProbeRoot{.offset = offset});

  harness.pointerDown({10.f, 90.f});
  std::optional<lambda::ComponentKey> key = harness.runtime.lastTapTargetKey();
  REQUIRE(key.has_value());

  lambda::OverlayConfig config{};
  config.anchor = harness.runtime.lastTapAnchor();
  config.anchorTrackComponentKey = key;
  config.placement = lambda::OverlayConfig::Placement::Below;
  lambda::OverlayId const id = harness.window.overlayManager().push(
      lambda::Element{lambda::Rectangle{}}.size(30.f, 10.f),
      std::move(config), &harness.runtime);

  lambda::OverlayEntry const* entry = harness.window.overlayManager().find(id);
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
  lambda::Reactive::Signal<lambda::Point> offset{lambda::Point{0.f, 0.f}};
  harness.setRoot(ScrollAnchoredProbeRoot{.offset = offset});

  harness.pointerDown({10.f, 90.f});
  std::optional<lambda::ComponentKey> key = harness.runtime.lastTapTargetKey();
  REQUIRE(key.has_value());

  lambda::OverlayConfig config{};
  config.anchor = harness.runtime.lastTapAnchor();
  config.anchorTrackComponentKey = key;
  config.placement = lambda::OverlayConfig::Placement::Below;
  config.autoFlipPreferredPlacement = lambda::OverlayConfig::Placement::Below;
  lambda::OverlayId const id = harness.window.overlayManager().push(
      lambda::Element{lambda::Rectangle{}}.size(30.f, 50.f),
      std::move(config), &harness.runtime);

  lambda::OverlayEntry const* entry = harness.window.overlayManager().find(id);
  REQUIRE(entry != nullptr);
  CHECK(entry->config.placement == lambda::OverlayConfig::Placement::Above);
  CHECK(entry->resolvedFrame.y == doctest::Approx(30.f));

  harness.scroll({10.f, 90.f}, {0.f, -60.f});
  harness.window.overlayManager().rebuild(harness.window.getSize(), harness.runtime);

  entry = harness.window.overlayManager().find(id);
  REQUIRE(entry != nullptr);
  REQUIRE(entry->config.anchor.has_value());
  CHECK(entry->config.placement == lambda::OverlayConfig::Placement::Below);
  CHECK(entry->resolvedFrame.y ==
        doctest::Approx(entry->config.anchor->y + entry->config.anchor->height));
}

TEST_CASE("tracked popover callout arrow follows flipped overlay placement") {
  RuntimeHarness harness;
  lambda::Reactive::Signal<lambda::Point> offset{lambda::Point{0.f, 60.f}};
  harness.setRoot(ScrollAnchoredProbeRoot{.offset = offset});

  harness.pointerDown({10.f, 30.f});
  std::optional<lambda::ComponentKey> key = harness.runtime.lastTapTargetKey();
  REQUIRE(key.has_value());

  lambda::OverlayConfig config{};
  config.anchor = harness.runtime.lastTapAnchor();
  config.anchorTrackComponentKey = key;
  config.placement = lambda::OverlayConfig::Placement::Below;
  config.autoFlipPreferredPlacement = lambda::OverlayConfig::Placement::Below;

  lambda::Theme const theme = lambda::Theme::light();
  lambda::OverlayId const id = harness.window.overlayManager().push(
      lambda::Popover{
          .content = lambda::Element{lambda::Rectangle{}}.size(30.f, 10.f),
          .placement = lambda::PopoverPlacement::Below,
          .arrow = true,
      },
      std::move(config), &harness.runtime);

  auto calloutContentY = [&]() {
    lambda::OverlayEntry const* entry = harness.window.overlayManager().find(id);
    REQUIRE(entry != nullptr);
    REQUIRE(entry->sceneGraph.root().children().size() >= 1);
    lambda::scenegraph::SceneNode const* callout =
        entry->sceneGraph.root().children().back().get();
    REQUIRE(callout != nullptr);
    REQUIRE(callout->children().size() == 2);
    return callout->children()[1]->position().y;
  };

  lambda::OverlayEntry const* entry = harness.window.overlayManager().find(id);
  REQUIRE(entry != nullptr);
  CHECK(entry->config.placement == lambda::OverlayConfig::Placement::Below);
  CHECK(calloutContentY() == doctest::Approx(theme.space3 + lambda::PopoverCalloutShape::kArrowH));

  offset = lambda::Point{0.f, 0.f};
  harness.window.overlayManager().rebuild(harness.window.getSize(), harness.runtime);

  entry = harness.window.overlayManager().find(id);
  REQUIRE(entry != nullptr);
  CHECK(entry->config.placement == lambda::OverlayConfig::Placement::Above);
  CHECK(calloutContentY() == doctest::Approx(theme.space3));
}

TEST_CASE("transparent overlay backdrop still dismisses on outside tap") {
  RuntimeHarness harness;
  harness.setRoot(SingleProbeRoot{});

  lambda::OverlayConfig config{};
  config.backdropColor = lambda::Colors::transparent;
  config.dismissOnOutsideTap = true;
  lambda::OverlayId const id = harness.window.overlayManager().push(
      lambda::Element{lambda::Rectangle{}}.size(30.f, 10.f),
      std::move(config), &harness.runtime);

  REQUIRE(harness.window.overlayManager().find(id) != nullptr);

  harness.pointerDown({1.f, 1.f});
  harness.pointerUp({1.f, 1.f});

  CHECK(harness.window.overlayManager().find(id) == nullptr);
}

TEST_CASE("transparent overlay backdrop ignores outside tap when dismissal is disabled") {
  RuntimeHarness harness;
  harness.setRoot(SingleProbeRoot{});

  lambda::OverlayConfig config{};
  config.backdropColor = lambda::Colors::transparent;
  config.dismissOnOutsideTap = false;
  lambda::OverlayId const id = harness.window.overlayManager().push(
      lambda::Element{lambda::Rectangle{}}.size(30.f, 10.f),
      std::move(config), &harness.runtime);

  REQUIRE(harness.window.overlayManager().find(id) != nullptr);

  harness.pointerDown({1.f, 1.f});
  harness.pointerUp({1.f, 1.f});

  CHECK(harness.window.overlayManager().find(id) != nullptr);
}

TEST_CASE("select popover anchors to focused trigger when opened from keyboard") {
  RuntimeHarness harness;
  harness.setRoot(SelectKeyboardProbeRoot{});

  harness.keyDown(lambda::keys::Tab);
  std::optional<lambda::Rect> focusAnchor = harness.runtime.focusAnchor();
  REQUIRE(focusAnchor.has_value());
  std::optional<lambda::ComponentKey> focusKey = harness.runtime.focusTargetKey();
  REQUIRE(focusKey.has_value());

  harness.keyDown(lambda::keys::Return);

  lambda::OverlayEntry const* entry = harness.window.overlayManager().top();
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
  lambda::Reactive::Signal<int> selected{-1};
  int changes = 0;
  harness.setRoot(SelectCommitProbeRoot{
      .selected = selected,
      .changeCount = &changes,
  });

  harness.keyDown(lambda::keys::Tab);
  harness.keyDown(lambda::keys::Return);
  REQUIRE(harness.window.overlayManager().top() != nullptr);

  harness.keyDown(lambda::keys::DownArrow);
  CHECK(selected.get() == -1);

  harness.keyDown(lambda::keys::Return);

  CHECK(harness.window.overlayManager().top() == nullptr);
  CHECK(selected.get() == 1);
  CHECK(changes == 1);
}

TEST_CASE("select option Tab commits closes popover and advances focus past trigger") {
  RuntimeHarness harness;
  lambda::Reactive::Signal<int> selected{-1};
  lambda::Reactive::Signal<bool> nextFocus;
  int changes = 0;
  harness.setRoot(SelectCommitProbeRoot{
      .selected = selected,
      .nextFocus = &nextFocus,
      .changeCount = &changes,
  });

  harness.keyDown(lambda::keys::Tab);
  harness.keyDown(lambda::keys::Return);
  REQUIRE(harness.window.overlayManager().top() != nullptr);

  harness.keyDown(lambda::keys::DownArrow);
  CHECK(selected.get() == -1);
  CHECK_FALSE(nextFocus.get());

  harness.keyDown(lambda::keys::Tab);

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
  lambda::OverlayEntry const* entry = harness.window.overlayManager().top();
  REQUIRE(entry != nullptr);
  harness.window.overlayManager().rebuild(harness.window.getSize(), harness.runtime);

  lambda::scenegraph::SceneNode const* viewport = findScrollViewport(entry->sceneGraph.root());
  REQUIRE(viewport != nullptr);
  REQUIRE(viewport->children().size() >= 1);
  lambda::scenegraph::SceneNode const* content = viewport->children()[0].get();
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
  lambda::Theme theme = lambda::Theme::light();
  theme.durationFast = 0.f;
  harness.window.setTheme(theme);
  harness.setRoot(LongSelectProbeRoot{});

  harness.pointerDown({20.f, 20.f});
  harness.pointerUp({20.f, 20.f});
  lambda::OverlayEntry const* entry = harness.window.overlayManager().top();
  REQUIRE(entry != nullptr);
  harness.window.overlayManager().rebuild(harness.window.getSize(), harness.runtime);

  std::vector<lambda::scenegraph::RectNode const*> rows;
  collectTapRects(entry->sceneGraph.root(), rows);
  REQUIRE(rows.size() >= 2);
  lambda::Color idleFill = solidFillColor(*rows[1]);
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
  auto bodyCalls = std::make_shared<int>(0);
  auto cleanups = std::make_shared<int>(0);
  lambda::Reactive::Signal<int> state;

  lambda::OverlayId const id = harness.window.overlayManager().push(
      StatefulOverlayProbe{.bodyCalls = bodyCalls, .cleanups = cleanups, .state = &state},
      lambda::OverlayConfig{}, &harness.runtime);

  REQUIRE(id.isValid());
  REQUIRE(harness.window.overlayManager().find(id) != nullptr);
  CHECK(*bodyCalls == 1);
  CHECK(*cleanups == 0);

  state.set(9);
  harness.window.overlayManager().rebuild({320.f, 180.f}, harness.runtime);

  CHECK(*bodyCalls == 1);
  CHECK(*cleanups == 0);
  CHECK(state.get() == 9);
}

TEST_CASE("window background follows theme unless overridden") {
  RuntimeHarness harness;
  checkSameColor(solidWindowBackground(harness.window), lambda::Theme::light().windowBackgroundColor);

  harness.window.setTheme(lambda::Theme::dark());
  checkSameColor(solidWindowBackground(harness.window), lambda::Theme::dark().windowBackgroundColor);

  lambda::Color const custom = lambda::Color::hex(0x123456);
  harness.window.setBackground(lambda::WindowBackground::solid(custom));
  harness.window.setTheme(lambda::Theme::light());
  checkSameColor(solidWindowBackground(harness.window), custom);
}
