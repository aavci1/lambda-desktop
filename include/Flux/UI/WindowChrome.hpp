#pragma once

/// \file Flux/UI/WindowChrome.hpp
///
/// Public window decoration and titlebar geometry primitives.

#include <Flux/Core/Geometry.hpp>

#include <cstdint>
#include <vector>

namespace flux {

enum class WindowDecorationMode : std::uint8_t {
  /// Use the platform default titlebar/decorations.
  System,
  /// The app draws all titlebar/chrome content and window controls.
  ClientSide,
  /// The app draws titlebar content while the platform/compositor owns window controls when available.
  IntegratedTitlebar,
};

enum class WindowResizeEdge : std::uint8_t {
  None,
  Top,
  Bottom,
  Left,
  Right,
  TopLeft,
  TopRight,
  BottomLeft,
  BottomRight,
};

struct WindowChromeMetrics {
  WindowDecorationMode decorationMode = WindowDecorationMode::System;
  float titlebarHeight = 0.f;
  std::vector<Rect> reservedRegions;
  bool nativeControlsVisible = false;
  bool active = true;

  bool operator==(WindowChromeMetrics const& other) const = default;
};

} // namespace flux
