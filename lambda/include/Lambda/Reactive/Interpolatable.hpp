#pragma once

/// \file Lambda/Reactive/Interpolatable.hpp
///
/// Part of the Lambda public API.


#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Core/Color.hpp>

#include <concepts>

namespace lambdaui {

inline float lerp(float a, float b, float t) {
  return a + (b - a) * t;
}

inline Color lerp(Color a, Color b, float t) {
  return Color{lerp(a.r, b.r, t), lerp(a.g, b.g, t), lerp(a.b, b.b, t), lerp(a.a, b.a, t)};
}

inline Point lerp(Point a, Point b, float t) {
  return Point{lerp(a.x, b.x, t), lerp(a.y, b.y, t)};
}

inline Size lerp(Size a, Size b, float t) {
  return Size{lerp(a.width, b.width, t), lerp(a.height, b.height, t)};
}

inline Rect lerp(Rect a, Rect b, float t) {
  return Rect{lerp(a.x, b.x, t), lerp(a.y, b.y, t), lerp(a.width, b.width, t), lerp(a.height, b.height, t)};
}

inline CornerRadius lerp(CornerRadius a, CornerRadius b, float t) {
  return CornerRadius{lerp(a.topLeft, b.topLeft, t), lerp(a.topRight, b.topRight, t),
                      lerp(a.bottomRight, b.bottomRight, t), lerp(a.bottomLeft, b.bottomLeft, t)};
}

template<typename T>
concept Interpolatable = requires(T a, T b, float t) {
  { lerp(a, b, t) } -> std::same_as<T>;
};

} // namespace lambdaui
