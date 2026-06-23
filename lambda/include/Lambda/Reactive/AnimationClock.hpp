#pragma once

/// \file Lambda/Reactive/AnimationClock.hpp
///
/// Part of the Lambda public API.


#include <Lambda/Reactive/Observer.hpp>
#include <Lambda/Reactive/SmallFn.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace lambdaui {

class AnimationBase;

/// One tick from the shared frame pump (`steady_clock` domain).
struct AnimationTick {
  /// `steady_clock` time since epoch in nanoseconds.
  std::int64_t deadlineNanos = 0;
  /// Monotonic time in seconds (same domain as `AnimationBase::tick`).
  double nowSeconds = 0.;
};

enum class FrameAction {
  /// Unsubscribe the frame callback without forcing a redraw.
  Stop,
  /// Unsubscribe the frame callback after requesting one final redraw.
  StopAndRedraw,
  /// Keep the callback active without forcing a redraw.
  Continue,
  /// Keep the callback active and redraw all windows on this tick.
  ContinueAndRedraw,
};

class AnimationClock {
public:
  static AnimationClock& instance();
  static double nowSeconds();

  /// Framework integration: UI installs callbacks that arm the native frame pump and request redraws.
  void setFrameDriver(std::function<void()> requestFrame, std::function<void()> requestRedraw);
  void clearFrameDriver();
  /// Called by the owning platform/UI layer when a frame boundary is reached.
  void notifyFrame(std::int64_t deadlineNanos);
  void shutdown();

  bool needsFramePump() const;
  void registerAnimation(AnimationBase* animation);
  void registerAnimation(std::shared_ptr<AnimationBase> animation);
  void unregisterAnimation(AnimationBase* animation);

  /// Subscribe to the shared animation tick. Return a redraw action when output actually changes.
  ObserverHandle subscribe(Reactive::SmallFn<FrameAction(AnimationTick const&)> callback);
  void unsubscribe(ObserverHandle handle);

#if defined(LAMBDAUI_TESTING)
  void testTick(double nowSeconds) { onTick(static_cast<std::int64_t>(nowSeconds * 1e9)); }
  std::size_t testOwnedAnimationCount() const { return ownedActive_.size(); }
#endif

private:
  AnimationClock();

  void onTick(std::int64_t deadlineNanos);
  void startFramePump();
  void stopFramePump();

  struct Subscriber {
    std::uint64_t id = 0;
    Reactive::SmallFn<FrameAction(AnimationTick const&)> callback;
    bool active = true;
  };

  std::vector<AnimationBase*> active_;
  std::vector<std::shared_ptr<AnimationBase>> ownedActive_;
  std::vector<Subscriber> subscribers_;
  std::uint64_t nextSubscriberId_ = 1;
  std::function<void()> requestFrame_;
  std::function<void()> requestRedraw_;

  bool running_ = false;
  bool dispatchingSubscribers_ = false;
  bool subscribersNeedCompaction_ = false;
};

} // namespace lambdaui
