#include <Lambda.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/UI.hpp>
#include <Lambda/UI/Views/Card.hpp>
#include <Lambda/UI/Views/Grid.hpp>
#include <Lambda/UI/Views/HStack.hpp>
#include <Lambda/UI/Views/Rectangle.hpp>
#include <Lambda/UI/Views/ScrollView.hpp>
#include <Lambda/UI/Views/Select.hpp>
#include <Lambda/UI/Views/Spacer.hpp>
#include <Lambda/UI/Views/Switch.hpp>
#include <Lambda/UI/Views/Text.hpp>
#include <Lambda/UI/Views/VStack.hpp>
#include <Lambda/UI/Views/ZStack.hpp>

#include <functional>
#include <sstream>
#include <string>
#include <vector>

using namespace lambda;

namespace {

Element makeSectionCard(Theme const &theme, std::string title, std::string caption, Element content) {
    return Card {
        .child = VStack {
            .spacing = theme.space3,
            .alignment = Alignment::Start,
            .children = children(
                Text {
                    .text = std::move(title),
                    .font = Font::title2(),
                    .color = Color::primary(),
                    .horizontalAlignment = HorizontalAlignment::Leading,
                },
                Text {
                    .text = std::move(caption),
                    .font = Font::footnote(),
                    .color = Color::secondary(),
                    .horizontalAlignment = HorizontalAlignment::Leading,
                    .wrapping = TextWrapping::Wrap,
                },
                std::move(content)
            )
        },
        .style = Card::Style {
            .padding = theme.space4,
            .cornerRadius = theme.radiusLarge,
        },
    };
}

Element colorBlock(Color color, float width, float height, float radius) {
    return Rectangle {}
        .size(width, height)
        .fill(FillStyle::solid(color))
        .cornerRadius(CornerRadius {radius});
}

Element justifyPreviewBlock(Color color, float width, float height, float radius) {
    return Rectangle {}
        .size(width, height)
        .fill(FillStyle::solid(color))
        .cornerRadius(CornerRadius {radius});
}

std::vector<SelectOption> stackAxisOptions() {
    return {
        SelectOption {.label = "HStack", .detail = "Horizontal main axis"},
        SelectOption {.label = "VStack", .detail = "Vertical main axis"},
    };
}

std::vector<SelectOption> stackAlignmentOptions() {
    return {
        SelectOption {.label = "Start", .detail = "Pin to the cross-axis start edge"},
        SelectOption {.label = "Center", .detail = "Center items on the cross axis"},
        SelectOption {.label = "End", .detail = "Pin to the cross-axis end edge"},
        SelectOption {.label = "Stretch", .detail = "Expand each item across the cross axis"},
    };
}

std::vector<SelectOption> justifyContentOptions() {
    return {
        SelectOption {.label = "Start", .detail = "Pack items at the start"},
        SelectOption {.label = "Center", .detail = "Pack items at the center"},
        SelectOption {.label = "End", .detail = "Pack items at the end"},
        SelectOption {.label = "SpaceBetween", .detail = "Distribute only the inner gaps"},
        SelectOption {.label = "SpaceAround", .detail = "Share space around each item"},
        SelectOption {.label = "SpaceEvenly", .detail = "Use even outer and inner gaps"},
    };
}

std::string stackAxisLabel(int index) {
    return index == 1 ? "VStack" : "HStack";
}

std::string stackAlignmentLabel(int index) {
    switch (index) {
    case 0:
        return "Start";
    case 2:
        return "End";
    case 3:
        return "Stretch";
    default:
        return "Center";
    }
}

std::string justifyContentLabel(int index) {
    switch (index) {
    case 0:
        return "Start";
    case 1:
        return "Center";
    case 2:
        return "End";
    case 3:
        return "SpaceBetween";
    case 4:
        return "SpaceAround";
    case 5:
        return "SpaceEvenly";
    default:
        return "Start";
    }
}

Alignment stackAlignmentFromIndex(int index) {
    switch (index) {
    case 0:
        return Alignment::Start;
    case 2:
        return Alignment::End;
    case 3:
        return Alignment::Stretch;
    default:
        return Alignment::Center;
    }
}

JustifyContent justifyContentFromIndex(int index) {
    switch (index) {
    case 1:
        return JustifyContent::Center;
    case 2:
        return JustifyContent::End;
    case 3:
        return JustifyContent::SpaceBetween;
    case 4:
        return JustifyContent::SpaceAround;
    case 5:
        return JustifyContent::SpaceEvenly;
    default:
        return JustifyContent::Start;
    }
}

Element makeJustifyControl(Theme const &theme, std::string label, Signal<int> selection,
                           std::vector<SelectOption> options) {
    return VStack {
        .spacing = theme.space1,
        .alignment = Alignment::Stretch,
        .children = children(
            Text {
                .text = std::move(label),
                .font = Font::caption(),
                .color = Color::secondary(),
                .horizontalAlignment = HorizontalAlignment::Leading,
            },
            Select {
                .selectedIndex = selection,
                .options = std::move(options),
                .showDetailInTrigger = false,
                .style = Select::Style {
                    .menuMaxHeight = 220.f,
                },
            }
        )
    };
}

Element makeHStackJustifyPlaygroundPreview(Theme const &theme, Alignment alignment, JustifyContent justifyContent) {
    return HStack {
        .spacing = theme.space2,
        .alignment = alignment,
        .justifyContent = justifyContent,
        .children = children(
            justifyPreviewBlock(Color::accent(), 40.f, 44.f, theme.radiusMedium),
            justifyPreviewBlock(Color::success(), 56.f, 88.f, theme.radiusMedium),
            justifyPreviewBlock(Color::warning(), 32.f, 60.f, theme.radiusMedium)
        ),
    }
        .size(0.f, 152.f)
        .padding(theme.space3)
        .fill(FillStyle::solid(Color::controlBackground()))
        .cornerRadius(CornerRadius {theme.radiusMedium});
}

Element makeVStackJustifyPlaygroundPreview(Theme const &theme, Alignment alignment, JustifyContent justifyContent) {
    return HStack {
        .alignment = Alignment::Center,
        .children = children(
            Spacer {},
            VStack {
                .spacing = theme.space2,
                .alignment = alignment,
                .justifyContent = justifyContent,
                .children = children(
                    justifyPreviewBlock(Color::accent(), 88.f, 32.f, theme.radiusMedium),
                    justifyPreviewBlock(Color::success(), 124.f, 44.f, theme.radiusMedium),
                    justifyPreviewBlock(Color::warning(), 68.f, 36.f, theme.radiusMedium)
                ),
            }
                .size(160.f, 188.f)
                .padding(theme.space3)
                .fill(FillStyle::solid(Color::controlBackground()))
                .cornerRadius(CornerRadius {theme.radiusMedium}),
            Spacer {}
        ),
    }
        .size(0.f, 188.f);
}

int justifyPreviewKey(int axisIndex, int alignmentIndex, int justifyIndex) {
    return axisIndex * 100 + alignmentIndex * 10 + justifyIndex;
}

Element makeVStackDemo(Theme const &theme) {
    return makeSectionCard(
        theme, "VStack",
        "Children flow top-to-bottom. Center alignment keeps each child at its intrinsic width and centers it in the column.",
        VStack {
            .spacing = theme.space3,
            .alignment = Alignment::Center,
            .children = children(
                colorBlock(Color::accent(), 160.f, 34.f, theme.radiusMedium),
                colorBlock(Color::success(), 220.f, 42.f, theme.radiusMedium),
                colorBlock(Color::warning(), 120.f, 30.f, theme.radiusMedium),
                HStack {
                    .spacing = theme.space2,
                    .alignment = Alignment::Center,
                    .children = children(
                        Text {
                            .text = "Rows can also host nested stacks",
                            .font = Font::headline(),
                            .color = Color::primary(),
                            .horizontalAlignment = HorizontalAlignment::Leading,
                        },
                        Spacer {},
                        Text {
                            .text = "nested",
                            .font = Font::caption(),
                            .color = Color::secondary(),
                            .horizontalAlignment = HorizontalAlignment::Trailing,
                        }
                    ),
                }
                    .padding(theme.space3)
                    .fill(FillStyle::solid(Color::controlBackground()))
                    .cornerRadius(CornerRadius {theme.radiusMedium})
            )
        } //
            .padding(theme.space3)
            .fill(FillStyle::solid(Color::windowBackground()))
            .cornerRadius(CornerRadius {theme.radiusMedium})
    );
}

Element makeHStackDemo(Theme const &theme) {
    return makeSectionCard(
        theme, "HStack",
        "Children flow left-to-right. Flex growth lets selected items absorb remaining width.",
        VStack {
            .spacing = theme.space3,
            .children = children(
                HStack {
                    .spacing = theme.space3,
                    .alignment = Alignment::Center,
                    .children = children(
                        colorBlock(Color::accent(), 56.f, 54.f, theme.radiusMedium).flex(2.f, 1.f, 0.f),
                        colorBlock(Color::success(), 56.f, 76.f, theme.radiusMedium),
                        colorBlock(Color::warning(), 56.f, 40.f, theme.radiusMedium).flex(1.f, 1.f, 0.f),
                        colorBlock(Color::danger(), 56.f, 54.f, theme.radiusMedium)
                    ),
                },
                HStack {
                    .spacing = 0.f,
                    .alignment = Alignment::Center,
                    .children = children(
                        Text {
                            .text = "Leading",
                            .font = Font::headline(),
                            .color = Color::primary(),
                        },
                        Spacer {},
                        Text {
                            .text = "Spacer pushes this trailing label",
                            .font = Font::caption(),
                            .color = Color::secondary(),
                            .horizontalAlignment = HorizontalAlignment::Trailing,
                        }
                    ),
                }
                    .padding(theme.space3)
                    .fill(FillStyle::solid(Color::controlBackground()))
                    .cornerRadius(CornerRadius {theme.radiusMedium})
            )
        } //
            .padding(theme.space3)
            .fill(FillStyle::solid(Color::windowBackground()))
            .cornerRadius(CornerRadius {theme.radiusMedium})
    );
}

Element makeZStackDemo(Theme const &theme) {
    return makeSectionCard(
        theme, "ZStack",
        "Children share the same space. This is useful for overlays, badges, and stacked decoration.",
        ZStack {
            .horizontalAlignment = Alignment::Center,
            .verticalAlignment = Alignment::Center,
            .children = children(
                Rectangle {}
                    .size(0.f, 180.f)
                    .fill(FillStyle::solid(Color::selectedContentBackground()))
                    .cornerRadius(CornerRadius {theme.radiusLarge}),
                Rectangle {}
                    .size(220.f, 104.f)
                    .fill(FillStyle::solid(Color::accent()))
                    .cornerRadius(CornerRadius {theme.radiusLarge}),
                VStack {
                    .spacing = theme.space1,
                    .alignment = Alignment::Center,
                    .justifyContent = JustifyContent::Center,
                    .children = children(
                        Text {
                            .text = "Overlay content",
                            .font = Font::title2(),
                            .color = Color::accentForeground(),
                            .horizontalAlignment = HorizontalAlignment::Center,
                        },
                        Text {
                            .text = "Centered inside a shared layer",
                            .font = Font::footnote(),
                            .color = Color::accentForeground(),
                            .horizontalAlignment = HorizontalAlignment::Center,
                        }
                    )
                }
            )
        }
            .padding(theme.space3)
            .fill(FillStyle::solid(Color::windowBackground()))
            .cornerRadius(CornerRadius {theme.radiusMedium})
    );
}

Element makeGridDemo(Theme const &theme) {
    std::vector<Element> cells;
    cells.reserve(8);

    std::vector<Color> palette = {
        Color::accent(),
        Color::success(),
        Color::warning(),
        Color::danger(),
        Color::selectedContentBackground(),
        Color::successBackground(),
        Color::warningBackground(),
        theme.hoveredControlBackgroundColor,
    };

    for (int i = 0; i < 8; ++i) {
        std::ostringstream title;
        title << "Cell " << (i + 1);
        cells.push_back(
            VStack {
                .spacing = theme.space1,
                .alignment = Alignment::Start,
                .children = children(
                    Text {
                        .text = title.str(),
                        .font = Font::caption(),
                        .color = i < 4 ? Color::accentForeground() : Color::primary(),
                        .horizontalAlignment = HorizontalAlignment::Leading,
                    },
                    Rectangle {}
                        .size(24.f + static_cast<float>((i % 3) * 18), 18.f + static_cast<float>((i % 2) * 12))
                        .fill(FillStyle::solid(i < 4 ? Color::accentForeground() : Color::secondary()))
                        .cornerRadius(CornerRadius {theme.radiusSmall})
                )
            } //
                .padding(theme.space3)
                .fill(FillStyle::solid(palette[static_cast<std::size_t>(i)]))
                .cornerRadius(CornerRadius {theme.radiusMedium})
        );
    }

    std::vector<Element> spanCells;
    spanCells.reserve(5);
    spanCells.push_back(
        HStack {
            .spacing = theme.space3,
            .alignment = Alignment::Center,
            .children = children(
                VStack {
                    .spacing = theme.space1,
                    .alignment = Alignment::Start,
                    .children = children(
                        Text {
                            .text = "Span 3",
                            .font = Font::headline(),
                            .color = Color::accentForeground(),
                            .horizontalAlignment = HorizontalAlignment::Leading,
                        },
                        Text {
                            .text = "A full-width cell can establish rhythm before smaller rows continue.",
                            .font = Font::caption(),
                            .color = Color::accentForeground(),
                            .horizontalAlignment = HorizontalAlignment::Leading,
                            .wrapping = TextWrapping::Wrap,
                        }
                    )
                }
                    .flex(1.f, 1.f, 0.f),
                colorBlock(Color::accentForeground(), 28.f, 28.f, theme.radiusSmall)
            )
        }
            .padding(theme.space3)
            .fill(FillStyle::solid(Color::accent()))
            .cornerRadius(CornerRadius {theme.radiusMedium})
            .colSpan(3u)
    );
    spanCells.push_back(
        VStack {
            .spacing = theme.space2,
            .alignment = Alignment::Start,
            .children = children(
                Text {
                    .text = "Rows 2",
                    .font = Font::headline(),
                    .color = Color::primary(),
                    .horizontalAlignment = HorizontalAlignment::Leading,
                },
                Text {
                    .text = "This card reserves two row tracks, so later cells flow around it.",
                    .font = Font::caption(),
                    .color = Color::secondary(),
                    .horizontalAlignment = HorizontalAlignment::Leading,
                    .wrapping = TextWrapping::Wrap,
                },
                Spacer {},
                colorBlock(Color::secondary(), 26.f, 52.f, theme.radiusSmall)
            )
        }
            .padding(theme.space3)
            .fill(FillStyle::solid(Color::controlBackground()))
            .cornerRadius(CornerRadius {theme.radiusMedium})
            .rowSpan(2u)
    );
    spanCells.push_back(
        VStack {
            .spacing = theme.space1,
            .alignment = Alignment::Start,
            .children = children(
                Text {
                    .text = "Span 2",
                    .font = Font::headline(),
                    .color = Color::primary(),
                    .horizontalAlignment = HorizontalAlignment::Leading,
                },
                Text {
                    .text = "Wider cells make detail panels and summaries easier to read.",
                    .font = Font::caption(),
                    .color = Color::secondary(),
                    .horizontalAlignment = HorizontalAlignment::Leading,
                    .wrapping = TextWrapping::Wrap,
                }
            )
        }
            .padding(theme.space3)
            .fill(FillStyle::solid(Color::successBackground()))
            .cornerRadius(CornerRadius {theme.radiusMedium})
            .colSpan(2u)
    );
    spanCells.push_back(
        VStack {
            .spacing = theme.space1,
            .alignment = Alignment::Start,
            .children = children(
                Text {
                    .text = "Span 1",
                    .font = Font::caption(),
                    .color = Color::primary(),
                    .horizontalAlignment = HorizontalAlignment::Leading,
                },
                colorBlock(Color::warning(), 20.f, 20.f, theme.radiusSmall)
            )
        }
            .padding(theme.space3)
            .fill(FillStyle::solid(Color::warningBackground()))
            .cornerRadius(CornerRadius {theme.radiusMedium})
    );
    spanCells.push_back(
        VStack {
            .spacing = theme.space1,
            .alignment = Alignment::Start,
            .children = children(
                Text {
                    .text = "Span 1",
                    .font = Font::caption(),
                    .color = Color::primary(),
                    .horizontalAlignment = HorizontalAlignment::Leading,
                },
                colorBlock(Color::danger(), 20.f, 20.f, theme.radiusSmall)
            )
        }
            .padding(theme.space3)
            .fill(FillStyle::solid(Color::dangerBackground()))
            .cornerRadius(CornerRadius {theme.radiusMedium})
    );

    return makeSectionCard(
        theme, "Grid",
        "Fixed columns place children row-by-row. Mixed intrinsic sizes stay aligned inside each cell, and column or row spans let specific items stretch across multiple tracks.",
        VStack {
            .spacing = theme.space3,
            .alignment = Alignment::Stretch,
            .children = children(
                Grid {
                    .columns = 3,
                    .horizontalSpacing = theme.space3,
                    .verticalSpacing = theme.space3,
                    .horizontalAlignment = Alignment::Center,
                    .verticalAlignment = Alignment::Center,
                    .children = std::move(cells),
                },
                Grid {
                    .columns = 3,
                    .horizontalSpacing = theme.space3,
                    .verticalSpacing = theme.space3,
                    .horizontalAlignment = Alignment::Start,
                    .verticalAlignment = Alignment::Stretch,
                    .children = std::move(spanCells),
                }
            )
        }
            .padding(theme.space3)
            .fill(FillStyle::solid(Color::windowBackground()))
            .cornerRadius(CornerRadius {theme.radiusMedium})
    );
}

Element makeMixedCompositionDemo(Theme const &theme) {
    std::vector<Element> rows;
    rows.reserve(4);

    for (int i = 0; i < 4; ++i) {
        std::ostringstream label;
        label << "Track " << (i + 1);
        rows.push_back(HStack {
            .spacing = theme.space3,
            .alignment = Alignment::Center,
            .children = children(
                Text {
                    .text = label.str(),
                    .font = Font::headline(),
                    .color = Color::primary(),
                    .horizontalAlignment = HorizontalAlignment::Leading,
                }
                    .width(72.f),
                Rectangle {}
                    .height(14.f)
                    .fill(FillStyle::solid(i % 2 == 0 ? Color::selectedContentBackground() : Color::successBackground()))
                    .cornerRadius(CornerRadius {theme.radiusSmall})
                    .flex(1.f, 1.f, 0.f),
                Text {
                    .text = i % 2 == 0 ? "auto" : "manual",
                    .font = Font::caption(),
                    .color = Color::secondary(),
                    .horizontalAlignment = HorizontalAlignment::Trailing,
                }
            )
        } //
                           .padding(theme.space2)
                           .fill(FillStyle::solid(Color::controlBackground()))
                           .cornerRadius(CornerRadius {theme.radiusMedium}));
    }

    return makeSectionCard(
        theme, "Composed Layout",
        "Real views usually mix stacks together. This section shows a common label-track-value row pattern.",
        VStack {
            .spacing = theme.space2,
            .children = std::move(rows)
        } //
            .padding(theme.space3)
            .fill(FillStyle::solid(Color::windowBackground()))
            .cornerRadius(CornerRadius {theme.radiusMedium})
    );
}

struct JustifyPlaygroundSection {
    Element body() const {
        auto theme = useEnvironment<ThemeKey>();
        Signal<int> const axisIndex = useState<int>(0);
        Signal<int> const alignmentIndex = useState<int>(1);
        Signal<int> const justifyIndex = useState<int>(4);

        std::vector<SwitchCase<int>> previewCases;
        previewCases.reserve(48);
        for (int axis = 0; axis < 2; ++axis) {
            for (int alignment = 0; alignment < 4; ++alignment) {
                for (int justify = 0; justify < 6; ++justify) {
                    previewCases.push_back(Case(
                        justifyPreviewKey(axis, alignment, justify),
                        [theme, axis, alignment, justify] {
                            Alignment const resolvedAlignment = stackAlignmentFromIndex(alignment);
                            JustifyContent const resolvedJustify = justifyContentFromIndex(justify);
                            return axis == 0
                                       ? makeHStackJustifyPlaygroundPreview(theme(), resolvedAlignment, resolvedJustify)
                                       : makeVStackJustifyPlaygroundPreview(theme(), resolvedAlignment, resolvedJustify);
                        }));
                }
            }
        }

        Element preview = Element {Switch(
            [axisIndex, alignmentIndex, justifyIndex] {
                return justifyPreviewKey(axisIndex(), alignmentIndex(), justifyIndex());
            },
            std::move(previewCases))};

        return makeSectionCard(
            theme(), "Justify Content",
            "Use the selects to switch between HStack and VStack, cross-axis alignment, and flexbox-like justify-content behavior in one preview.",
            VStack {
                .spacing = theme().space3,
                .alignment = Alignment::Stretch,
                .children = children(
                    makeJustifyControl(theme(), "Axis", axisIndex, stackAxisOptions()),
                    makeJustifyControl(theme(), "Alignment", alignmentIndex, stackAlignmentOptions()),
                    makeJustifyControl(theme(), "Justify", justifyIndex, justifyContentOptions()),
                    VStack {
                        .spacing = theme().space2,
                        .alignment = Alignment::Stretch,
                        .children = children(
                            Text {
                                .text = [axisIndex, alignmentIndex, justifyIndex] {
                                    return stackAxisLabel(axisIndex()) + " using " +
                                           stackAlignmentLabel(alignmentIndex()) +
                                           " alignment and " + justifyContentLabel(justifyIndex()) +
                                           " distribution.";
                                },
                                .font = Font::footnote(),
                                .color = Color::secondary(),
                                .horizontalAlignment = HorizontalAlignment::Leading,
                                .wrapping = TextWrapping::Wrap,
                            },
                            std::move(preview)
                        )
                    }
                        .padding(theme().space3)
                        .fill(FillStyle::solid(Color::windowBackground()))
                        .cornerRadius(CornerRadius {theme().radiusMedium})
                )
            }
        );
    }
};

Element makeBasisChip(Theme const &theme, std::string text, Color fill, Color foreground) {
    return Text {
        .text = std::move(text),
        .font = Font::headline(),
        .color = foreground,
        .horizontalAlignment = HorizontalAlignment::Leading,
    }
        .padding(theme.space2, theme.space3, theme.space2, theme.space3)
        .fill(FillStyle::solid(fill))
        .cornerRadius(CornerRadius {theme.radiusMedium});
}

Element makeFlexBasisLane(Theme const &theme, std::string label, std::string caption,
                          std::function<Element(Element)> applyLeft,
                          std::function<Element(Element)> applyRight) {
    return VStack {
        .spacing = theme.space2,
        .alignment = Alignment::Stretch,
        .children = children(
            Text {
                .text = std::move(label),
                .font = Font::headline(),
                .color = Color::primary(),
                .horizontalAlignment = HorizontalAlignment::Leading,
            },
            Text {
                .text = std::move(caption),
                .font = Font::caption(),
                .color = Color::secondary(),
                .horizontalAlignment = HorizontalAlignment::Leading,
                .wrapping = TextWrapping::Wrap,
            },
            HStack {
                .spacing = theme.space2,
                .alignment = Alignment::Stretch,
                .children = children(
                    applyLeft(makeBasisChip(theme, "Short", Color::accent(), Color::accentForeground())),
                    applyRight(makeBasisChip(theme, "A much wider content block", Color::success(), Color::accentForeground()))
                ),
            }
                .size(0.f, 56.f)
                .padding(theme.space2)
                .fill(FillStyle::solid(Color::controlBackground()))
                .cornerRadius(CornerRadius {theme.radiusMedium})
        )
    };
}

Element makeFlexBasisDemo(Theme const &theme) {
    return makeSectionCard(
        theme, "Flex Basis",
        "Equal grow factors can either preserve intrinsic size or ignore it. `flex(1)` and `flex(1, 1)` both use an auto basis, while `flex(..., 0)` starts from zero for equal columns.",
        VStack {
            .spacing = theme.space3,
            .alignment = Alignment::Stretch,
            .children = children(
                makeFlexBasisLane(
                    theme, "flex(1, 1)",
                    "Auto basis keeps the wider item wider, then distributes the remaining width equally.",
                    [](Element chip) { return std::move(chip).flex(1.f, 1.f); },
                    [](Element chip) { return std::move(chip).flex(1.f, 1.f); }
                ),
                makeFlexBasisLane(
                    theme, "flex(1)",
                    "The shorthand uses grow 1, shrink 1, and the same auto basis, so it matches the relative sizing above.",
                    [](Element chip) { return std::move(chip).flex(1.f); },
                    [](Element chip) { return std::move(chip).flex(1.f); }
                ),
                makeFlexBasisLane(
                    theme, "flex(1, 1, 0)",
                    "Zero basis ignores intrinsic width first, so equal grow factors produce equal columns.",
                    [](Element chip) { return std::move(chip).flex(1.f, 1.f, 0.f); },
                    [](Element chip) { return std::move(chip).flex(1.f, 1.f, 0.f); }
                )
            )
        }
            .padding(theme.space3)
            .fill(FillStyle::solid(Color::windowBackground()))
            .cornerRadius(CornerRadius {theme.radiusMedium})
    );
}

} // namespace

struct StackDemoRoot {
    Element body() const {
        auto theme = useEnvironment<ThemeKey>();

        return ScrollView {
            .axis = ScrollAxis::Vertical,
            .children = children(
                VStack {
                    .spacing = theme().space4,
                    .alignment = Alignment::Start,
                    .children = children(
                        Text {
                            .text = "Layout Demo",
                            .font = Font::largeTitle(),
                            .color = Color::primary(),
                            .horizontalAlignment = HorizontalAlignment::Leading,
                        },
                        Text {
                            .text =
                                "Focused examples for stacks, grids, an interactive justify-content playground, flex-basis behavior, and how they compose in practice.",
                            .font = Font::body(),
                            .color = Color::secondary(),
                            .horizontalAlignment = HorizontalAlignment::Leading,
                            .wrapping = TextWrapping::Wrap,
                        },
                        makeVStackDemo(theme()),
                        makeHStackDemo(theme()),
                        makeZStackDemo(theme()),
                        makeGridDemo(theme()),
                        makeMixedCompositionDemo(theme()),
                        Element {JustifyPlaygroundSection {}},
                        makeFlexBasisDemo(theme())
                    )
                } //
                    .padding(theme().space5)
            )
        } //
            .fill(FillStyle::solid(Color::windowBackground()));
    }
};

int main(int argc, char *argv[]) {
    Application app(argc, argv);

    auto &w = app.createWindow({
        .size = {800, 800},
        .title = "Lambda - Layout demo",
    });

    w.setView<StackDemoRoot>();

    return app.exec();
}
