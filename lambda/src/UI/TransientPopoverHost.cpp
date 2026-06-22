#include "UI/TransientPopoverHost.hpp"

#include <Lambda/Graphics/TextSystem.hpp>
#include <Lambda/Reactive/Scope.hpp>
#include <Lambda/SceneGraph/SceneInteraction.hpp>
#include <Lambda/SceneGraph/SceneNode.hpp>
#include <Lambda/SceneGraph/SceneRenderer.hpp>
#include <Lambda/SceneGraph/SceneTraversal.hpp>
#include <Lambda/UI/Application.hpp>
#include <Lambda/UI/Detail/RootHolder.hpp>
#include <Lambda/UI/InteractionData.hpp>
#include <Lambda/UI/MeasureContext.hpp>
#include <Lambda/UI/OverlaySurfaceHelpers.hpp>
#include <Lambda/UI/Theme.hpp>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace lambda {
namespace {

struct ElementRootHolder final : RootHolder {
  explicit ElementRootHolder(Element elementIn)
      : element(std::move(elementIn)) {}

  Element makeElement() override { return element; }

  Element element;
};

Element popoverRootElement(Popover const& popover, EnvironmentBinding const& environment, bool nativeShell) {
  if (!nativeShell) {
    return Element{popover};
  }
  Theme const theme = environment.value<ThemeKey>();
  float const pad = resolveFloat(popover.contentPadding, theme.space3);
  Element content = popover.content;
  if (pad > 0.f) {
    content = std::move(content).padding(pad);
  }
  return content;
}

LayoutConstraints measureConstraints(Size maxSize) {
  return LayoutConstraints{
      .maxWidth = std::max(1.f, maxSize.width),
      .maxHeight = std::max(1.f, maxSize.height),
  };
}

Size measurePopover(Element const& root, EnvironmentBinding const& environment, Size maxSize) {
  MeasureContext measureContext{Application::instance().textSystem(), environment};
  Size size = root.measure(measureContext, measureConstraints(maxSize), LayoutHints{},
                           Application::instance().textSystem());
  size.width = std::clamp(std::ceil(size.width), 1.f, std::max(1.f, maxSize.width));
  size.height = std::clamp(std::ceil(size.height), 1.f, std::max(1.f, maxSize.height));
  return size;
}

struct TargetSnapshot {
  scenegraph::SceneNode const* node = nullptr;
  InteractionData const* interaction = nullptr;
  ComponentKey stableTargetKey{};
  Cursor cursor = Cursor::Inherit;
  bool focusable = false;
  Reactive::SmallFn<void()> onPointerEnter;
  Reactive::SmallFn<void()> onPointerExit;
  Reactive::SmallFn<void()> onFocus;
  Reactive::SmallFn<void()> onBlur;
  Reactive::SmallFn<void(Point, MouseButton)> onPointerDown;
  Reactive::SmallFn<void(Point, MouseButton)> onPointerUp;
  Reactive::SmallFn<void(Point)> onPointerMove;
  Reactive::SmallFn<void(Vec2)> onScroll;
  Reactive::SmallFn<void(KeyCode, Modifiers)> onKeyDown;
  Reactive::SmallFn<void(KeyCode, Modifiers)> onKeyUp;
  Reactive::SmallFn<void(std::string const&)> onTextInput;
  Reactive::SmallFn<void(MouseButton)> onTap;
  Reactive::SmallFn<void(MouseButton, Modifiers)> onTapWithModifiers;
  Reactive::Signal<bool> hoverSignal;
  Reactive::Signal<bool> pressSignal;
  Reactive::Signal<bool> focusSignal;
  Reactive::Signal<bool> keyboardFocusSignal;
};

struct HitTarget {
  scenegraph::SceneNode const* node = nullptr;
  InteractionData const* interaction = nullptr;
  Point localPoint{};
};

struct InputState {
  std::vector<TargetSnapshot> hoverChain;
  std::optional<TargetSnapshot> pressTarget;
  std::optional<TargetSnapshot> focusTarget;
  Point pressPoint{};
  bool pressCancelled = false;
};

TargetSnapshot snapshot(scenegraph::SceneNode const* node, InteractionData const* interaction) {
  TargetSnapshot target;
  target.node = node;
  target.interaction = interaction;
  if (interaction) {
    target.stableTargetKey = interaction->stableTargetKey_;
    target.cursor = interaction->cursor.evaluate();
    target.focusable = interaction->focusable_.evaluate();
    target.onPointerEnter = interaction->onPointerEnter;
    target.onPointerExit = interaction->onPointerExit;
    target.onFocus = interaction->onFocus;
    target.onBlur = interaction->onBlur;
    target.onPointerDown = interaction->onPointerDown;
    target.onPointerUp = interaction->onPointerUp;
    target.onPointerMove = interaction->onPointerMove;
    target.onScroll = interaction->onScroll;
    target.onKeyDown = interaction->onKeyDown;
    target.onKeyUp = interaction->onKeyUp;
    target.onTextInput = interaction->onTextInput;
    target.onTap = interaction->onTap;
    target.onTapWithModifiers = interaction->onTapWithModifiers;
    target.hoverSignal = interaction->hoverSignal;
    target.pressSignal = interaction->pressSignal;
    target.focusSignal = interaction->focusSignal;
    target.keyboardFocusSignal = interaction->keyboardFocusSignal;
  }
  return target;
}

TargetSnapshot snapshot(HitTarget const& hit) {
  return snapshot(hit.node, hit.interaction);
}

bool sameTarget(TargetSnapshot const& lhs, TargetSnapshot const& rhs) {
  return lhs.node == rhs.node && lhs.interaction == rhs.interaction;
}

bool sameTarget(TargetSnapshot const& lhs, HitTarget const& rhs) {
  return lhs.node == rhs.node && lhs.interaction == rhs.interaction;
}

std::optional<HitTarget> hitGraph(scenegraph::SceneGraph const& graph, Point point) {
  if (auto hit = scenegraph::hitTestInteraction(graph, point, [](scenegraph::Interaction const&) {
        return true;
      })) {
    return HitTarget{
        .node = hit->node,
        .interaction = hit->interaction ? &interactionData(*hit->interaction) : nullptr,
        .localPoint = hit->localPoint,
    };
  }
  return std::nullopt;
}

std::optional<Point> localPointForTarget(scenegraph::SceneGraph const& graph, TargetSnapshot const& target,
                                         Point point) {
  if (!target.node) {
    return std::nullopt;
  }
  return scenegraph::localPointForNode(graph.root(), point, target.node);
}

std::vector<TargetSnapshot> hoverChainForHit(HitTarget const& hit) {
  std::vector<TargetSnapshot> chain;
  for (scenegraph::SceneNode const* node = hit.node; node; node = node->parent()) {
    if (scenegraph::Interaction const* interaction = node->interaction()) {
      chain.push_back(snapshot(node, &interactionData(*interaction)));
    }
  }
  std::reverse(chain.begin(), chain.end());
  return chain;
}

void writeSignal(Reactive::Signal<bool> const& signal, bool active) {
  if (!signal.disposed()) {
    signal.set(active);
  }
}

void setHoverActive(TargetSnapshot const& target, bool active) {
  writeSignal(target.hoverSignal, active);
}

void setPressActive(TargetSnapshot const& target, bool active) {
  writeSignal(target.pressSignal, active);
}

void setFocusActive(TargetSnapshot const& target, bool active, bool keyboard = false) {
  writeSignal(target.focusSignal, active);
  writeSignal(target.keyboardFocusSignal, active && keyboard);
}

void setFocus(InputState& input, std::optional<HitTarget> hit, bool notify = true, bool keyboard = false) {
  if (input.focusTarget && hit && sameTarget(*input.focusTarget, *hit)) {
    return;
  }
  Reactive::SmallFn<void()> blur;
  if (notify && input.focusTarget) {
    blur = input.focusTarget->onBlur;
  }
  if (input.focusTarget) {
    setFocusActive(*input.focusTarget, false);
  }
  input.focusTarget = hit ? std::optional<TargetSnapshot>{snapshot(*hit)} : std::nullopt;
  Reactive::SmallFn<void()> focus;
  if (notify && input.focusTarget) {
    focus = input.focusTarget->onFocus;
  }
  if (input.focusTarget) {
    setFocusActive(*input.focusTarget, true, keyboard);
  }
  if (blur) {
    blur();
  }
  if (focus) {
    focus();
  }
}

void updateHover(InputState& input, scenegraph::SceneGraph const& graph, Point point) {
  std::optional<HitTarget> hit = hitGraph(graph, point);
  std::vector<TargetSnapshot> nextChain = hit ? hoverChainForHit(*hit) : std::vector<TargetSnapshot>{};
  if (input.hoverChain.size() == nextChain.size()) {
    bool same = true;
    for (std::size_t i = 0; i < nextChain.size(); ++i) {
      same = same && sameTarget(input.hoverChain[i], nextChain[i]);
    }
    if (same) {
      return;
    }
  }

  std::size_t common = 0;
  while (common < input.hoverChain.size() && common < nextChain.size() &&
         sameTarget(input.hoverChain[common], nextChain[common])) {
    ++common;
  }
  for (std::size_t i = input.hoverChain.size(); i > common; --i) {
    setHoverActive(input.hoverChain[i - 1], false);
    if (auto exit = input.hoverChain[i - 1].onPointerExit) {
      exit();
    }
  }
  for (std::size_t i = common; i < nextChain.size(); ++i) {
    setHoverActive(nextChain[i], true);
    if (auto enter = nextChain[i].onPointerEnter) {
      enter();
    }
  }
  input.hoverChain = std::move(nextChain);
}

} // namespace

struct TransientPopoverHost::Impl {
  Popover popover;
  EnvironmentBinding environment;
  Size maxSize;
  Element rootElement;
  scenegraph::SceneGraph sceneGraph;
  std::unique_ptr<MountRoot> root;
  std::unique_ptr<scenegraph::SceneRenderer> renderer;
  std::function<void()> requestRedraw;
  std::function<void()> requestDismiss;
  InputState input;
  bool dismissed = false;

  Impl(Config config, Element root)
      : popover(std::move(config.popover))
      , environment(std::move(config.environment))
      , maxSize(config.maxSize)
      , rootElement(std::move(root))
      , requestRedraw(std::move(config.requestRedraw))
      , requestDismiss(std::move(config.requestDismiss)) {}
};

TransientPopoverHost::TransientPopoverHost(Config config) {
  Element root = popoverRootElement(config.popover, config.environment, config.useNativeShell);
  measuredSize_ = measurePopover(root, config.environment, config.maxSize);
  d = std::make_unique<Impl>(std::move(config), std::move(root));
}

TransientPopoverHost::~TransientPopoverHost() {
  notifyDismissed();
}

void TransientPopoverHost::mount(Size size) {
  if (!d) {
    return;
  }
  d->root = std::make_unique<MountRoot>(
      std::make_unique<ElementRootHolder>(d->rootElement),
      Application::instance().textSystem(),
      d->environment,
      size,
      [this] {
        if (d && d->requestRedraw) {
          d->requestRedraw();
        }
      });
  d->root->mount(d->sceneGraph);
}

void TransientPopoverHost::resize(Size size) {
  if (!d) {
    return;
  }
  if (!d->root) {
    mount(size);
    return;
  }
  d->root->resize(size, d->sceneGraph);
}

void TransientPopoverHost::render(Canvas& canvas) {
  if (!d) {
    return;
  }
  if (!d->renderer) {
    d->renderer = std::make_unique<scenegraph::SceneRenderer>(canvas);
  }
  canvas.clear(Colors::transparent);
  d->renderer->render(d->sceneGraph);
}

void TransientPopoverHost::pointerDown(Point point, MouseButton button, Modifiers modifiers) {
  (void)modifiers;
  if (!d) {
    return;
  }
  if (d->input.pressTarget) {
    setPressActive(*d->input.pressTarget, false);
  }
  d->input.pressTarget.reset();
  d->input.pressCancelled = false;
  d->input.pressPoint = point;
  if (auto hit = hitGraph(d->sceneGraph, point)) {
    TargetSnapshot target = snapshot(*hit);
    if (target.focusable) {
      setFocus(d->input, hit);
    } else {
      setFocus(d->input, std::nullopt);
    }
    d->input.pressTarget = target;
    setPressActive(target, true);
    if (target.onPointerDown) {
      target.onPointerDown(hit->localPoint, button);
    }
  } else {
    setFocus(d->input, std::nullopt);
  }
  updateHover(d->input, d->sceneGraph, point);
}

void TransientPopoverHost::pointerMove(Point point) {
  if (!d) {
    return;
  }
  if (d->input.pressTarget) {
    float const dx = point.x - d->input.pressPoint.x;
    float const dy = point.y - d->input.pressPoint.y;
    constexpr float kTapSlop = 8.f;
    if (dx * dx + dy * dy > kTapSlop * kTapSlop) {
      d->input.pressCancelled = true;
    }
    if (d->input.pressTarget->onPointerMove) {
      Point const local = localPointForTarget(d->sceneGraph, *d->input.pressTarget, point).value_or(point);
      d->input.pressTarget->onPointerMove(local);
    }
  } else if (auto hit = hitGraph(d->sceneGraph, point)) {
    if (hit->interaction && hit->interaction->onPointerMove) {
      hit->interaction->onPointerMove(hit->localPoint);
    }
  }
  updateHover(d->input, d->sceneGraph, point);
}

void TransientPopoverHost::pointerUp(Point point, MouseButton button, Modifiers modifiers) {
  if (!d) {
    return;
  }
  if (d->input.pressTarget) {
    TargetSnapshot released = *d->input.pressTarget;
    bool const cancelled = d->input.pressCancelled;
    d->input.pressTarget.reset();
    d->input.pressCancelled = false;
    setPressActive(released, false);
    if (released.onPointerUp) {
      Point const local = localPointForTarget(d->sceneGraph, released, point).value_or(point);
      released.onPointerUp(local, button);
    }
    if (!cancelled && released.onTap) {
      released.onTap(button);
    }
    if (!cancelled && released.onTapWithModifiers) {
      released.onTapWithModifiers(button, modifiers);
    }
  } else if (auto hit = hitGraph(d->sceneGraph, point)) {
    if (hit->interaction && hit->interaction->onPointerUp) {
      hit->interaction->onPointerUp(hit->localPoint, button);
    }
    if (hit->interaction && hit->interaction->onTap) {
      hit->interaction->onTap(button);
    }
    if (hit->interaction && hit->interaction->onTapWithModifiers) {
      hit->interaction->onTapWithModifiers(button, modifiers);
    }
  }
  updateHover(d->input, d->sceneGraph, point);
}

void TransientPopoverHost::scroll(Point point, Vec2 delta) {
  if (!d) {
    return;
  }
  if (auto hit = scenegraph::hitTestInteraction(d->sceneGraph, point, [](scenegraph::Interaction const& interaction) {
        return static_cast<bool>(interactionData(interaction).onScroll);
      })) {
    InteractionData const& interaction = interactionData(*hit->interaction);
    if (interaction.onScroll) {
      interaction.onScroll(delta);
    }
  }
}

void TransientPopoverHost::keyDown(KeyCode key, Modifiers modifiers) {
  if (!d) {
    return;
  }
  if (key == keys::Escape && d->popover.dismissOnEscape) {
    if (d->requestDismiss) {
      d->requestDismiss();
    }
    return;
  }
  if (d->input.focusTarget && d->input.focusTarget->onKeyDown) {
    d->input.focusTarget->onKeyDown(key, modifiers);
  }
}

void TransientPopoverHost::keyUp(KeyCode key, Modifiers modifiers) {
  if (d && d->input.focusTarget && d->input.focusTarget->onKeyUp) {
    d->input.focusTarget->onKeyUp(key, modifiers);
  }
}

void TransientPopoverHost::textInput(std::string const& text) {
  if (d && d->input.focusTarget && d->input.focusTarget->onTextInput) {
    d->input.focusTarget->onTextInput(text);
  }
}

void TransientPopoverHost::notifyDismissed() {
  if (!d || d->dismissed) {
    return;
  }
  d->dismissed = true;
  if (d->popover.onDismiss) {
    d->popover.onDismiss();
  }
}

} // namespace lambda
