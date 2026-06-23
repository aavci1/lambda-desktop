#pragma once

/// \file Lambda/UI/VisualTokens.hpp
///
/// Shared Lambda desktop material and color tokens.

#include <Lambda/Core/Color.hpp>

namespace lambdaui {

struct VisualTokens {
  static constexpr Color windowSurface{245.f / 255.f, 248.f / 255.f, 252.f / 255.f, 0.78f};
  static constexpr Color elevatedSurface{245.f / 255.f, 248.f / 255.f, 252.f / 255.f, 0.84f};
  static constexpr Color dockSurface{1.f, 1.f, 1.f, 97.f * (1.f / 255.f)};
  static constexpr Color sidebarSurface{225.f / 255.f, 235.f / 255.f, 245.f / 255.f, 0.42f};
  static constexpr Color controlSurface{1.f, 1.f, 1.f, 0.46f};
  static constexpr Color controlSurfaceHover{1.f, 1.f, 1.f, 0.58f};
  static constexpr Color selectedRow{80.f / 255.f, 140.f / 255.f, 1.f, 0.14f};
  static constexpr Color selectedRowStrong{45.f / 255.f, 125.f / 255.f, 1.f, 0.90f};
  static constexpr Color border{1.f, 1.f, 1.f, 0.60f};
  static constexpr Color separator{20.f / 255.f, 35.f / 255.f, 60.f / 255.f, 0.08f};
  static constexpr Color separatorSoft{20.f / 255.f, 35.f / 255.f, 60.f / 255.f, 0.05f};
  static constexpr Color primaryText{18.f / 255.f, 28.f / 255.f, 45.f / 255.f, 0.92f};
  static constexpr Color secondaryText{70.f / 255.f, 86.f / 255.f, 110.f / 255.f, 0.68f};
  static constexpr Color tertiaryText{70.f / 255.f, 86.f / 255.f, 110.f / 255.f, 0.50f};
  static constexpr Color hoverFill{20.f / 255.f, 35.f / 255.f, 60.f / 255.f, 0.06f};
  static constexpr Color pressedFill{20.f / 255.f, 35.f / 255.f, 60.f / 255.f, 0.10f};
  static constexpr Color accent{45.f / 255.f, 125.f / 255.f, 1.f, 1.f};
  static constexpr Color accentSoft{80.f / 255.f, 140.f / 255.f, 1.f, 0.18f};
  static constexpr Color windowShadow{15.f / 255.f, 35.f / 255.f, 60.f / 255.f, 0.22f};
  static constexpr Color dockShadow{20.f / 255.f, 40.f / 255.f, 70.f / 255.f, 0.20f};
  static constexpr Color controlShadow{20.f / 255.f, 40.f / 255.f, 70.f / 255.f, 0.12f};
};

} // namespace lambdaui
