#include <Lambda/UI/Views/Badge.hpp>
#include <Lambda/UI/Hooks.hpp>
#include <Lambda/UI/Views/Text.hpp>

namespace lambda {

namespace {

Badge::Style resolveStyle(Badge::Style const &style, Theme const &theme) {
    return Badge::Style {
        .font = resolveFont(style.font, theme.captionFont, theme),
        .paddingH = resolveFloat(style.paddingH, theme.space2),
        .paddingV = resolveFloat(style.paddingV, theme.space1),
        .cornerRadius = resolveFloat(style.cornerRadius, theme.radiusFull),
        .foregroundColor = resolveColor(style.foregroundColor, theme.labelColor, theme),
        .backgroundColor = resolveColor(style.backgroundColor, theme.selectedContentBackgroundColor, theme),
    };
}

} // namespace

Element Badge::body() const {
    Badge::Style const resolved = resolveStyle(style, lambda::useEnvironment<ThemeKey>()());
    return Text {
        .text = label,
        .font = resolved.font,
        .color = resolved.foregroundColor,
        .horizontalAlignment = HorizontalAlignment::Center,
    }
        .padding(resolved.paddingV, resolved.paddingH, resolved.paddingV, resolved.paddingH)
        .fill(FillStyle::solid(resolved.backgroundColor))
        .cornerRadius(resolved.cornerRadius);
}

} // namespace lambda
