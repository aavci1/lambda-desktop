#pragma once

/// \file Lambda/UI/Views/Button.hpp
///
/// Part of the Lambda public API.

#include <Lambda/Core/Color.hpp>
#include <Lambda/Graphics/Font.hpp>
#include <Lambda/Graphics/Styles.hpp>
#include <Lambda/UI/Element.hpp>
#include <Lambda/UI/Hooks.hpp>
#include <Lambda/UI/IconName.hpp>
#include <Lambda/UI/Theme.hpp>

#include <functional>
#include <string>

namespace lambda {

enum class ButtonVariant : std::uint8_t {
    Primary,
    Secondary,
    Destructive,
    Ghost,
};

struct Button : ViewModifiers<Button> {
    struct Style {
        /// Label font.
        Font font = Font::theme();
        /// Horizontal content inset inside the button chrome.
        float paddingH = kFloatFromTheme;
        /// Vertical content inset inside the button chrome.
        float paddingV = kFloatFromTheme;
        /// Corner radius of the button background.
        float cornerRadius = kFloatFromTheme;
        /// Accent used by primary / secondary variants.
        Color accentColor = Color::theme();
        /// Accent used by destructive variant.
        Color destructiveColor = Color::theme();

        bool operator==(Style const& other) const = default;
    };

    /// Button label text.
    Reactive::Bindable<std::string> label{std::string{}};
    /// Visual treatment preset.
    ButtonVariant variant = ButtonVariant::Primary;
    /// Disables interaction and uses disabled styling when true.
    Reactive::Bindable<bool> disabled{false};
    /// Optional token overrides.
    Style style {};
    /// Called when the button is activated.
    std::function<void()> onTap;

    bool operator==(Button const& other) const {
        bool const sameLabel = label.isValue() && other.label.isValue() && label.value() == other.label.value();
        bool const sameDisabled = disabled.isValue() && other.disabled.isValue() &&
                                  disabled.value() == other.disabled.value();
        return sameLabel && variant == other.variant && sameDisabled &&
               style == other.style;
    }

    Element body() const;
};

struct LinkButton : ViewModifiers<LinkButton> {
    struct Style {
        /// Label font.
        Font font = Font::theme();
        /// Link text color.
        Color color = Color::theme();

        bool operator==(Style const& other) const = default;
    };

    /// Link label text.
    Reactive::Bindable<std::string> label{std::string{}};
    /// Disables interaction and uses disabled styling when true.
    Reactive::Bindable<bool> disabled{false};
    /// Optional token overrides.
    Style style {};
    /// Called when the link button is activated.
    std::function<void()> onTap;

    bool operator==(LinkButton const& other) const {
        bool const sameLabel = label.isValue() && other.label.isValue() && label.value() == other.label.value();
        bool const sameDisabled = disabled.isValue() && other.disabled.isValue() &&
                                  disabled.value() == other.disabled.value();
        return sameLabel && sameDisabled && style == other.style;
    }

    Element body() const;
};

struct IconButton : ViewModifiers<IconButton> {
    struct Style {
        /// Square button size.
        float size = kFloatFromTheme;
        /// Icon stroke / glyph weight.
        float weight = kFloatFromTheme;
        /// Icon color.
        Color color = Color::theme();

        bool operator==(Style const& other) const = default;
    };

    /// Icon glyph to render.
    IconName icon {};
    /// Disables interaction and uses disabled styling when true.
    Reactive::Bindable<bool> disabled{false};
    /// Optional token overrides.
    Style style {};
    /// Called when the icon button is activated.
    std::function<void()> onTap;

    bool operator==(IconButton const& other) const {
        bool const sameDisabled = disabled.isValue() && other.disabled.isValue() &&
                                  disabled.value() == other.disabled.value();
        return icon == other.icon && sameDisabled && style == other.style;
    }

    Element body() const;
};

} // namespace lambda
