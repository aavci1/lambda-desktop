#pragma once

/// \file Lambda/Core/Geometry.hpp
///
/// Core math primitives shared by Lambda modules.

#include <algorithm>
#include <cmath>

namespace lambdaui {

struct Point {
  float x = 0;
  float y = 0;

  constexpr Point() = default;
  constexpr Point(float x, float y) : x(x), y(y) {}

  constexpr Point operator+(const Point& o) const { return {x + o.x, y + o.y}; }
  constexpr Point operator-(const Point& o) const { return {x - o.x, y - o.y}; }
  constexpr Point operator*(float s) const { return {x * s, y * s}; }
  constexpr Point operator/(float s) const { return {x / s, y / s}; }

  constexpr bool operator==(const Point& o) const = default;
};

using Vec2 = Point;

struct Size {
  float width = 0;
  float height = 0;

  constexpr Size() = default;
  constexpr Size(float width, float height) : width(width), height(height) {}

  constexpr bool isEmpty() const { return width <= 0 || height <= 0; }
  constexpr float area() const { return width * height; }
  constexpr bool operator==(const Size& other) const = default;
};

struct Rect;

struct CornerRadius {
  float topLeft = 0;
  float topRight = 0;
  float bottomRight = 0;
  float bottomLeft = 0;

  constexpr CornerRadius() = default;
  explicit constexpr CornerRadius(float all)
      : topLeft(all), topRight(all), bottomRight(all), bottomLeft(all) {}
  constexpr CornerRadius(float tl, float tr, float br, float bl)
      : topLeft(tl), topRight(tr), bottomRight(br), bottomLeft(bl) {}

  CornerRadius& operator=(float value) {
    topLeft = topRight = bottomRight = bottomLeft = value;
    return *this;
  }

  constexpr bool isUniform() const {
    return topLeft == topRight && topRight == bottomRight && bottomRight == bottomLeft;
  }
  constexpr bool isZero() const {
    return topLeft == 0 && topRight == 0 && bottomRight == 0 && bottomLeft == 0;
  }
  constexpr bool operator==(const CornerRadius& o) const = default;

  /// Full pill / circle corner radii for an axis-aligned `bounds`.
  static constexpr CornerRadius pill(Rect const& bounds);
};

struct Rect {
  float x = 0;
  float y = 0;
  float width = 0;
  float height = 0;

  constexpr Rect() = default;
  constexpr Rect(float x, float y, float width, float height)
      : x(x), y(y), width(width), height(height) {}

  static constexpr Rect sharp(float rx, float ry, float rw, float rh) {
    return Rect{rx, ry, rw, rh};
  }

  constexpr Point center() const { return {x + width * 0.5f, y + height * 0.5f}; }

  bool intersects(Rect o) const {
    return x < o.x + o.width && x + width > o.x && y < o.y + o.height && y + height > o.y;
  }

  constexpr bool contains(Point p) const {
    const float x0 = std::min(x, x + width);
    const float x1 = std::max(x, x + width);
    const float y0 = std::min(y, y + height);
    const float y1 = std::max(y, y + height);
    return p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1;
  }

  constexpr bool operator==(const Rect& o) const = default;
};

constexpr CornerRadius CornerRadius::pill(Rect const& bounds) {
  const float r = std::min(bounds.width, bounds.height) * 0.5f;
  return CornerRadius(r, r, r, r);
}

struct EdgeInsets {
  float top = 0.f;
  float right = 0.f;
  float bottom = 0.f;
  float left = 0.f;

  static constexpr EdgeInsets uniform(float v) { return EdgeInsets{v, v, v, v}; }

  constexpr bool isZero() const {
    return top == 0.f && right == 0.f && bottom == 0.f && left == 0.f;
  }

  constexpr bool operator==(EdgeInsets const& o) const = default;
};

struct Mat3 {
  float m[9]{};

  static constexpr Mat3 identity() {
    Mat3 r{};
    r.m[0] = 1.f;
    r.m[4] = 1.f;
    r.m[8] = 1.f;
    return r;
  }

  static constexpr Mat3 translate(Point offset) { return translate(offset.x, offset.y); }

  static constexpr Mat3 translate(float tx, float ty) {
    Mat3 r = identity();
    r.m[6] = tx;
    r.m[7] = ty;
    return r;
  }

  static constexpr Mat3 scale(float sx, float sy) {
    Mat3 r{};
    r.m[0] = sx;
    r.m[4] = sy;
    r.m[8] = 1.f;
    return r;
  }

  static constexpr Mat3 scale(float s) { return scale(s, s); }

  static Mat3 rotate(float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Mat3 r{};
    r.m[0] = c;
    r.m[1] = s;
    r.m[3] = -s;
    r.m[4] = c;
    r.m[8] = 1.f;
    return r;
  }

  static Mat3 rotate(float radians, Point pivot) {
    return translate(pivot) * rotate(radians) * translate(Point{-pivot.x, -pivot.y});
  }

  Mat3 operator*(Mat3 const& o) const {
    Mat3 r{};
    for (int j = 0; j < 3; ++j) {
      for (int i = 0; i < 3; ++i) {
        float s = 0.f;
        for (int k = 0; k < 3; ++k) {
          s += m[k * 3 + i] * o.m[j * 3 + k];
        }
        r.m[j * 3 + i] = s;
      }
    }
    return r;
  }

  Point apply(Point p) const {
    const float x = m[0] * p.x + m[3] * p.y + m[6];
    const float y = m[1] * p.x + m[4] * p.y + m[7];
    return {x, y};
  }

  bool isTranslationOnly(float eps = 1e-6f) const {
    return std::abs(m[0] - 1.f) <= eps && std::abs(m[1]) <= eps && std::abs(m[2]) <= eps &&
           std::abs(m[3]) <= eps && std::abs(m[4] - 1.f) <= eps && std::abs(m[5]) <= eps &&
           std::abs(m[8] - 1.f) <= eps;
  }

  Mat3 inverse() const {
    constexpr float kEps = 1e-12f;
    const float a = m[0];
    const float b = m[1];
    const float c = m[2];
    const float d = m[3];
    const float e = m[4];
    const float f = m[5];
    const float g = m[6];
    const float h = m[7];
    const float i = m[8];
    const float det =
        a * (e * i - f * h) - d * (b * i - c * h) + g * (b * f - c * e);
    if (std::abs(det) < kEps) {
      return identity();
    }
    const float invDet = 1.f / det;
    Mat3 r{};
    r.m[0] = (e * i - f * h) * invDet;
    r.m[1] = (c * h - b * i) * invDet;
    r.m[2] = (b * f - c * e) * invDet;
    r.m[3] = (f * g - d * i) * invDet;
    r.m[4] = (a * i - c * g) * invDet;
    r.m[5] = (c * d - a * f) * invDet;
    r.m[6] = (d * h - e * g) * invDet;
    r.m[7] = (b * g - a * h) * invDet;
    r.m[8] = (a * e - b * d) * invDet;
    return r;
  }

  float affineDeterminant() const { return m[0] * m[4] - m[1] * m[3]; }

  bool operator==(Mat3 const&) const = default;
};

} // namespace lambdaui
