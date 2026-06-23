#pragma once

/// \file Lambda/UI/Views/Toggle.hpp
///
/// Part of the Lambda public API.


#include <Lambda/Core/Color.hpp>
#include <Lambda/UI/Element.hpp>
#include <Lambda/UI/Hooks.hpp>
#include <Lambda/UI/Theme.hpp>

#include <functional>

namespace lambdaui {

/// On/off switch control (track + thumb). Binds to \ref value, supports keyboard (Space/Return),
/// pointer, focus ring, and theme-driven motion.
struct Toggle : ViewModifiers<Toggle> {
  /// Visual tokens; any field may use \c Color::theme() / \c kFloatFromTheme to inherit from \ref Theme.
  struct Style {
    float trackWidth = kFloatFromTheme;
    float trackHeight = kFloatFromTheme;
    float thumbInset = kFloatFromTheme;
    float borderWidth = kFloatFromTheme;
    float thumbBorderWidth = kFloatFromTheme;
    Color onColor = Color::theme();
    Color offColor = Color::theme();
    Color thumbColor = Color::theme();
    Color thumbBorderColor = Color::theme();
    Color borderColor = Color::theme();

    bool operator==(Style const& other) const = default;
  };

  // ── State ──────────────────────────────────────────────────────────────────

  /// Current on/off state; typically from \c useState<bool>() in a parent or owned by this subtree.
  Signal<bool> value { };

  // ── Properties ─────────────────────────────────────────────────────────────

  /// When true, ignores input and uses disabled styling.
  bool disabled { false };
  Style style { };

  // ── Events ─────────────────────────────────────────────────────────────────

  /// Invoked after the toggle changes \c value (same as mutating \c value from handlers).
  std::function<void(bool)> onChange;

  bool operator==(Toggle const& other) const {
    return value == other.value && disabled == other.disabled && style == other.style;
  }


  // ── Component protocol ─────────────────────────────────────────────────────

  /// Builds the animated track/thumb tree. Call only from a composite \c body().
  Element body() const;
};

} // namespace lambdaui
