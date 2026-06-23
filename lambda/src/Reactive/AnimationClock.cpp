#include <Lambda/Reactive/AnimationClock.hpp>
#include <Lambda/Reactive/Animation.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <utility>
#include <vector>

namespace lambdaui {

AnimationClock::AnimationClock() = default;

AnimationClock& AnimationClock::instance() {
  static AnimationClock sInstance;
  return sInstance;
}

double AnimationClock::nowSeconds() {
  auto const ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch());
  return static_cast<double>(ns.count()) * 1e-9;
}

void AnimationClock::setFrameDriver(std::function<void()> requestFrame,
                                    std::function<void()> requestRedraw) {
  requestFrame_ = std::move(requestFrame);
  requestRedraw_ = std::move(requestRedraw);
  if (needsFramePump()) {
    startFramePump();
  }
}

void AnimationClock::clearFrameDriver() {
  requestFrame_ = {};
  requestRedraw_ = {};
  running_ = false;
}

void AnimationClock::notifyFrame(std::int64_t deadlineNanos) {
  if (!running_) {
    return;
  }
  onTick(deadlineNanos);
  if (needsFramePump()) {
    startFramePump();
  }
}

void AnimationClock::shutdown() {
  stopFramePump();
  double const now = AnimationClock::nowSeconds();
  for (auto& animation : ownedActive_) {
    if (animation) {
      animation->cancelFromClock(now);
    }
  }
  active_.clear();
  ownedActive_.clear();
  subscribers_.clear();
  nextSubscriberId_ = 1;
  requestFrame_ = {};
  requestRedraw_ = {};
  dispatchingSubscribers_ = false;
  subscribersNeedCompaction_ = false;
}

bool AnimationClock::needsFramePump() const {
  return !active_.empty() || !ownedActive_.empty() || !subscribers_.empty();
}

void AnimationClock::registerAnimation(AnimationBase* animation) {
  if (!animation) {
    return;
  }
  if (std::find(active_.begin(), active_.end(), animation) != active_.end()) {
    return;
  }
  active_.push_back(animation);
  if (!running_) {
    startFramePump();
  }
}

void AnimationClock::registerAnimation(std::shared_ptr<AnimationBase> animation) {
  if (!animation) {
    return;
  }
  auto const alreadyRegistered = std::find_if(
      ownedActive_.begin(),
      ownedActive_.end(),
      [&](std::shared_ptr<AnimationBase> const& active) {
        return active.get() == animation.get();
      });
  if (alreadyRegistered != ownedActive_.end()) {
    return;
  }
  ownedActive_.push_back(std::move(animation));
  if (!running_) {
    startFramePump();
  }
}

void AnimationClock::unregisterAnimation(AnimationBase* animation) {
  if (!animation) {
    return;
  }
  std::erase(active_, animation);
  std::erase_if(ownedActive_, [&](std::shared_ptr<AnimationBase> const& active) {
    return active.get() == animation;
  });
  if (!needsFramePump()) {
    stopFramePump();
  }
}

ObserverHandle AnimationClock::subscribe(Reactive::SmallFn<FrameAction(AnimationTick const&)> callback) {
  if (!callback) {
    return {};
  }
  std::uint64_t const id = nextSubscriberId_++;
  subscribers_.push_back(Subscriber{id, std::move(callback), true});
  if (!running_) {
    startFramePump();
  }
  return ObserverHandle{id};
}

void AnimationClock::unsubscribe(ObserverHandle handle) {
  if (!handle.isValid()) {
    return;
  }
  if (dispatchingSubscribers_) {
    for (Subscriber& s : subscribers_) {
      if (s.id == handle.id) {
        s.active = false;
        subscribersNeedCompaction_ = true;
      }
    }
  } else {
    std::erase_if(subscribers_, [&](Subscriber const& s) { return s.id == handle.id; });
  }
  if (!needsFramePump()) {
    stopFramePump();
  }
}

void AnimationClock::onTick(std::int64_t deadlineNanos) {
  Reactive::detail::BatchGuard batch;
  const double now = static_cast<double>(deadlineNanos) * 1e-9;
  AnimationTick const tick{deadlineNanos, now};
  bool requestRedraw = false;

  static thread_local std::vector<AnimationBase*> snapshotBuffer;
  snapshotBuffer.assign(active_.begin(), active_.end());
  for (AnimationBase* p : snapshotBuffer) {
    if (!p) {
      continue;
    }
    if (!p->tick(now)) {
      unregisterAnimation(p);
    }
  }
  snapshotBuffer.clear();

  static thread_local std::vector<std::shared_ptr<AnimationBase>> ownedSnapshotBuffer;
  ownedSnapshotBuffer.assign(ownedActive_.begin(), ownedActive_.end());
  for (std::shared_ptr<AnimationBase> const& animation : ownedSnapshotBuffer) {
    if (!animation) {
      continue;
    }
    requestRedraw = requestRedraw || animation->requestsRedraw();
    if (!animation->tick(now)) {
      unregisterAnimation(animation.get());
    }
  }
  ownedSnapshotBuffer.clear();

  dispatchingSubscribers_ = true;
  std::size_t const subscriberCount = subscribers_.size();
  for (std::size_t i = 0; i < subscriberCount && i < subscribers_.size(); ++i) {
    std::uint64_t const id = subscribers_[i].id;
    if (!subscribers_[i].active || !subscribers_[i].callback) {
      continue;
    }
    auto callback = subscribers_[i].callback;
    FrameAction const action = callback(tick);
    if (action == FrameAction::ContinueAndRedraw || action == FrameAction::StopAndRedraw) {
      requestRedraw = true;
    }
    if (action == FrameAction::Stop || action == FrameAction::StopAndRedraw) {
      for (Subscriber& subscriber : subscribers_) {
        if (subscriber.id == id) {
          subscriber.active = false;
          subscribersNeedCompaction_ = true;
          break;
        }
      }
    }
  }
  dispatchingSubscribers_ = false;
  if (subscribersNeedCompaction_) {
    std::erase_if(subscribers_, [](Subscriber const& s) { return !s.active; });
    subscribersNeedCompaction_ = false;
  }

  if (requestRedraw && requestRedraw_) {
    requestRedraw_();
  }

  if (!needsFramePump()) {
    stopFramePump();
  }
}

void AnimationClock::startFramePump() {
  if (!requestFrame_) {
    return;
  }
  if (!running_) {
    running_ = true;
  }
  requestFrame_();
}

void AnimationClock::stopFramePump() {
  if (!running_) {
    return;
  }
  running_ = false;
}

} // namespace lambdaui
