#pragma once

/// \file Lambda/UI/EventQueue.hpp
///
/// Part of the Lambda public API.


#include <Lambda/UI/Events.hpp>

#include <any>
#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>
#include <typeindex>
#include <typeinfo>

namespace lambda {

class Application;

namespace detail {

template<typename T>
inline constexpr bool isEventAlternativeV =
    std::is_same_v<T, WindowLifecycleEvent> || std::is_same_v<T, WindowEvent> ||
    std::is_same_v<T, InputEvent> || std::is_same_v<T, TimerEvent> ||
    std::is_same_v<T, FrameEvent> ||
    std::is_same_v<T, CustomEvent>;

template<typename T>
inline std::uint32_t eventQueueCustomTypeId() {
  return static_cast<std::uint32_t>(std::type_index(typeid(T)).hash_code());
}

struct EventQueueImplAccess;

} // namespace detail

/// Application-owned queue; obtain via `Application::instance().eventQueue()`.
/// `post`, `dispatch`, and `on` are main-thread-only by contract (not enforced at runtime).
class EventQueue {
public:
  ~EventQueue();

  EventQueue(const EventQueue&) = delete;
  EventQueue& operator=(const EventQueue&) = delete;

  void post(Event event);

  /// User-defined payloads: wrapped in `CustomEvent`. Not for `Event` or first-class alternatives.
  template<typename T>
    requires(!detail::isEventAlternativeV<std::remove_cvref_t<T>> &&
             !std::is_same_v<std::remove_cvref_t<T>, Event>)
  void post(T&& value) {
    CustomEvent ce;
    ce.type = detail::eventQueueCustomTypeId<std::remove_cvref_t<T>>();
    ce.payload = std::forward<T>(value);
    post(Event(std::move(ce)));
  }

  /// First-class events (`WindowEvent`, `InputEvent`, …) and `CustomEvent` payloads.
  template<typename T>
    requires(detail::isEventAlternativeV<std::remove_cvref_t<T>>)
  void post(T&& value) {
    post(Event(std::forward<T>(value)));
  }

  template<typename T>
  void on(std::function<void(T const&)> handler);

  void dispatch();

private:
  friend class Application;

  EventQueue();

  friend struct detail::EventQueueImplAccess;
  struct Impl;
  std::unique_ptr<Impl> d;
};

namespace detail {

/// Internal; used by `EventQueue` templates and implementation. Do not call from application code.
struct EventQueueImplAccess {
  static void postInner(EventQueue& q, Event&& event);
  static void addFrameworkHandler(EventQueue& q, std::type_index idx, std::function<void(std::any const&)> handler);
  static void addCustomHandler(EventQueue& q, std::uint32_t tid, std::function<void(std::any const&)> handler);
  static void dispatchOne(EventQueue& q, Event& event);
};

} // namespace detail

template<typename T>
void EventQueue::on(std::function<void(T const&)> handler) {
  if constexpr (detail::isEventAlternativeV<T>) {
    auto wrapped = [h = std::move(handler)](std::any const& a) { h(std::any_cast<T const&>(a)); };
    detail::EventQueueImplAccess::addFrameworkHandler(*this, std::type_index(typeid(T)), std::move(wrapped));
  } else {
    std::uint32_t tid = detail::eventQueueCustomTypeId<T>();
    detail::EventQueueImplAccess::addCustomHandler(*this, tid, [h = std::move(handler)](std::any const& a) {
      h(std::any_cast<T const&>(a));
    });
  }
}

} // namespace lambda
