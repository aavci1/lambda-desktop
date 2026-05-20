#pragma once

#include <Flux/Core/Color.hpp>
#include <Flux/Core/Geometry.hpp>

#include <cstdint>

namespace flux::compositor {

struct ChromeConfig {
  std::int32_t titleBarHeight = 28;
  std::int32_t controlsWidth = 58;
  std::int32_t controlsInsetRight = 8;
  std::int32_t controlsInsetTop = 6;
  std::int32_t buttonSize = 16;
  float buttonRadius = 5.f;
  std::int32_t buttonGap = 4;
  Color closeGlyphColor = Color::hex(0x5b6781);
  Color closeGlyphHoverColor = Colors::white;
  Color closeHoverBackground = Color::hex(0xe25555);
  Color minimizeGlyphColor = Color::hex(0x5b6781);
  Color minimizeGlyphHoverColor = Color::hex(0x16203a);
  Color minimizeHoverBackground = Color{0.f, 0.f, 0.f, 0.07f};
  Color titleTextColor = Color::hex(0x16203a);
  float titleTextFontSize = 11.5f;
  float titleTextFontWeight = 600.f;
  CornerRadius windowCornerRadius = CornerRadius{14.f};
  std::int32_t resizeGripSize = 4;
  bool windowGlassEnabled = true;
  float windowGlassOpacity = 0.92f;
  Color glassTint = Color{1.f, 1.f, 1.f, 0.80f};
  float glassBlurRadius = 32.f;
  Color borderLineColor = Color{20.f / 255.f, 30.f / 255.f, 60.f / 255.f, 0.08f};
  Color insetHighlightColor = Color{1.f, 1.f, 1.f, 0.55f};
  Color focusedShadowColor = Color{20.f / 255.f, 30.f / 255.f, 60.f / 255.f, 0.35f};
  Color unfocusedShadowColor = Color{20.f / 255.f, 30.f / 255.f, 60.f / 255.f, 0.20f};
};

} // namespace flux::compositor
