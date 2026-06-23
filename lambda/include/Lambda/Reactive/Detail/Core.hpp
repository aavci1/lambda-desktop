#pragma once

#include <Lambda/Reactive/Profile.hpp>
#include <Lambda/Reactive/SmallFn.hpp>
#include <Lambda/Reactive/Transition.hpp>

#include <atomic>
#include <algorithm>
#include <cassert>
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace lambdaui::Reactive {

struct EffectState;

namespace detail {

enum Flag : std::uint16_t {
  Mutable = 1u << 0u,
  Watching = 1u << 1u,
  Recursed = 1u << 2u,
  Dirty = 1u << 3u,
  Pending = 1u << 4u,
  Disposed = 1u << 5u,
  Running = 1u << 6u,
};

inline bool hasFlag(std::uint16_t flags, Flag flag) {
  return (flags & static_cast<std::uint16_t>(flag)) != 0u;
}

inline void setFlag(std::uint16_t& flags, Flag flag) {
  flags |= static_cast<std::uint16_t>(flag);
}

inline void clearFlag(std::uint16_t& flags, Flag flag) {
  flags &= static_cast<std::uint16_t>(~static_cast<std::uint16_t>(flag));
}

struct Disposable {
  virtual ~Disposable() = default;
  virtual void dispose() = 0;
  virtual bool disposed() const = 0;
};

struct Observable;
struct Computation;

struct Link {
  Observable* source = nullptr;
  Computation* observer = nullptr;
  std::uint64_t sourceVersion = 0;
  std::uint32_t runVersion = 0;
  Link* nextSource = nullptr;
  Link* prevSource = nullptr;
  Link* nextSubscriber = nullptr;
  Link* prevSubscriber = nullptr;
};

inline std::atomic_size_t gLiveLinks = 0;
inline std::atomic_size_t gTotalLinks = 0;

inline Link* allocateLink() {
  gLiveLinks.fetch_add(1, std::memory_order_relaxed);
  gTotalLinks.fetch_add(1, std::memory_order_relaxed);
  return new Link();
}

inline void freeLink(Link* link) {
  if (!link) {
    return;
  }
  gLiveLinks.fetch_sub(1, std::memory_order_relaxed);
  delete link;
}

inline std::size_t debugLiveLinkCount() {
  return gLiveLinks.load(std::memory_order_relaxed);
}

inline std::size_t debugTotalLinkAllocations() {
  return gTotalLinks.load(std::memory_order_relaxed);
}

inline void debugResetLinkAllocationCount() {
  gTotalLinks.store(0, std::memory_order_relaxed);
}

struct ScopeState {
  bool disposed = false;
  std::vector<std::shared_ptr<Disposable>> owned;
  std::vector<SmallFn<void()>> cleanups;

  ~ScopeState() {
    dispose();
  }

  void own(std::shared_ptr<Disposable> disposable) {
    assert(!disposed && "cannot add reactive owner to a disposed scope");
    owned.push_back(std::move(disposable));
  }

  void onCleanup(SmallFn<void()> cleanup) {
    assert(!disposed && "cannot add cleanup to a disposed scope");
    cleanups.push_back(std::move(cleanup));
  }

  void dispose() {
    if (disposed) {
      return;
    }
    disposed = true;

    for (auto it = cleanups.rbegin(); it != cleanups.rend(); ++it) {
      (*it)();
    }
    cleanups.clear();

    for (auto it = owned.rbegin(); it != owned.rend(); ++it) {
      if (*it) {
        (*it)->dispose();
      }
    }
    owned.clear();
  }
};

inline thread_local Computation* sCurrentObserver = nullptr;
inline thread_local ScopeState* sCurrentOwner = nullptr;
inline thread_local int sBatchDepth = 0;
inline thread_local bool sFlushingEffects = false;
inline thread_local std::vector<EffectState*> sEffectQueue;
inline thread_local std::vector<EffectState*> sEffectFlushQueue;

inline void ownNode(std::shared_ptr<Disposable> disposable) {
  if (sCurrentOwner) {
    sCurrentOwner->own(std::move(disposable));
  }
}

struct ObserverContext {
  Computation* previous = nullptr;

  explicit ObserverContext(Computation* next)
      : previous(sCurrentObserver) {
    sCurrentObserver = next;
  }

  ~ObserverContext() {
    sCurrentObserver = previous;
  }
};

struct OwnerContext {
  ScopeState* previous = nullptr;

  explicit OwnerContext(ScopeState* next)
      : previous(sCurrentOwner) {
    sCurrentOwner = next;
  }

  ~OwnerContext() {
    sCurrentOwner = previous;
  }
};

struct Observable : Disposable {
  Link* subscribers = nullptr;
  std::uint64_t version = 0;
  std::uint16_t flags = 0;

  ~Observable() override {
    dispose();
  }

  bool disposed() const override {
    return hasFlag(flags, Disposed);
  }

  void dispose() override;
  virtual bool updateIfNeeded();
  virtual std::uint16_t observerDepthContribution() const;
  void reportRead();
  void subscribe(Computation& observer);
  void propagatePending();
  void propagateDirty();
  void detachSubscribers();
};

struct Computation : Observable {
  Link* sources = nullptr;
  Link* spareLinks = nullptr;
  bool scheduled = false;
  std::uint32_t runVersion = 0;
  std::uint16_t depth = 0;

  ~Computation() override {
    dispose();
    deleteSpareLinks();
  }

  void dispose() override;
  std::uint16_t observerDepthContribution() const override;
  bool pollSourcesChanged();
  bool updateIfNeeded() override = 0;
  void markDirty();
  void markPending();
  void clearSourcesForReuse();
  void beginTrackingRun();
  void sweepStaleSources();
  void deleteSpareLinks();
  Link* findSource(Observable const* source) const;
  Link* acquireLink();
  void retireLink(Link* link);
  virtual void run() = 0;
  virtual void onDirty() = 0;
  virtual void onPending() = 0;
};

inline void unlinkFromSourceList(Link* link) {
  auto* observer = link->observer;
  if (!observer) {
    return;
  }
  if (link->prevSource) {
    link->prevSource->nextSource = link->nextSource;
  } else {
    observer->sources = link->nextSource;
  }
  if (link->nextSource) {
    link->nextSource->prevSource = link->prevSource;
  }
  link->nextSource = nullptr;
  link->prevSource = nullptr;
}

inline void unlinkFromSubscriberList(Link* link) {
  auto* source = link->source;
  if (!source) {
    return;
  }
  if (link->prevSubscriber) {
    link->prevSubscriber->nextSubscriber = link->nextSubscriber;
  } else {
    source->subscribers = link->nextSubscriber;
  }
  if (link->nextSubscriber) {
    link->nextSubscriber->prevSubscriber = link->prevSubscriber;
  }
  link->nextSubscriber = nullptr;
  link->prevSubscriber = nullptr;
}

inline void Observable::dispose() {
  if (disposed()) {
    return;
  }
  setFlag(flags, Disposed);
  detachSubscribers();
}

inline bool Observable::updateIfNeeded() {
  return false;
}

inline std::uint16_t Observable::observerDepthContribution() const {
  return 0;
}

inline void Observable::reportRead() {
  if (!sCurrentObserver || disposed()) {
    return;
  }
  subscribe(*sCurrentObserver);
}

inline void Observable::subscribe(Computation& observer) {
  if (std::uint16_t const sourceDepth = observerDepthContribution()) {
    observer.depth = std::max(observer.depth, sourceDepth);
  }
  if (auto* existing = observer.findSource(this)) {
    existing->sourceVersion = version;
    existing->runVersion = observer.runVersion;
    return;
  }

  auto* link = observer.acquireLink();
  link->source = this;
  link->observer = &observer;
  link->sourceVersion = version;
  link->runVersion = observer.runVersion;

  link->nextSubscriber = subscribers;
  if (subscribers) {
    subscribers->prevSubscriber = link;
  }
  subscribers = link;

  link->nextSource = observer.sources;
  if (observer.sources) {
    observer.sources->prevSource = link;
  }
  observer.sources = link;
}

inline void Observable::propagatePending() {
  [[maybe_unused]] profile::ScopedTimer timer{profile::Bucket::PropagatePending};
  auto* link = subscribers;
  while (link) {
    auto* next = link->nextSubscriber;
    if (link->observer) {
      bool const staleDuringRun =
          hasFlag(link->observer->flags, Running) &&
          link->runVersion != link->observer->runVersion;
      if (!staleDuringRun) {
        link->observer->markPending();
      }
    }
    link = next;
  }
}

inline void Observable::propagateDirty() {
  auto* link = subscribers;
  while (link) {
    auto* next = link->nextSubscriber;
    if (link->observer) {
      bool const staleDuringRun =
          hasFlag(link->observer->flags, Running) &&
          link->runVersion != link->observer->runVersion;
      if (!staleDuringRun) {
        link->observer->markDirty();
      }
    }
    link = next;
  }
}

inline void Observable::detachSubscribers() {
  while (subscribers) {
    auto* link = subscribers;
    auto* observer = link->observer;
    unlinkFromSubscriberList(link);
    unlinkFromSourceList(link);
    link->source = nullptr;
    link->observer = nullptr;
    if (observer) {
      observer->retireLink(link);
    } else {
      freeLink(link);
    }
  }
}

inline void Computation::dispose() {
  if (disposed()) {
    return;
  }
  setFlag(flags, Disposed);
  scheduled = false;
  detachSubscribers();
  clearSourcesForReuse();
}

inline std::uint16_t Computation::observerDepthContribution() const {
  return static_cast<std::uint16_t>(depth + 1u);
}

inline bool Computation::pollSourcesChanged() {
  [[maybe_unused]] profile::ScopedTimer timer{profile::Bucket::PollSourcesChanged};
  // TODO(v5-action-items #10): add a transient Checking flag and per-flush poll result
  // cache if profiling shows diamond dependency graphs revisiting the same upstream
  // computeds. AmbientLoopLab's graph is currently shallow/wide and did not surface this
  // as a measurable hot path; effect-body work dominates the post-cutover samples.
  bool changed = false;
  auto* link = sources;
  while (link) {
    auto* next = link->nextSource;
    Observable* source = link->source;
    if (source) {
      bool const sourceChanged = source->updateIfNeeded();
      if (sourceChanged || link->sourceVersion != source->version) {
        changed = true;
      }
      link->sourceVersion = source->version;
    }
    link = next;
  }
  return changed;
}

inline void Computation::markDirty() {
  if (disposed() || hasFlag(flags, Dirty)) {
    return;
  }
  clearFlag(flags, Pending);
  setFlag(flags, Dirty);
  onDirty();
}

inline void Computation::markPending() {
  if (disposed() || hasFlag(flags, Dirty) || hasFlag(flags, Pending)) {
    return;
  }
  setFlag(flags, Pending);
  onPending();
}

inline void Computation::clearSourcesForReuse() {
  while (sources) {
    auto* link = sources;
    unlinkFromSubscriberList(link);
    unlinkFromSourceList(link);
    link->source = nullptr;
    link->observer = nullptr;
    retireLink(link);
  }
}

inline void Computation::beginTrackingRun() {
  ++runVersion;
  if (runVersion == 0) {
    ++runVersion;
  }
  setFlag(flags, Running);
}

inline void Computation::sweepStaleSources() {
  auto* link = sources;
  while (link) {
    auto* next = link->nextSource;
    if (link->runVersion != runVersion) {
      unlinkFromSubscriberList(link);
      unlinkFromSourceList(link);
      link->source = nullptr;
      link->observer = nullptr;
      retireLink(link);
    }
    link = next;
  }
  clearFlag(flags, Running);
}

inline void Computation::deleteSpareLinks() {
  while (spareLinks) {
    auto* link = spareLinks;
    spareLinks = spareLinks->nextSource;
    link->nextSource = nullptr;
    freeLink(link);
  }
}

inline Link* Computation::findSource(Observable const* source) const {
  auto* link = sources;
  while (link) {
    if (link->source == source) {
      return link;
    }
    link = link->nextSource;
  }
  return nullptr;
}

inline Link* Computation::acquireLink() {
  if (!spareLinks) {
    return allocateLink();
  }
  auto* link = spareLinks;
  spareLinks = spareLinks->nextSource;
  *link = Link{};
  return link;
}

inline void Computation::retireLink(Link* link) {
  *link = Link{};
  link->nextSource = spareLinks;
  spareLinks = link;
}

inline void flushEffects();

struct BatchGuard {
  BatchGuard() {
    ++sBatchDepth;
  }

  ~BatchGuard() {
    --sBatchDepth;
    if (sBatchDepth == 0) {
      flushEffects();
    }
  }
};

inline void scheduleEffect(EffectState* effect);

template <typename T>
concept EqualityComparable = requires(T const& a, T const& b) {
  { a == b } -> std::convertible_to<bool>;
};

} // namespace detail

class Scope {
public:
  Scope()
      : state_(std::make_shared<detail::ScopeState>()) {}

  Scope(Scope const&) = delete;
  Scope& operator=(Scope const&) = delete;

  Scope(Scope&&) noexcept = default;
  Scope& operator=(Scope&&) noexcept = default;

  ~Scope() {
    dispose();
  }

  void dispose() {
    if (state_) {
      state_->dispose();
    }
  }

  bool disposed() const {
    return !state_ || state_->disposed;
  }

  void onCleanup(SmallFn<void()> cleanup) {
    state_->onCleanup(std::move(cleanup));
  }

  detail::ScopeState* state() const {
    return state_.get();
  }

private:
  std::shared_ptr<detail::ScopeState> state_;
};

template <typename Fn>
decltype(auto) withOwner(Scope& scope, Fn&& fn) {
  detail::OwnerContext context(scope.state());
  return std::forward<Fn>(fn)();
}

template <typename Fn>
void onCleanup(Fn&& fn) {
  assert(detail::sCurrentOwner && "onCleanup called without an active owner");
  detail::sCurrentOwner->onCleanup(SmallFn<void()>(std::forward<Fn>(fn)));
}

template <typename Fn>
decltype(auto) untrack(Fn&& fn) {
  detail::ObserverContext context(nullptr);
  return std::forward<Fn>(fn)();
}

template <typename T>
struct SignalState final : detail::Observable {
  explicit SignalState(T initial)
      : value(std::move(initial)) {
    detail::setFlag(flags, detail::Mutable);
  }

  void set(T next) {
    assert(!disposed() && "writing to a disposed Signal");
    if constexpr (detail::EqualityComparable<T>) {
      if (value == next) {
        return;
      }
    }
    detail::BatchGuard batch;
    {
      [[maybe_unused]] detail::profile::ScopedTimer timer{detail::profile::Bucket::SignalSet};
      value = std::move(next);
      ++version;
      propagateDirty();
    }
  }

  T value;
};

template <typename T>
class Signal {
public:
  using Value = T;

  Signal()
      : Signal(T{}) {}

  explicit Signal(T initial)
      : state_(std::make_shared<SignalState<T>>(std::move(initial))) {
    detail::ownNode(state_);
  }

  T const& get() const {
    assert(state_ && "reading an empty Signal handle");
    assert(!state_->disposed() && "reading a disposed Signal");
    state_->reportRead();
    return state_->value;
  }

  T const& evaluate() const {
    return get();
  }

  T const& operator()() const {
    return get();
  }

  T const& operator*() const {
    return get();
  }

  T const& peek() const {
    assert(state_ && "peeking an empty Signal handle");
    assert(!state_->disposed() && "peeking a disposed Signal");
    return state_->value;
  }

  void set(T next) const {
    assert(state_ && "writing an empty Signal handle");
    state_->set(std::move(next));
  }

  Signal const& operator=(T value) const {
    set(std::move(value));
    return *this;
  }

  explicit operator bool() const requires std::same_as<T, bool> {
    return get();
  }

  bool operator==(Signal const& other) const noexcept {
    return state_ == other.state_;
  }

  bool disposed() const {
    return !state_ || state_->disposed();
  }

private:
  std::shared_ptr<SignalState<T>> state_;
};

template <typename T>
struct ComputedState final : detail::Computation {
  template <typename Fn>
  explicit ComputedState(Fn&& compute)
      : fn(std::forward<Fn>(compute)) {
    recompute();
  }

  void run() override {
    (void)recompute();
  }

  bool updateIfNeeded() override {
    if (disposed()) {
      return false;
    }
    if (!value) {
      return recompute();
    }
    if (detail::hasFlag(flags, detail::Dirty)) {
      return recompute();
    }
    if (detail::hasFlag(flags, detail::Pending)) {
      if (!pollSourcesChanged()) {
        detail::clearFlag(flags, detail::Pending);
        return false;
      }
      return recompute();
    }
    return false;
  }

  void onDirty() override {
    propagatePending();
  }

  void onPending() override {
    propagatePending();
  }

  bool recompute() {
    assert(!disposed() && "recomputing a disposed Computed");
    beginTrackingRun();
    auto next = [&] {
      detail::ObserverContext context(this);
      return fn();
    }();
    sweepStaleSources();
    bool changed = !value.has_value();
    if (!changed) {
      if constexpr (detail::EqualityComparable<T>) {
        changed = !(*value == next);
      } else {
        changed = true;
      }
    }
    value = std::move(next);
    if (changed) {
      ++version;
    }
    detail::clearFlag(flags, detail::Pending);
    detail::clearFlag(flags, detail::Dirty);
    return changed;
  }

  SmallFn<T()> fn;
  std::optional<T> value;
};

template <typename T>
class Computed {
public:
  using Value = T;

  Computed() = default;

  template <typename Fn>
    requires(!std::is_same_v<std::decay_t<Fn>, Computed>)
  explicit Computed(Fn&& fn)
      : state_(std::make_shared<ComputedState<T>>(std::forward<Fn>(fn))) {
    detail::ownNode(state_);
  }

  T const& get() const {
    assert(state_ && "reading an empty Computed handle");
    assert(!state_->disposed() && "reading a disposed Computed");
    if (detail::hasFlag(state_->flags, detail::Dirty) ||
        detail::hasFlag(state_->flags, detail::Pending) || !state_->value) {
      (void)state_->updateIfNeeded();
    }
    state_->reportRead();
    return *state_->value;
  }

  T const& evaluate() const {
    return get();
  }

  T const& operator()() const {
    return get();
  }

  T const& peek() const {
    assert(state_ && "peeking an empty Computed handle");
    assert(!state_->disposed() && "peeking a disposed Computed");
    if (detail::hasFlag(state_->flags, detail::Dirty) ||
        detail::hasFlag(state_->flags, detail::Pending) || !state_->value) {
      (void)state_->updateIfNeeded();
    }
    return *state_->value;
  }

  bool disposed() const {
    return !state_ || state_->disposed();
  }

private:
  std::shared_ptr<ComputedState<T>> state_;
};

template <typename Fn>
Computed(Fn) -> Computed<std::invoke_result_t<Fn&>>;

struct EffectState final : detail::Computation {
  template <typename Fn>
  explicit EffectState(Fn&& body)
      : fn(std::forward<Fn>(body)) {}

  void run() override {
    if (disposed()) {
      return;
    }
    [[maybe_unused]] detail::profile::ScopedTimer timer{detail::profile::Bucket::EffectRun};
    beginTrackingRun();
    detail::clearFlag(flags, detail::Pending);
    detail::clearFlag(flags, detail::Dirty);
    {
      ::lambdaui::detail::TransitionScopeSuspension transitionScope;
      detail::ObserverContext context(this);
      fn();
    }
    sweepStaleSources();
  }

  bool updateIfNeeded() override {
    if (disposed()) {
      return false;
    }
    if (detail::hasFlag(flags, detail::Dirty)) {
      run();
      return true;
    }
    if (detail::hasFlag(flags, detail::Pending)) {
      if (pollSourcesChanged()) {
        run();
        return true;
      }
      detail::clearFlag(flags, detail::Pending);
    }
    return false;
  }

  void onDirty() override {
    detail::scheduleEffect(this);
  }

  void onPending() override {
    detail::scheduleEffect(this);
  }

  BindingFn fn;
};

namespace detail {

inline void scheduleEffect(EffectState* effect) {
  if (effect->scheduled || effect->disposed()) {
    return;
  }
  effect->scheduled = true;
  auto const it = std::upper_bound(
      sEffectQueue.begin(), sEffectQueue.end(), effect,
      [](EffectState const* lhs, EffectState const* rhs) {
        return lhs->depth < rhs->depth;
      });
  sEffectQueue.insert(it, effect);
}

inline void flushEffects() {
  // Effects can write signals while running; the nested BatchGuard then
  // re-enters flushEffects and would clobber sEffectFlushQueue mid-iteration.
  // Bail out and let the outer flush loop drain newly scheduled effects.
  if (sFlushingEffects) {
    return;
  }
  sFlushingEffects = true;
  [[maybe_unused]] profile::ScopedTimer timer{profile::Bucket::FlushEffects};
  [[maybe_unused]] std::size_t flushIterations = 0;
  while (!sEffectQueue.empty()) {
#if !defined(NDEBUG) || defined(LAMBDAUI_TESTING)
    constexpr std::size_t kMaxEffectFlushIterations = 10000;
    if (++flushIterations > kMaxEffectFlushIterations) {
      std::fprintf(stderr,
                   "Lambda Reactive: effect flush exceeded %zu iterations; "
                   "dropping the remaining scheduled effects.\n",
                   kMaxEffectFlushIterations);
      for (auto* effect : sEffectQueue) {
        effect->scheduled = false;
      }
      sEffectQueue.clear();
      break;
    }
#endif
    sEffectFlushQueue.clear();
    sEffectFlushQueue.insert(sEffectFlushQueue.end(), sEffectQueue.begin(), sEffectQueue.end());
    sEffectQueue.clear();
    for (auto* effect : sEffectFlushQueue) {
      effect->scheduled = false;
      if (!effect->disposed() &&
          (hasFlag(effect->flags, Dirty) ||
           hasFlag(effect->flags, Pending))) {
        (void)effect->updateIfNeeded();
      }
    }
  }
  sFlushingEffects = false;
}

} // namespace detail

class Effect {
public:
  Effect() = default;

  template <typename Fn>
  explicit Effect(Fn&& fn)
      : state_(std::make_shared<EffectState>(std::forward<Fn>(fn))) {
    detail::ownNode(state_);
    state_->run();
  }

  void dispose() {
    if (state_) {
      state_->dispose();
    }
  }

  bool disposed() const {
    return !state_ || state_->disposed();
  }

private:
  std::shared_ptr<EffectState> state_;
};

template <typename Fn>
auto makeComputed(Fn&& fn) {
  using Value = std::invoke_result_t<Fn&>;
  return Computed<Value>(std::forward<Fn>(fn));
}

} // namespace lambdaui::Reactive
