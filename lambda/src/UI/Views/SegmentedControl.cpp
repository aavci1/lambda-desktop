#include <Lambda/UI/Views/SegmentedControl.hpp>

#include <Lambda/UI/KeyCodes.hpp>
#include <Lambda/Reactive/Interpolatable.hpp>
#include <Lambda/Reactive/Transition.hpp>
#include <Lambda/UI/Hooks.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/Views/HStack.hpp>
#include <Lambda/UI/Views/Rectangle.hpp>
#include <Lambda/UI/Views/Text.hpp>
#include <Lambda/UI/Views/ZStack.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace lambdaui {

namespace {

Color lighten(Color c, float t) {
    Color const w = Colors::white;
    return Color {lerp(c.r, w.r, t), lerp(c.g, w.g, t), lerp(c.b, w.b, t), c.a};
}

Color darken(Color c, float t) {
    Color const b = Colors::black;
    return Color {lerp(c.r, b.r, t), lerp(c.g, b.g, t), lerp(c.b, b.b, t), c.a};
}

Color withAlpha(Color c, float alpha) {
    c.a = alpha;
    return c;
}

struct ResolvedStyle {
    Font font {};
    float paddingH = 0.f;
    float paddingV = 0.f;
    float cornerRadius = 0.f;
    Color accentColor {};
    Color trackColor {};
    Color borderColor {};

    bool operator==(ResolvedStyle const &) const = default;
};

ResolvedStyle resolveStyle(SegmentedControl::Style const &style, Theme const &theme) {
    return ResolvedStyle {
        .font = resolveFont(style.font, theme.headlineFont, theme),
        .paddingH = resolveFloat(style.paddingH, theme.space4),
        .paddingV = resolveFloat(style.paddingV, (theme.space3 + theme.space2) * 0.5f + 1.f),
        .cornerRadius = resolveFloat(style.cornerRadius, theme.radiusLarge),
        .accentColor = resolveColor(style.accentColor, theme.accentColor, theme),
        .trackColor = resolveColor(style.trackColor, theme.windowBackgroundColor, theme),
        .borderColor = resolveColor(style.borderColor, theme.separatorColor, theme),
    };
}

int clampIndex(int index, std::size_t count) {
    if (count == 0) {
        return -1;
    }
    return std::clamp(index, 0, static_cast<int>(count - 1));
}

struct SegmentedControlItem : ViewModifiers<SegmentedControlItem> {
    SegmentedControlOption option;
    Signal<int> selectedIndex {};
    int index = 0;
    bool disabled = false;
    ResolvedStyle style {};
    Theme theme {};
    std::function<void()> onTap;

    bool operator==(SegmentedControlItem const &other) const {
        return option == other.option && selectedIndex == other.selectedIndex &&
               index == other.index && disabled == other.disabled && style == other.style && theme == other.theme &&
               static_cast<bool>(onTap) == static_cast<bool>(other.onTap);
    }

    Element body() const {
        bool const isDisabled = disabled || option.disabled;
        Reactive::Signal<bool> hovered = useHover();
        Reactive::Signal<bool> pressed = usePress();
        Reactive::Signal<bool> focused = useFocus();
        Color const disabledTextColor = theme.disabledTextColor;
        Color const accentForegroundColor = theme.accentForegroundColor;
        Color const secondaryLabelColor = theme.secondaryLabelColor;

        Color const selectedFill = style.accentColor;
        Color const selectedHoverFill = lighten(style.accentColor, 0.08f);
        Color const selectedPressFill = darken(style.accentColor, 0.08f);
        Color const hoverFill = darken(style.trackColor, 0.04f);
        Color const pressFill = darken(style.trackColor, 0.12f);
        Color const idleFill = withAlpha(hoverFill, 0.f);
        auto motion = [theme = theme] {
            return Transition::ease(theme.durationFast);
        };
        auto fillTarget = [selectedIndex = selectedIndex,
                           index = index,
                           isDisabled,
                           pressed,
                           hovered,
                           selectedFill,
                           selectedHoverFill,
                           selectedPressFill,
                           pressFill,
                           hoverFill,
                           idleFill] {
            bool const selected = selectedIndex() == index;
            return isDisabled ? Colors::transparent
                              : selected ? (pressed() ? selectedPressFill : hovered() ? selectedHoverFill : selectedFill)
                                         : (pressed() ? pressFill : hovered() ? hoverFill : idleFill);
        };
        auto labelTarget = [selectedIndex = selectedIndex,
                            index = index,
                            isDisabled,
                            disabledTextColor,
                            accentForegroundColor,
                            secondaryLabelColor] {
            bool const selected = selectedIndex() == index;
            return isDisabled ? disabledTextColor
                              : selected ? accentForegroundColor : secondaryLabelColor;
        };
        auto fillAnim = useAnimated(fillTarget, motion);
        auto labelAnim = useAnimated(labelTarget, motion);
        Reactive::Bindable<StrokeStyle> const stroke{[isDisabled, focused,
                                                       focusColor = theme.keyboardFocusIndicatorColor] {
            return !isDisabled && focused.get()
                       ? StrokeStyle::solid(focusColor, 2.f)
                       : StrokeStyle::none();
        }};

        auto handleTap = [isDisabled, onTap = onTap]() {
            if (!isDisabled && onTap) {
                onTap();
            }
        };
        auto handleKey = [handleTap](KeyCode key, Modifiers) {
            if (key == keys::Return || key == keys::Space) {
                handleTap();
            }
        };

        return Text {
            .text = option.label,
            .font = style.font,
            .color = [labelAnim] {
                return labelAnim();
            },
            .horizontalAlignment = HorizontalAlignment::Center,
            .verticalAlignment = VerticalAlignment::Center,
        }
            .padding(style.paddingV, style.paddingH, style.paddingV, style.paddingH)
            .fill([fillAnim] {
                return fillAnim();
            })
            .stroke(stroke)
            .cornerRadius(CornerRadius {std::max(0.f, style.cornerRadius - 2.f)})
            .cursor(isDisabled ? Cursor::Arrow : Cursor::Hand)
            .focusable(!isDisabled)
            .onKeyDown(isDisabled ? std::function<void(KeyCode, Modifiers)> {} :
                                    std::function<void(KeyCode, Modifiers)> {handleKey})
            .onTap(isDisabled ? std::function<void()> {} : std::function<void()> {handleTap});
    }
};

} // namespace

Element SegmentedControl::body() const {
    auto theme = useEnvironment<ThemeKey>();
    ResolvedStyle const resolved = resolveStyle(style, theme());
    Signal<int> const selection = selectedIndex;

    auto commitSelection = [selection, onChange = onChange](int index) {
        selection = index;
        if (onChange) {
            onChange(index);
        }
    };
    auto handleKey = [commitSelection, options = options, selection](KeyCode key, Modifiers) {
        if (options.empty()) {
            return;
        }

        int const currentIndex = clampIndex(selection.get(), options.size());
        int nextIndex = currentIndex;
        if (key == keys::LeftArrow) {
            nextIndex = std::max(0, currentIndex - 1);
        } else if (key == keys::RightArrow) {
            nextIndex = std::min(static_cast<int>(options.size() - 1), currentIndex + 1);
        } else {
            return;
        }

        while (nextIndex >= 0 && static_cast<std::size_t>(nextIndex) < options.size() &&
               options[static_cast<std::size_t>(nextIndex)].disabled) {
            nextIndex += key == keys::LeftArrow ? -1 : 1;
        }
        if (nextIndex >= 0 && static_cast<std::size_t>(nextIndex) < options.size()) {
            commitSelection(nextIndex);
        }
    };

    std::vector<Element> items;
    items.reserve(options.size());
    for (std::size_t i = 0; i < options.size(); ++i) {
        items.push_back(SegmentedControlItem {
            .option = options[i],
            .selectedIndex = selection,
            .index = static_cast<int>(i),
            .disabled = disabled,
            .style = resolved,
            .theme = theme(),
            .onTap = [commitSelection, index = static_cast<int>(i)] {
                commitSelection(index);
            },
        }.flex(1.f, 1.f, 0.f));
    }

    Element root = HStack {
        .spacing = 0.f,
        .alignment = Alignment::Stretch,
        .children = std::move(items),
    }
        .fill(FillStyle::solid(disabled ? theme().windowBackgroundColor : resolved.trackColor))
        .stroke(StrokeStyle::solid(resolved.borderColor, 1.f))
        .cornerRadius(CornerRadius {resolved.cornerRadius})
        .focusable(!disabled)
        .onKeyDown(disabled ? std::function<void(KeyCode, Modifiers)> {} :
                              std::function<void(KeyCode, Modifiers)> {handleKey});

    return root;
}

} // namespace lambdaui
