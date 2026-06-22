#pragma once

/// \file Lambda/Graphics/Styles.hpp
///
/// Part of the Lambda public API.


#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Core/Color.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <variant>

namespace lambda {

enum class StrokeCap { Butt, Round, Square };
enum class StrokeJoin { Miter, Round, Bevel };

enum class FillRule { NonZero, EvenOdd };

inline constexpr std::size_t kMaxGradientStops = 4;
inline constexpr std::size_t kMaxLinearGradientStops = kMaxGradientStops;

/// Compositing / blend mode for drawing. The Metal backend maps each value to fixed-function
/// blend state where possible; modes that are not representable use the same factors as `Normal`.
enum class BlendMode {
  Normal,
  Multiply,
  Screen,
  Overlay,
  Darken,
  Lighten,
  ColorDodge,
  ColorBurn,
  HardLight,
  SoftLight,
  Difference,
  Exclusion,
  Hue,
  Saturation,
  Color,
  Luminosity,
  Clear,
  Src,
  Dst,
  SrcOver,
  DstOver,
  SrcIn,
  DstIn,
  SrcOut,
  DstOut,
  SrcAtop,
  DstAtop,
  Xor
};

struct GradientStop {
  float position = 0.f;
  Color color = Colors::transparent;

  constexpr bool operator==(GradientStop const& other) const = default;
};

/// Unit-space linear gradient for paths and shapes. `start` and `end` are normalized to the filled
/// bounds, so `{0, 0}` is top-left and `{1, 1}` is bottom-right.
struct LinearGradient {
  Point start{0.f, 0.f};
  Point end{1.f, 1.f};
  std::array<GradientStop, kMaxGradientStops> stops{};
  std::uint8_t stopCount = 0;

  constexpr bool operator==(LinearGradient const& other) const = default;
};

/// Unit-space radial gradient. `center` and `radius` are normalized to the filled bounds.
struct RadialGradient {
  Point center{0.5f, 0.5f};
  float radius = 0.5f;
  std::array<GradientStop, kMaxGradientStops> stops{};
  std::uint8_t stopCount = 0;

  constexpr bool operator==(RadialGradient const& other) const = default;
};

/// Unit-space conical gradient. `startAngleRadians` rotates the 0-position ray clockwise from +x.
struct ConicalGradient {
  Point center{0.5f, 0.5f};
  float startAngleRadians = 0.f;
  std::array<GradientStop, kMaxGradientStops> stops{};
  std::uint8_t stopCount = 0;

  constexpr bool operator==(ConicalGradient const& other) const = default;
};

/// Fill for paths and shapes (solid color, gradient, or none). Matches [upstream lambda FillStyle](https://github.com/aavci1/lambda) conceptually.
struct FillStyle {
  std::variant<std::monostate, Color, LinearGradient, RadialGradient, ConicalGradient> data = std::monostate{};
  FillRule fillRule = FillRule::NonZero;

  static FillStyle none();
  static FillStyle solid(Color c);
  static FillStyle linearGradient(Color from, Color to, Point start = {0.f, 0.f}, Point end = {1.f, 1.f});
  static FillStyle linearGradient(std::initializer_list<GradientStop> stops, Point start = {0.f, 0.f},
                                  Point end = {1.f, 1.f});
  static FillStyle radialGradient(Color inner, Color outer, Point center = {0.5f, 0.5f}, float radius = 0.5f);
  static FillStyle radialGradient(std::initializer_list<GradientStop> stops, Point center = {0.5f, 0.5f},
                                  float radius = 0.5f);
  static FillStyle conicalGradient(Color from, Color to, Point center = {0.5f, 0.5f},
                                   float startAngleRadians = 0.f);
  static FillStyle conicalGradient(std::initializer_list<GradientStop> stops, Point center = {0.5f, 0.5f},
                                   float startAngleRadians = 0.f);

  bool isNone() const;

  bool solidColor(Color* out) const;
  bool linearGradient(LinearGradient* out) const;
  bool radialGradient(RadialGradient* out) const;
  bool conicalGradient(ConicalGradient* out) const;

  bool operator==(FillStyle const& other) const = default;
};

/// Stroke for paths, lines, and stroked rects. Matches upstream `StrokeStyle` factory pattern.
struct StrokeStyle {
  enum class Type { None, Solid };
  Type type = Type::None;
  Color color = Colors::black;
  float width = 1.f;
  StrokeCap cap = StrokeCap::Butt;
  StrokeJoin join = StrokeJoin::Miter;
  float miterLimit = 4.f;

  static StrokeStyle none();
  static StrokeStyle solid(Color c, float w = 1.f);

  bool isNone() const;

  bool solidColor(Color* out) const;

  bool operator==(StrokeStyle const& other) const = default;
};

/// Optional drop shadow for rects (logical points). When \c radius <= 0 or \c color.a <= 0, treated as off.
struct ShadowStyle {
  float radius = 0.f;
  Point offset{};
  Color color = Colors::transparent;

  static ShadowStyle none() { return {}; }

  bool isNone() const { return radius <= 0.f || color.a <= 0.f; }

  bool operator==(ShadowStyle const& other) const = default;
};

} // namespace lambda
