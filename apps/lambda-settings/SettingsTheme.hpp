#pragma once

#include <Lambda/Core/Color.hpp>

namespace lambda_settings {

/// Visual tokens aligned to the attached Lambda desktop design mock.
struct SettingsTheme {
  static constexpr lambda::Color accent = lambda::Color::hex(0x2A7FFF);
  static constexpr lambda::Color accent2 = lambda::Color::hex(0x5AA2FF);
  static constexpr lambda::Color text = lambda::Color::hex(0x16203A);
  static constexpr lambda::Color text2 = lambda::Color::hex(0x5B6781);
  static constexpr lambda::Color text3 = lambda::Color::hex(0x8893AD);
  static constexpr lambda::Color line = lambda::Color{0.08f, 0.12f, 0.24f, 0.08f};
  static constexpr lambda::Color line2 = lambda::Color{0.08f, 0.12f, 0.24f, 0.05f};
  static constexpr lambda::Color glassSoft = lambda::Color{1.f, 1.f, 1.f, 0.34f};
  static constexpr lambda::Color glassPanel = lambda::Color{1.f, 1.f, 1.f, 0.24f};
  static constexpr lambda::Color selectFill = lambda::Color{0.16f, 0.50f, 1.f, 0.14f};
  static constexpr lambda::Color selectBorder = lambda::Color{0.16f, 0.50f, 1.f, 0.35f};
  static constexpr lambda::Color hoverFill = lambda::Color{0.f, 0.f, 0.f, 0.04f};

  static constexpr float kSidebarHeaderHeight = 48.f;
  static constexpr float kSidebarWidth = 220.f;
  static constexpr float kSidePad = 10.f;
  static constexpr float kSideItemRadius = 7.f;
  static constexpr float kMainPadH = 26.f;
  static constexpr float kMainPadV = 18.f;
};

} // namespace lambda_settings
