#pragma once

/// \file Lambda/Core/Color.hpp
///
/// Core color primitive and semantic color tokens.

#include <cstdint>

namespace lambdaui {

struct Theme;

struct Color {
  float r = 0;
  float g = 0;
  float b = 0;
  float a = 1;
  std::uint8_t semantic = 0;

  constexpr Color() = default;
  constexpr Color(float r, float g, float b, float a = 1.f)
      : r(r), g(g), b(b), a(a), semantic(0) {}

  static constexpr Color rgb(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    const float s = 1.f / 255.f;
    return Color(r * s, g * s, b * s, 1.f);
  }

  static constexpr Color hex(std::uint32_t h) {
    return rgb(static_cast<std::uint8_t>((h >> 16) & 0xFF),
               static_cast<std::uint8_t>((h >> 8) & 0xFF),
               static_cast<std::uint8_t>(h & 0xFF));
  }

  static Color theme();
  static Color primary();
  static Color secondary();
  static Color tertiary();
  static Color quaternary();
  static Color placeholder();
  static Color disabled();
  static Color accent();
  static Color accentForeground();
  static Color windowBackground();
  static Color controlBackground();
  static Color elevatedBackground();
  static Color textBackground();
  static Color separator();
  static Color opaqueSeparator();
  static Color selectedContentBackground();
  static Color focusRing();
  static Color scrim();
  static Color popoverScrim();
  static Color success();
  static Color successForeground();
  static Color successBackground();
  static Color warning();
  static Color warningForeground();
  static Color warningBackground();
  static Color danger();
  static Color dangerForeground();
  static Color dangerBackground();

  constexpr int semanticToken() const { return static_cast<int>(semantic); }
  constexpr bool isSemantic() const { return semanticToken() != 0; }

  constexpr bool operator==(const Color& o) const = default;
};

constexpr inline Color resolveColor(Color override, Color themeValue) {
  return (override.semanticToken() == 1) ? themeValue : override;
}

Color resolveColor(Color value, Theme const& theme);
Color resolveColor(Color override, Color themeValue, Theme const& theme);

inline constexpr float kFloatFromTheme = -1.f;
inline constexpr float kHeightFromTheme = -1.f;

constexpr inline float resolveFloat(float override, float themeValue) {
  return (override < 0.f) ? themeValue : override;
}

namespace Colors {
constexpr Color white{1, 1, 1, 1};
constexpr Color black{0, 0, 0, 1};
constexpr Color transparent{0, 0, 0, 0};
constexpr Color red = Color::hex(0xF44336);
constexpr Color blue = Color::hex(0x2196F3);
constexpr Color green = Color::hex(0x4CAF50);
constexpr Color yellow = Color::hex(0xFFD700);
constexpr Color gray = Color::hex(0x9E9E9E);
constexpr Color darkGray = Color::hex(0x424242);
constexpr Color lightGray = Color::hex(0xE0E0E0);
} // namespace Colors

} // namespace lambdaui
