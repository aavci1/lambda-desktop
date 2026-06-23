#pragma once

/// \file Lambda/UI/Views/Icon.hpp
///
/// Part of the Lambda public API.


#include <Lambda/Core/Color.hpp>
#include <Lambda/Reactive/Bindable.hpp>
#include <Lambda/UI/Element.hpp>
#include <Lambda/UI/IconName.hpp>

namespace lambdaui {

struct Icon : ViewModifiers<Icon> {

    // ── Properties ───────────────────────────────────────────────────────────
    Reactive::Bindable<IconName> name {};

    /// Icon size in points. Drives both the font size and the component's intrinsic frame.
    /// `kFloatFromTheme` → `Theme::bodyFont.size`.
    float size = kFloatFromTheme;

    /// Icon weight. `kFloatFromTheme` → `Theme::bodyFont.weight`.
    float weight = kFloatFromTheme;

    /// Icon color. `Color::theme()` → `Theme::labelColor`.
    Reactive::Bindable<Color> color{Color::theme()};

    bool operator==(Icon const& other) const {
        bool const sameName = name.isValue() && other.name.isValue() && name.value() == other.name.value();
        bool const sameColor = color.isValue() && other.color.isValue() && color.value() == other.color.value();
        return sameName && size == other.size && weight == other.weight &&
               sameColor;
    }

    // ── Component protocol ───────────────────────────────────────────────────
    Element body() const;
};

} // namespace lambdaui
