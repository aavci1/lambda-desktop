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
  lambdaui::Application app;
  lambdaui::Window& window;
  lambdaui::Runtime runtime;

  RuntimeHarness()
      : disableNativePopovers("LAMBDA_DISABLE_NATIVE_POPOVERS", "1")
      , app()
      , window(app.createWindow(lambdaui::WindowConfig{
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
    runtime.setRoot(std::make_unique<lambdaui::TypedRootHolder<Root>>(
        std::in_place, std::move(root)));
  }

  void pointerMove(lambdaui::Point point) {
    dispatchPointer(lambdaui::InputEvent::Kind::PointerMove, point);
  }

  void pointerEnter(lambdaui::Point point) {
    dispatchPointer(lambdaui::InputEvent::Kind::PointerEnter, point, lambdaui::Modifiers::None, 0u);
  }

  void pointerLeave(lambdaui::Point point = {}) {
    dispatchPointer(lambdaui::InputEvent::Kind::PointerLeave, point, lambdaui::Modifiers::None, 0u);
  }

  void pointerDown(lambdaui::Point point, lambdaui::Modifiers modifiers = lambdaui::Modifiers::None) {
    dispatchPointer(lambdaui::InputEvent::Kind::PointerDown, point, modifiers);
  }

  void pointerUp(lambdaui::Point point, lambdaui::Modifiers modifiers = lambdaui::Modifiers::None) {
    dispatchPointer(lambdaui::InputEvent::Kind::PointerUp, point, modifiers);
  }

  void keyDown(lambdaui::KeyCode key, lambdaui::Modifiers modifiers = lambdaui::Modifiers::None) {
    lambdaui::InputEvent event{};
    event.kind = lambdaui::InputEvent::Kind::KeyDown;
    event.handle = window.handle();
    event.key = key;
    event.modifiers = modifiers;
    runtime.handleInput(event);
  }

  void textInput(std::string text) {
    lambdaui::InputEvent event{};
    event.kind = lambdaui::InputEvent::Kind::TextInput;
    event.handle = window.handle();
    event.text = std::move(text);
    runtime.handleInput(event);
  }

  void windowEvent(lambdaui::WindowEvent::Kind kind) {
    lambdaui::WindowEvent event{};
    event.kind = kind;
    event.handle = window.handle();
    runtime.handleWindowEvent(event);
  }

  void resize(lambdaui::Size size) {
    lambdaui::WindowEvent event{};
    event.kind = lambdaui::WindowEvent::Kind::Resize;
    event.handle = window.handle();
    event.size = size;
    runtime.handleWindowEvent(event);
  }

  void scroll(lambdaui::Point point, lambdaui::Vec2 delta, bool precise = true) {
    lambdaui::InputEvent event{};
    event.kind = lambdaui::InputEvent::Kind::Scroll;
    event.handle = window.handle();
    event.position = {point.x, point.y};
    event.scrollDelta = delta;
    event.preciseScrollDelta = precise;
    runtime.handleInput(event);
  }

private:
  void dispatchPointer(lambdaui::InputEvent::Kind kind, lambdaui::Point point,
                       lambdaui::Modifiers modifiers = lambdaui::Modifiers::None,
                       std::uint8_t pressedButtons = 1u) {
    lambdaui::InputEvent event{};
    event.kind = kind;
    event.handle = window.handle();
    event.position = {point.x, point.y};
    event.button = pressedButtons == 0u ? lambdaui::MouseButton::None : lambdaui::MouseButton::Left;
    event.modifiers = modifiers;
    event.pressedButtons =
        kind == lambdaui::InputEvent::Kind::PointerUp ? 0u : pressedButtons;
    runtime.handleInput(event);
  }
};

struct ProbeView {
  lambdaui::Reactive::Signal<bool>* hover = nullptr;
  lambdaui::Reactive::Signal<bool>* press = nullptr;
  lambdaui::Reactive::Signal<bool>* focus = nullptr;
  lambdaui::Reactive::Signal<bool>* keyboardFocus = nullptr;
  int* pointerEnterCount = nullptr;
  int* pointerExitCount = nullptr;
  lambdaui::Cursor cursor = lambdaui::Cursor::Inherit;

  lambdaui::Element body() const {
    if (hover) {
      *hover = lambdaui::useHover();
    }
    if (press) {
      *press = lambdaui::usePress();
    }
    if (focus) {
      *focus = lambdaui::useFocus();
    }
    if (keyboardFocus) {
      *keyboardFocus = lambdaui::useKeyboardFocus();
    }
    auto element = lambdaui::Element{lambdaui::Rectangle{}}
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
    if (cursor != lambdaui::Cursor::Inherit) {
      element = std::move(element).cursor(cursor);
    }
    return element;
  }
};

struct SingleProbeRoot {
  lambdaui::Reactive::Signal<bool>* hover = nullptr;
  lambdaui::Reactive::Signal<bool>* press = nullptr;
  lambdaui::Reactive::Signal<bool>* focus = nullptr;
  lambdaui::Reactive::Signal<bool>* keyboardFocus = nullptr;
  int* pointerEnterCount = nullptr;
  int* pointerExitCount = nullptr;
  lambdaui::Cursor cursor = lambdaui::Cursor::Inherit;

  lambdaui::Element body() const {
    return ProbeView{hover, press, focus, keyboardFocus, pointerEnterCount, pointerExitCount, cursor};
  }
};

struct CapturedMoveUnmountRoot {
  lambdaui::Reactive::Signal<bool>* visible = nullptr;
  int* moveCount = nullptr;

  lambdaui::Element body() const {
    return lambdaui::Show(
        *visible,
        [visible = visible, moveCount = moveCount] {
          return lambdaui::Element{lambdaui::Rectangle{}}
              .size(40.f, 40.f)
              .onPointerMove([visible, moveCount](lambdaui::Point) {
                ++*moveCount;
                visible->set(false);
              });
        });
  }
};

struct TextInputFocusRoot {
  lambdaui::Reactive::Signal<std::string>* first = nullptr;
  lambdaui::Reactive::Signal<std::string>* second = nullptr;

  lambdaui::Element body() const {
    return lambdaui::VStack{
        .spacing = 8.f,
        .children = lambdaui::children(
            lambdaui::TextInput{
                .value = *first,
                .placeholder = "First",
            },
            lambdaui::TextInput{
                .value = *second,
                .placeholder = "Second",
            }),
    };
  }
};

struct ControlledTextInputRoot {
  lambdaui::Reactive::Signal<std::string>* text = nullptr;
  lambdaui::Reactive::Signal<lambdaui::detail::TextEditSelection>* selection = nullptr;
  int* editCount = nullptr;
  bool multiline = false;
  std::function<bool(lambdaui::KeyCode, lambdaui::Modifiers)> onPreviewKeyDown;
  std::function<bool(std::string const&)> onPreviewCommand;

  lambdaui::Element body() const {
    return lambdaui::TextInput{
        .value = *text,
        .selection = *selection,
        .placeholder = "Controlled",
        .multiline = multiline,
        .onEdit = [editCount = editCount](
                      std::string const&, lambdaui::detail::TextEditSelection) {
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
  lambdaui::Reactive::Signal<int>* state = nullptr;

  lambdaui::Element body() const {
    ++*bodyCalls;
    auto localState = lambdaui::useState(1);
    *state = localState;
    lambdaui::Reactive::onCleanup([cleanups = cleanups] {
      ++*cleanups;
    });
    return lambdaui::Element{lambdaui::Rectangle{}}
        .size([localState] {
          return 20.f + static_cast<float>(localState.get());
        }, 12.f);
  }
};

struct TwoProbeRoot {
  lambdaui::Reactive::Signal<bool>* firstFocus = nullptr;
  lambdaui::Reactive::Signal<bool>* secondFocus = nullptr;

  lambdaui::Element body() const {
    return lambdaui::HStack{
        .spacing = 8.f,
        .children = lambdaui::children(
            ProbeView{nullptr, nullptr, firstFocus, nullptr},
            ProbeView{nullptr, nullptr, secondFocus, nullptr}),
    };
  }
};

struct ScopedAutoFocusProbe {
  lambdaui::Reactive::Signal<int> focusGeneration;
  lambdaui::Reactive::Signal<bool>* firstFocus = nullptr;
  lambdaui::Reactive::Signal<bool>* secondFocus = nullptr;

  lambdaui::Element body() const {
    lambdaui::useAutoFocus(focusGeneration);
    return lambdaui::HStack{
        .spacing = 8.f,
        .children = lambdaui::children(
            ProbeView{nullptr, nullptr, firstFocus, nullptr},
            ProbeView{nullptr, nullptr, secondFocus, nullptr}),
    };
  }
};

struct AutoFocusScopeRoot {
  lambdaui::Reactive::Signal<int> focusGeneration;
  lambdaui::Reactive::Signal<bool>* outsideFocus = nullptr;
  lambdaui::Reactive::Signal<bool>* firstInsideFocus = nullptr;
  lambdaui::Reactive::Signal<bool>* secondInsideFocus = nullptr;

  lambdaui::Element body() const {
    return lambdaui::HStack{
        .spacing = 8.f,
        .children = lambdaui::children(
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

  lambdaui::Element body() const {
    lambdaui::useFocus();
    lambdaui::useViewAction("demo.save", [fired = fired] {
      ++*fired;
    });
    return lambdaui::Element{lambdaui::Rectangle{}}
        .size(20.f, 20.f)
        .focusable(true)
        .onTap([] {});
  }
};

struct TwoActionRoot {
  int* firstFired = nullptr;
  int* secondFired = nullptr;

  lambdaui::Element body() const {
    return lambdaui::HStack{
        .spacing = 8.f,
        .children = lambdaui::children(
            ActionProbeView{firstFired},
            ActionProbeView{secondFired}),
    };
  }
};

struct ConditionalActionRoot {
  lambdaui::Reactive::Signal<bool> visible;
  int* fired = nullptr;

  lambdaui::Element body() const {
    return lambdaui::Show(visible, [fired = fired] {
      return lambdaui::Element{ActionProbeView{fired}};
    });
  }
};

struct AncestorActionShowRoot {
  lambdaui::Reactive::Signal<bool> visible;
  int* fired = nullptr;

  lambdaui::Element body() const {
    lambdaui::useViewAction("demo.save", [fired = fired] {
      ++*fired;
    });
    return lambdaui::Show(visible, [] {
      return lambdaui::Element{ProbeView{}};
    });
  }
};

struct AncestorActionForRoot {
  lambdaui::Reactive::Signal<std::vector<int>> items;
  int* fired = nullptr;

  lambdaui::Element body() const {
    lambdaui::useViewAction("demo.save", [fired = fired] {
      ++*fired;
    });
    return lambdaui::For(
        items,
        [](int value) {
          return value;
        },
        [](int, lambdaui::Reactive::Signal<std::size_t>) {
          return lambdaui::Element{ProbeView{}};
        });
  }
};

struct ConditionalHoverRoot {
  lambdaui::Reactive::Signal<bool> visible;
  lambdaui::Reactive::Signal<bool>* hover = nullptr;

  lambdaui::Element body() const {
    return lambdaui::Show(visible, [hover = hover] {
      return lambdaui::Element{ProbeView{hover, nullptr, nullptr, nullptr}};
    });
  }
};

struct ScrollProbeRoot {
  lambdaui::Reactive::Signal<lambdaui::Point> offset;

  lambdaui::Element body() const {
    return lambdaui::ScrollView{
        .axis = lambdaui::ScrollAxis::Vertical,
        .scrollOffset = offset,
        .children = lambdaui::children(
            lambdaui::Rectangle{}.size(100.f, 100.f),
            lambdaui::Rectangle{}.size(100.f, 100.f)),
    };
  }
};

struct ScrollAnchoredProbeRoot {
  lambdaui::Reactive::Signal<lambdaui::Point> offset;

  lambdaui::Element body() const {
    return lambdaui::ScrollView{
        .axis = lambdaui::ScrollAxis::Vertical,
        .scrollOffset = offset,
        .children = lambdaui::children(
            lambdaui::Rectangle{}.size(100.f, 80.f),
            ProbeView{},
            lambdaui::Rectangle{}.size(100.f, 100.f)),
    };
  }
};

struct SelectKeyboardProbeRoot {
  lambdaui::Element body() const {
    return lambdaui::Element{lambdaui::Select{
        .options = {
            lambdaui::SelectOption{.label = "First"},
            lambdaui::SelectOption{.label = "Second"},
        },
    }}.width(120.f);
  }
};

struct SelectCommitProbeRoot {
  lambdaui::Reactive::Signal<int> selected;
  lambdaui::Reactive::Signal<bool>* nextFocus = nullptr;
  int* changeCount = nullptr;

  lambdaui::Element body() const {
    return lambdaui::VStack{
        .spacing = 8.f,
        .alignment = lambdaui::Alignment::Stretch,
        .children = lambdaui::children(
            lambdaui::Element{lambdaui::Select{
                .selectedIndex = selected,
                .options = {
                    lambdaui::SelectOption{.label = "First"},
                    lambdaui::SelectOption{.label = "Second"},
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
  lambdaui::Element body() const {
    return lambdaui::Element{lambdaui::Select{
        .options = {
            lambdaui::SelectOption{.label = "Option 1"},
            lambdaui::SelectOption{.label = "Option 2"},
            lambdaui::SelectOption{.label = "Option 3"},
            lambdaui::SelectOption{.label = "Option 4"},
            lambdaui::SelectOption{.label = "Option 5"},
            lambdaui::SelectOption{.label = "Option 6"},
            lambdaui::SelectOption{.label = "Option 7"},
            lambdaui::SelectOption{.label = "Option 8"},
            lambdaui::SelectOption{.label = "Option 9"},
            lambdaui::SelectOption{.label = "Option 10"},
            lambdaui::SelectOption{.label = "Option 11"},
            lambdaui::SelectOption{.label = "Option 12"},
        },
    }}.width(160.f);
  }
};

struct HoverPopoverProbeRoot {
  lambdaui::Element body() const {
    auto [showPopover, hidePopover, presented] = lambdaui::usePopover();
    (void)presented;
    return lambdaui::Element{lambdaui::Rectangle{}}
        .size(20.f, 20.f)
        .onPointerEnter([showPopover] {
          showPopover(lambdaui::Popover{
              .content = lambdaui::Element{lambdaui::Rectangle{}}.size(30.f, 10.f),
              .placement = lambdaui::PopoverPlacement::Below,
              .gap = 0.f,
              .arrow = false,
              .contentPadding = 0.f,
              .backdropColor = lambdaui::Colors::transparent,
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
  lambdaui::Element body() const {
    lambdaui::useTooltip(lambdaui::TooltipConfig{
        .text = "Nested button tooltip",
        .placement = lambdaui::PopoverPlacement::Below,
        .delayMs = 1,
    });
    return lambdaui::Element{lambdaui::Button{
        .label = "Hover",
        .variant = lambdaui::ButtonVariant::Secondary,
    }}.size(80.f, 36.f);
  }
};

struct WrappedScrollProbeRoot {
  lambdaui::Reactive::Signal<lambdaui::Point> offset;
  lambdaui::Reactive::Signal<lambdaui::Size> viewport;
  lambdaui::Reactive::Signal<lambdaui::Size> content;

  lambdaui::Element body() const {
    return lambdaui::VStack{
        .spacing = 0.f,
        .alignment = lambdaui::Alignment::Stretch,
        .children = lambdaui::children(
            lambdaui::Rectangle{}.height(20.f),
            lambdaui::ScrollView{
                .axis = lambdaui::ScrollAxis::Vertical,
                .scrollOffset = offset,
                .viewportSize = viewport,
                .contentSize = content,
                .children = lambdaui::children(
                    lambdaui::Rectangle{}.size(100.f, 100.f),
                    lambdaui::Rectangle{}.size(100.f, 100.f)),
            }
                .flex(1.f, 1.f, 0.f)
                .fill(lambdaui::Color::windowBackground())),
    };
  }
};

void checkSameColor(lambdaui::Color actual, lambdaui::Color expected) {
  CHECK(actual.r == doctest::Approx(expected.r));
  CHECK(actual.g == doctest::Approx(expected.g));
  CHECK(actual.b == doctest::Approx(expected.b));
  CHECK(actual.a == doctest::Approx(expected.a));
}

lambdaui::Color solidWindowBackground(lambdaui::Window const& window) {
  lambdaui::Color color{};
  REQUIRE(window.background().kind == lambdaui::WindowBackgroundKind::Fill);
  REQUIRE(window.background().fill.solidColor(&color));
  return color;
}

void registerSaveAction(lambdaui::Window& window) {
  window.registerAction("demo.save", lambdaui::ActionDescriptor{
      .label = "Save",
      .shortcut = lambdaui::shortcuts::Save,
  });
}

lambdaui::scenegraph::SceneNode const* findScrollViewport(lambdaui::scenegraph::SceneNode const& node) {
  auto const* interaction = lambdaui::interactionData(node);
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

void collectTapRects(lambdaui::scenegraph::SceneNode const& node,
                     std::vector<lambdaui::scenegraph::RectNode const*>& out) {
  auto const* interaction = lambdaui::interactionData(node);
  if (node.kind() == lambdaui::scenegraph::SceneNodeKind::Rect &&
      interaction && interaction->onTap) {
    out.push_back(static_cast<lambdaui::scenegraph::RectNode const*>(&node));
  }
  for (auto const& child : node.children()) {
    if (child) {
      collectTapRects(*child, out);
    }
  }
}

lambdaui::Point windowPointInside(lambdaui::OverlayEntry const& entry,
                              lambdaui::scenegraph::SceneNode const& node) {
  lambdaui::Point origin{entry.resolvedFrame.x, entry.resolvedFrame.y};
  lambdaui::scenegraph::SceneNode const* current = &node;
  while (current) {
    origin.x += current->position().x;
    origin.y += current->position().y;
    current = current->parent();
  }
  lambdaui::Size const size = node.size();
  return lambdaui::Point{origin.x + std::min(12.f, std::max(1.f, size.width * 0.5f)),
                     origin.y + std::min(12.f, std::max(1.f, size.height * 0.5f))};
}

float solidFillAlpha(lambdaui::scenegraph::RectNode const& node) {
  lambdaui::Color color{};
  if (node.fill().solidColor(&color)) {
    return color.a;
  }
  return 0.f;
}

lambdaui::Color solidFillColor(lambdaui::scenegraph::RectNode const& node) {
  lambdaui::Color color{};
  if (node.fill().solidColor(&color)) {
    return color;
  }
  return {};
}

} // namespace

TEST_CASE("pointer move into element flips hover signal") {
  RuntimeHarness harness;
  lambdaui::Reactive::Signal<bool> hover;
  harness.setRoot(SingleProbeRoot{.hover = &hover});

  harness.pointerMove({10.f, 10.f});
  CHECK(hover.get());

  harness.pointerMove({100.f, 100.f});
  CHECK_FALSE(hover.get());
}

TEST_CASE("pointer move performs one scene hit traversal") {
  RuntimeHarness harness;
  lambdaui::Reactive::Signal<bool> hover;
  harness.setRoot(SingleProbeRoot{
      .hover = &hover,
      .cursor = lambdaui::Cursor::Hand,
  });

  lambdaui::scenegraph::detail::resetHitTestTraversalCountForTesting();

  harness.pointerMove({10.f, 10.f});

  CHECK(lambdaui::scenegraph::detail::hitTestTraversalCountForTesting() == 1);
  CHECK(hover.get());
}

TEST_CASE("pointer leave clears hover without blurring keyboard focus") {
  RuntimeHarness harness;
  lambdaui::Reactive::Signal<bool> hover;
  lambdaui::Reactive::Signal<bool> focus;
  lambdaui::Reactive::Signal<bool> keyboardFocus;
  int pointerExitCount = 0;
  harness.setRoot(SingleProbeRoot{
      .hover = &hover,
      .focus = &focus,
      .keyboardFocus = &keyboardFocus,
      .pointerExitCount = &pointerExitCount,
      .cursor = lambdaui::Cursor::Hand,
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
  lambdaui::Reactive::Signal<bool> press;
  harness.setRoot(SingleProbeRoot{.press = &press});

  harness.pointerDown({10.f, 10.f});
  CHECK(press.get());

  harness.pointerUp({10.f, 10.f});
  CHECK_FALSE(press.get());
}

TEST_CASE("pointer down then drag out keeps press until release") {
  RuntimeHarness harness;
  lambdaui::Reactive::Signal<bool> press;
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
  lambdaui::Reactive::Signal<bool> visible{true};
  int moveCount = 0;
  harness.setRoot(CapturedMoveUnmountRoot{&visible, &moveCount});

  harness.pointerDown({5.f, 5.f});
  harness.pointerMove({16.f, 5.f});

  CHECK(moveCount == 1);
  CHECK_FALSE(visible.get());
}

TEST_CASE("focus moves between elements") {
  RuntimeHarness harness;
  lambdaui::Reactive::Signal<bool> firstFocus;
  lambdaui::Reactive::Signal<bool> secondFocus;
  harness.setRoot(TwoProbeRoot{.firstFocus = &firstFocus, .secondFocus = &secondFocus});

  harness.keyDown(lambdaui::keys::Tab);
  CHECK(firstFocus.get());
  CHECK_FALSE(secondFocus.get());

  harness.keyDown(lambdaui::keys::Tab);
  CHECK_FALSE(firstFocus.get());
  CHECK(secondFocus.get());
}

TEST_CASE("keyboard focus signal differs from pointer focus") {
  RuntimeHarness harness;
  lambdaui::Reactive::Signal<bool> focus;
  lambdaui::Reactive::Signal<bool> keyboardFocus;
  harness.setRoot(SingleProbeRoot{.focus = &focus, .keyboardFocus = &keyboardFocus});

  harness.pointerDown({10.f, 10.f});
  CHECK(focus.get());
  CHECK_FALSE(keyboardFocus.get());

  harness.pointerDown({100.f, 100.f});
  REQUIRE_FALSE(focus.get());

  harness.keyDown(lambdaui::keys::Tab);
  CHECK(focus.get());
  CHECK(keyboardFocus.get());
}

TEST_CASE("root mount selects the only focusable target") {
  RuntimeHarness harness;
  lambdaui::Reactive::Signal<bool> focus;
  lambdaui::Reactive::Signal<bool> keyboardFocus;
  harness.setRoot(SingleProbeRoot{.focus = &focus, .keyboardFocus = &keyboardFocus});

  CHECK(focus.get());
  CHECK(keyboardFocus.get());
}

TEST_CASE("window focus reselects the only focusable target") {
  RuntimeHarness harness;
  lambdaui::Reactive::Signal<bool> focus;
  lambdaui::Reactive::Signal<bool> keyboardFocus;
  harness.setRoot(SingleProbeRoot{.focus = &focus, .keyboardFocus = &keyboardFocus});

  harness.windowEvent(lambdaui::WindowEvent::Kind::FocusLost);
  REQUIRE_FALSE(focus.get());
  REQUIRE_FALSE(keyboardFocus.get());

  harness.windowEvent(lambdaui::WindowEvent::Kind::FocusGained);

  CHECK(focus.get());
  CHECK(keyboardFocus.get());
}

TEST_CASE("root mount does not select among multiple focusable targets") {
  RuntimeHarness harness;
  lambdaui::Reactive::Signal<bool> firstFocus;
  lambdaui::Reactive::Signal<bool> secondFocus;
  harness.setRoot(TwoProbeRoot{.firstFocus = &firstFocus, .secondFocus = &secondFocus});

  CHECK_FALSE(firstFocus.get());
  CHECK_FALSE(secondFocus.get());
}

TEST_CASE("auto focus requests first focusable target inside hook scope") {
  RuntimeHarness harness;
  lambdaui::Reactive::Signal<int> focusGeneration{0};
  lambdaui::Reactive::Signal<bool> outsideFocus;
  lambdaui::Reactive::Signal<bool> firstInsideFocus;
  lambdaui::Reactive::Signal<bool> secondInsideFocus;
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

  harness.keyDown(lambdaui::keys::Tab);
  harness.keyDown(lambdaui::keys::Tab);
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
  lambdaui::Reactive::Signal<std::string> first{""};
  lambdaui::Reactive::Signal<std::string> second{""};
  harness.setRoot(TextInputFocusRoot{.first = &first, .second = &second});

  harness.keyDown(lambdaui::keys::Tab);
  CHECK(harness.runtime.focusTargetKey().has_value());
  harness.textInput("A");

  harness.keyDown(lambdaui::keys::Tab);
  harness.textInput("B");

  CHECK(first.get() == "A");
  CHECK(second.get() == "B");
}

TEST_CASE("resize preserves focused text input target") {
  RuntimeHarness harness;
  lambdaui::Reactive::Signal<std::string> first{""};
  lambdaui::Reactive::Signal<std::string> second{""};
  harness.setRoot(TextInputFocusRoot{.first = &first, .second = &second});

  harness.keyDown(lambdaui::keys::Tab);
  REQUIRE(harness.runtime.focusTargetKey().has_value());
  harness.textInput("A");

  harness.resize({320.f, 180.f});
  harness.textInput("B");

  CHECK(first.get() == "AB");
  CHECK(second.get().empty());
}

TEST_CASE("controlled text input handles Ctrl+A and reports edit selection") {
  RuntimeHarness harness;
  lambdaui::Reactive::Signal<std::string> text{"abc"};
  lambdaui::Reactive::Signal<lambdaui::detail::TextEditSelection> selection{
      lambdaui::detail::TextEditSelection{.caretByte = 3, .anchorByte = 3}};
  int editCount = 0;
  harness.setRoot(ControlledTextInputRoot{
      .text = &text,
      .selection = &selection,
      .editCount = &editCount,
  });

  harness.keyDown(lambdaui::keys::A, lambdaui::Modifiers::Ctrl);

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

  lambdaui::Reactive::Signal<std::string> text{"hello world"};
  lambdaui::Reactive::Signal<lambdaui::detail::TextEditSelection> selection{
      lambdaui::detail::TextEditSelection{.caretByte = 11, .anchorByte = 6}};
  int editCount = 0;
  harness.setRoot(ControlledTextInputRoot{
      .text = &text,
      .selection = &selection,
      .editCount = &editCount,
  });

  harness.keyDown(lambdaui::keys::C, lambdaui::Modifiers::Ctrl);
  REQUIRE(harness.app.clipboard().readText().has_value());
  CHECK(*harness.app.clipboard().readText() == "world");
  CHECK(text.get() == "hello world");

  harness.app.clipboard().writeText("");
  harness.keyDown(lambdaui::keys::C, lambdaui::Modifiers::Meta);
  CHECK_FALSE(harness.app.clipboard().readText().has_value());

  harness.app.clipboard().writeText("Flux ");
  selection.set(lambdaui::detail::TextEditSelection{.caretByte = 0, .anchorByte = 0});
  harness.keyDown(lambdaui::keys::V, lambdaui::Modifiers::Ctrl);

  CHECK(text.get() == "Flux hello world");
  CHECK(selection.get().caretByte == 5);
  CHECK(selection.get().anchorByte == 5);
  CHECK(editCount == 1);
}

TEST_CASE("controlled text input handles registered semantic edit commands") {
  RuntimeHarness harness;
  harness.window.registerCommand("edit.deleteWordBackward", lambdaui::CommandDescriptor{
      .title = "Delete Word Backward",
      .shortcut = lambdaui::Shortcut{lambdaui::keys::Delete, lambdaui::Modifiers::Ctrl},
  });
  harness.window.registerCommand("selection.lineStart", lambdaui::CommandDescriptor{
      .title = "Select to Line Start",
      .shortcut = lambdaui::Shortcut{lambdaui::keys::Home, lambdaui::Modifiers::Shift},
  });

  lambdaui::Reactive::Signal<std::string> text{"hello world"};
  lambdaui::Reactive::Signal<lambdaui::detail::TextEditSelection> selection{
      lambdaui::detail::TextEditSelection{.caretByte = 11, .anchorByte = 11}};
  int editCount = 0;
  harness.setRoot(ControlledTextInputRoot{
      .text = &text,
      .selection = &selection,
      .editCount = &editCount,
  });

  harness.keyDown(lambdaui::keys::Tab);
  harness.keyDown(lambdaui::keys::Delete, lambdaui::Modifiers::Ctrl);

  CHECK(text.get() == "hello ");
  CHECK(selection.get().caretByte == 6);
  CHECK(selection.get().anchorByte == 6);
  CHECK(editCount == 1);

  harness.keyDown(lambdaui::keys::Home, lambdaui::Modifiers::Shift);

  CHECK(selection.get().caretByte == 0);
  CHECK(selection.get().anchorByte == 6);
}

TEST_CASE("controlled text input preview hooks consume raw keys and semantic commands") {
  RuntimeHarness harness;
  harness.window.registerCommand("cursor.right", lambdaui::CommandDescriptor{
      .title = "Move Cursor Right",
      .shortcut = lambdaui::Shortcut{lambdaui::keys::RightArrow, lambdaui::Modifiers::None},
  });

  lambdaui::Reactive::Signal<std::string> text{"abc"};
  lambdaui::Reactive::Signal<lambdaui::detail::TextEditSelection> selection{
      lambdaui::detail::TextEditSelection{.caretByte = 0, .anchorByte = 0}};
  int editCount = 0;
  int previewKeyCount = 0;
  int previewCommandCount = 0;
  harness.setRoot(ControlledTextInputRoot{
      .text = &text,
      .selection = &selection,
      .editCount = &editCount,
      .onPreviewKeyDown = [&previewKeyCount](lambdaui::KeyCode key, lambdaui::Modifiers) {
        if (key == lambdaui::keys::PageDown) {
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

  harness.keyDown(lambdaui::keys::Tab);
  harness.keyDown(lambdaui::keys::RightArrow);
  harness.keyDown(lambdaui::keys::PageDown);

  CHECK(previewCommandCount == 1);
  CHECK(previewKeyCount == 1);
  CHECK(selection.get().caretByte == 0);
  CHECK(selection.get().anchorByte == 0);
  CHECK(editCount == 0);
}

TEST_CASE("multiline text input handles registered line edit commands") {
  RuntimeHarness harness;
  harness.window.registerCommand("edit.deleteLine", lambdaui::CommandDescriptor{
      .title = "Delete Line",
      .shortcut = lambdaui::Shortcut{lambdaui::keys::K,
                                   lambdaui::Modifiers::Ctrl | lambdaui::Modifiers::Shift},
  });

  lambdaui::Reactive::Signal<std::string> text{"one\ntwo\nthree"};
  lambdaui::Reactive::Signal<lambdaui::detail::TextEditSelection> selection{
      lambdaui::detail::TextEditSelection{.caretByte = 5, .anchorByte = 5}};
  int editCount = 0;
  harness.setRoot(ControlledTextInputRoot{
      .text = &text,
      .selection = &selection,
      .editCount = &editCount,
      .multiline = true,
  });

  harness.keyDown(lambdaui::keys::Tab);
  harness.keyDown(lambdaui::keys::K, lambdaui::Modifiers::Ctrl | lambdaui::Modifiers::Shift);

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

  harness.keyDown(lambdaui::keys::Tab);
  harness.keyDown(lambdaui::keys::S, lambdaui::Modifiers::Meta);
  CHECK(firstFired == 1);
  CHECK(secondFired == 0);

  harness.keyDown(lambdaui::keys::Tab);
  harness.keyDown(lambdaui::keys::S, lambdaui::Modifiers::Meta);
  CHECK(firstFired == 1);
  CHECK(secondFired == 1);
}

TEST_CASE("view action deregisters on view unmount") {
  RuntimeHarness harness;
  registerSaveAction(harness.window);
  lambdaui::Reactive::Signal<bool> visible{true};
  int fired = 0;
  harness.setRoot(ConditionalActionRoot{.visible = visible, .fired = &fired});

  harness.pointerDown({10.f, 10.f});
  harness.keyDown(lambdaui::keys::S, lambdaui::Modifiers::Meta);
  REQUIRE(fired == 1);

  visible.set(false);
  harness.keyDown(lambdaui::keys::S, lambdaui::Modifiers::Meta);
  CHECK(fired == 1);
}

TEST_CASE("view action on ancestor resolves through Show branch remount") {
  RuntimeHarness harness;
  registerSaveAction(harness.window);
  lambdaui::Reactive::Signal<bool> visible{true};
  int fired = 0;
  harness.setRoot(AncestorActionShowRoot{.visible = visible, .fired = &fired});

  harness.pointerDown({10.f, 10.f});
  harness.keyDown(lambdaui::keys::S, lambdaui::Modifiers::Meta);
  REQUIRE(fired == 1);

  visible.set(false);
  harness.app.eventQueue().dispatch();
  visible.set(true);
  harness.app.eventQueue().dispatch();

  harness.pointerDown({10.f, 10.f});
  harness.keyDown(lambdaui::keys::S, lambdaui::Modifiers::Meta);
  CHECK(fired == 2);
}

TEST_CASE("view action on ancestor resolves through For row replacement") {
  RuntimeHarness harness;
  registerSaveAction(harness.window);
  lambdaui::Reactive::Signal<std::vector<int>> items{std::vector<int>{1}};
  int fired = 0;
  harness.setRoot(AncestorActionForRoot{.items = items, .fired = &fired});

  harness.pointerDown({10.f, 10.f});
  harness.keyDown(lambdaui::keys::S, lambdaui::Modifiers::Meta);
  REQUIRE(fired == 1);

  items.set(std::vector<int>{2});
  harness.app.eventQueue().dispatch();

  harness.pointerDown({10.f, 10.f});
  harness.keyDown(lambdaui::keys::S, lambdaui::Modifiers::Meta);
  CHECK(fired == 2);
}

TEST_CASE("hover chain disposes signals on subtree unmount") {
  RuntimeHarness harness;
  lambdaui::Reactive::Signal<bool> visible{true};
  lambdaui::Reactive::Signal<bool> hover;
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
  lambdaui::Reactive::Signal<lambdaui::Point> offset{lambdaui::Point{0.f, 0.f}};
  harness.setRoot(ScrollProbeRoot{.offset = offset});

  harness.scroll({10.f, 10.f}, {0.f, -12.f});

  CHECK(offset.get().x == doctest::Approx(0.f));
  CHECK(offset.get().y == doctest::Approx(12.f));
}

TEST_CASE("scroll view measurement does not overwrite mounted scroll range") {
  RuntimeHarness harness;
  lambdaui::Reactive::Signal<lambdaui::Point> offset{lambdaui::Point{0.f, 0.f}};
  lambdaui::Reactive::Signal<lambdaui::Size> viewport{lambdaui::Size{0.f, 0.f}};
  lambdaui::Reactive::Signal<lambdaui::Size> content{lambdaui::Size{0.f, 0.f}};
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
  std::optional<lambdaui::Rect> hoverAnchor = harness.runtime.hoverAnchor();
  REQUIRE(hoverAnchor.has_value());
  CHECK(harness.runtime.hoverTargetKey().has_value());
  CHECK(hoverAnchor->x == doctest::Approx(0.f));
  CHECK(hoverAnchor->y == doctest::Approx(0.f));
  CHECK(hoverAnchor->width == doctest::Approx(20.f));
  CHECK(hoverAnchor->height == doctest::Approx(20.f));

  harness.pointerDown({10.f, 10.f});
  std::optional<lambdaui::Rect> tapAnchor = harness.runtime.lastTapAnchor();
  REQUIRE(tapAnchor.has_value());
  CHECK(harness.runtime.lastTapTargetKey().has_value());
  CHECK(tapAnchor->x == doctest::Approx(0.f));
  CHECK(tapAnchor->y == doctest::Approx(0.f));
  CHECK(tapAnchor->width == doctest::Approx(20.f));
  CHECK(tapAnchor->height == doctest::Approx(20.f));

  harness.keyDown(lambdaui::keys::Tab);
  std::optional<lambdaui::Rect> focusAnchor = harness.runtime.focusAnchor();
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

  lambdaui::OverlayEntry const* entry = harness.window.overlayManager().top();
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

  harness.app.eventQueue().post(lambdaui::TimerEvent{.timerId = 1});
  harness.app.eventQueue().dispatch();

  lambdaui::OverlayEntry const* entry = harness.window.overlayManager().top();
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
  lambdaui::Reactive::Signal<lambdaui::Point> offset{lambdaui::Point{0.f, 0.f}};
  harness.setRoot(ScrollAnchoredProbeRoot{.offset = offset});

  harness.pointerDown({10.f, 90.f});
  std::optional<lambdaui::ComponentKey> key = harness.runtime.lastTapTargetKey();
  REQUIRE(key.has_value());

  lambdaui::OverlayConfig config{};
  config.anchor = harness.runtime.lastTapAnchor();
  config.anchorTrackComponentKey = key;
  config.placement = lambdaui::OverlayConfig::Placement::Below;
  lambdaui::OverlayId const id = harness.window.overlayManager().push(
      lambdaui::Element{lambdaui::Rectangle{}}.size(30.f, 10.f),
      std::move(config), &harness.runtime);

  lambdaui::OverlayEntry const* entry = harness.window.overlayManager().find(id);
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
  lambdaui::Reactive::Signal<lambdaui::Point> offset{lambdaui::Point{0.f, 0.f}};
  harness.setRoot(ScrollAnchoredProbeRoot{.offset = offset});

  harness.pointerDown({10.f, 90.f});
  std::optional<lambdaui::ComponentKey> key = harness.runtime.lastTapTargetKey();
  REQUIRE(key.has_value());

  lambdaui::OverlayConfig config{};
  config.anchor = harness.runtime.lastTapAnchor();
  config.anchorTrackComponentKey = key;
  config.placement = lambdaui::OverlayConfig::Placement::Below;
  config.autoFlipPreferredPlacement = lambdaui::OverlayConfig::Placement::Below;
  lambdaui::OverlayId const id = harness.window.overlayManager().push(
      lambdaui::Element{lambdaui::Rectangle{}}.size(30.f, 50.f),
      std::move(config), &harness.runtime);

  lambdaui::OverlayEntry const* entry = harness.window.overlayManager().find(id);
  REQUIRE(entry != nullptr);
  CHECK(entry->config.placement == lambdaui::OverlayConfig::Placement::Above);
  CHECK(entry->resolvedFrame.y == doctest::Approx(30.f));

  harness.scroll({10.f, 90.f}, {0.f, -60.f});
  harness.window.overlayManager().rebuild(harness.window.getSize(), harness.runtime);

  entry = harness.window.overlayManager().find(id);
  REQUIRE(entry != nullptr);
  REQUIRE(entry->config.anchor.has_value());
  CHECK(entry->config.placement == lambdaui::OverlayConfig::Placement::Below);
  CHECK(entry->resolvedFrame.y ==
        doctest::Approx(entry->config.anchor->y + entry->config.anchor->height));
}

TEST_CASE("tracked popover callout arrow follows flipped overlay placement") {
  RuntimeHarness harness;
  lambdaui::Reactive::Signal<lambdaui::Point> offset{lambdaui::Point{0.f, 60.f}};
  harness.setRoot(ScrollAnchoredProbeRoot{.offset = offset});

  harness.pointerDown({10.f, 30.f});
  std::optional<lambdaui::ComponentKey> key = harness.runtime.lastTapTargetKey();
  REQUIRE(key.has_value());

  lambdaui::OverlayConfig config{};
  config.anchor = harness.runtime.lastTapAnchor();
  config.anchorTrackComponentKey = key;
  config.placement = lambdaui::OverlayConfig::Placement::Below;
  config.autoFlipPreferredPlacement = lambdaui::OverlayConfig::Placement::Below;

  lambdaui::Theme const theme = lambdaui::Theme::light();
  lambdaui::OverlayId const id = harness.window.overlayManager().push(
      lambdaui::Popover{
          .content = lambdaui::Element{lambdaui::Rectangle{}}.size(30.f, 10.f),
          .placement = lambdaui::PopoverPlacement::Below,
          .arrow = true,
      },
      std::move(config), &harness.runtime);

  auto calloutContentY = [&]() {
    lambdaui::OverlayEntry const* entry = harness.window.overlayManager().find(id);
    REQUIRE(entry != nullptr);
    REQUIRE(entry->sceneGraph.root().children().size() >= 1);
    lambdaui::scenegraph::SceneNode const* callout =
        entry->sceneGraph.root().children().back().get();
    REQUIRE(callout != nullptr);
    REQUIRE(callout->children().size() == 2);
    return callout->children()[1]->position().y;
  };

  lambdaui::OverlayEntry const* entry = harness.window.overlayManager().find(id);
  REQUIRE(entry != nullptr);
  CHECK(entry->config.placement == lambdaui::OverlayConfig::Placement::Below);
  CHECK(calloutContentY() == doctest::Approx(theme.space3 + lambdaui::PopoverCalloutShape::kArrowH));

  offset = lambdaui::Point{0.f, 0.f};
  harness.window.overlayManager().rebuild(harness.window.getSize(), harness.runtime);

  entry = harness.window.overlayManager().find(id);
  REQUIRE(entry != nullptr);
  CHECK(entry->config.placement == lambdaui::OverlayConfig::Placement::Above);
  CHECK(calloutContentY() == doctest::Approx(theme.space3));
}

TEST_CASE("transparent overlay backdrop still dismisses on outside tap") {
  RuntimeHarness harness;
  harness.setRoot(SingleProbeRoot{});

  lambdaui::OverlayConfig config{};
  config.backdropColor = lambdaui::Colors::transparent;
  config.dismissOnOutsideTap = true;
  lambdaui::OverlayId const id = harness.window.overlayManager().push(
      lambdaui::Element{lambdaui::Rectangle{}}.size(30.f, 10.f),
      std::move(config), &harness.runtime);

  REQUIRE(harness.window.overlayManager().find(id) != nullptr);

  harness.pointerDown({1.f, 1.f});
  harness.pointerUp({1.f, 1.f});

  CHECK(harness.window.overlayManager().find(id) == nullptr);
}

TEST_CASE("transparent overlay backdrop ignores outside tap when dismissal is disabled") {
  RuntimeHarness harness;
  harness.setRoot(SingleProbeRoot{});

  lambdaui::OverlayConfig config{};
  config.backdropColor = lambdaui::Colors::transparent;
  config.dismissOnOutsideTap = false;
  lambdaui::OverlayId const id = harness.window.overlayManager().push(
      lambdaui::Element{lambdaui::Rectangle{}}.size(30.f, 10.f),
      std::move(config), &harness.runtime);

  REQUIRE(harness.window.overlayManager().find(id) != nullptr);

  harness.pointerDown({1.f, 1.f});
  harness.pointerUp({1.f, 1.f});

  CHECK(harness.window.overlayManager().find(id) != nullptr);
}

TEST_CASE("select popover anchors to focused trigger when opened from keyboard") {
  RuntimeHarness harness;
  harness.setRoot(SelectKeyboardProbeRoot{});

  harness.keyDown(lambdaui::keys::Tab);
  std::optional<lambdaui::Rect> focusAnchor = harness.runtime.focusAnchor();
  REQUIRE(focusAnchor.has_value());
  std::optional<lambdaui::ComponentKey> focusKey = harness.runtime.focusTargetKey();
  REQUIRE(focusKey.has_value());

  harness.keyDown(lambdaui::keys::Return);

  lambdaui::OverlayEntry const* entry = harness.window.overlayManager().top();
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
  lambdaui::Reactive::Signal<int> selected{-1};
  int changes = 0;
  harness.setRoot(SelectCommitProbeRoot{
      .selected = selected,
      .changeCount = &changes,
  });

  harness.keyDown(lambdaui::keys::Tab);
  harness.keyDown(lambdaui::keys::Return);
  REQUIRE(harness.window.overlayManager().top() != nullptr);

  harness.keyDown(lambdaui::keys::DownArrow);
  CHECK(selected.get() == -1);

  harness.keyDown(lambdaui::keys::Return);

  CHECK(harness.window.overlayManager().top() == nullptr);
  CHECK(selected.get() == 1);
  CHECK(changes == 1);
}

TEST_CASE("select option Tab commits closes popover and advances focus past trigger") {
  RuntimeHarness harness;
  lambdaui::Reactive::Signal<int> selected{-1};
  lambdaui::Reactive::Signal<bool> nextFocus;
  int changes = 0;
  harness.setRoot(SelectCommitProbeRoot{
      .selected = selected,
      .nextFocus = &nextFocus,
      .changeCount = &changes,
  });

  harness.keyDown(lambdaui::keys::Tab);
  harness.keyDown(lambdaui::keys::Return);
  REQUIRE(harness.window.overlayManager().top() != nullptr);

  harness.keyDown(lambdaui::keys::DownArrow);
  CHECK(selected.get() == -1);
  CHECK_FALSE(nextFocus.get());

  harness.keyDown(lambdaui::keys::Tab);

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
  lambdaui::OverlayEntry const* entry = harness.window.overlayManager().top();
  REQUIRE(entry != nullptr);
  harness.window.overlayManager().rebuild(harness.window.getSize(), harness.runtime);

  lambdaui::scenegraph::SceneNode const* viewport = findScrollViewport(entry->sceneGraph.root());
  REQUIRE(viewport != nullptr);
  REQUIRE(viewport->children().size() >= 1);
  lambdaui::scenegraph::SceneNode const* content = viewport->children()[0].get();
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
  lambdaui::Theme theme = lambdaui::Theme::light();
  theme.durationFast = 0.f;
  harness.window.setTheme(theme);
  harness.setRoot(LongSelectProbeRoot{});

  harness.pointerDown({20.f, 20.f});
  harness.pointerUp({20.f, 20.f});
  lambdaui::OverlayEntry const* entry = harness.window.overlayManager().top();
  REQUIRE(entry != nullptr);
  harness.window.overlayManager().rebuild(harness.window.getSize(), harness.runtime);

  std::vector<lambdaui::scenegraph::RectNode const*> rows;
  collectTapRects(entry->sceneGraph.root(), rows);
  REQUIRE(rows.size() >= 2);
  lambdaui::Color idleFill = solidFillColor(*rows[1]);
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
  lambdaui::Reactive::Signal<int> state;

  lambdaui::OverlayId const id = harness.window.overlayManager().push(
      StatefulOverlayProbe{.bodyCalls = bodyCalls, .cleanups = cleanups, .state = &state},
      lambdaui::OverlayConfig{}, &harness.runtime);

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
  checkSameColor(solidWindowBackground(harness.window), lambdaui::Theme::light().windowBackgroundColor);

  harness.window.setTheme(lambdaui::Theme::dark());
  checkSameColor(solidWindowBackground(harness.window), lambdaui::Theme::dark().windowBackgroundColor);

  lambdaui::Color const custom = lambdaui::Color::hex(0x123456);
  harness.window.setBackground(lambdaui::WindowBackground::solid(custom));
  harness.window.setTheme(lambdaui::Theme::light());
  checkSameColor(solidWindowBackground(harness.window), custom);
}
