#pragma once

/// \file Lambda/UI/Views/Select.hpp
///
/// Part of the Lambda public API.

#include <Lambda/Core/Color.hpp>
#include <Lambda/Graphics/Font.hpp>
#include <Lambda/UI/Element.hpp>
#include <Lambda/UI/Hooks.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/Views/PopoverPlacement.hpp>

#include <functional>
#include <string>
#include <vector>

namespace lambda {

enum class SelectTriggerMode : std::uint8_t {
    /// Button-like field with border and chevron.
    Field,
    /// Link-style trigger with minimal chrome.
    Link,
};

/// One menu row in a \ref Select.
struct SelectOption {
    /// Primary row label.
    std::string label;
    /// Optional secondary description shown in the menu and, optionally, trigger.
    std::string detail;
    /// Prevents selection when true.
    bool disabled = false;

    bool operator==(SelectOption const& other) const = default;
};

/// Single-select dropdown backed by a popover menu.
///
/// `selectedIndex == -1` means “no selection”, which shows \ref placeholder when present.
struct Select : ViewModifiers<Select> {
    struct Style {
        /// Primary label font.
        Font labelFont = Font::theme();
        /// Secondary detail font.
        Font detailFont = Font::theme();
        /// Trigger corner radius.
        float cornerRadius = kFloatFromTheme;
        /// Menu surface corner radius.
        float menuCornerRadius = kFloatFromTheme;
        /// Maximum menu height before scrolling.
        float menuMaxHeight = kFloatFromTheme;
        /// Maximum menu width. `0` means unconstrained except by viewport.
        float menuMaxWidth = 0.f;
        /// Minimum menu width before trigger matching is applied.
        float minMenuWidth = kFloatFromTheme;
        /// Accent used for focused / selected affordances.
        Color accentColor = Color::theme();
        /// Trigger background color.
        Color fieldColor = Color::theme();
        /// Trigger background color while hovered.
        Color fieldHoverColor = Color::theme();
        /// Trigger border color.
        Color borderColor = Color::theme();
        /// Menu row hover background.
        Color rowHoverColor = Color::theme();

        bool operator==(Style const& other) const = default;
    };

    /// Controlled selection state. When omitted, the control manages its own local selection.
    Signal<int> selectedIndex = Signal<int>{-1};

    /// Menu options in display order.
    std::vector<SelectOption> options;

    /// Trigger text shown when there is no current selection.
    std::string placeholder = "Select an option";
    /// Optional helper text shown below the trigger.
    std::string helperText;
    /// Fallback text shown when the options list is empty.
    std::string emptyText = "No options available";

    /// Disables the control when true.
    bool disabled = false;
    /// Shows a checkmark beside the selected option in the menu.
    bool showCheckmark = true;
    /// Dismisses the menu immediately after a selection.
    bool dismissOnSelect = true;
    /// Shows option detail text in the trigger for the selected row.
    bool showDetailInTrigger = true;
    /// Expands the menu width to at least the trigger width.
    bool matchTriggerWidth = true;

    /// Trigger chrome style.
    SelectTriggerMode triggerMode = SelectTriggerMode::Field;

    /// Preferred popover placement relative to the trigger.
    PopoverPlacement placement = PopoverPlacement::Below;
    /// Optional token overrides.
    Style style {};

    /// Called after user selection changes. Receives the selected option index.
    std::function<void(int)> onChange;

    bool operator==(Select const& other) const {
        return selectedIndex == other.selectedIndex && options == other.options &&
               placeholder == other.placeholder && helperText == other.helperText &&
               emptyText == other.emptyText && disabled == other.disabled &&
               showCheckmark == other.showCheckmark && dismissOnSelect == other.dismissOnSelect &&
               showDetailInTrigger == other.showDetailInTrigger &&
               matchTriggerWidth == other.matchTriggerWidth && triggerMode == other.triggerMode &&
               placement == other.placement && style == other.style &&
               static_cast<bool>(onChange) == static_cast<bool>(other.onChange);
    }

    Element body() const;
};

} // namespace lambda
