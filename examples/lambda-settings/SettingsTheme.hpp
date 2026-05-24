#pragma once

#include <Flux/Core/Color.hpp>

namespace lambda_settings {

/// Visual tokens aligned to the attached Lambda desktop design mock.
struct SettingsTheme {
  static constexpr flux::Color accent = flux::Color::hex(0x2A7FFF);
  static constexpr flux::Color accent2 = flux::Color::hex(0x5AA2FF);
  static constexpr flux::Color text = flux::Color::hex(0x16203A);
  static constexpr flux::Color text2 = flux::Color::hex(0x5B6781);
  static constexpr flux::Color text3 = flux::Color::hex(0x8893AD);
  static constexpr flux::Color line = flux::Color{0.08f, 0.12f, 0.24f, 0.08f};
  static constexpr flux::Color line2 = flux::Color{0.08f, 0.12f, 0.24f, 0.05f};
  static constexpr flux::Color glassSoft = flux::Color{1.f, 1.f, 1.f, 0.34f};
  static constexpr flux::Color glassPanel = flux::Color{1.f, 1.f, 1.f, 0.24f};
  static constexpr flux::Color selectFill = flux::Color{0.16f, 0.50f, 1.f, 0.14f};
  static constexpr flux::Color selectBorder = flux::Color{0.16f, 0.50f, 1.f, 0.35f};
  static constexpr flux::Color hoverFill = flux::Color{0.f, 0.f, 0.f, 0.04f};
  static constexpr flux::Color swatchLightTop = flux::Color::hex(0xFFFFFF);
  static constexpr flux::Color swatchLightBottom = flux::Color::hex(0xDEE5F2);
  static constexpr flux::Color swatchDarkTop = flux::Color::hex(0x1D2440);
  static constexpr flux::Color swatchDarkBottom = flux::Color::hex(0x0C1024);

  static constexpr float kSidebarHeaderHeight = 48.f;
  static constexpr float kSidebarWidth = 220.f;
  static constexpr float kSidePad = 10.f;
  static constexpr float kSideItemRadius = 7.f;
  static constexpr float kMainPadH = 26.f;
  static constexpr float kMainPadV = 18.f;
};

} // namespace lambda_settings
