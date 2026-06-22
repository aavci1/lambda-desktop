#pragma once

/// \file Lambda/UI/InputFieldChrome.hpp
///
/// Shared theme resolution for field-style controls (e.g. \ref Picker trigger) — avoids duplicating
/// \c resolveColor / \c resolveFloat blocks. Shell decoration uses
/// \ref applyOuterInputFieldDecoration.

#include <Lambda/Core/Color.hpp>
#include <Lambda/Graphics/Styles.hpp>

namespace lambda {

struct Theme;
namespace detail {
struct ElementModifiers;
}

/// Raw tokens matching \ref TextInput field chrome (sentinels \c Color::theme() / \c kFloatFromTheme allowed).
struct InputFieldChromeSpec {
  Color textColor = Color::theme();
  Color placeholderColor = Color::theme();
  Color backgroundColor = Color::theme();
  Color borderColor = Color::theme();
  Color borderFocusColor = Color::theme();
  Color caretColor = Color::theme();
  Color selectionColor = Color::theme();
  Color disabledColor = Color::theme();
  float borderWidth = 1.f;
  float borderFocusWidth = 2.f;
  float cornerRadius = kFloatFromTheme;
  float paddingH = kFloatFromTheme;
  float paddingV = kFloatFromTheme;

  bool operator==(InputFieldChromeSpec const& other) const = default;
};

/// Resolved field chrome (uniform corner radius; use \c CornerRadius{cornerRadius} when a struct is needed).
struct ResolvedInputFieldChrome {
  Color textColor;
  Color placeholderColor;
  Color backgroundColor;
  Color borderColor;
  Color borderFocusColor;
  Color caretColor;
  Color selectionColor;
  Color disabledColor;
  float borderWidth;
  float borderFocusWidth;
  float cornerRadius;
  float paddingH;
  float paddingV;

  bool operator==(ResolvedInputFieldChrome const& other) const = default;
};

ResolvedInputFieldChrome resolveInputFieldChrome(InputFieldChromeSpec const& spec, Theme const& theme);

} // namespace lambda
