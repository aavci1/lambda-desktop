#include <Lambda/UI/Views/Select.hpp>

#include <Lambda/UI/Application.hpp>
#include <Lambda/UI/KeyCodes.hpp>
#include <Lambda/Graphics/TextSystem.hpp>
#include <Lambda/Reactive/Interpolatable.hpp>
#include <Lambda/Reactive/Transition.hpp>
#include <Lambda/UI/Hooks.hpp>
#include <Lambda/UI/InputFieldChrome.hpp>
#include <Lambda/UI/InputFieldLayout.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/Views/HStack.hpp>
#include <Lambda/UI/Views/Icon.hpp>
#include <Lambda/UI/Views/Popover.hpp>
#include <Lambda/UI/Views/Rectangle.hpp>
#include <Lambda/UI/Views/ScrollView.hpp>
#include <Lambda/UI/Views/Text.hpp>
#include <Lambda/UI/Views/VStack.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

namespace lambda {

namespace {

float inputFieldShellInset(ResolvedInputFieldChrome const &chrome) {
    return std::max(chrome.borderWidth, chrome.borderFocusWidth);
}

float singleLineTriggerFieldHeight(Font const &labelFont, ResolvedInputFieldChrome const &chrome) {
    float const shellInset = inputFieldShellInset(chrome);
    return resolvedInputFieldHeight(labelFont, chrome.textColor, chrome.paddingV + shellInset, 0.f);
}

float singleLineTriggerContentHeight(Font const &labelFont, ResolvedInputFieldChrome const &chrome) {
    float const shellInset = inputFieldShellInset(chrome);
    return std::max(0.f, singleLineTriggerFieldHeight(labelFont, chrome) - 2.f * (chrome.paddingV + shellInset));
}

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

bool isValidIndex(int index, std::size_t count) {
    return index >= 0 && static_cast<std::size_t>(index) < count;
}

float intrinsicTextWidth(std::string const &text, Font const &font, Color color) {
    if (text.empty()) {
        return 0.f;
    }
    TextLayoutOptions opts {};
    opts.wrapping = TextWrapping::NoWrap;
    Size const measured = Application::instance().textSystem().measure(text, font, color, 0.f, opts);
    return measured.width;
}

int firstEnabledIndex(std::vector<SelectOption> const &options) {
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (!options[i].disabled) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int lastEnabledIndex(std::vector<SelectOption> const &options) {
    for (int i = static_cast<int>(options.size()) - 1; i >= 0; --i) {
        if (!options[static_cast<std::size_t>(i)].disabled) {
            return i;
        }
    }
    return -1;
}

int stepEnabledIndex(std::vector<SelectOption> const &options, int current, int direction) {
    if (direction > 0) {
        for (int i = std::max(current + 1, 0); i < static_cast<int>(options.size()); ++i) {
            if (!options[static_cast<std::size_t>(i)].disabled) {
                return i;
            }
        }
        return current;
    }
    for (int i = std::min(current - 1, static_cast<int>(options.size()) - 1); i >= 0; --i) {
        if (!options[static_cast<std::size_t>(i)].disabled) {
            return i;
        }
    }
    return current;
}

struct SelectResolvedStyle {
    Font labelFont {};
    Font detailFont {};
    float cornerRadius = 0.f;
    ResolvedInputFieldChrome fieldChrome {};
    float menuCornerRadius = 0.f;
    float menuMaxHeight = 0.f;
    float menuMaxWidth = 0.f;
    float minMenuWidth = 0.f;
    Color accentColor {};
    Color fieldHoverColor {};
    Color rowHoverColor {};

    bool operator==(SelectResolvedStyle const& other) const = default;
};

SelectResolvedStyle resolveStyle(Select::Style const &style, Theme const &theme) {
    InputFieldChromeSpec fieldChromeSpec {};
    fieldChromeSpec.backgroundColor = style.fieldColor;
    fieldChromeSpec.borderColor = style.borderColor;
    fieldChromeSpec.cornerRadius = style.cornerRadius;
    ResolvedInputFieldChrome const fieldChrome = resolveInputFieldChrome(fieldChromeSpec, theme);

    return SelectResolvedStyle {
        .labelFont = resolveFont(style.labelFont, theme.bodyFont, theme),
        .detailFont = resolveFont(style.detailFont, theme.footnoteFont, theme),
        .cornerRadius = resolveFloat(style.cornerRadius, theme.radiusLarge),
        .fieldChrome = fieldChrome,
        .menuCornerRadius = resolveFloat(style.menuCornerRadius, theme.radiusLarge),
        .menuMaxHeight = resolveFloat(style.menuMaxHeight, 260.f),
        .menuMaxWidth = resolveFloat(style.menuMaxWidth, 0.f),
        .minMenuWidth = resolveFloat(style.minMenuWidth, 180.f),
        .accentColor = resolveColor(style.accentColor, theme.accentColor, theme),
        .fieldHoverColor = resolveColor(style.fieldHoverColor, fieldChrome.backgroundColor, theme),
        .rowHoverColor = resolveColor(style.rowHoverColor, theme.rowHoverBackgroundColor, theme),
    };
}

float intrinsicMenuWidth(std::vector<SelectOption> const &options, SelectResolvedStyle const &style,
                         Theme const &theme, bool showCheckmark, std::string const &emptyText) {
    auto rowContentWidth = [&](SelectOption const &option) {
        float const labelWidth = intrinsicTextWidth(option.label, style.labelFont, theme.labelColor);
        float contentWidth = labelWidth;
        if (!option.detail.empty()) {
            float const detailWidth = intrinsicTextWidth(option.detail, style.detailFont, theme.secondaryLabelColor);
            contentWidth = std::max(contentWidth, detailWidth);
        }
        return contentWidth;
    };

    float maxContentWidth = 0.f;
    if (options.empty()) {
        maxContentWidth = intrinsicTextWidth(emptyText, style.detailFont, theme.secondaryLabelColor);
    } else {
        for (SelectOption const &option : options) {
            maxContentWidth = std::max(maxContentWidth, rowContentWidth(option));
        }
    }

    float const reservedCheckmarkWidth = showCheckmark ? (18.f + theme.space3) : 0.f;
    float const rowHorizontalPadding = theme.space3 * 2.f;
    float const menuHorizontalPadding = theme.space1 * 2.f;
    float width = maxContentWidth + reservedCheckmarkWidth + rowHorizontalPadding + menuHorizontalPadding;
    width = std::max(width, style.minMenuWidth);
    if (style.menuMaxWidth > 0.f) {
        width = std::min(width, style.menuMaxWidth);
    }
    return width;
}

struct SelectMenuRow : ViewModifiers<SelectMenuRow> {
    SelectOption option;
    Signal<int> selectedIndex {};
    Signal<int> activeIndex {};
    int index = -1;
    bool showCheckmark = true;
    SelectResolvedStyle style {};
    Theme theme {};
    std::function<void()> onTap;

    bool operator==(SelectMenuRow const& other) const {
        return option == other.option && selectedIndex == other.selectedIndex &&
               activeIndex == other.activeIndex && index == other.index &&
               showCheckmark == other.showCheckmark && style == other.style &&
               theme == other.theme && static_cast<bool>(onTap) == static_cast<bool>(other.onTap);
    }

    Element body() const {
        bool const disabled = option.disabled;
        Reactive::Signal<bool> pressed = usePress();
        Reactive::Signal<bool> focused = useFocus();
        Reactive::Signal<bool> keyboardFocused = useKeyboardFocus();

        Color const hoverFill = style.rowHoverColor;
        Color const idleFill = withAlpha(hoverFill, 0.f);
        Color const pressFill = darken(style.rowHoverColor, 0.04f);

        auto motion = [theme = theme] {
            return Transition::ease(theme.durationFast);
        };
        auto isSelected = [selectedIndex = selectedIndex, index = index] {
            return selectedIndex.get() == index;
        };
        auto isActive = [activeIndex = activeIndex, index = index] {
            return activeIndex.get() == index;
        };
        auto fillTarget = [disabled, isActive, pressed, keyboardFocused,
                           pressFill, hoverFill, idleFill] {
            bool const activeFocus = keyboardFocused();
            bool const active = isActive() || activeFocus;
            return disabled ? Colors::transparent :
                   pressed() ? pressFill :
                   active ? hoverFill :
                            idleFill;
        };
        auto fillAnim = useAnimated(fillTarget, motion);
        auto labelTarget = [disabled, isSelected, accentColor = style.accentColor, theme = theme] {
            return disabled ? theme.disabledTextColor :
                   isSelected() ? accentColor :
                                  theme.labelColor;
        };
        auto detailTarget = [disabled, isSelected, accentColor = style.accentColor, theme = theme] {
            return disabled ? theme.disabledTextColor :
                   isSelected() ? lighten(accentColor, 0.08f) :
                                  theme.secondaryLabelColor;
        };
        auto iconTarget = [disabled, isSelected, accentColor = style.accentColor, theme = theme] {
            Color color = disabled ? theme.disabledTextColor : accentColor;
            if (!isSelected()) {
                color.a = 0.f;
            }
            return color;
        };
        auto labelAnim = useAnimated(labelTarget, motion);
        auto detailAnim = useAnimated(detailTarget, motion);
        auto iconAnim = useAnimated(iconTarget, motion);

        bool const hasDetail = !option.detail.empty();
        Element textBlock = Text {
            .text = option.label,
            .font = style.labelFont,
            .color = [labelAnim] {
                return labelAnim();
            },
            .horizontalAlignment = HorizontalAlignment::Leading,
            .verticalAlignment = VerticalAlignment::Center,
        };
        if (hasDetail) {
            std::vector<Element> textChildren;
            textChildren.reserve(2);
            textChildren.emplace_back(Text {
                .text = option.label,
                .font = style.labelFont,
                .color = [labelAnim] {
                    return labelAnim();
                },
                .horizontalAlignment = HorizontalAlignment::Leading,
                .verticalAlignment = VerticalAlignment::Center,
            });
            textChildren.emplace_back(Text {
                .text = option.detail,
                .font = style.detailFont,
                .color = [detailAnim] {
                    return detailAnim();
                },
                .horizontalAlignment = HorizontalAlignment::Leading,
                .verticalAlignment = VerticalAlignment::Center,
                .wrapping = TextWrapping::Wrap,
            });
            textBlock = VStack {
                .spacing = theme.space1 * 0.5f,
                .alignment = Alignment::Start,
                .children = std::move(textChildren),
            };
        }

        std::vector<Element> rowChildren;
        rowChildren.reserve(showCheckmark ? 2 : 1);
        rowChildren.emplace_back(std::move(textBlock).flex(1.f, 1.f, 0.f));
        if (showCheckmark) {
            rowChildren.emplace_back(Icon {
                .name = IconName::Check,
                .size = 18.f,
                .color = [iconAnim] {
                    return iconAnim();
                },
            });
        }

        auto activate = [onTap = onTap, disabled]() {
            if (disabled) {
                return;
            }
            if (onTap) {
                onTap();
            }
        };
        auto handleKey = [activate](KeyCode key, Modifiers) {
            if (key == keys::Return || key == keys::Space || key == keys::Tab) {
                activate();
            }
        };
        auto activateHover = [activeIndex = activeIndex, index = index, disabled]() {
            if (!disabled) {
                activeIndex = index;
            }
        };
        auto deactivateHover = [activeIndex = activeIndex, index = index, disabled]() {
            if (!disabled && activeIndex.get() == index) {
                activeIndex = -1;
            }
        };

        return HStack {
            .spacing = theme.space3,
            .alignment = Alignment::Center,
            .children = std::move(rowChildren),
        }
            .padding(theme.space2, theme.space3, theme.space2, theme.space3)
            .fill([fillAnim] {
                return fillAnim();
            })
            .stroke([focused, disabled, focusColor = theme.keyboardFocusIndicatorColor] {
                return focused.get() && !disabled ? StrokeStyle::solid(focusColor, 2.f)
                                                  : StrokeStyle::none();
            })
            .cornerRadius(CornerRadius {style.cornerRadius})
            .cursor(disabled ? Cursor::Inherit : Cursor::Hand)
            .focusable(!disabled)
            .onKeyDown(disabled ? std::function<void(KeyCode, Modifiers)> {} : std::function<void(KeyCode, Modifiers)> {handleKey})
            .onPointerEnter(disabled ? std::function<void()> {} : std::function<void()> {activateHover})
            .onPointerExit(disabled ? std::function<void()> {} : std::function<void()> {deactivateHover})
            .onTap(disabled ? std::function<void()> {} : std::function<void()> {activate});
    }
};

struct SelectMenuContent {
    Signal<int> selectedIndex {};
    Signal<int> activeIndex {};
    std::vector<SelectOption> options;
    std::string emptyText;
    bool showCheckmark = true;
    std::optional<float> menuWidth;
    SelectResolvedStyle style {};
    Theme theme {};
    std::function<void(int)> onSelect;

    bool operator==(SelectMenuContent const& other) const {
        return selectedIndex == other.selectedIndex && activeIndex == other.activeIndex &&
               options == other.options && emptyText == other.emptyText &&
               showCheckmark == other.showCheckmark && menuWidth == other.menuWidth &&
               style == other.style && theme == other.theme &&
               static_cast<bool>(onSelect) == static_cast<bool>(other.onSelect);
    }

    Element body() const {
        if (options.empty()) {
            return Text {
                .text = emptyText,
                .font = style.detailFont,
                .color = Color::secondary(),
                .horizontalAlignment = HorizontalAlignment::Leading,
                .wrapping = TextWrapping::Wrap,
            }
                .padding(theme.space3);
        }

        std::vector<Element> rows;
        rows.reserve(options.size());
        for (std::size_t i = 0; i < options.size(); ++i) {
            int const index = static_cast<int>(i);
            rows.emplace_back(SelectMenuRow {
                .option = options[i],
                .selectedIndex = selectedIndex,
                .activeIndex = activeIndex,
                .index = index,
                .showCheckmark = showCheckmark,
                .style = style,
                .theme = theme,
                .onTap = [onSelect = onSelect, index] {
                    if (onSelect) {
                        onSelect(index);
                    }
                },
            });
        }

        Element menu = ScrollView {
            .axis = ScrollAxis::Vertical,
            .children = children(
                VStack {
                    .spacing = theme.space1 * 0.5f,
                    .alignment = Alignment::Stretch,
                    .children = std::move(rows),
                }
                    .padding(theme.space1)
            ),
        }
                           .clipContent(true);
        if (menuWidth.has_value() && *menuWidth > 0.f) {
            menu = std::move(menu).width(*menuWidth);
        }
        return menu;
    }
};

struct SelectTrigger : ViewModifiers<SelectTrigger> {
    Signal<int> selectedIndex {};
    std::vector<SelectOption> options;
    std::string placeholder;
    std::string emptyText;
    bool disabled = false;
    bool showCheckmark = true;
    bool dismissOnSelect = true;
    bool showDetailInTrigger = true;
    bool matchTriggerWidth = true;
    SelectTriggerMode triggerMode = SelectTriggerMode::Field;
    PopoverPlacement placement = PopoverPlacement::Below;
    SelectResolvedStyle style {};
    std::function<void(int)> onChange;

    bool operator==(SelectTrigger const& other) const {
        return selectedIndex == other.selectedIndex && options == other.options &&
               placeholder == other.placeholder && emptyText == other.emptyText &&
               disabled == other.disabled && showCheckmark == other.showCheckmark &&
               dismissOnSelect == other.dismissOnSelect &&
               showDetailInTrigger == other.showDetailInTrigger &&
               matchTriggerWidth == other.matchTriggerWidth &&
               triggerMode == other.triggerMode && placement == other.placement &&
               style == other.style &&
               static_cast<bool>(onChange) == static_cast<bool>(other.onChange);
    }

    Element body() const {
        auto theme = useEnvironment<ThemeKey>();

        auto [showPopover, hidePopover, isPresented] = usePopover();
        auto activeIndex = useState<int>(-1);
        auto menuOpen = useState<bool>(isPresented);

        int const currentIndex = *selectedIndex;
        SelectOption const *currentOption =
            isValidIndex(currentIndex, options.size()) ? &options[static_cast<std::size_t>(currentIndex)] : nullptr;

        bool const isDisabled = disabled;
        Reactive::Signal<bool> hovered = useHover();
        Reactive::Signal<bool> pressed = usePress();
        Reactive::Signal<bool> focused = useFocus();
        Rect const bounds = useBounds();
        ResolvedInputFieldChrome const &fieldChrome = style.fieldChrome;
        float const shellInset = inputFieldShellInset(fieldChrome);
        float const fieldVerticalPadding = fieldChrome.paddingV + shellInset;
        float const fieldHorizontalPadding = fieldChrome.paddingH + shellInset;

        Color const idleFill = fieldChrome.backgroundColor;
        Color const hoverFill = style.fieldHoverColor;
        Color const pressFill = darken(style.fieldHoverColor, 0.03f);
        Color const openFill = style.fieldHoverColor;
        auto motion = [theme] {
            return Transition::ease(theme().durationFast);
        };
        auto fillTarget = [triggerMode = triggerMode, pressed, hovered, menuOpen,
                           pressFill, openFill, hoverFill, idleFill] {
            return triggerMode == SelectTriggerMode::Link ? Colors::transparent :
                   pressed()                             ? pressFill :
                   menuOpen.get()                        ? openFill :
                   hovered()                             ? hoverFill :
                                                           idleFill;
        };

        bool const hasCurrentOption = currentOption != nullptr;
        auto labelTarget = [triggerMode = triggerMode, isDisabled, pressed,
                            hovered, menuOpen, accent = style.accentColor,
                            hasCurrentOption, theme] {
            if (triggerMode == SelectTriggerMode::Link) {
                return isDisabled ? theme().disabledTextColor :
                       pressed() ? darken(accent, 0.12f) :
                       (hovered() || menuOpen.get()) ? lighten(accent, 0.12f) :
                                                       accent;
            }
            return isDisabled ? theme().disabledTextColor :
                   hasCurrentOption ? theme().labelColor : theme().placeholderTextColor;
        };
        auto detailTarget = [triggerMode = triggerMode, isDisabled, labelTarget, theme] {
            return triggerMode == SelectTriggerMode::Link ? labelTarget() :
                   isDisabled ? theme().disabledTextColor : theme().secondaryLabelColor;
        };
        auto chevronTarget = [triggerMode = triggerMode, isDisabled, menuOpen,
                              labelTarget, accent = style.accentColor,
                              theme] {
            return triggerMode == SelectTriggerMode::Link ? labelTarget() :
                   isDisabled ? theme().disabledTextColor :
                   menuOpen.get() ? accent : theme().secondaryLabelColor;
        };
        auto fillAnim = useAnimated(fillTarget, motion);
        auto labelAnim = useAnimated(labelTarget, motion);
        auto detailAnim = useAnimated(detailTarget, motion);
        auto chevronAnim = useAnimated(chevronTarget, motion);

        float const triggerWidth = bounds.width > 0.f ? bounds.width : style.minMenuWidth;
        EdgeInsets const anchorOutsets = EdgeInsets {};
        std::optional<float> const menuWidth = matchTriggerWidth ? std::optional<float> {triggerWidth} :
                                                                   std::optional<float> {intrinsicMenuWidth(options, style, theme(), showCheckmark, emptyText)};
        std::optional<float> const anchorMaxHeight =
            triggerMode == SelectTriggerMode::Link ? std::nullopt : (bounds.height > 0.f ? std::optional<float> {bounds.height} : std::nullopt);

        auto applySelection = [selectedIndex = selectedIndex, options = options, onChange = onChange](int nextIndex) {
            if (!isValidIndex(nextIndex, options.size()) || options[static_cast<std::size_t>(nextIndex)].disabled) {
                return;
            }
            if (*selectedIndex != nextIndex) {
                selectedIndex = nextIndex;
                if (onChange) {
                    onChange(nextIndex);
                }
            }
        };
        auto commitActiveSelection = [applySelection, activeIndex = activeIndex, options = options]() {
            int const nextIndex = activeIndex.get();
            if (isValidIndex(nextIndex, options.size()) && !options[static_cast<std::size_t>(nextIndex)].disabled) {
                applySelection(nextIndex);
            }
        };
        auto closeMenu = [menuOpen = menuOpen, hidePopover]() {
            menuOpen = false;
            hidePopover();
        };

        auto openMenu = [showPopover,
                         closeMenu,
                         selectedIndex = selectedIndex,
                         activeIndex = activeIndex,
                         menuOpen = menuOpen,
                         options = options,
                         emptyText = emptyText,
                         showCheckmark = showCheckmark,
                         dismissOnSelect = dismissOnSelect,
                         style = style,
                         theme = theme(),
                         menuWidth,
                         matchTriggerWidth = matchTriggerWidth,
                         triggerMode = triggerMode,
                         placement = placement,
                         anchorMaxHeight,
                         anchorOutsets,
                         popoverGap = triggerMode == SelectTriggerMode::Field ? theme().space1 : kFloatFromTheme,
                         onChange = onChange](bool highlightInitial) {
            menuOpen = true;
            int const selected = selectedIndex.get();
            activeIndex = highlightInitial
                              ? (isValidIndex(selected, options.size()) &&
                                         !options[static_cast<std::size_t>(selected)].disabled
                                     ? selected
                                     : firstEnabledIndex(options))
                              : -1;
            auto handleSelect = [selectedIndex, options = options, dismissOnSelect, closeMenu, onChange](int nextIndex) {
                if (!isValidIndex(nextIndex, options.size()) || options[static_cast<std::size_t>(nextIndex)].disabled) {
                    return;
                }
                if (*selectedIndex != nextIndex) {
                    selectedIndex = nextIndex;
                    if (onChange) {
                        onChange(nextIndex);
                    }
                }
                if (dismissOnSelect) {
                    closeMenu();
                }
            };

            showPopover(Popover {
                .content = Element {SelectMenuContent {
                    .selectedIndex = selectedIndex,
                    .activeIndex = activeIndex,
                    .options = options,
                    .emptyText = emptyText,
                    .showCheckmark = showCheckmark,
                    .menuWidth = menuWidth,
                    .style = style,
                    .theme = theme,
                    .onSelect = handleSelect,
                }},
                .placement = placement,
                .crossAlignment = triggerMode == SelectTriggerMode::Link ? OverlayConfig::CrossAlignment::PreferStart : OverlayConfig::CrossAlignment::Center,
                .gap = popoverGap,
                .arrow = false,
                .backgroundColor = Color::elevatedBackground(),
                .borderColor = Color::separator(),
                .borderWidth = 1.f,
                .cornerRadius = style.menuCornerRadius,
                .contentPadding = 0.f,
                .maxSize = [style, menuWidth, matchTriggerWidth]() -> std::optional<Size> {
                    float maxWidth = 0.f;
                    if (matchTriggerWidth || menuWidth.has_value()) {
                        maxWidth = menuWidth.has_value() ? *menuWidth : 0.f;
                    } else {
                        maxWidth = style.menuMaxWidth;
                    }
                    float const maxHeight = style.menuMaxHeight;
                    if (maxWidth > 0.f || maxHeight > 0.f) {
                        return Size {maxWidth, maxHeight};
                    }
                    return std::nullopt;
                }(),
                .backdropColor = Colors::transparent,
                .anchorMaxHeight = anchorMaxHeight,
                .anchorOutsets = anchorOutsets,
                .dismissOnEscape = true,
                .dismissOnOutsideTap = true,
                .onDismiss = [menuOpen] {
                    menuOpen = false;
                },
                .useTapAnchor = false,
                .useFocusAnchor = true,
            });
        };

        auto moveSelection = [applySelection, activeIndex = activeIndex, options = options,
                              selectedIndex = selectedIndex, menuOpen](int direction) {
            bool const open = menuOpen.get();
            Signal<int> target = open ? activeIndex : selectedIndex;
            int const next = stepEnabledIndex(options, target.get(), direction);
            if (next != target.get()) {
                if (open) {
                    activeIndex = next;
                    return;
                }
                applySelection(next);
            }
        };

        auto jumpSelection = [applySelection, activeIndex = activeIndex, options = options, menuOpen](bool toEnd) {
            int const next = toEnd ? lastEnabledIndex(options) : firstEnabledIndex(options);
            if (next >= 0) {
                if (menuOpen.get()) {
                    activeIndex = next;
                    return;
                }
                applySelection(next);
            }
        };

        auto toggleMenu = [isDisabled, menuOpen = menuOpen, openMenu, closeMenu](bool highlightInitial) {
            if (isDisabled) {
                return;
            }
            if (menuOpen.get()) {
                closeMenu();
            } else {
                openMenu(highlightInitial);
            }
        };

        auto handleKey = [isDisabled, menuOpen = menuOpen, openMenu, closeMenu, commitActiveSelection,
                          moveSelection, jumpSelection](KeyCode key, Modifiers) {
            if (isDisabled) {
                return;
            }
            bool const open = menuOpen.get();
            if (key == keys::Return || key == keys::Space) {
                if (open) {
                    commitActiveSelection();
                    closeMenu();
                } else {
                    openMenu(true);
                }
                return;
            }
            if (key == keys::Tab && open) {
                commitActiveSelection();
                closeMenu();
                return;
            }
            if (key == keys::Escape && open) {
                closeMenu();
                return;
            }
            if (key == keys::DownArrow) {
                moveSelection(1);
                return;
            }
            if (key == keys::UpArrow) {
                moveSelection(-1);
                return;
            }
            if (key == keys::Home) {
                jumpSelection(false);
                return;
            }
            if (key == keys::End) {
                jumpSelection(true);
            }
        };
        auto handleTap = [toggleMenu] {
            toggleMenu(false);
        };

        bool const hasDetail = showDetailInTrigger && currentOption && !currentOption->detail.empty();
        Element triggerLabel = Text {
            .text = [selectedIndex = selectedIndex, options = options, placeholder = placeholder] {
                int const index = selectedIndex.get();
                return isValidIndex(index, options.size())
                           ? options[static_cast<std::size_t>(index)].label
                           : placeholder;
            },
            .font = style.labelFont,
            .color = [labelAnim] {
                return labelAnim();
            },
            .horizontalAlignment = HorizontalAlignment::Leading,
            .verticalAlignment = VerticalAlignment::Center,
            .wrapping = TextWrapping::NoWrap,
        };

        Element triggerTextBlock = ZStack {
            .horizontalAlignment = Alignment::Start,
            .verticalAlignment = Alignment::Center,
            .children = children(std::move(triggerLabel)),
        }
                                       .height(singleLineTriggerContentHeight(style.labelFont, fieldChrome));
        if (hasDetail) {
            std::vector<Element> triggerTextChildren;
            triggerTextChildren.reserve(2);
            triggerTextChildren.emplace_back(Text {
                .text = [selectedIndex = selectedIndex, options = options, placeholder = placeholder] {
                    int const index = selectedIndex.get();
                    return isValidIndex(index, options.size())
                               ? options[static_cast<std::size_t>(index)].label
                               : placeholder;
                },
                .font = style.labelFont,
                .color = [labelAnim] {
                    return labelAnim();
                },
                .horizontalAlignment = HorizontalAlignment::Leading,
                .verticalAlignment = VerticalAlignment::Center,
                .wrapping = TextWrapping::Wrap,
            });
            triggerTextChildren.emplace_back(Text {
                .text = [selectedIndex = selectedIndex, options = options] {
                    int const index = selectedIndex.get();
                    return isValidIndex(index, options.size())
                               ? options[static_cast<std::size_t>(index)].detail
                               : std::string {};
                },
                .font = style.detailFont,
                .color = [detailAnim] {
                    return detailAnim();
                },
                .horizontalAlignment = HorizontalAlignment::Leading,
                .verticalAlignment = VerticalAlignment::Center,
                .wrapping = TextWrapping::Wrap,
                .maxLines = 2,
            });
            triggerTextBlock = VStack {
                .spacing = theme().space1 * 0.5f,
                .alignment = Alignment::Start,
                .children = std::move(triggerTextChildren),
            };
        }

        Element trigger = HStack {
            .spacing = theme().space1,
            .alignment = Alignment::Center,
            .children = children(
                std::move(triggerTextBlock).flex(triggerMode == SelectTriggerMode::Field ? 1.f : 0.f, 1.f),
                Icon {
                    .name = [menuOpen] {
                        return menuOpen.get() ? IconName::KeyboardArrowUp : IconName::KeyboardArrowDown;
                    },
                    .size = 18.f,
                    .color = [chevronAnim] {
                        return chevronAnim();
                    },
                }
            )
        };

        if (triggerMode == SelectTriggerMode::Link) {
            return std::move(trigger)
                .padding(0.f, 3.f, 0.f, 3.f)
                .fill(FillStyle::none())
                .stroke([focused, isDisabled, theme] {
                    return !isDisabled && focused.get() ? StrokeStyle::solid(theme().keyboardFocusIndicatorColor, 2.f)
                                                        : StrokeStyle::none();
                })
                .cornerRadius(CornerRadius {theme().radiusXSmall})
                .cursor(isDisabled ? Cursor::Inherit : Cursor::Hand)
                .focusable(!isDisabled)
                .onKeyDown(isDisabled ? std::function<void(KeyCode, Modifiers)> {} : std::function<void(KeyCode, Modifiers)> {handleKey})
                .onTap(isDisabled ? std::function<void()> {} : std::function<void()> {handleTap});
        }

        Element fieldTrigger = std::move(trigger)
            .padding(fieldVerticalPadding, fieldHorizontalPadding, fieldVerticalPadding, fieldHorizontalPadding)
            .fill([fillAnim] {
                return fillAnim();
            })
            .stroke([focused, menuOpen, isDisabled, fieldChrome] {
                return (focused.get() || menuOpen.get()) && !isDisabled
                           ? StrokeStyle::solid(fieldChrome.borderFocusColor, fieldChrome.borderFocusWidth)
                           : StrokeStyle::solid(fieldChrome.borderColor, fieldChrome.borderWidth);
            })
            .cornerRadius(CornerRadius {fieldChrome.cornerRadius})
            .cursor(isDisabled ? Cursor::Inherit : Cursor::Hand)
            .focusable(!isDisabled)
            .onKeyDown(isDisabled ? std::function<void(KeyCode, Modifiers)> {} : std::function<void(KeyCode, Modifiers)> {handleKey})
            .onTap(isDisabled ? std::function<void()> {} : std::function<void()> {handleTap});

        if (isDisabled) {
            Color overlay = fieldChrome.disabledColor;
            overlay.a *= 0.35f;
            fieldTrigger = std::move(fieldTrigger).overlay(Rectangle {}.fill(FillStyle::solid(overlay)));
        }

        return fieldTrigger;
    }
};

} // namespace

Element Select::body() const {
    auto theme = useEnvironment<ThemeKey>();
    SelectResolvedStyle const resolved = resolveStyle(style, theme());

    Signal<int> const selection = selectedIndex;
    Element field = SelectTrigger {
        .selectedIndex = selection,
        .options = options,
        .placeholder = placeholder,
        .emptyText = emptyText,
        .disabled = disabled,
        .showCheckmark = showCheckmark,
        .dismissOnSelect = dismissOnSelect,
        .showDetailInTrigger = showDetailInTrigger,
        .matchTriggerWidth = matchTriggerWidth,
        .triggerMode = triggerMode,
        .placement = placement,
        .style = resolved,
        .onChange = onChange,
    };

    if (helperText.empty()) {
        return field;
    }

    return VStack {
        .spacing = theme().space1,
        .alignment = Alignment::Start,
        .children = children(
            std::move(field),
            Text {
                .text = helperText,
                .font = resolved.detailFont,
                .color = disabled ? theme().disabledTextColor : theme().secondaryLabelColor,
                .horizontalAlignment = HorizontalAlignment::Leading,
                .wrapping = TextWrapping::Wrap,
            }
        )
    };
}

} // namespace lambda
