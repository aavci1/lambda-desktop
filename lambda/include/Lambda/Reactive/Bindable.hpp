#pragma once

#include <Lambda/Reactive/SmallFn.hpp>

#include <concepts>
#include <type_traits>
#include <utility>
#include <variant>

namespace lambdaui::Reactive {

template <typename T>
class Bindable {
public:
  Bindable() = default;

  Bindable(T value)
      : storage_(std::move(value)) {}

  template <typename U>
    requires(!std::is_same_v<std::decay_t<U>, Bindable> &&
             std::convertible_to<U&&, T> &&
             !std::is_invocable_r_v<T, std::decay_t<U>&>)
  Bindable(U&& value)
      : storage_(T(std::forward<U>(value))) {}

  template <typename Fn>
    requires(!std::is_same_v<std::decay_t<Fn>, Bindable> &&
             std::is_invocable_r_v<T, std::decay_t<Fn>&>)
  Bindable(Fn&& fn)
      : storage_(SmallFn<T()>(std::forward<Fn>(fn))) {}

  bool isReactive() const {
    return std::holds_alternative<SmallFn<T()>>(storage_);
  }

  bool isValue() const {
    return std::holds_alternative<T>(storage_);
  }

  T const& value() const {
    return std::get<T>(storage_);
  }

  T evaluate() const {
    if (auto const* value = std::get_if<T>(&storage_)) {
      return *value;
    }
    return std::get<SmallFn<T()>>(storage_)();
  }

  bool operator==(Bindable const& other) const
    requires std::equality_comparable<T>
  {
    return isValue() && other.isValue() && value() == other.value();
  }

private:
  std::variant<T, SmallFn<T()>> storage_{T{}};
};

} // namespace lambdaui::Reactive
