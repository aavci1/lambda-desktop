#include <Lambda/Reactive/Interpolatable.hpp>
#include <Lambda/Reactive/Transition.hpp>
#include <Lambda/UI/Hooks.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/Views/Button.hpp>
#include <Lambda/UI/Views/Icon.hpp>
#include <Lambda/UI/Views/Rectangle.hpp>
#include <Lambda/UI/Views/ScaleAroundCenter.hpp>
#include <Lambda/UI/Views/Text.hpp>
#include <Lambda/UI/Views/ZStack.hpp>

#include <Lambda/UI/KeyCodes.hpp>

#include <cmath>
#include <cstdint>

namespace lambda {

namespace {

Color lighten(Color c, float t) {
    Color const w = Colors::white;
    return Color {lerp(c.r, w.r, t), lerp(c.g, w.g, t), lerp(c.b, w.b, t), c.a};
}

Color darken(Color c, float t) {
    Color const b = Colors::black;
    return Color {lerp(c.r, b.r, t), lerp(c.g, b.g, t), lerp(c.b, b.b, t), c.a};
}

struct ButtonColors {
    Color fill {};
    Color fillHover {};
    Color fillPress {};
    Color label {};
    Color border {};
    Color focusRing {};
    ShadowStyle shadow = ShadowStyle::none();
};

Button::Style resolveStyle(Button::Style const &style, Theme const &theme) {
    return Button::Style {
        .font = resolveFont(style.font, theme.headlineFont, theme),
        .paddingH = resolveFloat(style.paddingH, theme.space4),
        .paddingV = resolveFloat(style.paddingV, theme.space3),
        .cornerRadius = resolveFloat(style.cornerRadius, theme.radiusLarge),
        .accentColor = resolveColor(style.accentColor, theme.accentColor, theme),
        .destructiveColor = resolveColor(style.destructiveColor, theme.dangerColor, theme),
    };
}

LinkButton::Style resolveStyle(LinkButton::Style const &style, Theme const &theme) {
    return LinkButton::Style {
        .font = resolveFont(style.font, theme.headlineFont, theme),
        .color = resolveColor(style.color, theme.accentColor, theme),
    };
}

IconButton::Style resolveStyle(IconButton::Style const &style, Theme const &theme) {
    return IconButton::Style {
        .size = resolveFloat(style.size, theme.bodyFont.size),
        .weight = resolveFloat(style.weight, theme.bodyFont.weight),
        .color = resolveColor(style.color, theme.accentColor, theme),
    };
}

ButtonColors deriveColors(ButtonVariant variant, Color accent, Color destructive, Color onAccent,
                          Color onDanger, Theme const &theme) {
    switch (variant) {
    case ButtonVariant::Primary:
        return {
            .fill = accent,
            .fillHover = lighten(accent, 0.08f),
            .fillPress = darken(accent, 0.08f),
            .label = onAccent,
            .border = Colors::transparent,
            .focusRing = theme.keyboardFocusIndicatorColor,
            .shadow = ShadowStyle::none(),
        };
    case ButtonVariant::Secondary:
        return {
            .fill = theme.elevatedBackgroundColor,
            .fillHover = theme.hoveredControlBackgroundColor,
            .fillPress = theme.disabledControlBackgroundColor,
            .label = theme.labelColor,
            .border = theme.separatorColor,
            .focusRing = theme.keyboardFocusIndicatorColor,
            .shadow = ShadowStyle::none(),
        };
    case ButtonVariant::Destructive:
        return {
            .fill = destructive,
            .fillHover = lighten(destructive, 0.08f),
            .fillPress = darken(destructive, 0.08f),
            .label = onDanger,
            .border = Colors::transparent,
            .focusRing = theme.keyboardFocusIndicatorColor,
            .shadow = ShadowStyle::none(),
        };
    case ButtonVariant::Ghost:
        return {
            .fill = Colors::transparent,
            .fillHover = theme.selectedContentBackgroundColor,
            .fillPress = Color {theme.selectedContentBackgroundColor.r, theme.selectedContentBackgroundColor.g, theme.selectedContentBackgroundColor.b, std::min(theme.selectedContentBackgroundColor.a + 0.08f, 1.f)},
            .label = accent,
            .border = Colors::transparent,
            .focusRing = theme.keyboardFocusIndicatorColor,
            .shadow = ShadowStyle::none(),
        };
    }
    return {};
}

} // namespace

Element Button::body() const {
    auto theme = useEnvironment<ThemeKey>();
    auto [fontResolved, paddingH, paddingV, radiusResolved, accent, destructive] = resolveStyle(style, theme());
    Reactive::Bindable<bool> disabledBinding = disabled;
    ButtonColors const colors = deriveColors(variant, accent, destructive, theme().accentForegroundColor, theme().dangerForegroundColor, theme());
    Reactive::Signal<bool> hovered = useHover();
    Reactive::Signal<bool> pressed = usePress();
    Reactive::Signal<bool> focused = useFocus();

    auto motion = [theme] {
        return Transition::ease(theme().durationFast);
    };
    auto fillTarget = [disabledBinding, pressed, hovered, colors, theme] {
        return disabledBinding.evaluate() ? theme().disabledControlBackgroundColor :
               pressed()                 ? colors.fillPress :
               hovered()                 ? colors.fillHover :
                                           colors.fill;
    };
    auto labelTarget = [disabledBinding, colors, theme] {
        return disabledBinding.evaluate() ? theme().disabledTextColor : colors.label;
    };
    auto scaleTarget = [disabledBinding, pressed] {
        return (pressed() && !disabledBinding.evaluate()) ? 0.97f : 1.f;
    };
    auto fillAnim = useAnimated(fillTarget, motion);
    auto labelAnim = useAnimated(labelTarget, motion);
    auto scaleAnim = useAnimated(scaleTarget, motion);
    Reactive::Bindable<ShadowStyle> const shadow{[disabledBinding, pressed, hovered, colors, theme] {
        if (disabledBinding.evaluate()) {
            return ShadowStyle::none();
        }
        if (pressed()) {
            return ShadowStyle {.radius = theme().shadowRadiusControl + 2.f, .offset = {0.f, theme().shadowOffsetYControl + 1.f}, .color = Color {theme().shadowColor.r, theme().shadowColor.g, theme().shadowColor.b, std::min(theme().shadowColor.a + 0.08f, 1.f)}};
        }
        if (hovered()) {
            return colors.shadow.isNone() ? ShadowStyle {.radius = theme().shadowRadiusControl * 0.8f, .offset = {0.f, theme().shadowOffsetYControl + 0.5f}, .color = Color {theme().shadowColor.r, theme().shadowColor.g, theme().shadowColor.b, theme().shadowColor.a * 0.7f}} : colors.shadow;
        }
        return colors.shadow;
    }};

    auto handleTap = [onTap = onTap, disabledBinding]() {
        if (disabledBinding.evaluate()) {
            return;
        }
        if (onTap) {
            onTap();
        }
    };
    auto handleKey = [handleTap](KeyCode k, Modifiers) {
        if (k == keys::Return || k == keys::Space) {
            handleTap();
        }
    };

    CornerRadius const cr {radiusResolved};
    Reactive::Bindable<StrokeStyle> const stroke{[disabledBinding, focused, colors] {
        return !disabledBinding.evaluate() && focused.get() ? StrokeStyle::solid(colors.focusRing, 2.f) :
               (!disabledBinding.evaluate() && colors.border.a > 0.01f) ? StrokeStyle::solid(colors.border, 1.f) :
                                                                            StrokeStyle::none();
    }};

    return ScaleAroundCenter {
        .scale = [scaleAnim] {
            return scaleAnim();
        },
        .child = Text {
            .text = label,
            .font = fontResolved,
            .color = [labelAnim] {
                return labelAnim();
            },
            .horizontalAlignment = HorizontalAlignment::Center,
            .verticalAlignment = VerticalAlignment::Center,
        }
                     .fill([fillAnim] {
                         return fillAnim();
                     })
                     .stroke(stroke)
                     .cornerRadius(cr)
                     .shadow(shadow)
                     .padding(paddingV, paddingH, paddingV, paddingH)
                     .cursor([disabledBinding] {
                         return disabledBinding.evaluate() ? Cursor::Inherit : Cursor::Hand;
                     })
                     .focusable([disabledBinding] {
                         return !disabledBinding.evaluate();
                     })
                     .onKeyDown(std::function<void(KeyCode, Modifiers)> {handleKey})
                     .onTap(std::function<void()> {handleTap})
    };
}

Element LinkButton::body() const {
    auto theme = useEnvironment<ThemeKey>();
    auto [fontResolved, accentResolved] = resolveStyle(style, theme());
    Reactive::Bindable<bool> disabledBinding = disabled;
    Reactive::Signal<bool> hovered = useHover();
    Reactive::Signal<bool> pressed = usePress();
    Reactive::Signal<bool> focused = useFocus();
    Reactive::Signal<bool> keyboardFocused = useKeyboardFocus();

    auto motion = [theme] {
        return Transition::ease(theme().durationFast);
    };
    auto labelTarget = [disabledBinding, pressed, hovered, accentResolved, theme] {
        return disabledBinding.evaluate() ? theme().disabledTextColor :
               pressed()                 ? darken(accentResolved, 0.12f) :
               hovered()                 ? lighten(accentResolved, 0.12f) :
                                           accentResolved;
    };
    auto labelAnim = useAnimated(labelTarget, motion);

    auto handleTap = [onTap = onTap, disabledBinding]() {
        if (disabledBinding.evaluate()) {
            return;
        }
        if (onTap) {
            onTap();
        }
    };
    auto handleKey = [handleTap](KeyCode k, Modifiers) {
        if (k == keys::Return || k == keys::Space) {
            handleTap();
        }
    };

    Reactive::Bindable<StrokeStyle> const focusStroke{[disabledBinding, focused, keyboardFocused, theme] {
        return !disabledBinding.evaluate() && focused.get() && keyboardFocused.get()
                   ? StrokeStyle::solid(theme().keyboardFocusIndicatorColor, 2.f)
                   : StrokeStyle::none();
    }};

    return Text {
        .text = label,
        .font = fontResolved,
        .color = [labelAnim] {
            return labelAnim();
        },
        .horizontalAlignment = HorizontalAlignment::Leading,
        .verticalAlignment = VerticalAlignment::Center,
    }
        .fill(FillStyle::none())
        .stroke(focusStroke)
        .cornerRadius(CornerRadius {theme().radiusXSmall})
        .padding(0.f, 3.f, 0.f, 3.f)
        .cursor([disabledBinding] {
            return disabledBinding.evaluate() ? Cursor::Inherit : Cursor::Hand;
        })
        .focusable([disabledBinding] {
            return !disabledBinding.evaluate();
        })
        .onKeyDown(std::function<void(KeyCode, Modifiers)> {handleKey})
        .onTap(std::function<void()> {handleTap});
}

Element IconButton::body() const {
    auto theme = useEnvironment<ThemeKey>();
    auto [sizeResolved, weightResolved, accentResolved] = resolveStyle(style, theme());
    Reactive::Bindable<bool> disabledBinding = disabled;
    Reactive::Signal<bool> hovered = useHover();
    Reactive::Signal<bool> pressed = usePress();
    Reactive::Signal<bool> focused = useFocus();
    Reactive::Signal<bool> keyboardFocused = useKeyboardFocus();

    auto motion = [theme] {
        return Transition::ease(theme().durationFast);
    };
    auto iconTarget = [disabledBinding, pressed, hovered, accentResolved, theme] {
        return disabledBinding.evaluate() ? theme().disabledTextColor :
               pressed()                 ? darken(accentResolved, 0.12f) :
               hovered()                 ? lighten(accentResolved, 0.12f) :
                                           accentResolved;
    };
    auto iconAnim = useAnimated(iconTarget, motion);

    auto handleTap = [onTap = onTap, disabledBinding]() {
        if (disabledBinding.evaluate()) {
            return;
        }
        if (onTap) {
            onTap();
        }
    };
    auto handleKey = [handleTap](KeyCode k, Modifiers) {
        if (k == keys::Return || k == keys::Space) {
            handleTap();
        }
    };

    Reactive::Bindable<StrokeStyle> const focusStroke{[disabledBinding, focused, keyboardFocused, theme] {
        return !disabledBinding.evaluate() && focused.get() && keyboardFocused.get()
                   ? StrokeStyle::solid(theme().keyboardFocusIndicatorColor, 2.f)
                   : StrokeStyle::none();
    }};

    return Icon {
        .name = icon,
        .size = sizeResolved,
        .weight = weightResolved,
        .color = [iconAnim] {
            return iconAnim();
        },
    }
        .fill(FillStyle::none())
        .stroke(focusStroke)
        .cornerRadius(CornerRadius {theme().radiusXSmall})
        .cursor([disabledBinding] {
            return disabledBinding.evaluate() ? Cursor::Inherit : Cursor::Hand;
        })
        .focusable([disabledBinding] {
            return !disabledBinding.evaluate();
        })
        .onKeyDown(std::function<void(KeyCode, Modifiers)> {handleKey})
        .onTap(std::function<void()> {handleTap});
}

} // namespace lambda
