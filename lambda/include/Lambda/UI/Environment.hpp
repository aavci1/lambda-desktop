#pragma once

/// \file Lambda/UI/Environment.hpp
///
/// Part of the Lambda public API.


#include <Lambda/UI/Detail/EnvironmentSlot.hpp>

#include <concepts>
#include <typeinfo>

namespace lambda {

template<typename Tag>
struct EnvironmentKey;

#define LAMBDA_DEFINE_ENVIRONMENT_KEY(KeyTag, ValueT, DefaultExpr)                         \
  struct KeyTag {};                                                                       \
  template<>                                                                              \
  struct EnvironmentKey<KeyTag> {                                                         \
    using Value = ValueT;                                                                 \
    static_assert(std::copy_constructible<Value>,                                         \
                  "Environment key values must be copy-constructible.");                  \
    static_assert(std::equality_comparable<Value>,                                        \
                  "Environment key values must define operator==.");                      \
    static Value defaultValue() { return (DefaultExpr); }                                 \
    static ::lambda::detail::EnvironmentSlot const& slot() {                                \
      static ::lambda::detail::EnvironmentSlot s{                                           \
          ::lambda::detail::allocateEnvironmentSlot(typeid(KeyTag))};                       \
      return s;                                                                           \
    }                                                                                     \
  }

} // namespace lambda
