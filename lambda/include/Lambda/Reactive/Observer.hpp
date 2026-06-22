#pragma once

/// \file Lambda/Reactive/Observer.hpp
///
/// Part of the Lambda public API.


#include <cstdint>

namespace lambda {

/// Opaque handle returned by subscription-style APIs such as AnimationClock.
struct ObserverHandle {
  std::uint64_t id = 0;
  bool isValid() const;
};

} // namespace lambda
