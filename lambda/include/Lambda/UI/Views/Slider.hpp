#pragma once

/// \file Lambda/UI/Views/Slider.hpp
///
/// Part of the Lambda public API.


#include <Lambda/Core/Color.hpp>
#include <Lambda/UI/Element.hpp>
#include <Lambda/UI/Hooks.hpp>
#include <Lambda/UI/Theme.hpp>

#include <functional>

namespace lambda {

/// Horizontal slider for a bounded numeric \ref value. Supports pointer drag, scroll wheel where
/// applicable, arrow keys with optional \c step snapping, and focus styling.
struct Slider : ViewModifiers<Slider> {
  struct Style {
    Color activeColor = Color::theme();
    Color inactiveColor = Color::theme();
    Color thumbColor = Color::theme();
    Color thumbBorderColor = Color::theme();
    float trackHeight = kFloatFromTheme;
    float thumbSize = kFloatFromTheme;

    bool operator==(Style const& other) const = default;
  };

  // ── State ──────────────────────────────────────────────────────────────────

  /// Current value in \c [min, max]; use \c useState<float>() or similar.
  Signal<float> value { };

  // ── Properties ─────────────────────────────────────────────────────────────

  /// Inclusive range endpoints.
  float min { 0.f };
  float max { 1.f };
  /// If \c > 0, pointer and keyboard changes snap to this increment; \c 0 means continuous (pointer)
  /// and small keyboard nudges based on range.
  float step { 0.f };
  bool disabled { false };
  Style style { };

  // ── Events ───────────────────────────────────────────────────────────────

  /// Called whenever \c value changes from user interaction.
  std::function<void(float)> onChange;

  bool operator==(Slider const& other) const {
    return value == other.value && min == other.min && max == other.max && step == other.step &&
           disabled == other.disabled && style == other.style;
  }

  // ── Component protocol ───────────────────────────────────────────────────

  Element body() const;
};

} // namespace lambda
