#pragma once

/// \file Lambda/UI/Views/Card.hpp
///
/// Part of the Lambda public API.

#include <Lambda/Graphics/Styles.hpp>
#include <Lambda/Reactive/Bindable.hpp>
#include <Lambda/UI/Element.hpp>
#include <Lambda/UI/Theme.hpp>

namespace lambdaui {

/// Surface container for card-like content: padded elevated background, optional border, radius, and shadow.
struct Card : ViewModifiers<Card> {
  struct Style {
    /// Uniform content inset. `paddingH` / `paddingV` override per-axis when set.
    float padding = kFloatFromTheme;
    /// Horizontal content inset override.
    float paddingH = kFloatFromTheme;
    /// Vertical content inset override.
    float paddingV = kFloatFromTheme;
    /// Card corner radius.
    float cornerRadius = kFloatFromTheme;
    /// Border thickness.
    float borderWidth = kFloatFromTheme;
    /// Card fill color.
    Reactive::Bindable<Color> backgroundColor{Color::theme()};
    /// Card stroke color.
    Reactive::Bindable<Color> borderColor{Color::theme()};
    /// Card shadow style.
    Reactive::Bindable<ShadowStyle> shadow{ShadowStyle::none()};

    bool operator==(Style const& other) const {
      bool const sameBackground = backgroundColor.isValue() && other.backgroundColor.isValue() &&
                                  backgroundColor.value() == other.backgroundColor.value();
      bool const sameBorder = borderColor.isValue() && other.borderColor.isValue() &&
                              borderColor.value() == other.borderColor.value();
      bool const sameShadow = shadow.isValue() && other.shadow.isValue() &&
                              shadow.value() == other.shadow.value();
      return padding == other.padding && paddingH == other.paddingH && paddingV == other.paddingV &&
             cornerRadius == other.cornerRadius && borderWidth == other.borderWidth &&
             sameBackground && sameBorder && sameShadow;
    }
  };

  /// Card body content.
  Element child;
  /// Optional token overrides.
  Style style {};

  Element body() const;
};

} // namespace lambdaui
