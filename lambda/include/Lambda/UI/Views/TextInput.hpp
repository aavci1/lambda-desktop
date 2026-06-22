#pragma once

/// \file Lambda/UI/Views/TextInput.hpp
///
/// Part of the Lambda public API.

#include <Lambda/UI/Cursor.hpp>
#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Core/Color.hpp>
#include <Lambda/Graphics/AttributedString.hpp>
#include <Lambda/Graphics/Font.hpp>
#include <Lambda/Graphics/Styles.hpp>
#include <Lambda/UI/Detail/PrimitiveForwards.hpp>
#include <Lambda/UI/Element.hpp>
#include <Lambda/UI/Hooks.hpp>
#include <Lambda/UI/Input.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/Views/TextEditUtils.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace lambda {

class MountContext;
namespace scenegraph {
class SceneNode;
}

struct TextInputHeight {
    /// Fixed control height. `0` means size from content / style.
    float fixed = 0.f;
    /// Minimum intrinsic height for multiline inputs.
    float minIntrinsic = 80.f;
    /// Maximum intrinsic height for multiline inputs. `0` means no cap.
    float maxIntrinsic = 0.f;

    bool operator==(TextInputHeight const& other) const = default;
};

struct TextInput : ViewModifiers<TextInput> {
    struct Style {
        /// Text font.
        Reactive::Bindable<Font> font{Font::theme()};
        /// Main text color.
        Color textColor = Color::theme();
        /// Placeholder text color.
        Color placeholderColor = Color::theme();
        /// Input background fill.
        Color backgroundColor = Color::theme();
        /// Border color at rest.
        Color borderColor = Color::theme();
        /// Border color while focused.
        Color borderFocusColor = Color::theme();
        /// Caret color.
        Color caretColor = Color::theme();
        /// Selection highlight color.
        Color selectionColor = Color::theme();
        /// Tint applied when disabled.
        Color disabledColor = Color::theme();
        /// Border thickness at rest.
        float borderWidth = kFloatFromTheme;
        /// Border thickness while focused.
        float borderFocusWidth = kFloatFromTheme;
        /// Corner radius of the input chrome.
        float cornerRadius = kFloatFromTheme;
        /// Horizontal text inset.
        float paddingH = kFloatFromTheme;
        /// Vertical text inset.
        float paddingV = kFloatFromTheme;
        /// Explicit line height override. `0` uses font metrics.
        Reactive::Bindable<float> lineHeight{0.f};
        /// Legacy single-line height override. Prefer modifiers or `multilineHeight` for new code.
        float height = 0.f;

        static Style plain() {
            return Style {.backgroundColor = Colors::transparent, .borderColor = Colors::transparent, .borderFocusColor = Colors::transparent, .borderWidth = 0.f, .borderFocusWidth = 0.f, .cornerRadius = 0.f, .paddingH = 0.f, .paddingV = 0.f};
        }

        bool operator==(Style const& other) const = default;
    };

    /// Controlled text value.
    Signal<std::string> value {};
    /// Optional controlled caret/selection state.
    std::optional<Signal<detail::TextEditSelection>> selection {};
    /// Placeholder shown when `value` is empty.
    std::string placeholder;

    /// Optional syntax / attributed-run builder for custom text styling.
    std::function<std::vector<AttributedRun>(std::string_view)> styler;
    /// Optional validation tint derived from the current text.
    std::function<Color(std::string_view)> validationColor;

    /// Optional token overrides.
    Style style {};

    /// Enables multiline editing behavior when true.
    bool multiline = false;
    /// Wrapping strategy used by multiline editing.
    TextWrapping wrapping = TextWrapping::Wrap;
    /// Disables editing and focus when true.
    bool disabled = false;
    /// Maximum accepted character count. `0` means unlimited.
    int maxLength = 0;
    /// Height policy for multiline mode.
    TextInputHeight multilineHeight {};

    /// Called whenever the text changes.
    std::function<void(std::string const &)> onChange;
    /// Called whenever the text changes, after the caret/selection has been updated.
    std::function<void(std::string const &, detail::TextEditSelection)> onEdit;
    /// Called on submit/confirm action.
    std::function<void(std::string const &)> onSubmit;
    /// Called when Escape is pressed while the control is focused.
    std::function<void(std::string const &)> onEscape;
    /// Called before built-in key handling. Return true to consume the key.
    std::function<bool(KeyCode, Modifiers)> onPreviewKeyDown;
    /// Called before built-in semantic command handling. Return true to consume the command.
    std::function<bool(std::string const&)> onPreviewCommand;

    bool operator==(TextInput const& other) const {
        bool const sameSelection =
            (!selection && !other.selection) ||
            (selection && other.selection && *selection == *other.selection);
        return value == other.value && sameSelection &&
               placeholder == other.placeholder &&
               static_cast<bool>(styler) == static_cast<bool>(other.styler) &&
               static_cast<bool>(validationColor) == static_cast<bool>(other.validationColor) &&
               style == other.style && multiline == other.multiline &&
               wrapping == other.wrapping &&
               disabled == other.disabled && maxLength == other.maxLength &&
               multilineHeight == other.multilineHeight &&
               static_cast<bool>(onChange) == static_cast<bool>(other.onChange) &&
               static_cast<bool>(onEdit) == static_cast<bool>(other.onEdit) &&
               static_cast<bool>(onSubmit) == static_cast<bool>(other.onSubmit) &&
               static_cast<bool>(onEscape) == static_cast<bool>(other.onEscape) &&
               static_cast<bool>(onPreviewKeyDown) == static_cast<bool>(other.onPreviewKeyDown) &&
               static_cast<bool>(onPreviewCommand) == static_cast<bool>(other.onPreviewCommand);
    }

    Size measure(MeasureContext&, LayoutConstraints const&, LayoutHints const&, TextSystem&) const;
    std::unique_ptr<scenegraph::SceneNode> mount(MountContext&) const;
    Element body() const;
};

} // namespace lambda
