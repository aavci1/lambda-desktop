#pragma once

#include <Flux/Core/Color.hpp>
#include <Flux/Core/Geometry.hpp>

#include <cstdint>

namespace flux::compositor {

struct ChromeConfig {
  std::int32_t titleBarHeight = 28;
  std::int32_t controlsWidth = 84;
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
  float windowGlassOpacity = 0.84f;
  Color glassTint = Color{238.f / 255.f, 244.f / 255.f, 1.f, 0.60f};
  float glassBlurRadius = 40.f;
  Color windowBorderColor = Color{216.f / 255.f, 222.f / 255.f, 232.f / 255.f, 0.60f};
  float windowBorderWidth = 1.f;
  Color borderLineColor = Color{20.f / 255.f, 30.f / 255.f, 60.f / 255.f, 0.08f};
  Color insetHighlightColor = Colors::transparent;
  Color focusedShadowColor = Color{20.f / 255.f, 30.f / 255.f, 60.f / 255.f, 0.35f};
  Color unfocusedShadowColor = Color{20.f / 255.f, 30.f / 255.f, 60.f / 255.f, 0.20f};
};

} // namespace flux::compositor
