#pragma once

/// \file Lambda/UI/Views/Divider.hpp
///
/// Part of the Lambda public API.

#include <Lambda/UI/Element.hpp>
#include <Lambda/UI/Theme.hpp>

namespace lambdaui {

struct Divider : ViewModifiers<Divider> {
    enum class Orientation : std::uint8_t {
        Horizontal,
        Vertical,
    };

    struct Style {
        float thickness = kFloatFromTheme;
        float cornerRadius = kFloatFromTheme;
        Color color = Color::theme();

        bool operator==(Style const& other) const = default;
    };

    /// Divider direction.
    Orientation orientation = Orientation::Horizontal;
    /// Optional token overrides.
    Style style {};

    bool operator==(Divider const& other) const {
        return orientation == other.orientation && style == other.style;
    }
    Element body() const;
};

} // namespace lambdaui
