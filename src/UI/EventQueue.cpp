#include <Lambda/UI/EventQueue.hpp>

#include <Lambda/UI/Application.hpp>

#include <algorithm>
#include <any>
#include <array>
#include <deque>
#include <functional>
#include <mutex>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace lambda {

namespace {

constexpr std::size_t kLifecycle = 0;
constexpr std::size_t kWindow = 1;
constexpr std::size_t kInput = 2;
constexpr std::size_t kTimer = 3;
constexpr std::size_t kFrame = 4;
constexpr std::size_t kCustom = 5;
constexpr std::size_t kBucketCount = kCustom + 1;
static_assert(kBucketCount == 6);

template<typename T>
constexpr std::size_t bucketFor() {
  if constexpr (std::is_same_v<T, WindowLifecycleEvent>) {
    return kLifecycle;
  } else if constexpr (std::is_same_v<T, WindowEvent>) {
    return kWindow;
  } else if constexpr (std::is_same_v<T, InputEvent>) {
    return kInput;
  } else if constexpr (std::is_same_v<T, TimerEvent>) {
    return kTimer;
  } else if constexpr (std::is_same_v<T, FrameEvent>) {
    return kFrame;
  } else if constexpr (std::is_same_v<T, CustomEvent>) {
    return kCustom;
  } else {
    static_assert(sizeof(T) == 0, "unknown event type");
    return 0;
  }
}

} // namespace

struct EventQueue::Impl {
  using AnyHandler = std::function<void(std::any const&)>;

  std::mutex mutex_{};
  std::array<std::deque<Event>, kBucketCount> buckets_{};
  std::unordered_map<std::type_index, std::vector<AnyHandler>> frameworkHandlers_;
  std::unordered_map<std::uint32_t, std::vector<std::function<void(std::any const&)>>> customPayloadHandlers_;

  bool dispatching_ = false;
};

EventQueue::EventQueue() : d(std::make_unique<Impl>()) {}

EventQueue::~EventQueue() = default;

void EventQueue::dispatch() {
  if (d->dispatching_) {
    return;
  }
  d->dispatching_ = true;
  bool progress = true;
  while (progress) {
    progress = false;
    for (auto& bucket : d->buckets_) {
      while (true) {
        Event e;
        {
          std::lock_guard lock(d->mutex_);
          if (bucket.empty()) {
            break;
          }
          e = std::move(bucket.front());
          bucket.pop_front();
        }
        detail::EventQueueImplAccess::dispatchOne(*this, e);
        progress = true;
      }
    }
  }
  d->dispatching_ = false;
}

void EventQueue::post(Event event) {
  detail::EventQueueImplAccess::postInner(*this, std::move(event));
  if (Application::hasInstance() && !Application::instance().isMainThread()) {
    Application::instance().wakeEventLoop();
  }
}

void detail::EventQueueImplAccess::addFrameworkHandler(EventQueue& q, std::type_index idx,
                                                         std::function<void(std::any const&)> handler) {
  q.d->frameworkHandlers_[idx].push_back(std::move(handler));
}

void detail::EventQueueImplAccess::addCustomHandler(EventQueue& q, std::uint32_t tid,
                                                    std::function<void(std::any const&)> handler) {
  q.d->customPayloadHandlers_[tid].push_back(std::move(handler));
}

void detail::EventQueueImplAccess::postInner(EventQueue& q, Event&& event) {
  std::visit(
      [&q](auto&& ev) {
        using T = std::decay_t<decltype(ev)>;
        constexpr std::size_t b = bucketFor<T>();
        std::lock_guard lock(q.d->mutex_);
        q.d->buckets_[b].push_back(Event(std::move(ev)));
      },
      std::move(event));
}

void detail::EventQueueImplAccess::dispatchOne(EventQueue& q, Event& event) {
  std::visit(
      [&q](auto& ev) {
        using T = std::decay_t<decltype(ev)>;
        std::vector<std::function<void(std::any const&)>> handlers;
        auto it = q.d->frameworkHandlers_.find(std::type_index(typeid(T)));
        if (it != q.d->frameworkHandlers_.end()) {
          handlers = it->second;
        }
        if (!handlers.empty()) {
          std::any a = ev;
          for (auto const& f : handlers) {
            f(a);
          }
        }
        if constexpr (std::is_same_v<T, CustomEvent>) {
          std::vector<std::function<void(std::any const&)>> customHandlers;
          auto pit = q.d->customPayloadHandlers_.find(ev.type);
          if (pit != q.d->customPayloadHandlers_.end()) {
            customHandlers = pit->second;
          }
          for (auto const& fn : customHandlers) {
            if (fn) {
              fn(ev.payload);
            }
          }
        }
      },
      event);
}

} // namespace lambda
