#pragma once

/// \file Lambda/UI/Hooks.hpp
///
/// Scope-owned v5 hooks and environment accessors.

#include <Lambda/Reactive/Animation.hpp>
#include <Lambda/Reactive/Computed.hpp>
#include <Lambda/Reactive/Effect.hpp>
#include <Lambda/Reactive/Signal.hpp>
#include <Lambda/Core/Identity.hpp>
#include <Lambda/UI/Window.hpp>
#include <Lambda/UI/Detail/Runtime.hpp>
#include <Lambda/UI/Environment.hpp>
#include <Lambda/UI/EnvironmentKeys.hpp>
#include <Lambda/Layout/LayoutEngine.hpp>
#include <Lambda/UI/MeasureContext.hpp>
#include <Lambda/UI/MountContext.hpp>
#include <Lambda/UI/Theme.hpp>

#include <cmath>
#include <cassert>
#include <cstdint>
#include <functional>
#include <optional>
#include <source_location>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace lambdaui {

namespace detail {

struct InteractionSignalBundle {
  Reactive::Signal<bool> hover;
  Reactive::Signal<bool> press;
  Reactive::Signal<bool> focus;
  Reactive::Signal<bool> keyboardFocus;
};

struct OwnerInteractionSignals {
  InteractionSignalBundle signals;
  bool cleanupRegistered = false;
};

struct OwnerInteractionScopeKey {
  ComponentKey key;
  bool cleanupRegistered = false;
};

#ifndef NDEBUG
struct UseStateDebugKey {
  char const* file = nullptr;
  std::uint_least32_t line = 0;
  std::uint_least32_t column = 0;

  bool operator==(UseStateDebugKey const&) const = default;
};

struct UseStateDebugKeyHash {
  std::size_t operator()(UseStateDebugKey const& key) const noexcept {
    std::size_t seed = std::hash<char const*>{}(key.file);
    seed ^= std::hash<std::uint_least32_t>{}(key.line) + 0x9e3779b9u + (seed << 6) + (seed >> 2);
    seed ^= std::hash<std::uint_least32_t>{}(key.column) + 0x9e3779b9u + (seed << 6) + (seed >> 2);
    return seed;
  }
};

struct OwnerUseStateDebugInfo {
  std::unordered_set<UseStateDebugKey, UseStateDebugKeyHash> callSites;
  bool cleanupRegistered = false;
};
#endif

inline std::unordered_map<Reactive::detail::ScopeState*, OwnerInteractionSignals>&
ownerInteractionSignals() {
  static thread_local std::unordered_map<Reactive::detail::ScopeState*, OwnerInteractionSignals> signals;
  return signals;
}

inline std::unordered_map<Reactive::detail::ScopeState*, OwnerInteractionScopeKey>&
ownerInteractionScopeKeys() {
  static thread_local std::unordered_map<Reactive::detail::ScopeState*, OwnerInteractionScopeKey> keys;
  return keys;
}

#ifndef NDEBUG
inline std::unordered_map<Reactive::detail::ScopeState*, OwnerUseStateDebugInfo>&
ownerUseStateDebugInfo() {
  static thread_local std::unordered_map<Reactive::detail::ScopeState*, OwnerUseStateDebugInfo> info;
  return info;
}
#endif

inline std::vector<InteractionSignalBundle const*>& interactionSignalMountStack() {
  static thread_local std::vector<InteractionSignalBundle const*> stack;
  return stack;
}

inline std::vector<ComponentKey const*>& interactionScopeKeyMountStack() {
  static thread_local std::vector<ComponentKey const*> stack;
  return stack;
}

inline InteractionSignalBundle const* currentInteractionSignals() {
  auto& stack = interactionSignalMountStack();
  return stack.empty() ? nullptr : stack.back();
}

inline std::vector<InteractionSignalBundle const*> currentInteractionSignalChain() {
  std::vector<InteractionSignalBundle const*> chain;
  for (InteractionSignalBundle const* signals : interactionSignalMountStack()) {
    if (signals) {
      chain.push_back(signals);
    }
  }
  return chain;
}

inline ComponentKey const* currentInteractionScopeKey() {
  auto& stack = interactionScopeKeyMountStack();
  return stack.empty() ? nullptr : stack.back();
}

inline std::optional<ComponentKey> currentInteractionScopeKeyCopy() {
  if (ComponentKey const* key = currentInteractionScopeKey()) {
    return *key;
  }
  return std::nullopt;
}

inline void appendComponentKey(ComponentKey& key, ComponentKey const& suffix) {
  for (LocalId const id : suffix.materialize()) {
    key.push_back(id);
  }
}

inline ComponentKey makeMountedInteractionScopeKey(Reactive::detail::ScopeState* owner) {
  ComponentKey const localKey = ComponentKey::fromScope(owner);
  if (ComponentKey const* parentKey = currentInteractionScopeKey();
      parentKey && !parentKey->empty()) {
    ComponentKey key = *parentKey;
    appendComponentKey(key, localKey);
    return key;
  }
  return localKey;
}

inline ComponentKey interactionScopeKeyForOwner(Reactive::detail::ScopeState* owner) {
  if (!owner) {
    return {};
  }
  auto const it = ownerInteractionScopeKeys().find(owner);
  if (it != ownerInteractionScopeKeys().end()) {
    return it->second.key;
  }
  return ComponentKey::fromScope(owner);
}

class ScopedInteractionScopeKey {
public:
  explicit ScopedInteractionScopeKey(ComponentKey const* key)
      : pushed_(key && !key->empty()) {
    if (pushed_) {
      interactionScopeKeyMountStack().push_back(key);
    }
  }

  explicit ScopedInteractionScopeKey(std::optional<ComponentKey> const& key)
      : ScopedInteractionScopeKey(key ? &*key : nullptr) {}

  ScopedInteractionScopeKey(ScopedInteractionScopeKey const&) = delete;
  ScopedInteractionScopeKey& operator=(ScopedInteractionScopeKey const&) = delete;

  ~ScopedInteractionScopeKey() {
    if (pushed_) {
      auto& stack = interactionScopeKeyMountStack();
      if (!stack.empty()) {
        stack.pop_back();
      }
    }
  }

private:
  bool pushed_ = false;
};

class HookInteractionSignalScope {
public:
  explicit HookInteractionSignalScope(Reactive::Scope& owner)
      : scopeKey_(makeMountedInteractionScopeKey(owner.state())) {
    auto& keyEntry = ownerInteractionScopeKeys()[owner.state()];
    keyEntry.key = scopeKey_;
    if (!keyEntry.cleanupRegistered) {
      keyEntry.cleanupRegistered = true;
      Reactive::detail::ScopeState* ownerState = owner.state();
      owner.onCleanup([ownerState] {
        ownerInteractionScopeKeys().erase(ownerState);
      });
    }
    auto const it = ownerInteractionSignals().find(owner.state());
    if (it != ownerInteractionSignals().end()) {
      signals_ = &it->second.signals;
    }
    interactionSignalMountStack().push_back(signals_);
    interactionScopeKeyMountStack().push_back(&scopeKey_);
  }

  HookInteractionSignalScope(HookInteractionSignalScope const&) = delete;
  HookInteractionSignalScope& operator=(HookInteractionSignalScope const&) = delete;

  ~HookInteractionSignalScope() {
    auto& signalStack = interactionSignalMountStack();
    if (!signalStack.empty()) {
      signalStack.pop_back();
    }
    auto& keyStack = interactionScopeKeyMountStack();
    if (!keyStack.empty()) {
      keyStack.pop_back();
    }
  }

private:
  ComponentKey scopeKey_;
  InteractionSignalBundle const* signals_ = nullptr;
};

inline std::vector<LayoutConstraints>& hookLayoutConstraintStack() {
  static thread_local std::vector<LayoutConstraints> stack;
  return stack;
}

class HookLayoutScope {
public:
  explicit HookLayoutScope(LayoutConstraints constraints) {
    hookLayoutConstraintStack().push_back(constraints);
  }

  HookLayoutScope(HookLayoutScope const&) = delete;
  HookLayoutScope& operator=(HookLayoutScope const&) = delete;

  ~HookLayoutScope() {
    auto& stack = hookLayoutConstraintStack();
    if (!stack.empty()) {
      stack.pop_back();
    }
  }
};

inline LayoutConstraints const* currentHookLayoutConstraints() {
  auto& stack = hookLayoutConstraintStack();
  return stack.empty() ? nullptr : &stack.back();
}

enum class InteractionSignalKind {
  Hover,
  Press,
  Focus,
  KeyboardFocus,
};

inline Reactive::Signal<bool>& signalSlot(InteractionSignalBundle& signals,
                                          InteractionSignalKind kind) {
  switch (kind) {
  case InteractionSignalKind::Hover:
    return signals.hover;
  case InteractionSignalKind::Press:
    return signals.press;
  case InteractionSignalKind::Focus:
    return signals.focus;
  case InteractionSignalKind::KeyboardFocus:
    return signals.keyboardFocus;
  }
  return signals.hover;
}

inline Reactive::Signal<bool> useInteractionSignal(InteractionSignalKind kind) {
  Reactive::detail::ScopeState* owner = Reactive::detail::sCurrentOwner;
  if (!owner) {
    return Reactive::Signal<bool>{false};
  }

  auto& entry = ownerInteractionSignals()[owner];
  if (!entry.cleanupRegistered) {
    entry.cleanupRegistered = true;
    Reactive::onCleanup([owner] {
      ownerInteractionSignals().erase(owner);
    });
  }
  return signalSlot(entry.signals, kind);
}

#ifndef NDEBUG
inline void debugRegisterUseState(std::source_location location) {
  Reactive::detail::ScopeState* owner = Reactive::detail::sCurrentOwner;
  if (!owner) {
    return;
  }

  auto& entry = ownerUseStateDebugInfo()[owner];
  UseStateDebugKey const key{
      .file = location.file_name(),
      .line = location.line(),
      .column = location.column(),
  };
  bool const inserted = entry.callSites.insert(key).second;
  assert(inserted && "useState call site was evaluated more than once in the same mount scope");
  if (!entry.cleanupRegistered) {
    entry.cleanupRegistered = true;
    Reactive::onCleanup([owner] {
      ownerUseStateDebugInfo().erase(owner);
    });
  }
}
#endif

} // namespace detail

template<typename Key>
Reactive::Signal<typename EnvironmentKey<Key>::Value> useEnvironment() {
  if (MountContext* mount = detail::currentMountContext()) {
    if (auto signal = mount->environmentBinding().signal<Key>()) {
      return *signal;
    }
    return Reactive::Signal<typename EnvironmentKey<Key>::Value>{
        mount->environmentBinding().value<Key>()};
  }
  if (MeasureContext* measure = detail::currentMeasureContext()) {
    if (auto signal = measure->environmentBinding().signal<Key>()) {
      return *signal;
    }
    return Reactive::Signal<typename EnvironmentKey<Key>::Value>{
        measure->environmentBinding().value<Key>()};
  }
  return Reactive::Signal<typename EnvironmentKey<Key>::Value>{
      EnvironmentKey<Key>::defaultValue()};
}

/// Each `useState` call allocates a fresh `Signal<T>` owned by the current reactive scope.
/// The v5 retained UI model guarantees `body()` runs exactly once for a mount cycle; therefore
/// each `useState` call site allocates one signal for that cycle. Re-entering the same body and
/// evaluating the same call site twice is a framework error and is asserted in debug builds.
template<typename T>
Reactive::Signal<T> useState(T initial = T{},
                             std::source_location location = std::source_location::current()) {
#ifndef NDEBUG
  detail::debugRegisterUseState(location);
#else
  (void)location;
#endif
  return Reactive::Signal<T>(std::move(initial));
}

template<typename Fn>
auto useComputed(Fn&& fn) {
  return Reactive::makeComputed(std::forward<Fn>(fn));
}

template<typename Fn>
void useEffect(Fn&& fn) {
  Reactive::Effect{std::forward<Fn>(fn)};
}

template<Interpolatable T>
Animated<T> useAnimated(T initial = T{}) {
  return Animated<T>(std::move(initial));
}

template<Interpolatable T>
Animated<T> useAnimated(T initial, AnimationOptions options) {
  Animated<T> animation{std::move(initial)};
  animation.play(animation.get(), std::move(options));
  return animation;
}

namespace detail {

inline bool animationTargetChanged(float current, float next) {
  return std::abs(current - next) > 0.001f;
}

template<typename T>
bool animationTargetChanged(T const& current, T const& next) {
  return !(current == next);
}

} // namespace detail

template<Interpolatable T, typename TargetFn, typename TransitionFn>
Animated<T> useAnimated(T initial, TargetFn&& target, TransitionFn&& transition) {
  Animated<T> animation = useAnimated<T>(std::move(initial));
  useEffect([animation,
             target = std::forward<TargetFn>(target),
             transition = std::forward<TransitionFn>(transition)]() mutable {
    T next = target();
    if (detail::animationTargetChanged(animation.peek(), next)) {
      animation.set(std::move(next), transition());
    }
  });
  return animation;
}

template<Interpolatable T, typename TargetFn>
Animated<T> useAnimated(T initial, TargetFn&& target, Transition transition) {
  return useAnimated<T>(
      std::move(initial),
      std::forward<TargetFn>(target),
      [transition] {
        return transition;
      });
}

template<typename TargetFn, typename TransitionFn>
  requires std::is_invocable_v<TargetFn&> &&
           Interpolatable<std::remove_cvref_t<std::invoke_result_t<TargetFn&>>> &&
           std::is_invocable_r_v<Transition, TransitionFn&>
Animated<std::remove_cvref_t<std::invoke_result_t<TargetFn&>>>
useAnimated(TargetFn&& target, TransitionFn&& transition) {
  using T = std::remove_cvref_t<std::invoke_result_t<TargetFn&>>;
  std::decay_t<TargetFn> targetFn{std::forward<TargetFn>(target)};
  T initial = targetFn();
  return useAnimated<T>(
      std::move(initial),
      std::move(targetFn),
      std::forward<TransitionFn>(transition));
}

template<typename TargetFn>
  requires std::is_invocable_v<TargetFn&> &&
           Interpolatable<std::remove_cvref_t<std::invoke_result_t<TargetFn&>>>
Animated<std::remove_cvref_t<std::invoke_result_t<TargetFn&>>>
useAnimated(TargetFn&& target, Transition transition) {
  using T = std::remove_cvref_t<std::invoke_result_t<TargetFn&>>;
  std::decay_t<TargetFn> targetFn{std::forward<TargetFn>(target)};
  T initial = targetFn();
  return useAnimated<T>(
      std::move(initial),
      std::move(targetFn),
      std::move(transition));
}

/// Scope-owned frame callback for custom per-frame work that is not a single interpolated value.
/// Prefer \ref useAnimated for normal control transitions.
template<typename Fn>
void useFrame(Fn&& callback) {
  using Callback = std::decay_t<Fn>;
  if constexpr (std::is_invocable_v<Callback&, AnimationTick const&>) {
    using Result = std::invoke_result_t<Callback&, AnimationTick const&>;
    static_assert(std::is_void_v<Result> || std::is_same_v<Result, FrameAction>,
                  "useFrame callback must return void or FrameAction");
    Callback storedCallback{std::forward<Fn>(callback)};
    ObserverHandle const handle =
        AnimationClock::instance().subscribe(
            Reactive::SmallFn<FrameAction(AnimationTick const&)>{
                [callback = std::move(storedCallback)](AnimationTick const& tick) mutable -> FrameAction {
                  if constexpr (std::is_void_v<Result>) {
                    callback(tick);
                    return FrameAction::Continue;
                  } else {
                    return callback(tick);
                  }
                }});
    Reactive::onCleanup([handle] {
      AnimationClock::instance().unsubscribe(handle);
    });
  } else {
    static_assert(std::is_invocable_v<Callback&, AnimationTick const&>,
                  "useFrame callback must accept AnimationTick const&");
  }
}

inline Reactive::Signal<bool> useFocus() {
  return detail::useInteractionSignal(detail::InteractionSignalKind::Focus);
}

inline Reactive::Signal<bool> useKeyboardFocus() {
  return detail::useInteractionSignal(detail::InteractionSignalKind::KeyboardFocus);
}

inline Reactive::Signal<bool> useHover() {
  return detail::useInteractionSignal(detail::InteractionSignalKind::Hover);
}

inline Reactive::Signal<bool> usePress() {
  return detail::useInteractionSignal(detail::InteractionSignalKind::Press);
}

inline void useAutoFocus(Reactive::Signal<int> generation) {
  Runtime* runtime = Runtime::current();
  Reactive::detail::ScopeState* scope = Reactive::detail::sCurrentOwner;
  if (!runtime || !scope) {
    return;
  }

  ComponentKey const fallbackKey = detail::interactionScopeKeyForOwner(scope);
  Reactive::Signal<int> lastFocusedGeneration = useState(-1);
  useEffect([runtime, scope, fallbackKey, generation, lastFocusedGeneration] {
    int const currentGeneration = generation.get();
    if (lastFocusedGeneration.peek() == currentGeneration) {
      return;
    }
    ComponentKey key = detail::interactionScopeKeyForOwner(scope);
    if (key.empty()) {
      key = fallbackKey;
    }
    runtime->requestFocusAfterLayout(key);
    lastFocusedGeneration.set(currentGeneration);
  });
}

inline LayoutConstraints const* useLayoutConstraints() {
  return detail::currentHookLayoutConstraints();
}

inline Rect useBounds() {
  LayoutConstraints const* constraints = useLayoutConstraints();
  if (!constraints) {
    return {};
  }

  Rect bounds{};
  if (std::isfinite(constraints->maxWidth) && constraints->maxWidth > 0.f) {
    bounds.width = constraints->maxWidth;
  }
  if (std::isfinite(constraints->maxHeight) && constraints->maxHeight > 0.f) {
    bounds.height = constraints->maxHeight;
  }
  return bounds;
}

inline void useViewCommand(std::string const& name, std::function<void()> handler,
                           std::function<bool()> isEnabled = {}) {
  Runtime* runtime = Runtime::current();
  if (!runtime) {
    return;
  }
  Reactive::detail::ScopeState* scope = Reactive::detail::sCurrentOwner;
  ComponentKey const key = detail::interactionScopeKeyForOwner(scope);
  CommandId const id = runtime->commandRegistry().registerViewHandler(
      key, name, std::move(handler), std::move(isEnabled));
  Reactive::onCleanup([runtime, id] {
    runtime->commandRegistry().unregister(id);
  });
}

inline void useViewAction(std::string const& name, std::function<void()> handler,
                          std::function<bool()> isEnabled = {}) {
  useViewCommand(name, std::move(handler), std::move(isEnabled));
}

inline void useWindowCommand(std::string const& name, std::function<void()> handler,
                             std::function<bool()> isEnabled = {}) {
  Runtime* runtime = Runtime::current();
  if (!runtime) {
    return;
  }
  CommandId const id = runtime->commandRegistry().registerWindowHandler(
      name, std::move(handler), std::move(isEnabled));
  Reactive::onCleanup([runtime, id] {
    runtime->commandRegistry().unregister(id);
  });
}

inline void useWindowAction(std::string const& name, std::function<void()> handler,
                            std::function<bool()> isEnabled = {}) {
  useWindowCommand(name, std::move(handler), std::move(isEnabled));
}

inline void useWindowCommand(std::string const& name, std::function<void()> handler,
                             CommandDescriptor descriptor) {
  Runtime* runtime = Runtime::current();
  if (!runtime) {
    return;
  }
  std::function<bool()> isEnabled = descriptor.isEnabled;
  descriptor.isEnabled = {};
  runtime->window().registerCommand(name, std::move(descriptor));
  CommandId const id = runtime->commandRegistry().registerWindowHandler(
      name, std::move(handler), std::move(isEnabled));
  Reactive::onCleanup([runtime, id] {
    runtime->commandRegistry().unregister(id);
  });
}

inline void useWindowAction(std::string const& name, std::function<void()> handler,
                            ActionDescriptor descriptor) {
  useWindowCommand(name, std::move(handler), std::move(descriptor));
}

} // namespace lambdaui
