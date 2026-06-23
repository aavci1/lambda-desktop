#pragma once

/// \file Lambda/UI/Views/SegmentedControl.hpp
///
/// Part of the Lambda public API.

#include <Lambda/Core/Color.hpp>
#include <Lambda/Graphics/Font.hpp>
#include <Lambda/UI/Element.hpp>
#include <Lambda/UI/Hooks.hpp>
#include <Lambda/UI/Theme.hpp>

#include <functional>
#include <string>
#include <vector>

namespace lambdaui {

struct SegmentedControlOption {
    /// Segment label text.
    std::string label;
    /// Prevents selection when true.
    bool disabled = false;

    bool operator==(SegmentedControlOption const& other) const = default;
};

struct SegmentedControl : ViewModifiers<SegmentedControl> {
    struct Style {
        /// Segment label font.
        Font font = Font::theme();
        /// Horizontal inset for each segment.
        float paddingH = kFloatFromTheme;
        /// Vertical inset for each segment.
        float paddingV = kFloatFromTheme;
        /// Outer track and segment corner radius.
        float cornerRadius = kFloatFromTheme;
        /// Accent used for the selected segment.
        Color accentColor = Color::theme();
        /// Background track color.
        Color trackColor = Color::theme();
        /// Track border color.
        Color borderColor = Color::theme();

        bool operator==(Style const& other) const = default;
    };

    /// Controlled selected segment index.
    Signal<int> selectedIndex {};
    /// Segment definitions in visual order.
    std::vector<SegmentedControlOption> options;
    /// Disables the whole control when true.
    bool disabled = false;
    /// Optional token overrides.
    Style style {};
    /// Called after the user changes selection.
    std::function<void(int)> onChange;

    bool operator==(SegmentedControl const& other) const {
        return selectedIndex == other.selectedIndex && options == other.options &&
               disabled == other.disabled && style == other.style &&
               static_cast<bool>(onChange) == static_cast<bool>(other.onChange);
    }

    Element body() const;
};

} // namespace lambdaui
