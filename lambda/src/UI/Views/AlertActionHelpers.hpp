#pragma once

#include <functional>
#include <utility>

namespace lambda::detail {

inline std::function<void()> wrapDismissThenInvoke(std::function<void()> dismiss,
                                                   std::function<void()> action) {
  return [dismiss = std::move(dismiss), action = std::move(action)]() {
    std::function<void()> const dismissCopy = dismiss;
    std::function<void()> const actionCopy = action;
    if (dismissCopy) {
      dismissCopy();
    }
    if (actionCopy) {
      actionCopy();
    }
  };
}

} // namespace lambda::detail
