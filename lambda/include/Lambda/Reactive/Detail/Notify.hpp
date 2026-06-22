#pragma once

/// \file Lambda/Reactive/Detail/Notify.hpp
///
/// Part of the Lambda public API.


#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace lambda::detail {

/// Runs observer callbacks with snapshot + deferred nested notifications (see Signal.cpp).
void notifyObserverList(std::vector<std::pair<std::uint64_t, std::function<void()>>>& observers);

} // namespace lambda::detail
