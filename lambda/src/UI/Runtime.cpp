#include <Lambda/UI/Detail/Runtime.hpp>

#include <Lambda/UI/Application.hpp>
#include <Lambda/UI/EventQueue.hpp>
#include <Lambda/UI/KeyCodes.hpp>
#include <Lambda/UI/Window.hpp>
#include <Lambda/UI/Detail/RootHolder.hpp>
#include <Lambda/UI/InteractionData.hpp>
#include <Lambda/SceneGraph/SceneInteraction.hpp>
#include <Lambda/SceneGraph/SceneGraph.hpp>
#include <Lambda/SceneGraph/SceneTraversal.hpp>
#include <Lambda/UI/MountRoot.hpp>
#include <Lambda/UI/Overlay.hpp>

#include "Detail/ResizeTrace.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <vector>
#include <utility>

namespace lambdaui {

namespace {

struct RuntimeTargetSnapshot {
  scenegraph::SceneNode const* node = nullptr;
  InteractionData const* interaction = nullptr;
  ComponentKey stableTargetKey{};
  OverlayId overlay{};
  bool inOverlay = false;
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
  std::vector<Reactive::Signal<bool>> hoverSignals;
  std::vector<Reactive::Signal<bool>> pressSignals;
  std::vector<Reactive::Signal<bool>> focusSignals;
  std::vector<Reactive::Signal<bool>> keyboardFocusSignals;
  bool windowDragRegion = false;
  WindowResizeEdge windowResizeEdge = WindowResizeEdge::None;
};

struct RuntimeInputState {
  std::vector<RuntimeTargetSnapshot> hoverChain;
  std::optional<RuntimeTargetSnapshot> pressTarget;
  std::optional<RuntimeTargetSnapshot> focusTarget;
  std::optional<Rect> lastTapAnchor;
  std::uint32_t lastTapSerial = 0;
  std::optional<Rect> hoverAnchor;
  std::optional<ComponentKey> lastTapTargetKey;
  std::optional<ComponentKey> hoverTargetKey;
  Point pressPoint{};
  bool pressCancelled = false;
  Cursor currentCursor = Cursor::Arrow;
  FocusInputKind focusKind = FocusInputKind::Pointer;
};

struct RuntimeFocusRequestEvent {
  unsigned int handle = 0;
  ComponentKey key;
};

std::optional<Rect> windowRectForTarget(RuntimeTargetSnapshot const& target, Window& window);
void focusSingleTargetOnWindowFocus(RuntimeInputState& input, Window& window);

} // namespace

struct Runtime::Impl {
  Window& window;
  std::unique_ptr<MountRoot> root;
  CommandRegistry commands;
  RuntimeInputState input;
  std::shared_ptr<bool> alive = std::make_shared<bool>(true);

  explicit Impl(Window& w) : window(w) {}
};

thread_local Runtime* Runtime::current_ = nullptr;

Runtime::Runtime(Window& window)
    : d(std::make_unique<Impl>(window)) {
  if (Application::hasInstance()) {
    unsigned int const handle = window.handle();
    std::shared_ptr<bool> alive = d->alive;
    Application::instance().eventQueue().on<InputEvent>(
        [this, handle, alive](InputEvent const& event) {
          if (*alive && event.handle == handle) {
            handleInput(event);
          }
        });
    Application::instance().eventQueue().on<WindowEvent>(
        [this, handle, alive](WindowEvent const& event) {
          if (*alive && event.handle == handle) {
            handleWindowEvent(event);
          }
        });
    Application::instance().eventQueue().on<RuntimeFocusRequestEvent>(
        [this, handle, alive](RuntimeFocusRequestEvent const& event) {
          if (*alive && event.handle == handle) {
            requestFocus(event.key);
          }
        });
  }
}

Runtime::~Runtime() {
  if (d && d->alive) {
    *d->alive = false;
  }
}

void Runtime::setRoot(std::unique_ptr<RootHolder> holder) {
  d->commands.beginRebuild();
  d->root = std::make_unique<MountRoot>(
      std::move(holder), Application::instance().textSystem(), d->window.environmentBinding(),
      d->window.getSize(), [handle = d->window.handle()] {
        Window::postRedraw(handle);
      });
  Runtime* previous = current_;
  current_ = this;
  d->root->mount(d->window.sceneGraph());
  current_ = previous;
  focusSingleTargetOnWindowFocus(d->input, d->window);
  d->window.requestRedraw();
}

void Runtime::beginShutdown() {
  beginShutdown(d->window.hasSceneGraph() ? &d->window.sceneGraph() : nullptr);
}

void Runtime::beginShutdown(scenegraph::SceneGraph* sceneGraph) {
  if (d->root && sceneGraph) {
    d->root->unmount(*sceneGraph);
  }
  d->root.reset();
}

bool Runtime::isCommandCurrentlyEnabled(std::string const& name) const {
  ComponentKey const focusedKey = d->input.focusTarget
      ? d->input.focusTarget->stableTargetKey
      : ComponentKey{};
  return isCommandCurrentlyEnabledFrom(focusedKey, name);
}

bool Runtime::isCommandCurrentlyEnabledFrom(ComponentKey const& focusedKey,
                                            std::string const& name) const {
  if (Application::hasInstance()) {
    return d->commands.isHandlerEnabled(focusedKey, name, Application::instance().commandDescriptors());
  }
  return d->commands.isHandlerEnabled(focusedKey, name, d->window.commandDescriptors());
}

bool Runtime::dispatchCommand(std::string const& name) {
  ComponentKey const focusedKey = d->input.focusTarget
      ? d->input.focusTarget->stableTargetKey
      : ComponentKey{};
  return dispatchCommandFrom(focusedKey, name);
}

bool Runtime::dispatchCommandFrom(ComponentKey const& focusedKey, std::string const& name) {
  if (Application::hasInstance()) {
    return d->commands.dispatchCommand(focusedKey, name, Application::instance().commandDescriptors());
  }
  return d->commands.dispatchCommand(focusedKey, name, d->window.commandDescriptors());
}

CommandRegistry& Runtime::commandRegistry() noexcept {
  return d->commands;
}

CommandRegistry const& Runtime::commandRegistry() const noexcept {
  return d->commands;
}

std::optional<Rect> Runtime::lastTapAnchor() const noexcept {
  return d->input.lastTapAnchor;
}

std::uint32_t Runtime::lastTapSerial() const noexcept {
  return d->input.lastTapSerial;
}

std::optional<Rect> Runtime::hoverAnchor() const noexcept {
  return d->input.hoverAnchor;
}

std::optional<Rect> Runtime::focusAnchor() const noexcept {
  if (!d->input.focusTarget) {
    return std::nullopt;
  }
  return windowRectForTarget(*d->input.focusTarget, d->window);
}

std::optional<ComponentKey> Runtime::lastTapTargetKey() const noexcept {
  return d->input.lastTapTargetKey;
}

std::optional<ComponentKey> Runtime::hoverTargetKey() const noexcept {
  return d->input.hoverTargetKey;
}

std::optional<ComponentKey> Runtime::focusTargetKey() const noexcept {
  if (!d->input.focusTarget || d->input.focusTarget->stableTargetKey.empty()) {
    return std::nullopt;
  }
  return d->input.focusTarget->stableTargetKey;
}

Window& Runtime::window() noexcept {
  return d->window;
}

Window const& Runtime::window() const noexcept {
  return d->window;
}

Runtime* Runtime::current() noexcept {
  return current_;
}

namespace {

struct HitTarget {
  scenegraph::SceneNode const* node = nullptr;
  InteractionData const* interaction = nullptr;
  Point localPoint{};
  OverlayId overlay{};
};

struct PointerHitSnapshot {
  std::optional<HitTarget> hit;
  std::vector<RuntimeTargetSnapshot> hoverChain;
  Cursor cursor = Cursor::Arrow;
};

using InteractionFilter = Reactive::SmallFn<bool(scenegraph::Interaction const&)>;

bool acceptAnyInteraction(scenegraph::Interaction const&) {
  return true;
}

Point eventPoint(InputEvent const& event) {
  return Point{event.position.x, event.position.y};
}

std::optional<HitTarget> hitOverlay(OverlayEntry const& entry, Point windowPoint,
                                    InteractionFilter const& acceptTarget) {
  Point const local{windowPoint.x - entry.resolvedFrame.x, windowPoint.y - entry.resolvedFrame.y};
  if (auto hit = scenegraph::hitTestInteraction(entry.sceneGraph, local, acceptTarget)) {
    return HitTarget{
        .node = hit->node,
        .interaction = hit->interaction ? &interactionData(*hit->interaction) : nullptr,
        .localPoint = hit->localPoint,
        .overlay = entry.id,
    };
  }
  scenegraph::SceneNode const& root = entry.sceneGraph.root();
  scenegraph::Interaction const* rootInteraction = root.interaction();
  if (rootInteraction && acceptTarget(*rootInteraction)) {
    Size const rootSize = root.size();
    if (windowPoint.x >= 0.f && windowPoint.y >= 0.f &&
        windowPoint.x <= rootSize.width && windowPoint.y <= rootSize.height) {
      return HitTarget{
          .node = &root,
          .interaction = &interactionData(*rootInteraction),
          .localPoint = windowPoint,
          .overlay = entry.id,
      };
    }
  }
  return std::nullopt;
}

std::optional<HitTarget> hitWindow(Window& window, Point point,
                                   InteractionFilter const& acceptTarget = acceptAnyInteraction) {
  auto const& overlays = window.overlayManager().entries();
  for (auto it = overlays.rbegin(); it != overlays.rend(); ++it) {
    if (*it) {
      if (auto hit = hitOverlay(**it, point, acceptTarget)) {
        return hit;
      }
    }
  }
  if (window.hasSceneGraph()) {
    if (auto hit = scenegraph::hitTestInteraction(window.sceneGraph(), point, acceptTarget)) {
      return HitTarget{
          .node = hit->node,
          .interaction = hit->interaction ? &interactionData(*hit->interaction) : nullptr,
          .localPoint = hit->localPoint,
      };
    }
  }
  return std::nullopt;
}

Rect windowRectForHit(HitTarget const& hit, Point windowPoint) {
  Size const size = hit.node ? hit.node->size() : Size{};
  return Rect{
      windowPoint.x - hit.localPoint.x,
      windowPoint.y - hit.localPoint.y,
      std::max(0.f, size.width),
      std::max(0.f, size.height),
  };
}

Rect windowRectForNode(scenegraph::SceneNode const& node, Point rootOrigin = {}) {
  Point origin = rootOrigin;
  scenegraph::SceneNode const* current = &node;
  while (current) {
    origin.x += current->position().x;
    origin.y += current->position().y;
    current = current->parent();
  }
  Size const size = node.size();
  return Rect{origin.x, origin.y, std::max(0.f, size.width), std::max(0.f, size.height)};
}

void appendFocusableTargets(scenegraph::SceneGraph const& graph, OverlayId overlay,
                            std::vector<HitTarget>& out) {
  for (ComponentKey const& key : scenegraph::collectFocusableKeys(graph)) {
    auto const [node, interaction] = scenegraph::findInteractionByKey(graph, key);
    InteractionData const* data = interaction ? &interactionData(*interaction) : nullptr;
    if (node && data && interaction->focusable()) {
      out.push_back(HitTarget{
          .node = node,
          .interaction = data,
          .localPoint = {},
          .overlay = overlay,
      });
    }
  }
}

std::vector<HitTarget> focusableTargets(Window& window) {
  std::vector<HitTarget> targets;
  auto const& overlays = window.overlayManager().entries();
  for (auto it = overlays.rbegin(); it != overlays.rend(); ++it) {
    if (*it) {
      appendFocusableTargets((*it)->sceneGraph, (*it)->id, targets);
      if ((*it)->config.modal && !targets.empty()) {
        return targets;
      }
    }
  }
  if (window.hasSceneGraph()) {
    appendFocusableTargets(window.sceneGraph(), kInvalidOverlayId, targets);
  }
  return targets;
}

bool invalidateRenderCaches(Window& window) {
  bool invalidated = false;
  if (window.hasSceneGraph()) {
    window.sceneGraph().invalidateRenderCaches();
    invalidated = true;
  }
  for (std::unique_ptr<OverlayEntry> const& entry : window.overlayManager().entries()) {
    if (entry) {
      entry->sceneGraph.invalidateRenderCaches();
      invalidated = true;
    }
  }
  return invalidated;
}

RuntimeTargetSnapshot snapshot(scenegraph::SceneNode const* node,
                               InteractionData const* interaction,
                               OverlayId overlay) {
  RuntimeTargetSnapshot target{};
  target.node = node;
  target.interaction = interaction;
  target.overlay = overlay;
  target.inOverlay = overlay.isValid();
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
    target.hoverSignals = interaction->hoverSignals;
    target.pressSignals = interaction->pressSignals;
    target.focusSignals = interaction->focusSignals;
    target.keyboardFocusSignals = interaction->keyboardFocusSignals;
    target.windowDragRegion = interaction->windowDragRegion;
    target.windowResizeEdge = interaction->windowResizeEdge;
  }
  return target;
}

RuntimeTargetSnapshot snapshot(HitTarget const& hit) {
  return snapshot(hit.node, hit.interaction, hit.overlay);
}

bool sameTarget(RuntimeTargetSnapshot const& lhs, RuntimeTargetSnapshot const& rhs) {
  return lhs.node == rhs.node && lhs.interaction == rhs.interaction &&
         lhs.overlay == rhs.overlay;
}

bool sameTarget(RuntimeTargetSnapshot const& lhs, HitTarget const& rhs) {
  return lhs.node == rhs.node && lhs.interaction == rhs.interaction &&
         lhs.overlay == rhs.overlay;
}

std::vector<RuntimeTargetSnapshot> hoverChainForHit(HitTarget const& hit) {
  std::vector<RuntimeTargetSnapshot> chain;
  for (scenegraph::SceneNode const* node = hit.node; node; node = node->parent()) {
    if (scenegraph::Interaction const* interaction = node->interaction()) {
      chain.push_back(snapshot(node, &interactionData(*interaction), hit.overlay));
    }
  }
  std::reverse(chain.begin(), chain.end());
  return chain;
}

PointerHitSnapshot pointerHitSnapshot(Window& window, Point point) {
  PointerHitSnapshot snapshot{};
  snapshot.hit = hitWindow(window, point);
  if (snapshot.hit) {
    snapshot.hoverChain = hoverChainForHit(*snapshot.hit);
    for (auto it = snapshot.hoverChain.rbegin(); it != snapshot.hoverChain.rend(); ++it) {
      if (it->cursor != Cursor::Inherit) {
        snapshot.cursor = it->cursor;
        break;
      }
    }
  }
  return snapshot;
}

std::optional<Point> localPointForTarget(RuntimeTargetSnapshot const& target,
                                         Window& window, Point windowPoint) {
  if (!target.node) {
    return std::nullopt;
  }
  if (target.inOverlay) {
    OverlayEntry const* entry = window.overlayManager().find(target.overlay);
    if (!entry) {
      return std::nullopt;
    }
    Point const rootPoint{windowPoint.x - entry->resolvedFrame.x,
                          windowPoint.y - entry->resolvedFrame.y};
    return scenegraph::localPointForNode(entry->sceneGraph.root(), rootPoint, target.node);
  }
  if (!window.hasSceneGraph()) {
    return std::nullopt;
  }
  return scenegraph::localPointForNode(window.sceneGraph().root(), windowPoint, target.node);
}

std::optional<Rect> windowRectForTarget(RuntimeTargetSnapshot const& target, Window& window) {
  if (!target.node) {
    return std::nullopt;
  }
  if (target.inOverlay) {
    OverlayEntry const* entry = window.overlayManager().find(target.overlay);
    if (!entry) {
      return std::nullopt;
    }
    return windowRectForNode(*target.node, Point{entry->resolvedFrame.x, entry->resolvedFrame.y});
  }
  if (!window.hasSceneGraph()) {
    return std::nullopt;
  }
  return windowRectForNode(*target.node);
}

void applyCursor(RuntimeInputState& input, Window& window, Cursor cursor) {
  if (cursor == Cursor::Inherit) {
    cursor = Cursor::Arrow;
  }
  if (input.currentCursor == cursor) {
    return;
  }
  input.currentCursor = cursor;
  window.setCursor(cursor);
}

void writeSignal(Reactive::Signal<bool> const& signal, bool active) {
  if (!signal.disposed()) {
    signal.set(active);
  }
}

void writeSignals(std::vector<Reactive::Signal<bool>> const& signals, bool active) {
  for (auto const& signal : signals) {
    writeSignal(signal, active);
  }
}

void setHoverActive(RuntimeTargetSnapshot const& target, bool active) {
  writeSignal(target.hoverSignal, active);
  writeSignals(target.hoverSignals, active);
}

void setPressActive(RuntimeTargetSnapshot const& target, bool active) {
  writeSignal(target.pressSignal, active);
  writeSignals(target.pressSignals, active);
}

void setFocusActive(RuntimeTargetSnapshot const& target, bool active,
                    FocusInputKind kind = FocusInputKind::Pointer) {
  writeSignal(target.focusSignal, active);
  writeSignal(target.keyboardFocusSignal, active && kind == FocusInputKind::Keyboard);
  writeSignals(target.focusSignals, active);
  writeSignals(target.keyboardFocusSignals, active && kind == FocusInputKind::Keyboard);
}

void updateCursorForHit(RuntimeInputState& input, Window& window, PointerHitSnapshot const& hit) {
  if (input.pressTarget && input.pressTarget->cursor != Cursor::Inherit) {
    applyCursor(input, window, input.pressTarget->cursor);
    return;
  }

  applyCursor(input, window, hit.cursor);
}

void updateHoverForHit(RuntimeInputState& input, Point point, PointerHitSnapshot const& hit) {
  input.hoverAnchor = hit.hit ? std::optional<Rect>{windowRectForHit(*hit.hit, point)}
                          : std::nullopt;
  if (hit.hit && hit.hit->interaction && !hit.hit->interaction->stableTargetKey_.empty()) {
    input.hoverTargetKey = hit.hit->interaction->stableTargetKey_;
  } else {
    input.hoverTargetKey.reset();
  }
  std::vector<RuntimeTargetSnapshot> nextChain = hit.hoverChain;
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
    Reactive::SmallFn<void()> exit = input.hoverChain[i - 1].onPointerExit;
    if (exit) {
      exit();
    }
  }

  for (std::size_t i = common; i < nextChain.size(); ++i) {
    setHoverActive(nextChain[i], true);
    Reactive::SmallFn<void()> enter = nextChain[i].onPointerEnter;
    if (enter) {
      enter();
    }
  }

  input.hoverChain = std::move(nextChain);
}

void updateHoverForPoint(RuntimeInputState& input, Window& window, Point point) {
  PointerHitSnapshot const hit = pointerHitSnapshot(window, point);
  updateHoverForHit(input, point, hit);
}

void updateCursorForPoint(RuntimeInputState& input, Window& window, Point point) {
  PointerHitSnapshot const hit = pointerHitSnapshot(window, point);
  updateCursorForHit(input, window, hit);
}

void setFocus(RuntimeInputState& input, std::optional<HitTarget> hit, bool notify = true,
              FocusInputKind kind = FocusInputKind::Pointer) {
  if (input.focusTarget && hit && sameTarget(*input.focusTarget, *hit)) {
    input.focusKind = kind;
    setFocusActive(*input.focusTarget, true, kind);
    return;
  }

  Reactive::SmallFn<void()> blur;
  if (notify && input.focusTarget) {
    blur = input.focusTarget->onBlur;
  }
  if (input.focusTarget) {
    setFocusActive(*input.focusTarget, false);
  }
  input.focusTarget = hit ? std::optional<RuntimeTargetSnapshot>{snapshot(*hit)}
                         : std::nullopt;
  input.focusKind = kind;
  Reactive::SmallFn<void()> focus;
  if (notify && input.focusTarget) {
    focus = input.focusTarget->onFocus;
  }
  if (input.focusTarget) {
    setFocusActive(*input.focusTarget, true, kind);
  }
  if (blur) {
    blur();
  }
  if (focus) {
    focus();
  }
}

void cycleKeyboardFocus(RuntimeInputState& input, Window& window, bool reverse) {
  std::vector<HitTarget> targets = focusableTargets(window);
  if (targets.empty()) {
    setFocus(input, std::nullopt, true, FocusInputKind::Keyboard);
    return;
  }

  std::size_t nextIndex = reverse ? targets.size() - 1 : 0;
  if (input.focusTarget) {
    for (std::size_t i = 0; i < targets.size(); ++i) {
      if (sameTarget(*input.focusTarget, targets[i])) {
        nextIndex = reverse ? (i == 0 ? targets.size() - 1 : i - 1)
                            : (i + 1) % targets.size();
        break;
      }
    }
  }

  setFocus(input, targets[nextIndex], true, FocusInputKind::Keyboard);
}

void cycleKeyboardFocusFromKey(RuntimeInputState& input, Window& window,
                               ComponentKey const& fromKey, bool reverse) {
  std::vector<HitTarget> targets = focusableTargets(window);
  if (targets.empty()) {
    setFocus(input, std::nullopt, true, FocusInputKind::Keyboard);
    return;
  }

  if (!fromKey.empty()) {
    for (std::size_t i = 0; i < targets.size(); ++i) {
      if (targets[i].interaction &&
          targets[i].interaction->stableTargetKey_ == fromKey) {
        std::size_t const nextIndex =
            reverse ? (i == 0 ? targets.size() - 1 : i - 1)
                    : (i + 1) % targets.size();
        setFocus(input, targets[nextIndex], true, FocusInputKind::Keyboard);
        return;
      }
    }
  }

  cycleKeyboardFocus(input, window, reverse);
}

void focusSingleTargetOnWindowFocus(RuntimeInputState& input, Window& window) {
  if (input.focusTarget) {
    return;
  }

  std::vector<HitTarget> targets = focusableTargets(window);
  if (targets.size() == 1) {
    setFocus(input, targets.front(), true, FocusInputKind::Keyboard);
  }
}

std::optional<HitTarget> focusableTargetForExactKey(Window& window, ComponentKey const& key) {
  if (key.empty()) {
    return std::nullopt;
  }
  std::vector<HitTarget> targets = focusableTargets(window);
  for (HitTarget const& target : targets) {
    if (target.interaction && target.interaction->stableTargetKey_ == key) {
      return target;
    }
  }
  return std::nullopt;
}

ComponentKey focusedActionKey(RuntimeInputState const& input) {
  return input.focusTarget ? input.focusTarget->stableTargetKey : ComponentKey{};
}

void clearPointerTransientTargets(RuntimeInputState& input,
                                  Window& window,
                                  bool notifyHoverExit,
                                  bool clearTapState) {
  for (RuntimeTargetSnapshot const& target : input.hoverChain) {
    setHoverActive(target, false);
  }
  if (notifyHoverExit) {
    for (auto it = input.hoverChain.rbegin(); it != input.hoverChain.rend(); ++it) {
      RuntimeTargetSnapshot const& target = *it;
      Reactive::SmallFn<void()> exit = target.onPointerExit;
      if (exit) {
        exit();
      }
    }
  }
  if (input.pressTarget) {
    setPressActive(*input.pressTarget, false);
  }
  input.hoverChain.clear();
  input.pressTarget.reset();
  input.hoverAnchor.reset();
  input.hoverTargetKey.reset();
  if (clearTapState) {
    input.lastTapAnchor.reset();
    input.lastTapSerial = 0;
    input.lastTapTargetKey.reset();
  }
  input.pressCancelled = false;
  applyCursor(input, window, Cursor::Arrow);
}

void resetTransientTargets(RuntimeInputState& input, Window& window) {
  clearPointerTransientTargets(input, window, false, true);
  setFocus(input, std::nullopt, false);
}

} // namespace

bool Runtime::requestFocus(ComponentKey const& key) {
  if (key.empty()) {
    return false;
  }

  std::vector<HitTarget> targets = focusableTargets(d->window);
  for (HitTarget const& target : targets) {
    if (target.interaction &&
        target.interaction->stableTargetKey_.hasPrefix(key)) {
      setFocus(d->input, target, true, FocusInputKind::Keyboard);
      return true;
    }
  }
  return false;
}

void Runtime::requestFocusAfterLayout(ComponentKey key) {
  if (key.empty()) {
    return;
  }

  if (Application::hasInstance()) {
    Application::instance().eventQueue().post(RuntimeFocusRequestEvent{
        .handle = d->window.handle(),
        .key = std::move(key),
    });
    return;
  }

  requestFocus(key);
}

void Runtime::handleInput(InputEvent const& event) {
  if (Application::hasInstance() &&
      Application::instance().isWindowInputBlockedByModal(d->window.handle())) {
    return;
  }

  Runtime* previousRuntime = current_;
  current_ = this;
  struct RestoreCurrentRuntime {
    Runtime*& slot;
    Runtime* previous;
    ~RestoreCurrentRuntime() {
      slot = previous;
    }
  } restoreCurrentRuntime{current_, previousRuntime};

  Point const point = eventPoint(event);

  if (event.kind == InputEvent::Kind::KeyDown && event.key == keys::Escape) {
    OverlayEntry const* top = d->window.overlayManager().top();
    if (top && top->config.dismissOnEscape) {
      d->window.removeOverlay(top->id);
      return;
    }
  }

  if (event.kind == InputEvent::Kind::KeyDown && event.key == keys::Tab) {
    bool const reverse = any(event.modifiers & Modifiers::Shift);
    std::optional<OverlayId> overlayId;
    std::optional<ComponentKey> anchorKey;
    if (d->input.focusTarget && d->input.focusTarget->onKeyDown) {
      if (d->input.focusTarget->inOverlay) {
        overlayId = d->input.focusTarget->overlay;
        if (OverlayEntry const* entry = d->window.overlayManager().find(*overlayId)) {
          anchorKey = entry->config.anchorTrackComponentKey;
        }
      } else if (OverlayEntry const* top = d->window.overlayManager().top();
                 top && top->config.anchorTrackComponentKey &&
                 *top->config.anchorTrackComponentKey == d->input.focusTarget->stableTargetKey) {
        overlayId = top->id;
        anchorKey = top->config.anchorTrackComponentKey;
      }
      if (overlayId) {
        d->input.focusTarget->onKeyDown(event.key, event.modifiers);
      }
    }
    if (overlayId && anchorKey && !d->window.overlayManager().find(*overlayId)) {
      cycleKeyboardFocusFromKey(d->input, d->window, *anchorKey, reverse);
    } else {
      cycleKeyboardFocus(d->input, d->window, reverse);
    }
    return;
  }

  bool const menuOwnsShortcut = event.kind == InputEvent::Kind::KeyDown &&
                                Application::hasInstance() &&
                                Application::instance().isCommandShortcutClaimed(event.key, event.modifiers);
  if (menuOwnsShortcut) {
    Application::instance().dispatchCommandForShortcut(event.key, event.modifiers);
    return;
  }
  if (event.kind == InputEvent::Kind::KeyDown && !menuOwnsShortcut) {
    auto const& descriptors = Application::hasInstance()
        ? Application::instance().commandDescriptors()
        : d->window.commandDescriptors();
    if (d->commands.dispatchShortcut(focusedActionKey(d->input), event.key, event.modifiers,
                                     descriptors)) {
      return;
    }
  }

  if (event.kind == InputEvent::Kind::KeyDown ||
      event.kind == InputEvent::Kind::KeyUp ||
      event.kind == InputEvent::Kind::TextInput) {
    if (d->input.focusTarget) {
      switch (event.kind) {
      case InputEvent::Kind::KeyDown:
        if (d->input.focusTarget->onKeyDown) {
          d->input.focusTarget->onKeyDown(event.key, event.modifiers);
        }
        return;
      case InputEvent::Kind::KeyUp:
        if (d->input.focusTarget->onKeyUp) {
          d->input.focusTarget->onKeyUp(event.key, event.modifiers);
        }
        return;
      case InputEvent::Kind::TextInput:
        if (d->input.focusTarget->onTextInput) {
          d->input.focusTarget->onTextInput(event.text);
        }
        return;
      default:
        break;
      }
    }

    std::optional<HitTarget> hit = hitWindow(d->window, point);
    if (!hit || !hit->interaction) {
      return;
    }
    InteractionData const& interaction = *hit->interaction;
    switch (event.kind) {
    case InputEvent::Kind::KeyDown:
      if (interaction.onKeyDown) {
        interaction.onKeyDown(event.key, event.modifiers);
      }
      break;
    case InputEvent::Kind::KeyUp:
      if (interaction.onKeyUp) {
        interaction.onKeyUp(event.key, event.modifiers);
      }
      break;
    case InputEvent::Kind::TextInput:
      if (interaction.onTextInput) {
        interaction.onTextInput(event.text);
      }
      break;
    default:
      break;
    }
    return;
  }

  switch (event.kind) {
  case InputEvent::Kind::PointerEnter: {
    PointerHitSnapshot const hit = pointerHitSnapshot(d->window, point);
    updateHoverForHit(d->input, point, hit);
    updateCursorForHit(d->input, d->window, hit);
    break;
  }
  case InputEvent::Kind::PointerLeave:
    clearPointerTransientTargets(d->input, d->window, true, false);
    break;
  case InputEvent::Kind::PointerDown:
    if (d->input.pressTarget) {
      setPressActive(*d->input.pressTarget, false);
    }
    d->input.pressTarget.reset();
    d->input.pressCancelled = false;
    d->input.pressPoint = point;
    if (auto hit = hitWindow(d->window, point)) {
      d->input.lastTapAnchor = windowRectForHit(*hit, point);
      d->input.lastTapSerial = event.platformSerial;
      if (hit->interaction && !hit->interaction->stableTargetKey_.empty()) {
        d->input.lastTapTargetKey = hit->interaction->stableTargetKey_;
      } else {
        d->input.lastTapTargetKey.reset();
      }
      RuntimeTargetSnapshot target = snapshot(*hit);
      if (target.focusable) {
        setFocus(d->input, hit, true, FocusInputKind::Pointer);
      } else {
        setFocus(d->input, std::nullopt);
      }
      d->input.pressTarget = target;
      setPressActive(target, true);
      if (event.button == MouseButton::Left && target.windowResizeEdge != WindowResizeEdge::None) {
        setPressActive(target, false);
        d->input.pressTarget.reset();
        d->input.pressCancelled = false;
        d->window.beginWindowResize(target.windowResizeEdge, event);
        return;
      }
      if (event.button == MouseButton::Left && target.windowDragRegion) {
        setPressActive(target, false);
        d->input.pressTarget.reset();
        d->input.pressCancelled = false;
        d->window.beginWindowDrag(event);
        return;
      }
      if (target.onPointerDown) {
        target.onPointerDown(hit->localPoint, event.button);
      }
    } else {
      d->input.lastTapAnchor.reset();
      d->input.lastTapSerial = 0;
      d->input.lastTapTargetKey.reset();
      setFocus(d->input, std::nullopt);
    }
    updateHoverForPoint(d->input, d->window, point);
    updateCursorForPoint(d->input, d->window, point);
    break;
  case InputEvent::Kind::PointerMove: {
    PointerHitSnapshot const hit = pointerHitSnapshot(d->window, point);
    bool hitMayBeStale = false;
    if (d->input.pressTarget) {
      float const dx = point.x - d->input.pressPoint.x;
      float const dy = point.y - d->input.pressPoint.y;
      constexpr float kTapSlop = 8.f;
      if (dx * dx + dy * dy > kTapSlop * kTapSlop) {
        d->input.pressCancelled = true;
      }
      if (d->input.pressTarget->onPointerMove) {
        RuntimeTargetSnapshot target = *d->input.pressTarget;
        Point const local = localPointForTarget(target, d->window, point).value_or(point);
        target.onPointerMove(local);
        hitMayBeStale = true;
      }
    } else if (hit.hit) {
      if (hit.hit->interaction && hit.hit->interaction->onPointerMove) {
        auto onPointerMove = hit.hit->interaction->onPointerMove;
        Point const localPoint = hit.hit->localPoint;
        onPointerMove(localPoint);
        hitMayBeStale = true;
      }
    }
    if (hitMayBeStale) {
      updateHoverForPoint(d->input, d->window, point);
      updateCursorForPoint(d->input, d->window, point);
    } else {
      updateHoverForHit(d->input, point, hit);
      updateCursorForHit(d->input, d->window, hit);
    }
    break;
  }
  case InputEvent::Kind::PointerUp:
    if (d->input.pressTarget) {
      RuntimeTargetSnapshot released = *d->input.pressTarget;
      bool const cancelled = d->input.pressCancelled;
      d->input.pressTarget.reset();
      d->input.pressCancelled = false;
      setPressActive(released, false);
      if (released.onPointerUp) {
        Point const local = localPointForTarget(released, d->window, point).value_or(point);
        released.onPointerUp(local, event.button);
      }
      if (!cancelled && released.onTap) {
        released.onTap(event.button);
      }
      if (!cancelled && released.onTapWithModifiers) {
        released.onTapWithModifiers(event.button, event.modifiers);
      }
    } else if (auto hit = hitWindow(d->window, point)) {
      if (hit->interaction) {
        // Copy handlers before dispatch: a handler may unmount the hit node,
        // destroying the interaction (and the running closure) mid-call.
        auto onPointerUp = hit->interaction->onPointerUp;
        auto onTap = hit->interaction->onTap;
        auto onTapWithModifiers = hit->interaction->onTapWithModifiers;
        Point const localPoint = hit->localPoint;
        if (onPointerUp) {
          onPointerUp(localPoint, event.button);
        }
        if (onTap) {
          onTap(event.button);
        }
        if (onTapWithModifiers) {
          onTapWithModifiers(event.button, event.modifiers);
        }
      }
    }
    updateHoverForPoint(d->input, d->window, point);
    updateCursorForPoint(d->input, d->window, point);
    break;
  case InputEvent::Kind::Scroll: {
    if (auto hit = hitWindow(d->window, point, [](scenegraph::Interaction const& interaction) {
          return static_cast<bool>(interactionData(interaction).onScroll);
        })) {
      Vec2 delta = event.scrollDelta;
      if (!event.preciseScrollDelta) {
        constexpr float kLineHeight = 40.f;
        delta.x *= kLineHeight;
        delta.y *= kLineHeight;
      }
      if (hit->interaction && hit->interaction->onScroll) {
        hit->interaction->onScroll(delta);
      }
    }
    break;
  }
  case InputEvent::Kind::KeyDown:
  case InputEvent::Kind::KeyUp:
  case InputEvent::Kind::TextInput:
    break;
  case InputEvent::Kind::TouchBegin:
  case InputEvent::Kind::TouchMove:
  case InputEvent::Kind::TouchEnd:
    break;
  }
}

void Runtime::handleWindowEvent(WindowEvent const& event) {
  d->window.refreshChromeMetrics();
  switch (event.kind) {
  case WindowEvent::Kind::Resize:
    if (d->root && d->window.hasSceneGraph()) {
      bool const traceResize = detail::resizeTraceEnabled();
      auto const resizeStart = traceResize ? std::chrono::steady_clock::now()
                                           : std::chrono::steady_clock::time_point{};
      std::optional<RuntimeTargetSnapshot> previousFocus = d->input.focusTarget;
      ComponentKey const previousFocusKey =
          previousFocus ? previousFocus->stableTargetKey : ComponentKey{};
      FocusInputKind const previousFocusKind = d->input.focusKind;
      clearPointerTransientTargets(d->input, d->window, false, true);
      d->input.focusTarget.reset();
      Runtime* previous = current_;
      current_ = this;
      d->root->resize(event.size, d->window.sceneGraph());
      current_ = previous;
      d->window.overlayManager().rebuild(event.size, *this);
      if (!previousFocusKey.empty()) {
        if (auto restored = focusableTargetForExactKey(d->window, previousFocusKey)) {
          d->input.focusTarget = snapshot(*restored);
          d->input.focusKind = previousFocusKind;
          setFocusActive(*d->input.focusTarget, true, previousFocusKind);
        } else if (previousFocus) {
          setFocusActive(*previousFocus, false);
          Reactive::SmallFn<void()> blur = previousFocus->onBlur;
          if (blur) {
            blur();
          }
        }
      } else if (previousFocus) {
        setFocusActive(*previousFocus, false);
        Reactive::SmallFn<void()> blur = previousFocus->onBlur;
        if (blur) {
          blur();
        }
      }
      d->window.requestRedraw();
      if (traceResize) {
        auto const elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - resizeStart).count();
        LAMBDA_RESIZE_TRACE("runtime",
                            "resize window=%u size=%.0fx%.0f elapsed=%.3fms\n",
                            d->window.handle(),
                            event.size.width,
                            event.size.height,
                            static_cast<double>(elapsed) / 1000.0);
      }
    }
    break;
  case WindowEvent::Kind::FocusLost:
    resetTransientTargets(d->input, d->window);
    break;
  case WindowEvent::Kind::FocusGained:
    focusSingleTargetOnWindowFocus(d->input, d->window);
    break;
  case WindowEvent::Kind::DpiChanged:
    if (invalidateRenderCaches(d->window)) {
      d->window.requestRedraw();
    }
    break;
  case WindowEvent::Kind::CloseRequest:
    break;
  }
}

} // namespace lambdaui
