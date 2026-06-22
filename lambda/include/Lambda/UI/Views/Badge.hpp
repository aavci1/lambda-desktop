#pragma once

/// \file Lambda/UI/Views/Badge.hpp
///
/// Part of the Lambda public API.

#include <Lambda/Graphics/Font.hpp>
#include <Lambda/UI/Element.hpp>
#include <Lambda/UI/Theme.hpp>

#include <string>

namespace lambda {

struct Badge : ViewModifiers<Badge> {
    struct Style {
        Font font = Font::theme();
        float paddingH = kFloatFromTheme;
        float paddingV = kFloatFromTheme;
        float cornerRadius = kFloatFromTheme;
        Color foregroundColor = Color::theme();
        Color backgroundColor = Color::theme();

        bool operator==(Style const& other) const = default;
    };

    std::string label;
    Style style {};

    bool operator==(Badge const& other) const {
        return label == other.label && style == other.style;
    }
    Element body() const;
};

} // namespace lambda
