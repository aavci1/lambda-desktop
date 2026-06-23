#include <Lambda/UI/Views/Toggle.hpp>

#include <Lambda/UI/KeyCodes.hpp>
#include <Lambda/Reactive/Interpolatable.hpp>
#include <Lambda/Reactive/Transition.hpp>
#include <Lambda/UI/Hooks.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/Views/Rectangle.hpp>
#include <Lambda/UI/Views/ScaleAroundCenter.hpp>
#include <Lambda/UI/Views/ZStack.hpp>

#include <algorithm>
#include <cmath>

namespace lambdaui {

Toggle::Style resolveStyle(Toggle::Style const &style, Theme const &theme) {
    return Toggle::Style {
        .trackWidth = std::max(1.f, resolveFloat(style.trackWidth, theme.toggleTrackWidth)),
        .trackHeight = std::max(1.f, resolveFloat(style.trackHeight, theme.toggleTrackHeight)),
        .thumbInset = std::max(0.f, resolveFloat(style.thumbInset, theme.toggleThumbInset)),
        .borderWidth = std::max(0.f, resolveFloat(style.borderWidth, theme.toggleBorderWidth)),
        .thumbBorderWidth = std::max(0.f, resolveFloat(style.thumbBorderWidth, theme.toggleThumbBorderWidth)),
        .onColor = resolveColor(style.onColor, theme.toggleOnColor, theme),
        .offColor = resolveColor(style.offColor, theme.toggleOffColor, theme),
        .thumbColor = resolveColor(style.thumbColor, theme.toggleThumbColor, theme),
        .thumbBorderColor = resolveColor(style.thumbBorderColor, theme.toggleThumbBorderColor, theme),
        .borderColor = resolveColor(style.borderColor, theme.toggleBorderColor, theme),
    };
}

Element Toggle::body() const {
    auto theme = useEnvironment<ThemeKey>();

    auto [trackWidth,
          trackHeight,
          thumbInset,
          borderWidth,
          thumbBorderWidth,
          onColor,
          offColor,
          thumbColor,
          thumbBorderColor,
          borderColor] = resolveStyle(style, theme());
    auto disabledColor = theme().disabledTextColor;
    auto focusColor = theme().keyboardFocusIndicatorColor;

    float const maxInset = std::max(0.f, trackHeight * 0.5f - 0.5f);
    thumbInset = std::min(thumbInset, maxInset);
    float const thumbSize = std::max(1.f, trackHeight - 2.f * thumbInset);
    trackWidth = std::max(trackWidth, thumbSize + 2.f * thumbInset);
    float const xOff = thumbInset;
    float const xOn = std::max(xOff, trackWidth - thumbInset - thumbSize);

    Reactive::Signal<bool> focused = useFocus();
    Reactive::Signal<bool> pressed = usePress();
    bool const isDisabled = disabled;
    auto v = value;

    auto targetMotion = [theme, isDisabled] {
        return isDisabled ? Transition::instant() : Transition::ease(theme().durationMedium);
    };
    auto pressMotion = [theme] {
        return Transition::ease(theme().durationFast);
    };
    auto thumbXTarget = [v, xOn, xOff] {
        return v() ? xOn : xOff;
    };
    auto trackFillTarget = [theme, style = style, v, isDisabled] {
        Theme const &currentTheme = theme();
        Toggle::Style const currentStyle = resolveStyle(style, currentTheme);
        return isDisabled ? currentTheme.disabledControlBackgroundColor
                          : v() ? currentStyle.onColor : currentStyle.offColor;
    };
    auto scaleTarget = [pressed, isDisabled] {
        return (pressed() && !isDisabled) ? 0.90f : 1.f;
    };

    auto thumbXAnim = useAnimated(thumbXTarget, targetMotion);
    auto trackFillAnim = useAnimated(trackFillTarget, targetMotion);
    auto scaleAnim = useAnimated(scaleTarget, pressMotion);

    auto handleToggle = [v, onChange = onChange, isDisabled]() {
        if (isDisabled) {
            return;
        }
        bool const next = !v.peek();
        v = next;
        if (onChange) {
            onChange(next);
        }
    };

    auto handleKey = [handleToggle](KeyCode k, Modifiers) {
        if (k == keys::Space || k == keys::Return) {
            handleToggle();
        }
    };

    return ScaleAroundCenter {
        .scale = [scaleAnim] {
            return scaleAnim();
        },
        .child = ZStack {
            .horizontalAlignment = Alignment::Start,
            .verticalAlignment = Alignment::Start,
            .children = lambdaui::children(
                Rectangle {}
                    .fill([trackFillAnim] {
                        return trackFillAnim();
                    })
                    .stroke([focused, focusColor, borderColor, borderWidth] {
                        return StrokeStyle::solid(focused() ? focusColor : borderColor,
                                                  focused() ? std::max(borderWidth, 2.f) : borderWidth);
                    })
                    .size(trackWidth, trackHeight)
                    .cornerRadius(CornerRadius {trackHeight * 0.5f}),
                Rectangle {}
                    .fill(FillStyle::solid(isDisabled ? disabledColor : thumbColor))
                    .stroke(StrokeStyle::solid(thumbBorderColor, thumbBorderWidth))
                    .shadow(isDisabled ? ShadowStyle::none() : ShadowStyle {.radius = theme().shadowRadiusControl, .offset = {0.f, theme().shadowOffsetYControl}, .color = theme().shadowColor})
                    .position([thumbXAnim] {
                        return thumbXAnim();
                    }, thumbInset)
                    .size(thumbSize, thumbSize)
                    .cornerRadius(CornerRadius {thumbSize * 0.5f})
            ),
        }
                     .cursor(isDisabled ? Cursor::Inherit : Cursor::Hand)
                     .focusable(!isDisabled)
                     .onKeyDown(isDisabled ? std::function<void(KeyCode, Modifiers)> {} : std::function<void(KeyCode, Modifiers)> {handleKey})
                     .onTap(isDisabled ? std::function<void()> {} : std::function<void()> {handleToggle}),
    };
}

} // namespace lambdaui
