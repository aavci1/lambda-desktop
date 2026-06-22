#include <Lambda/UI/Views/ProgressBar.hpp>
#include <Lambda/UI/Hooks.hpp>
#include <Lambda/UI/Views/Rectangle.hpp>
#include <Lambda/UI/Views/ZStack.hpp>

#include <algorithm>

namespace lambda {

namespace {

constexpr float kDefaultProgressBarWidth = 160.f;

ProgressBar::Style resolveStyle(ProgressBar::Style const &style, Theme const &theme) {
    return ProgressBar::Style {
        .activeColor = resolveColor(style.activeColor, theme.accentColor, theme),
        .inactiveColor = resolveColor(style.inactiveColor, theme.disabledControlBackgroundColor, theme),
        .trackHeight = std::max(1.f, resolveFloat(style.trackHeight, theme.sliderTrackHeight)),
    };
}

} // namespace

Element ProgressBar::body() const {
    ProgressBar::Style const resolved = resolveStyle(style, lambda::useEnvironment<ThemeKey>()());
    float const clamped = std::clamp(progress, 0.f, 1.f);
    Rect const bounds = useBounds();
    float const componentWidth = bounds.width > 0.f ? bounds.width : kDefaultProgressBarWidth;

    std::vector<Element> childrenList;
    childrenList.reserve(clamped > 0.f ? 2 : 1);
    childrenList.push_back(
        Rectangle {}
            .fill(FillStyle::solid(resolved.inactiveColor))
            .size(componentWidth, resolved.trackHeight)
            .cornerRadius(CornerRadius {resolved.trackHeight * 0.5f})
    );
    if (clamped > 0.f) {
        childrenList.push_back(
            Rectangle {}
                .fill(FillStyle::solid(resolved.activeColor))
                .size(componentWidth * clamped, resolved.trackHeight)
                .cornerRadius(CornerRadius {resolved.trackHeight * 0.5f})
        );
    }

    return Element {ZStack {
        .horizontalAlignment = Alignment::Start,
        .verticalAlignment = Alignment::Start,
        .children = std::move(childrenList),
    }}
        .size(componentWidth, resolved.trackHeight);
}

} // namespace lambda
