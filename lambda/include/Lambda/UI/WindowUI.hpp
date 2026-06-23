#pragma once

/// \file Lambda/UI/WindowUI.hpp
///
/// Part of the Lambda public API.


#include <Lambda/UI/Window.hpp>
#include <Lambda/UI/Detail/RootHolder.hpp>

#include <type_traits>

namespace lambdaui {

template<typename C>
void Window::setView(C&& component) {
  setViewRoot(std::make_unique<TypedRootHolder<std::decay_t<C>>>(
      std::in_place, std::forward<C>(component)));
}

template<typename C>
void Window::setView() {
  setViewRoot(std::make_unique<TypedRootHolder<C>>(std::in_place));
}

} // namespace lambdaui
