#pragma once

/// \file Lambda/UI/WindowChrome.hpp
///
/// Public window titlebar geometry primitives.

#include <Lambda/Core/Geometry.hpp>

#include <cstdint>
#include <vector>

namespace lambdaui {

enum class WindowTitlebarMode : std::uint8_t {
  /// Use the platform/compositor default titlebar and controls.
  System,
  /// The app draws titlebar content while the platform/compositor owns controls when available.
  Integrated,
  /// The app draws all titlebar content and controls.
  Client,
  /// No titlebar or titlebar controls.
  None,
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
  WindowTitlebarMode titlebarMode = WindowTitlebarMode::System;
  float titlebarHeight = 0.f;
  std::vector<Rect> reservedRegions;
  bool systemControlsVisible = false;
  bool active = true;

  bool operator==(WindowChromeMetrics const& other) const = default;
};

} // namespace lambdaui
