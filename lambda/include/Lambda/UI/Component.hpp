#pragma once

/// \file Lambda/UI/Component.hpp
///
/// Part of the Lambda public API.

#include <concepts>
#include <type_traits>

namespace lambdaui {

class Canvas;
class MeasureContext;
class TextSystem;

template<typename T>
concept BodyComponent = requires(T const& t) {
  { t.body() };
};

template<typename T>
concept Component = true;

} // namespace lambdaui
