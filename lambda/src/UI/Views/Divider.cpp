#include <Lambda/UI/Views/Divider.hpp>
#include <Lambda/UI/Hooks.hpp>
#include <Lambda/UI/Views/Rectangle.hpp>

#include <algorithm>

namespace lambda {

namespace {

Divider::Style resolveStyle(Divider::Style const &style, Theme const &theme) {
    float const thickness = std::max(1.f, resolveFloat(style.thickness, 1.f));
    return Divider::Style {
        .thickness = thickness,
        .cornerRadius = resolveFloat(style.cornerRadius, thickness * 0.5f),
        .color = resolveColor(style.color, theme.separatorColor, theme),
    };
}

} // namespace

Element Divider::body() const {
    Divider::Style const resolved = resolveStyle(style, lambda::useEnvironment<ThemeKey>()());
    return Rectangle {}
        .size(orientation == Orientation::Horizontal ? 0.f : resolved.thickness,
              orientation == Orientation::Vertical ? 0.f : resolved.thickness)
        .cornerRadius(resolved.cornerRadius)
        .fill(FillStyle::solid(resolved.color));
}

} // namespace lambda
