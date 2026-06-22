#pragma once

/// \file Lambda/UI/Views/Dialog.hpp
///
/// Part of the Lambda public API.

#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Core/Color.hpp>
#include <Lambda/Graphics/Font.hpp>
#include <Lambda/Graphics/Styles.hpp>
#include <Lambda/UI/Element.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/ViewModifiers.hpp>

#include <functional>
#include <string>
#include <vector>

namespace lambda {

/// Centered application dialog shell with a title bar, close affordance, content area, and
/// optional footer row. The content and footer are supplied by the application.
struct Dialog : ViewModifiers<Dialog> {
  struct Style {
    /// Dialog card width. `kFloatFromTheme` resolves to `Theme::dialogWidth`.
    float width = kFloatFromTheme;
    /// Spacing between header children.
    float headerSpacing = kFloatFromTheme;
    /// Spacing between content rows.
    float contentSpacing = kFloatFromTheme;
    /// Spacing between footer children.
    float footerSpacing = kFloatFromTheme;
    /// Header padding. Per-edge `kFloatFromTheme` resolves to `Theme::dialogHeaderPadding`.
    EdgeInsets headerPadding{kFloatFromTheme, kFloatFromTheme, kFloatFromTheme, kFloatFromTheme};
    /// Content padding. Per-edge `kFloatFromTheme` resolves to `Theme::dialogContentPadding`.
    EdgeInsets contentPadding{kFloatFromTheme, kFloatFromTheme, kFloatFromTheme, kFloatFromTheme};
    /// Footer padding. Per-edge `kFloatFromTheme` resolves to `Theme::dialogFooterPadding`.
    EdgeInsets footerPadding{kFloatFromTheme, kFloatFromTheme, kFloatFromTheme, kFloatFromTheme};
    /// Title font.
    Font titleFont = Font::theme();
    /// Title text color.
    Color titleColor = Color::theme();
    /// Card fill color.
    Color surfaceColor = Color::theme();
    /// Card stroke color.
    Color surfaceStrokeColor = Color::theme();
    /// Divider color.
    Color dividerColor = Color::theme();
    /// Footer row fill color.
    Color footerColor = Color::theme();
    /// Card stroke width.
    float surfaceStrokeWidth = kFloatFromTheme;
    /// Divider thickness.
    float dividerThickness = kFloatFromTheme;
    /// Card corner radius.
    float cornerRadius = kFloatFromTheme;
    /// Card shadow radius.
    float shadowRadius = kFloatFromTheme;
    /// Card shadow horizontal offset.
    float shadowOffsetX = kFloatFromTheme;
    /// Card shadow vertical offset.
    float shadowOffsetY = kFloatFromTheme;
    /// Card shadow color.
    Color shadowColor = Color::theme();
    /// Close button square size.
    float closeButtonSize = kFloatFromTheme;
    /// Close button hover background radius.
    float closeButtonCornerRadius = kFloatFromTheme;
    /// Close icon size.
    float closeIconSize = kFloatFromTheme;
    /// Close icon weight.
    float closeIconWeight = kFloatFromTheme;
    /// Close icon color.
    Color closeIconColor = Color::theme();
    /// Close button hover fill color.
    Color closeHoverColor = Color::theme();

    bool operator==(Style const& other) const = default;
  };

  std::string title;
  std::vector<Element> content;
  std::vector<Element> footer;
  std::function<void()> onClose;
  /// Optional token overrides. Defaults resolve from `Theme` like other controls.
  Style style {};

  Element body() const;
};

} // namespace lambda
