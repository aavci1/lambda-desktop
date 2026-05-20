#pragma once

#include <Flux/Core/Color.hpp>

#include <cstdint>

namespace flux::compositor {

struct ChromeConfig {
  std::int32_t titleBarHeight = 42;
  std::int32_t controlsWidth = 90;
  std::int32_t controlsInsetRight = 10;
  std::int32_t controlsInsetTop = 8;
  std::int32_t buttonSize = 26;
  float buttonRadius = 7.f;
  std::int32_t buttonGap = 4;
  Color closeGlyphColor = Color::hex(0x5b6781);
  Color closeGlyphHoverColor = Colors::white;
  Color closeHoverBackground = Color::hex(0xe25555);
  Color minimizeGlyphColor = Color::hex(0x5b6781);
  Color minimizeGlyphHoverColor = Color::hex(0x16203a);
  Color minimizeHoverBackground = Color{0.f, 0.f, 0.f, 0.07f};
  Color titleTextColor = Color::hex(0x16203a);
  float titleTextFontSize = 12.5f;
  float titleTextFontWeight = 600.f;
  float windowCornerRadius = 14.f;
  Color glassTint = Color{1.f, 1.f, 1.f, 0.80f};
  float glassBlurRadius = 32.f;
  Color borderLineColor = Color{20.f / 255.f, 30.f / 255.f, 60.f / 255.f, 0.08f};
  Color insetHighlightColor = Color{1.f, 1.f, 1.f, 0.55f};
  Color focusedShadowColor = Color{20.f / 255.f, 30.f / 255.f, 60.f / 255.f, 0.35f};
  Color unfocusedShadowColor = Color{20.f / 255.f, 30.f / 255.f, 60.f / 255.f, 0.20f};
};

} // namespace flux::compositor
