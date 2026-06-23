#pragma once

/// \file Lambda/UI/Views/Checkbox.hpp
///
/// Part of the Lambda public API.


#include <Lambda/Core/Color.hpp>
#include <Lambda/Reactive/Bindable.hpp>
#include <Lambda/UI/Element.hpp>
#include <Lambda/UI/Hooks.hpp>
#include <Lambda/UI/Theme.hpp>

#include <functional>

namespace lambdaui {

/// Boolean box with optional indeterminate state and checkmark. Uses \ref Theme checkbox tokens
/// when style fields use sentinels.
struct Checkbox : ViewModifiers<Checkbox> {
  // ── Binding ──────────────────────────────────────────────────────────────

  /// Checked state; bind with \c useState<bool>() or equivalent.
  Signal<bool> value { };

  // ── Indeterminate ────────────────────────────────────────────────────────

  /// Dash when true; tap sets value true — clear indeterminate in onChange.
  Reactive::Bindable<bool> indeterminate{false};

  // ── Layout ───────────────────────────────────────────────────────────────
  // Flex: use chained `.flex(...)` on the `Element` from `body()`.

  // ── Behaviour ────────────────────────────────────────────────────────────

  Reactive::Bindable<bool> disabled{false};

  // ── Style ────────────────────────────────────────────────────────────────

  struct Style {
    float boxSize = kFloatFromTheme;
    float cornerRadius = kFloatFromTheme;
    float borderWidth = kFloatFromTheme;
    Color checkedColor = Color::theme();
    Color uncheckedColor = Color::theme();
    Color checkColor = Color::theme();
    Color borderColor = Color::theme();

    bool operator==(Style const& other) const = default;
  };

  Style style { };


  // ── Events ───────────────────────────────────────────────────────────────

  /// Fires when the user toggles or activates via keyboard; update \c value and clear indeterminate as needed.
  std::function<void(bool)> onChange;

  bool operator==(Checkbox const& other) const {
    bool const sameIndeterminate = indeterminate.isValue() && other.indeterminate.isValue() &&
                                   indeterminate.value() == other.indeterminate.value();
    bool const sameDisabled = disabled.isValue() && other.disabled.isValue() &&
                              disabled.value() == other.disabled.value();
    return value == other.value && sameIndeterminate && sameDisabled && style == other.style;
  }

  // ── Component protocol ───────────────────────────────────────────────────

  Element body() const;
};

} // namespace lambdaui
