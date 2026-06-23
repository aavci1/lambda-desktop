#pragma once

#include <Lambda/UI/Component.hpp>
#include <Lambda/UI/Element.hpp>

#include <utility>

namespace lambdaui {

struct RootHolder {
  virtual ~RootHolder() = default;
  virtual Element makeElement() = 0;
};

template<typename C>
struct TypedRootHolder final : RootHolder {
  template<typename... Args>
  explicit TypedRootHolder(std::in_place_t, Args&&... args)
      : value(std::forward<Args>(args)...) {}

  Element makeElement() override {
    if constexpr (BodyComponent<C>) {
      return Element{value.body()};
    } else {
      return Element{value};
    }
  }

  C value;
};

} // namespace lambdaui
