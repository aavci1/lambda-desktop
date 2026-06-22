#pragma once

/// \file Lambda/Reactive/Detail/TypeTraits.hpp
///
/// Part of the Lambda public API.


namespace lambda::detail {

template<typename T>
constexpr bool equalityComparableV = requires(T const& a, T const& b) {
  { a == b } -> std::convertible_to<bool>;
};

} // namespace lambda::detail
