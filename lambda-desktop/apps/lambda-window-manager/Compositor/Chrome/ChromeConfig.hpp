#pragma once

#include <Lambda/Core/Color.hpp>
#include <Lambda/Core/Geometry.hpp>

#include <cstdint>

namespace lambdaui::compositor {

struct GlassEffectConfig {
  float blurRadius = 64.f;
  Color baseColor = Color{221.f * (1.f / 255.f), 221.f * (1.f / 255.f), 1.f, 1.f};
  Color tintColor = Color{221.f * (1.f / 255.f), 1.f, 1.f, 1.f};
  Color borderColor = Color{1.f, 1.f, 1.f, 102.f * (1.f / 255.f)};
  float opacity = 0.05f;
  Color contrastColor = Colors::black;
  float focusedContrastOpacity = 0.18f;
  float unfocusedContrastOpacity = 0.13f;
};

struct ChromeConfig {
  std::int32_t titleBarHeight = 28;
  std::int32_t controlsWidth = 84;
  std::int32_t controlsInsetRight = 8;
  std::int32_t controlsInsetTop = 6;
  std::int32_t buttonSize = 16;
  float buttonRadius = 5.f;
  std::int32_t buttonGap = 4;
  Color closeGlyphColor = Colors::white;
  Color closeGlyphHoverColor = Colors::white;
  Color closeHoverBackground = Color::hex(0xe25555);
  Color minimizeGlyphColor = Colors::white;
  Color minimizeGlyphHoverColor = Colors::white;
  Color minimizeHoverBackground = Color{0.f, 0.f, 0.f, 0.07f};
  Color titleTextColor = Colors::white;
  float titleTextFontSize = 11.5f;
  float titleTextFontWeight = 600.f;
  CornerRadius windowCornerRadius = CornerRadius{14.f};
  // Legacy config name: this is the external frame/ring outset, not an inset applied to client content.
  float contentInsetWidth = 4.f;
  std::int32_t resizeGripSize = 4;
  GlassEffectConfig glass{};
  Color windowBorderColor = Color{1.f, 1.f, 1.f, 102.f * (1.f / 255.f)};
  float windowBorderWidth = 1.f;
  Color borderLineColor = Color{1.f, 1.f, 1.f, 102.f * (1.f / 255.f)};
  Color insetHighlightColor = Colors::transparent;
  float focusedShadowRadius = 18.f;
  float unfocusedShadowRadius = 12.f;
  Point focusedShadowOffset{};
  Point unfocusedShadowOffset{};
  Color focusedShadowColor = Color{20.f / 255.f, 30.f / 255.f, 60.f / 255.f, 0.52f};
  Color unfocusedShadowColor = Color{20.f / 255.f, 30.f / 255.f, 60.f / 255.f, 0.32f};
};

} // namespace lambdaui::compositor
