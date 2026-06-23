#include <Lambda.hpp>
#include <Lambda/UI/Detail/Runtime.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/UI.hpp>
#include <Lambda/UI/Views/Views.hpp>

// This demo intentionally keeps one mounted tree while toggling light/dark themes.
// Window::setTheme writes the Theme environment signal; theme-dependent bindings
// read theme() and update retained nodes without a Switch-driven remount.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using namespace lambdaui;

namespace {

struct ToolbarNavButton : ViewModifiers<ToolbarNavButton> {
    std::string label;
    Bindable<bool> active = false;
    std::function<void()> onTap;

    Element body() const {
        auto theme = useEnvironment<ThemeKey>();
        auto hovered = useState(false);

        Bindable<Color> fill {[active = active, hovered, theme] {
            return active.evaluate() ? Color::selectedContentBackground() :
                   hovered() ? theme().rowHoverBackgroundColor :
                                   Colors::transparent;
        }};
        Bindable<Color> textColor {[active = active, hovered] {
            return active.evaluate() ? Color::accent() :
                   hovered() ? Color::primary() :
                                   Color::secondary();
        }};

        return Text {
            .text = label,
            .font = Font::body(),
            .color = textColor,
        }
            .padding(5.f, theme().space3, 5.f, theme().space3)
            .fill(fill)
            .cornerRadius(CornerRadius {theme().radiusSmall})
            .cursor(Cursor::Hand)
            .onPointerEnter(std::function<void()> {[hovered] { hovered = true; }})
            .onPointerExit(std::function<void()> {[hovered] { hovered = false; }})
            .onTap(onTap);
    }
};

struct SidebarGlyphButton : ViewModifiers<SidebarGlyphButton> {
    IconName icon {};
    std::string label;
    Bindable<bool> selected = false;
    std::function<void()> onTap;

    Element body() const {
        auto theme = useEnvironment<ThemeKey>();
        auto hovered = useState(false);

        Bindable<Color> fill {[selected = selected, hovered, theme] {
            return selected.evaluate() ? Color::selectedContentBackground() :
                   hovered() ? theme().hoveredControlBackgroundColor :
                                   Colors::transparent;
        }};
        Bindable<Color> foreground {[selected = selected] {
            return selected.evaluate() ? Color::accent() : Color::secondary();
        }};

        return VStack {
            .spacing = 2.f,
            .alignment = Alignment::Center,
            .children = children(
                Icon {
                    .name = icon,
                    .size = 20.f,
                    .weight = 400.f,
                    .color = foreground,
                },
                Text {
                    .text = label,
                    .font = Font {.size = 8.f, .weight = 400.f},
                    .color = foreground,
                    .horizontalAlignment = HorizontalAlignment::Center,
                }
            ),
        }
            .padding(theme().space2)
            .flex(0.f, 0.f, 72.f)
            .fill(fill)
            .cornerRadius(CornerRadius {theme().radiusLarge})
            // .width(72.f)
            .cursor(Cursor::Hand)
            .onPointerEnter(std::function<void()> {[hovered] { hovered = true; }})
            .onPointerExit(std::function<void()> {[hovered] { hovered = false; }})
            .onTap(onTap);
    }
};

struct DensityPreviewRow : ViewModifiers<DensityPreviewRow> {
    std::string label;
    Theme previewTheme;
    Signal<std::string> searchValue {};
    bool stacked = false;

    Element body() const {
        auto theme = useEnvironment<ThemeKey>();

        Element preview = HStack {
            .spacing = previewTheme.space4,
            .alignment = Alignment::Center,
            .children = children(
                Button {
                    .label = "Save",
                    .variant = ButtonVariant::Primary,
                },
                TextInput {
                    .value = searchValue,
                    .placeholder = "Search…",
                }
                    .width(180.f)
            ),
        }
            .padding(previewTheme.space3)
            .fill(Color::controlBackground())
            .stroke(Color::separator(), 1.f)
            .cornerRadius(CornerRadius {previewTheme.radiusXLarge})
            .environment<ThemeKey>(previewTheme);

        if (stacked) {
            return VStack {
                .spacing = theme().space2,
                .alignment = Alignment::Start,
                .children = children(
                    Text {
                        .text = label,
                        .font = Font {.family = theme().monospacedBodyFont.family, .size = 10.f, .weight = 500.f},
                        .color = Color::tertiary(),
                    },
                    std::move(preview)
                ),
            };
        }

        return HStack {
            .spacing = theme().space5,
            .alignment = Alignment::Center,
            .children = children(
                Text {
                    .text = label,
                    .font = Font {.family = theme().monospacedBodyFont.family, .size = 10.f, .weight = 500.f},
                    .color = Color::tertiary(),
                }
                    .width(160.f),
                std::move(preview).flex(1.f, 1.f, 0.f)
            ),
        };
    }
};

struct PaletteColumnData {
    std::string_view name;
    std::array<Color, 10> swatches;
};

constexpr std::array<std::string_view, 10> kPaletteSteps = {"50", "100", "200", "300", "400",
                                                             "500", "600", "700", "800", "900"};

Font monoFont(Theme const &theme, float size, float weight = 500.f) {
    return Font {
        .family = theme.monospacedBodyFont.family,
        .size = size,
        .weight = weight,
    };
}

Font heroFont(float size, float weight = 300.f) { return Font {.size = size, .weight = weight}; }

Color withAlpha(Color c, float alpha) { return Color {c.r, c.g, c.b, alpha}; }

float luminance(Color const &c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }

std::string colorHex(Color const &c) {
    auto channel = [](float v) {
        float const clamped = std::clamp(v, 0.f, 1.f);
        return static_cast<int>(std::lround(clamped * 255.f));
    };

    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", channel(c.r), channel(c.g), channel(c.b));
    return buffer;
}

std::string decimalLabel(float value) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%.2f", static_cast<double>(value));
    return buffer;
}

Element panel(Theme const &theme, Element content, float padding = -1.f) {
    float const resolvedPadding = padding >= 0.f ? padding : theme.space5;
    return std::move(content)
        .padding(resolvedPadding)
        .fill(Color::controlBackground())
        .stroke(Color::separator(), 1.f)
        .cornerRadius(CornerRadius {theme.radiusXLarge});
}

Element divider(Theme const &theme, float width = 0.f) {
    Element line = Rectangle {}
                       .size(width, 1.f)
                       .fill(Color::separator());
    if (width <= 0.f) {
        return line;
    }
    return std::move(line).width(width);
}

Element accentDot(Color color, float size = 8.f, bool bordered = false) {
    Element dot = Rectangle {}
                      .size(size, size)
                      .fill(FillStyle::solid(color))
                      .cornerRadius(CornerRadius {size * 0.5f});
    if (bordered) {
        return std::move(dot).stroke(Color::separator(), 1.f);
    }
    return dot;
}

std::vector<PaletteColumnData> makePaletteColumns(LambdaPalette const &palette) {
    return {
        {"blue", {palette.blue50, palette.blue100, palette.blue200, palette.blue300, palette.blue400,
                  palette.blue500, palette.blue600, palette.blue700, palette.blue800, palette.blue900}},
        {"red", {palette.red50, palette.red100, palette.red200, palette.red300, palette.red400,
                 palette.red500, palette.red600, palette.red700, palette.red800, palette.red900}},
        {"green", {palette.green50, palette.green100, palette.green200, palette.green300, palette.green400,
                   palette.green500, palette.green600, palette.green700, palette.green800, palette.green900}},
        {"amber", {palette.amber50, palette.amber100, palette.amber200, palette.amber300, palette.amber400,
                   palette.amber500, palette.amber600, palette.amber700, palette.amber800, palette.amber900}},
        {"neutral", {palette.neutral50, palette.neutral100, palette.neutral200, palette.neutral300, palette.neutral400,
                     palette.neutral500, palette.neutral600, palette.neutral700, palette.neutral800, palette.neutral900}},
    };
}

Element paletteSwatch(Theme const &theme, Color color, std::string_view step) {
    Color const labelColor = luminance(color) > 0.72f ? Color::secondary() : Colors::white;
    bool const needsBorder = color.a < 1.f || luminance(color) > 0.94f;

    Element swatch = Rectangle {}
                         .height(32.f)
                         .fill(FillStyle::solid(color))
                         .cornerRadius(CornerRadius {theme.radiusXSmall})
                         .overlay(
                             Text {
                                 .text = std::string(step),
                                 .font = monoFont(theme, 8.f),
                                 .color = labelColor,
                                 .horizontalAlignment = HorizontalAlignment::Trailing,
                                 .verticalAlignment = VerticalAlignment::Bottom,
                             }
                                 .padding(0.f, 6.f, 4.f, 6.f)
                         );

    if (needsBorder) {
        return std::move(swatch).stroke(Color::separator(), 1.f);
    }
    return swatch;
}

Element paletteColumn(Theme const &theme, PaletteColumnData const &column) {
    std::vector<Element> swatches;
    swatches.reserve(column.swatches.size() + 1);
    swatches.push_back(
        Text {
            .text = std::string(column.name),
            .font = monoFont(theme, 9.f),
            .color = Color::tertiary(),
        }
    );

    for (std::size_t i = 0; i < column.swatches.size(); ++i) {
        swatches.push_back(paletteSwatch(theme, column.swatches[i], kPaletteSteps[i]));
    }

    return VStack {
        .spacing = 2.f,
        .alignment = Alignment::Stretch,
        .children = std::move(swatches),
    };
}

Element heroChip(Theme const &theme, std::string label, Color dotColor = Colors::transparent, bool bordered = false) {
    std::vector<Element> row;
    row.reserve(dotColor.a > 0.f ? 2u : 1u);

    if (dotColor.a > 0.f) {
        row.push_back(accentDot(dotColor, 6.f, bordered));
    }

    row.push_back(
        Text {
            .text = std::move(label),
            .font = monoFont(theme, 10.f),
            .color = Color::secondary(),
        }
    );

    return HStack {
        .spacing = theme.space2,
        .alignment = Alignment::Center,
        .children = std::move(row),
    }
        .padding(5.f, theme.space3, 5.f, theme.space3)
        .fill(Color::controlBackground())
        .stroke(Color::separator(), 1.f)
        .cornerRadius(CornerRadius {theme.radiusFull});
}

Element statusBadge(Theme const &theme, std::string label, Color fill, Color textColor) {
    return Text {
        .text = std::move(label),
        .font = Font::footnote(),
        .color = textColor,
    }
        .padding(4.f, theme.space2, 4.f, theme.space2)
        .fill(fill)
        .cornerRadius(CornerRadius {theme.radiusFull});
}

Element sectionHeader(Theme const &theme, std::string number, std::string title, std::string description) {
    return HStack {
        .spacing = theme.space3,
        .alignment = Alignment::Start,
        .children = children(
            Text {
                .text = std::move(number),
                .font = monoFont(theme, 11.f),
                .color = Color::tertiary(),
            },
            VStack {
                .spacing = 2.f,
                .alignment = Alignment::Start,
                .children = children(
                    Text {
                        .text = std::move(title),
                        .font = Font::title2(),
                        .color = Color::primary(),
                    },
                    Text {
                        .text = std::move(description),
                        .font = Font::body(),
                        .color = Color::secondary(),
                        .wrapping = TextWrapping::Wrap,
                    }
                ),
            }
                .flex(1.f, 1.f, 0.f)
        ),
    };
}

Element semanticChip(Theme const &theme, std::string name, Color color, std::string usage) {
    return panel(
        theme,
        VStack {
            .spacing = theme.space2,
            .alignment = Alignment::Start,
            .children = children(
                Rectangle {}
                    .height(6.f)
                    .fill(FillStyle::solid(color))
                    .cornerRadius(CornerRadius {3.f}),
                Text {
                    .text = std::move(name),
                    .font = monoFont(theme, 10.f),
                    .color = Color::tertiary(),
                },
                Text {
                    .text = colorHex(color),
                    .font = monoFont(theme, 12.f, 400.f),
                    .color = Color::primary(),
                },
                Text {
                    .text = std::move(usage),
                    .font = Font::caption2(),
                    .color = Color::secondary(),
                    .wrapping = TextWrapping::Wrap,
                }
            ),
        },
        theme.space4
    );
}

Element labelHierarchyRow(Theme const &theme, std::string token, std::string sample, Font sampleFont, Color color) {
    return HStack {
        .spacing = theme.space3,
        .alignment = Alignment::Center,
        .children = children(
            Text {
                .text = std::move(token),
                .font = monoFont(theme, 9.f),
                .color = Color::tertiary(),
            }
                .width(160.f),
            Text {
                .text = std::move(sample),
                .font = sampleFont,
                .color = color,
            }
                .flex(1.f, 1.f, 0.f)
        ),
    };
}

Element typeScaleRow(Theme const &theme, bool stacked, std::string role, std::string spec, Font font, Color color,
                     std::string sample) {
    Element meta = VStack {
        .spacing = 2.f,
        .alignment = Alignment::Start,
        .children = children(
            Text {
                .text = std::move(role),
                .font = monoFont(theme, 10.f),
                .color = Color::tertiary(),
            },
            Text {
                .text = std::move(spec),
                .font = monoFont(theme, 10.f, 400.f),
                .color = Color::quaternary(),
            }
        ),
    };

    Element sampleText = Text {
        .text = std::move(sample),
        .font = font,
        .color = color,
        .wrapping = TextWrapping::Wrap,
    };

    if (stacked) {
        return VStack {
            .spacing = theme.space2,
            .alignment = Alignment::Start,
            .children = children(
                std::move(meta),
                std::move(sampleText)
            ),
        };
    }

    return HStack {
        .spacing = theme.space5,
        .alignment = Alignment::Start,
        .children = children(
            std::move(meta).width(180.f),
            std::move(sampleText).flex(1.f, 1.f, 0.f)
        ),
    };
}

Element spaceToken(Theme const &theme, std::string label, int value, float boxSize) {
    return VStack {
        .spacing = theme.space2,
        .alignment = Alignment::Center,
        .children = children(
            Rectangle {}
                .size(boxSize, boxSize)
                .fill(FillStyle::solid(withAlpha(resolveColor(Color::accent(), theme), 0.6f)))
                .cornerRadius(CornerRadius {3.f}),
            Text {
                .text = std::move(label),
                .font = monoFont(theme, 9.f),
                .color = Color::tertiary(),
                .horizontalAlignment = HorizontalAlignment::Center,
            },
            Text {
                .text = std::to_string(value),
                .font = monoFont(theme, 9.f, 400.f),
                .color = Color::secondary(),
                .horizontalAlignment = HorizontalAlignment::Center,
            }
        ),
    };
}

Element radiusToken(Theme const &theme, std::string label, float radius, std::string valueLabel) {
    return VStack {
        .spacing = theme.space2,
        .alignment = Alignment::Center,
        .children = children(
            Rectangle {}
                .size(44.f, 44.f)
                .fill(Color::selectedContentBackground())
                .stroke(Color::accent(), 1.5f)
                .cornerRadius(CornerRadius {radius}),
            Text {
                .text = std::move(label) + "\n" + std::move(valueLabel),
                .font = monoFont(theme, 9.f),
                .color = Color::tertiary(),
                .horizontalAlignment = HorizontalAlignment::Center,
                .wrapping = TextWrapping::Wrap,
            }
        ),
    };
}

Element componentCard(Theme const &theme, std::string title, Element content) {
    return panel(
        theme,
        VStack {
            .spacing = theme.space4,
            .alignment = Alignment::Start,
            .children = children(
                Text {
                    .text = std::move(title),
                    .font = monoFont(theme, 10.f),
                    .color = Color::tertiary(),
                },
                std::move(content)
            ),
        }
    );
}

Element controlRow(Theme const &theme, std::string label, Element control) {
    return HStack {
        .spacing = theme.space3,
        .alignment = Alignment::Center,
        .children = children(
            std::move(control),
            Text {
                .text = std::move(label),
                .font = Font::subheadline(),
                .color = Color::primary(),
            }
        ),
    };
}

Element sliderMetricRow(Theme const &theme, std::string label, Signal<float> value, float min, float max, float step) {
    return HStack {
        .spacing = theme.space4,
        .alignment = Alignment::Center,
        .children = children(
            Text {
                .text = std::move(label),
                .font = Font::subheadline(),
                .color = Color::primary(),
            }
                .width(88.f),
            Slider {
                .value = value,
                .min = min,
                .max = max,
                .step = step,
            }
                .flex(1.f, 1.f, 0.f),
            Text {
                .text = [value] {
                    return decimalLabel(value());
                },
                .font = monoFont(theme, 12.f, 400.f),
                .color = Color::secondary(),
                .horizontalAlignment = HorizontalAlignment::Trailing,
            }
                .width(42.f)
        ),
    };
}

Element makeToolbar(Theme const &theme, bool showNav, Bindable<int> activeNav, Signal<bool> darkMode,
                    std::function<void(float)> onJump, std::function<void(bool)> onDarkModeChange) {
    return VStack {
        .spacing = 0.f,
        .children = children(
            HStack {
                .spacing = theme.space2,
                .alignment = Alignment::Center,
                .children = children(
                    Text {
                        .text = "Lambda",
                        .font = Font::headline(),
                        .color = Color::primary(),
                    },
                    Text {
                        .text = "v4",
                        .font = monoFont(theme, 10.f),
                        .color = Color::secondary(),
                    }
                        .padding(2.f, 7.f, 2.f, 7.f)
                        .fill(Color::controlBackground())
                        .stroke(Color::separator(), 1.f)
                        .cornerRadius(CornerRadius {theme.radiusFull}),
                    Spacer {},
                    ToolbarNavButton {
                        .label = "Colors",
                        .active = [activeNav] {
                            return activeNav.evaluate() == 0;
                        },
                        .onTap = [onJump] { onJump(0.00f); },
                    },
                    ToolbarNavButton {
                        .label = "Type",
                        .active = [activeNav] {
                            return activeNav.evaluate() == 1;
                        },
                        .onTap = [onJump] { onJump(0.34f); },
                    },
                    ToolbarNavButton {
                        .label = "Spacing",
                        .active = [activeNav] {
                            return activeNav.evaluate() == 2;
                        },
                        .onTap = [onJump] { onJump(0.63f); },
                    },
                    ToolbarNavButton {
                        .label = "Components",
                        .active = [activeNav] {
                            return activeNav.evaluate() == 3;
                        },
                        .onTap = [onJump] { onJump(0.83f); },
                    },
                    Icon {
                        .name = [darkMode] {
                            return darkMode() ? IconName::LightMode : IconName::DarkMode;
                        },
                        .size = 16.f,
                        .weight = 400.f,
                        .color = Color::secondary(),
                    },
                    Toggle {
                        .value = darkMode,
                        .onChange = std::move(onDarkModeChange),
                    }
                )
            }
                .padding(theme.space4, theme.space6, theme.space4, theme.space6)
                .fill(Color::elevatedBackground()),
            Rectangle {}
                .size(0.f, 1.f)
                .fill(Color::separator())
        ),
    };
}

} // namespace

struct ThemeDemoPage {
    Signal<bool> darkMode {};
    Signal<Point> scrollOffset {};
    Signal<Size> viewportSize {};
    Signal<Size> contentSize {};
    Signal<bool> flashAttention {};
    Signal<bool> enableThinking {};
    Signal<bool> useMmap {};
    Signal<float> temperature {};
    Signal<float> topP {};
    Signal<std::string> repoSearch {};
    Signal<std::string> workspacePath {};
    Signal<std::string> disabledField {};
    Signal<std::string> compactSearch {};
    Signal<std::string> defaultSearch {};
    Signal<std::string> comfortableSearch {};
    Signal<int> selectedSidebar {};
    Theme theme {};
    std::function<void(bool)> onDarkModeChange;

    auto body() const {
        auto scrollOffsetState = scrollOffset;
        auto viewportSizeState = viewportSize;
        auto contentSizeState = contentSize;
        auto selectedSidebarState = selectedSidebar;

        Theme const compactTheme = theme.withDensity(0.75f);
        Theme const comfortableTheme = theme.withDensity(1.25f);
        LambdaPalette const palette {};

        Rect const bounds = useBounds();
        float const viewportWidth = bounds.width > 0.f ? bounds.width : 1280.f;
        bool const compact = viewportWidth < 980.f;
        bool const narrow = viewportWidth < 760.f;
        bool const showToolbarNav = viewportWidth >= 860.f;

        float const horizontalInset = narrow ? theme.space4 : theme.space7;
        float const contentWidth = std::clamp(viewportWidth - horizontalInset * 2.f, 320.f, 1080.f);
        Bindable<int> activeNav {[scrollOffsetState, viewportSizeState, contentSizeState] {
            float const maxScroll = std::max(0.f, contentSizeState().height - viewportSizeState().height);
            float const scrollProgress = maxScroll > 0.f ? scrollOffsetState().y / maxScroll : 0.f;
            return scrollProgress < 0.24f ? 0 : scrollProgress < 0.53f ? 1 : scrollProgress < 0.73f ? 2 : 3;
        }};

        auto jumpToSection = [scrollOffsetState, viewportSizeState, contentSizeState](float ratio) {
            float const maxY = std::max(0.f, contentSizeState().height - viewportSizeState().height);
            scrollOffsetState = Point {0.f, std::clamp(maxY * ratio, 0.f, maxY)};
        };

        std::vector<Element> heroChips;
        heroChips.push_back(heroChip(theme, "accentColor " + colorHex(theme.accentColor), theme.accentColor));
        heroChips.push_back(heroChip(theme, "labelColor " + colorHex(theme.labelColor), theme.labelColor));
        heroChips.push_back(heroChip(theme, "windowBackgroundColor", theme.windowBackgroundColor, true));
        heroChips.push_back(heroChip(theme, "bodyFont · monospacedBodyFont"));
        heroChips.push_back(heroChip(theme, "space1 → space8"));
        heroChips.push_back(heroChip(theme, "Material Symbols Rounded"));

        Element hero = VStack {
            .spacing = theme.space6,
            .alignment = Alignment::Start,
            .children = children(
                HStack {
                    .spacing = theme.space2,
                    .alignment = Alignment::Center,
                    .children = children(
                        accentDot(theme.accentColor),
                        Text {
                            .text = "lambdaui::Theme — Lambda Studio",
                            .font = monoFont(theme, 11.f),
                            .color = Color::tertiary(),
                        }
                    ),
                },
                VStack {
                    .spacing = 0.f,
                    .alignment = Alignment::Start,
                    .children = children(
                        HStack {
                            .spacing = theme.space2,
                            .alignment = Alignment::Center,
                            .children = children(
                                Text {
                                    .text = "The",
                                    .font = heroFont(narrow ? 40.f : 52.f, 300.f),
                                    .color = Color::primary(),
                                },
                                Text {
                                    .text = "Lambda",
                                    .font = heroFont(narrow ? 40.f : 52.f, 600.f),
                                    .color = Color::primary(),
                                }
                            ),
                        },
                        Text {
                            .text = "theme system.",
                            .font = heroFont(narrow ? 40.f : 52.f, 300.f),
                            .color = Color::primary(),
                        }
                    ),
                },
                Text {
                    .text = "A C++23 UI framework for macOS. Every token in this document is a direct "
                            "`lambdaui::Theme` field, and every card is composed from native Lambda views.",
                    .font = Font::title3(),
                    .color = Color::secondary(),
                    .wrapping = TextWrapping::Wrap,
                },
                Grid {
                    .columns = static_cast<std::size_t>(narrow ? 2 : 3),
                    .horizontalSpacing = theme.space2,
                    .verticalSpacing = theme.space2,
                    .horizontalAlignment = Alignment::Stretch,
                    .verticalAlignment = Alignment::Start,
                    .children = std::move(heroChips),
                }
            ),
        };

        std::vector<Element> paletteColumns;
        for (PaletteColumnData const &column : makePaletteColumns(palette)) {
            paletteColumns.push_back(paletteColumn(theme, column));
        }

        Element colorsSection = VStack {
            .spacing = theme.space5,
            .alignment = Alignment::Start,
            .children = children(
                sectionHeader(theme, "01", "LambdaPalette & Theme colors",
                              "Five hues by ten steps. Semantic tokens keep the same field names as `struct Theme`."),
                Grid {
                    .columns = static_cast<std::size_t>(narrow ? 2 : 5),
                    .horizontalSpacing = theme.space3,
                    .verticalSpacing = theme.space3,
                    .horizontalAlignment = Alignment::Stretch,
                    .verticalAlignment = Alignment::Start,
                    .children = std::move(paletteColumns),
                },
                Grid {
                    .columns = static_cast<std::size_t>(narrow ? 1 : (compact ? 2 : 4)),
                    .horizontalSpacing = theme.space3,
                    .verticalSpacing = theme.space3,
                    .horizontalAlignment = Alignment::Stretch,
                    .verticalAlignment = Alignment::Start,
                    .children = children(
                        semanticChip(theme, "accentColor", theme.accentColor, "Button, Toggle, focus ring"),
                        semanticChip(theme, "successColor", theme.successColor, "Positive state, completion"),
                        semanticChip(theme, "warningColor", theme.warningColor, "Approval, caution"),
                        semanticChip(theme, "dangerColor", theme.dangerColor, "Destructive, error")
                    ),
                },
                panel(
                    theme,
                    VStack {
                        .spacing = theme.space2,
                        .alignment = Alignment::Start,
                        .children = children(
                            labelHierarchyRow(theme, "labelColor", "Primary label", Font {.size = 14.f, .weight = 400.f}, Color::primary()),
                            labelHierarchyRow(theme, "secondaryLabelColor", "Secondary label", Font::body(), Color::secondary()),
                            labelHierarchyRow(theme, "tertiaryLabelColor", "Tertiary label", Font::subheadline(), Color::tertiary()),
                            labelHierarchyRow(theme, "quaternaryLabelColor", "Quaternary / placeholder", Font::footnote(), Color::quaternary())
                        ),
                    }
                )
            ),
        };

        std::vector<Element> typeRows;
        auto pushTypeRow = [&](Element row) {
            if (!typeRows.empty()) {
                typeRows.push_back(divider(theme));
            }
            typeRows.push_back(std::move(row));
        };

        pushTypeRow(typeScaleRow(theme, narrow, "largeTitleFont", "34px / 400", theme.largeTitleFont, Color::primary(),
                                 "Lambda Studio"));
        pushTypeRow(typeScaleRow(theme, narrow, "titleFont", "28px / 400", theme.titleFont, Color::primary(),
                                 "Model Parameters"));
        pushTypeRow(typeScaleRow(theme, narrow, "title2Font", "22px / 400", theme.title2Font, Color::primary(),
                                 "Chats"));
        pushTypeRow(typeScaleRow(theme, narrow, "title3Font", "20px / 400", theme.title3Font, Color::primary(),
                                 "Settings"));
        pushTypeRow(typeScaleRow(theme, narrow, "headlineFont", "13px / 600", theme.headlineFont, Color::primary(),
                                 "Llama-3.2-3B-Instruct-Q4_K_M.gguf"));
        pushTypeRow(typeScaleRow(theme, narrow, "bodyFont", "13px / 400", theme.bodyFont, Color::primary(),
                                 "Create a new chat or select one from the list."));
        pushTypeRow(typeScaleRow(theme, narrow, "calloutFont", "12px / 400", theme.calloutFont, Color::primary(),
                                 "Callout text — brief, supporting, mid-weight messages."));
        pushTypeRow(typeScaleRow(theme, narrow, "subheadlineFont", "12px / 400", theme.subheadlineFont, Color::primary(),
                                 "Subheadline — paragraph leads."));
        pushTypeRow(typeScaleRow(theme, narrow, "footnoteFont", "11px / 400", theme.footnoteFont, Color::secondary(),
                                 "4.2 tok/s · TTFT 312 ms · 148 tokens · Llama 3.2 3B"));
        pushTypeRow(typeScaleRow(theme, narrow, "captionFont", "11px / 400", theme.captionFont, Color::tertiary(),
                                 "llama.cpp · Updated 3 days ago · MIT"));
        pushTypeRow(typeScaleRow(theme, narrow, "caption2Font", "10px / 400", theme.caption2Font, Color::tertiary(),
                                 "v4 · Release Candidate · CoreText fallback"));
        pushTypeRow(typeScaleRow(theme, narrow, "monospacedBodyFont", "13px / Menlo", theme.monospacedBodyFont, Color::secondary(),
                                 R"({"path": "src/main.cpp", "limit": 50})"));

        Element typographySection = VStack {
            .spacing = theme.space5,
            .alignment = Alignment::Start,
            .children = children(
                sectionHeader(theme, "02", "Typography — Theme::*Font fields",
                              "Production uses CoreText and San Francisco. The demo renders directly through the semantic type scale."),
                panel(
                    theme,
                    VStack {
                        .spacing = theme.space3,
                        .alignment = Alignment::Start,
                        .children = std::move(typeRows),
                    },
                    theme.space4
                )
            ),
        };

        std::vector<Element> spacingTokens = {
            spaceToken(theme, "space1", 4, 20.f),
            spaceToken(theme, "space2", 8, 28.f),
            spaceToken(theme, "space3", 12, 36.f),
            spaceToken(theme, "space4", 16, 44.f),
            spaceToken(theme, "space5", 20, 52.f),
            spaceToken(theme, "space6", 24, 60.f),
            spaceToken(theme, "space7", 32, 72.f),
            spaceToken(theme, "space8", 48, 92.f),
        };

        std::vector<Element> radiusTokens = {
            radiusToken(theme, "radiusNone", theme.radiusNone, "0"),
            radiusToken(theme, "radiusXSmall", theme.radiusXSmall, "4"),
            radiusToken(theme, "radiusSmall", theme.radiusSmall, "6"),
            radiusToken(theme, "radiusMedium", theme.radiusMedium, "8"),
            radiusToken(theme, "radiusLarge", theme.radiusLarge, "10"),
            radiusToken(theme, "radiusXLarge", theme.radiusXLarge, "14"),
            radiusToken(theme, "radiusFull", theme.radiusFull, "∞"),
        };

        Element spacingSection = VStack {
            .spacing = theme.space5,
            .alignment = Alignment::Start,
            .children = children(
                sectionHeader(theme, "03", "Spacing, radii, density",
                              "Spacing tokens scale with `Theme::withDensity(d)` while the radii remain stable."),
                Grid {
                    .columns = static_cast<std::size_t>(narrow ? 4 : 8),
                    .horizontalSpacing = theme.space2,
                    .verticalSpacing = theme.space3,
                    .horizontalAlignment = Alignment::Center,
                    .verticalAlignment = Alignment::End,
                    .children = std::move(spacingTokens),
                },
                Grid {
                    .columns = static_cast<std::size_t>(narrow ? 3 : 7),
                    .horizontalSpacing = theme.space3,
                    .verticalSpacing = theme.space3,
                    .horizontalAlignment = Alignment::Center,
                    .verticalAlignment = Alignment::Start,
                    .children = std::move(radiusTokens),
                },
                panel(
                    theme,
                    VStack {
                        .spacing = theme.space5,
                        .alignment = Alignment::Start,
                        .children = children(
                            DensityPreviewRow {.label = "Theme::compact()", .previewTheme = compactTheme, .searchValue = compactSearch, .stacked = narrow},
                            divider(theme),
                            DensityPreviewRow {.label = "default (1.0)", .previewTheme = theme, .searchValue = defaultSearch, .stacked = narrow},
                            divider(theme),
                            DensityPreviewRow {.label = "Theme::comfortable()", .previewTheme = comfortableTheme, .searchValue = comfortableSearch, .stacked = narrow}
                        ),
                    }
                )
            ),
        };

        std::vector<Element> buttonItems = {
            Button {.label = "Search", .variant = ButtonVariant::Primary},
            Button {.label = "Cancel", .variant = ButtonVariant::Secondary},
            Button {.label = "Download", .variant = ButtonVariant::Ghost},
            Button {.label = "Delete", .variant = ButtonVariant::Destructive},
        };

        Element buttonsCard = componentCard(
            theme, "Button — lambdaui::Button",
            Grid {
                .columns = static_cast<std::size_t>(narrow ? 2 : 4),
                .horizontalSpacing = theme.space2,
                .verticalSpacing = theme.space2,
                .horizontalAlignment = Alignment::Stretch,
                .verticalAlignment = Alignment::Center,
                .children = std::move(buttonItems),
            }
        );

        Element badgesCard = componentCard(
            theme, "Badge — lambdaui::Text + bubble",
            Grid {
                .columns = static_cast<std::size_t>(narrow ? 2 : 4),
                .horizontalSpacing = theme.space2,
                .verticalSpacing = theme.space2,
                .horizontalAlignment = Alignment::Stretch,
                .verticalAlignment = Alignment::Center,
                .children = children(
                    statusBadge(theme, "Completed", Color::successBackground(), Color::success()),
                    statusBadge(theme, "Running", Color::selectedContentBackground(), Color::accent()),
                    statusBadge(theme, "Approval", Color::warningBackground(), Color::warning()),
                    statusBadge(theme, "Failed", Color::dangerBackground(), Color::danger())
                ),
            }
        );

        Element toggleCard = componentCard(
            theme, "Toggle & Checkbox — lambdaui::Toggle · lambdaui::Checkbox",
            VStack {
                .spacing = theme.space3,
                .alignment = Alignment::Start,
                .children = children(
                    controlRow(theme, "flashAttention", Toggle {.value = flashAttention}),
                    controlRow(theme, "enableThinking", Toggle {.value = enableThinking}),
                    controlRow(theme, "useMmap", Checkbox {.value = useMmap})
                ),
            }
        );

        Element sliderCard = componentCard(
            theme, "Slider — lambdaui::Slider",
            VStack {
                .spacing = theme.space4,
                .alignment = Alignment::Start,
                .children = children(
                    sliderMetricRow(theme, "temperature", temperature, 0.f, 1.f, 0.01f),
                    sliderMetricRow(theme, "top-p", topP, 0.f, 1.f, 0.01f)
                ),
            }
        );

        Element textFieldCard = componentCard(
            theme, "TextField — lambdaui::TextInput",
            VStack {
                .spacing = theme.space3,
                .alignment = Alignment::Start,
                .children = children(
                    TextInput {.value = repoSearch, .placeholder = "Search GGUF repositories…"},
                    TextInput {.value = workspacePath, .placeholder = "/Users/alex/projects"},
                    TextInput {.value = disabledField, .placeholder = "Disabled", .disabled = true}
                ),
            }
        );

        Element sidebarCard = componentCard(
            theme, "SidebarButton — icon + label",
            Grid {
                .columns = static_cast<std::size_t>(narrow ? 2 : 4),
                .horizontalSpacing = theme.space3,
                .verticalSpacing = theme.space3,
                .horizontalAlignment = Alignment::Center,
                .verticalAlignment = Alignment::Center,
                .children = children(
                    SidebarGlyphButton {
                        .icon = IconName::Chat,
                        .label = "Chats",
                        .selected = [selectedSidebarState] {
                            return selectedSidebarState() == 0;
                        },
                        .onTap = [selectedSidebarState] { selectedSidebarState = 0; },
                    },
                    SidebarGlyphButton {
                        .icon = IconName::Hub,
                        .label = "Hub",
                        .selected = [selectedSidebarState] {
                            return selectedSidebarState() == 1;
                        },
                        .onTap = [selectedSidebarState] { selectedSidebarState = 1; },
                    },
                    SidebarGlyphButton {
                        .icon = IconName::Folder,
                        .label = "Files",
                        .selected = [selectedSidebarState] {
                            return selectedSidebarState() == 2;
                        },
                        .onTap = [selectedSidebarState] { selectedSidebarState = 2; },
                    },
                    SidebarGlyphButton {
                        .icon = IconName::Settings,
                        .label = "Settings",
                        .selected = [selectedSidebarState] {
                            return selectedSidebarState() == 3;
                        },
                        .onTap = [selectedSidebarState] { selectedSidebarState = 3; },
                    }
                ),
            }
        );

        Element componentsSection = VStack {
            .spacing = theme.space5,
            .alignment = Alignment::Start,
            .children = children(
                sectionHeader(theme, "04", "Components — 1:1 with Lambda views",
                              "The cards below use the same semantic tokens as the rest of the page, but rendered through real controls."),
                Grid {
                    .columns = static_cast<std::size_t>(narrow ? 1 : 2),
                    .horizontalSpacing = theme.space4,
                    .verticalSpacing = theme.space4,
                    .horizontalAlignment = Alignment::Stretch,
                    .verticalAlignment = Alignment::Start,
                    .children = children(
                        std::move(buttonsCard),
                        std::move(badgesCard),
                        std::move(toggleCard),
                        std::move(sliderCard),
                        std::move(textFieldCard),
                        std::move(sidebarCard)
                    ),
                }
            ),
        };

        Element footer = HStack {
            .spacing = theme.space3,
            .alignment = Alignment::Center,
            .children = children(
                Icon {
                    .name = IconName::Bolt,
                    .size = 18.f,
                    .weight = 500.f,
                    .color = Color::tertiary(),
                },
                Text {
                    .text = "Lambda v4 · lambdaui::Theme",
                    .font = Font::subheadline(),
                    .color = Color::tertiary(),
                },
                Text {
                    .text = "github.com/aavci1/lambda-v4",
                    .font = monoFont(theme, 11.f, 400.f),
                    .color = Color::quaternary(),
                }
            ),
        };

        std::vector<Element> scrollChildren;
        scrollChildren.reserve(9);
        scrollChildren.push_back(std::move(hero).width(contentWidth));
        scrollChildren.push_back(divider(theme, contentWidth));
        scrollChildren.push_back(std::move(colorsSection).width(contentWidth));
        scrollChildren.push_back(divider(theme, contentWidth));
        scrollChildren.push_back(std::move(typographySection).width(contentWidth));
        scrollChildren.push_back(divider(theme, contentWidth));
        scrollChildren.push_back(std::move(spacingSection).width(contentWidth));
        scrollChildren.push_back(divider(theme, contentWidth));
        scrollChildren.push_back(std::move(componentsSection).width(contentWidth));
        scrollChildren.push_back(divider(theme, contentWidth));
        scrollChildren.push_back(std::move(footer).width(contentWidth));

        Element content = VStack {
            .spacing = theme.space7,
            .alignment = Alignment::Center,
            .children = std::move(scrollChildren),
        }
            .padding(theme.space7, horizontalInset, theme.space7, horizontalInset);

        return VStack {
            .spacing = 0.f,
            .alignment = Alignment::Stretch,
            .children = children(
                makeToolbar(theme, showToolbarNav, activeNav, darkMode, jumpToSection, onDarkModeChange),
                ScrollView {
                    .axis = ScrollAxis::Vertical,
                    .scrollOffset = scrollOffset,
                    .viewportSize = viewportSize,
                    .contentSize = contentSize,
                    .children = children(std::move(content)),
                }
                    .flex(1.f, 1.f, 0.f)
                    .fill(Color::windowBackground())
            ),
        }
            .fill(Color::windowBackground());
    }
};

struct ThemeDemoRoot {
    auto body() const {
        auto theme = useEnvironment<ThemeKey>();
        auto darkMode = useState(false);
        auto scrollOffset = useState(Point {0.f, 0.f});
        auto viewportSize = useState(Size {0.f, 0.f});
        auto contentSize = useState(Size {0.f, 0.f});
        auto flashAttention = useState(true);
        auto enableThinking = useState(false);
        auto useMmap = useState(true);
        auto temperature = useState(0.70f);
        auto topP = useState(0.92f);
        auto repoSearch = useState(std::string {});
        auto workspacePath = useState(std::string {"/Users/alex/projects"});
        auto disabledField = useState(std::string {"Disabled"});
        auto compactSearch = useState(std::string {});
        auto defaultSearch = useState(std::string {});
        auto comfortableSearch = useState(std::string {});
        auto selectedSidebar = useState(0);
        Runtime* runtime = Runtime::current();

        return ThemeDemoPage {
            .darkMode = darkMode,
            .scrollOffset = scrollOffset,
            .viewportSize = viewportSize,
            .contentSize = contentSize,
            .flashAttention = flashAttention,
            .enableThinking = enableThinking,
            .useMmap = useMmap,
            .temperature = temperature,
            .topP = topP,
            .repoSearch = repoSearch,
            .workspacePath = workspacePath,
            .disabledField = disabledField,
            .compactSearch = compactSearch,
            .defaultSearch = defaultSearch,
            .comfortableSearch = comfortableSearch,
            .selectedSidebar = selectedSidebar,
            .theme = theme(),
            .onDarkModeChange = [runtime](bool enabled) {
                if (runtime) {
                    runtime->window().setTheme(enabled ? Theme::dark() : Theme::light());
                }
            },
        };
    }
};

int main(int argc, char *argv[]) {
    Application app(argc, argv);

    auto &window = app.createWindow<Window>({
        .size = {1280, 980},
        .title = "Lambda — Theme Demo",
        .resizable = true,
    });

    window.setView<ThemeDemoRoot>();
    return app.exec();
}
