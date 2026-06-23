#pragma once

/// \file Lambda/UI/Views/ProgressBar.hpp
///
/// Part of the Lambda public API.

#include <Lambda/UI/Element.hpp>
#include <Lambda/UI/Theme.hpp>

namespace lambdaui {

struct ProgressBar : ViewModifiers<ProgressBar> {
    struct Style {
        /// Filled-track color.
        Color activeColor = Color::theme();
        /// Empty-track color.
        Color inactiveColor = Color::theme();
        /// Track thickness.
        float trackHeight = kFloatFromTheme;

        bool operator==(Style const& other) const = default;
    };

    /// Progress in `[0, 1]`.
    float progress = 0.f;
    /// Optional token overrides.
    Style style {};

    bool operator==(ProgressBar const& other) const {
        return progress == other.progress && style == other.style;
    }
    Element body() const;
};

} // namespace lambdaui
