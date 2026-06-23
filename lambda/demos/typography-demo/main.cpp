#include <Lambda.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/UI.hpp>
#include <Lambda/UI/Views/Card.hpp>
#include <Lambda/UI/Views/Grid.hpp>
#include <Lambda/UI/Views/HStack.hpp>
#include <Lambda/UI/Views/Rectangle.hpp>
#include <Lambda/UI/Views/ScrollView.hpp>
#include <Lambda/UI/Views/TableView.hpp>
#include <Lambda/UI/Views/Text.hpp>
#include <Lambda/UI/Views/VStack.hpp>

#include <string>

using namespace lambdaui;

namespace {

Element dot(Color color) {
    return Rectangle {}
        .size(10.f, 10.f)
        .fill(color)
        .cornerRadius(999.f);
}

Element sectionCard(Theme const &theme, std::string eyebrow, std::string title, std::string body, Element content) {
    return Card {
        .child = VStack {
            .spacing = theme.space3,
            .alignment = Alignment::Start,
            .children = children(
                Text {
                    .text = std::move(eyebrow),
                    .font = Font::caption(),
                    .color = Color::accent(),
                },
                Text {
                    .text = std::move(title),
                    .font = Font::title2(),
                    .color = Color::primary(),
                },
                Text {
                    .text = std::move(body),
                    .font = Font::body(),
                    .color = Color::secondary(),
                    .wrapping = TextWrapping::Wrap,
                },
                std::move(content)
            )
        },
    };
}

Element typeRow(Theme const &theme, std::string tokenName, Font token, std::string note) {
    std::vector<Element> cells;
    cells.push_back(
        TableCell {
            .content = VStack {
                .spacing = theme.space1,
                .alignment = Alignment::Start,
                .children = children(
                    Text {
                        .text = std::move(tokenName),
                        .font = Font::monospacedBody(),
                        .color = Color::tertiary(),
                    },
                    Text {
                        .text = std::move(note),
                        .font = Font::caption(),
                        .color = Color::secondary(),
                        .wrapping = TextWrapping::Wrap,
                    })
            },
            .style = TableCell::Style {
                .width = 210.f,
                .alignment = HorizontalAlignment::Leading,
            },
        });
    cells.push_back(
        TableCell {
            .content = Text {
                .text = "The quick brown fox jumps over the lazy dog.",
                .font = token,
                .color = Color::primary(),
                .wrapping = TextWrapping::Wrap,
            },
            .style = TableCell::Style {
                .alignment = HorizontalAlignment::Leading,
            },
        });

    return TableRow {
        .cells = std::move(cells),
        .style = TableRow::Style {
            .paddingH = 0.f,
            .paddingV = theme.space3,
            .spacing = theme.space3,
            .backgroundColor = Colors::transparent,
            .hoverBackgroundColor = Colors::transparent,
            .selectedBackgroundColor = Colors::transparent,
        },
    };
}

Element swatchTile(Theme const &theme, std::string name, Color swatch, std::string note, Color sampleText = Color::primary()) {
    return VStack {
        .spacing = theme.space2,
        .alignment = Alignment::Start,
        .children = children(
            Rectangle {}
                .height(48.f)
                .fill(swatch)
                .stroke(Color::separator(), 1.f)
                .cornerRadius(theme.radiusMedium),
            Text {
                .text = std::move(name),
                .font = Font::headline(),
                .color = sampleText,
            },
            Text {
                .text = std::move(note),
                .font = Font::footnote(),
                .color = Color::secondary(),
                .wrapping = TextWrapping::Wrap,
            }
        )
    }
        .flex(1.f);
}

Element swatchGrid(Theme const &theme, std::vector<Element> tiles) {
    return Grid {
        .columns = 4,
        .horizontalSpacing = theme.space3,
        .verticalSpacing = theme.space3,
        .horizontalAlignment = Alignment::Start,
        .verticalAlignment = Alignment::Start,
        .children = std::move(tiles),
    };
}

Element previewWindow(Theme previewTheme, std::string name, std::string note) {
    auto theme = useEnvironment<ThemeKey>();

    Element content = VStack {
        .spacing = theme().space3,
        .alignment = Alignment::Start,
        .children = children(
            HStack {
                .spacing = theme().space2,
                .alignment = Alignment::Center,
                .children = children(
                    dot(Color::danger()),
                    dot(Color::warning()),
                    dot(Color::success()),
                    Text {
                        .text = std::move(name),
                        .font = Font::headline(),
                        .color = Color::secondary(),
                    }
                )
            },
            VStack {
                .spacing = theme().space1,
                .alignment = Alignment::Start,
                .children = children(
                    Text {
                        .text = "Semantic Theme",
                        .font = Font::title3(),
                        .color = Color::primary(),
                    },
                    Text {
                        .text = std::move(note),
                        .font = Font::body(),
                        .color = Color::secondary(),
                        .wrapping = TextWrapping::Wrap,
                    }
                )
            },
            VStack {
                .spacing = theme().space2,
                .alignment = Alignment::Start,
                .children = children(
                    Text {
                        .text = "Selected row",
                        .font = Font::headline(),
                        .color = Color::primary(),
                    }
                        .padding(theme().space3)
                        .fill(Color::selectedContentBackground())
                        .cornerRadius(theme().radiusMedium),
                    Text {
                        .text = "Placeholder",
                        .font = Font::body(),
                        .color = Color::placeholder(),
                    }
                        .padding(theme().space3)
                        .fill(Color::textBackground())
                        .stroke(Color::opaqueSeparator(), 1.f)
                        .cornerRadius(theme().radiusMedium),
                    HStack {
                        .spacing = theme().space2,
                        .alignment = Alignment::Center,
                        .children = children(
                            Text {
                                .text = "Continue",
                                .font = Font::headline(),
                                .color = Color::accentForeground(),
                            }
                                .padding(theme().space3)
                                .fill(Color::accent())
                                .cornerRadius(theme().radiusMedium),
                            Text {
                                .text = "Sync complete",
                                .font = Font::footnote(),
                                .color = Color::success(),
                            }
                        )
                    }
                )
            }
        )
    }
        .padding(theme().space4)
        .fill(Color::controlBackground())
        .stroke(Color::separator(), 1.f)
        .cornerRadius(theme().radiusXLarge)
        .environment<ThemeKey>(previewTheme);

    return std::move(content).flex(1.f);
}

} // namespace

struct TypographyDemoRoot {
    auto body() const {
        auto theme = useEnvironment<ThemeKey>();

        Element previews = HStack {
            .spacing = theme().space3,
            .alignment = Alignment::Start,
            .children = children(
                previewWindow(Theme::light(), "Light Appearance", "Primary and secondary content should feel calm and legible."),
                previewWindow(Theme::dark(), "Dark Appearance", "The same semantic tokens should keep hierarchy intact at night.")
            )
        };

        std::vector<Element> typeRows;
        typeRows.push_back(typeRow(theme(), "Font::largeTitle()", Font::largeTitle(), "Screen headline"));
        typeRows.push_back(typeRow(theme(), "Font::title()", Font::title(), "Primary section title"));
        typeRows.push_back(typeRow(theme(), "Font::title2()", Font::title2(), "Panel or card title"));
        typeRows.push_back(typeRow(theme(), "Font::title3()", Font::title3(), "Subsection title"));
        typeRows.push_back(typeRow(theme(), "Font::headline()", Font::headline(), "Control label or emphasized row"));
        typeRows.push_back(typeRow(theme(), "Font::subheadline()", Font::subheadline(), "Supporting hierarchy"));
        typeRows.push_back(typeRow(theme(), "Font::body()", Font::body(), "Default reading text"));
        typeRows.push_back(typeRow(theme(), "Font::callout()", Font::callout(), "Compact callout copy"));
        typeRows.push_back(typeRow(theme(), "Font::footnote()", Font::footnote(), "Metadata and support text"));
        typeRows.push_back(typeRow(theme(), "Font::caption()", Font::caption(), "Dense UI label"));
        typeRows.push_back(typeRow(theme(), "Font::caption2()", Font::caption2(), "Tight caption"));
        typeRows.push_back(typeRow(theme(), "Font::monospacedBody()", Font::monospacedBody(), "system.token = semantic"));

        Element typeHeader = TableRow {
            .cells = {
                Element {TableCell {
                    .content = Text {
                        .text = "Token",
                        .font = Font::headline(),
                        .color = Color::tertiary(),
                    },
                    .style = TableCell::Style {
                        .width = 210.f,
                        .alignment = HorizontalAlignment::Leading,
                    },
                }},
                Element {TableCell {
                    .content = Text {
                        .text = "Rendered sample",
                        .font = Font::headline(),
                        .color = Color::tertiary(),
                    },
                }},
            },
            .style = TableRow::Style {
                .paddingH = 0.f,
                .paddingV = theme().space2,
                .spacing = theme().space3,
                .backgroundColor = theme().controlBackgroundColor,
                .hoverBackgroundColor = theme().controlBackgroundColor,
                .selectedBackgroundColor = theme().controlBackgroundColor,
            },
        };

        Element scaleSection = sectionCard(
            theme(), "Typography", "Apple-style text roles",
            "These samples render exclusively through semantic `Font::...` tokens. The theme decides the concrete face, size, and weight later.",
            TableView {
                .header = std::move(typeHeader),
                .rows = std::move(typeRows),
                .columns = {
                    TableColumn {.width = 210.f},
                    TableColumn {.flexGrow = 1.f},
                },
                .scrollBody = false,
                .style = TableView::Style {
                    .dividerInsetH = 0.f,
                    .backgroundColor = theme().controlBackgroundColor,
                    .dividerColor = theme().separatorColor,
                },
            }
        );

        Element textColors = sectionCard(
            theme(), "Color", "Semantic text colors",
            "These follow the macOS model: content asks for meaning like primary or placeholder, not a concrete hex value.",
            swatchGrid(
                theme(),
                children(
                    swatchTile(theme(), "Color::primary()", Color::primary(), "Main reading ink."),
                    swatchTile(theme(), "Color::secondary()", Color::secondary(), "Supporting descriptions.", Color::secondary()),
                    swatchTile(theme(), "Color::tertiary()", Color::tertiary(), "Metadata and quiet detail.", Color::tertiary()),
                    swatchTile(theme(), "Color::placeholder()", Color::placeholder(), "Transient hints before input.", Color::placeholder())
                )
            )
        );

        Element surfaces = sectionCard(
            theme(), "Surfaces", "Background and separation",
            "Window, control, elevated, and text surfaces stay distinct without hard-coding different palettes in each component.",
            swatchGrid(
                theme(),
                children(
                    swatchTile(theme(), "Color::windowBackground()", Color::windowBackground(), "Canvas and app backdrop."),
                    swatchTile(theme(), "Color::controlBackground()", Color::controlBackground(), "Cards and panels."),
                    swatchTile(theme(), "Color::elevatedBackground()", Color::elevatedBackground(), "Raised surfaces like sheets."),
                    swatchTile(theme(), "Color::textBackground()", Color::textBackground(), "Fields and editable regions.")
                )
            )
        );

        Element states = sectionCard(
            theme(), "States", "Accent, focus, and feedback",
            "Interactive and status colors stay semantic too, including fills that now resolve late in the scene graph.",
            swatchGrid(
                theme(),
                children(
                    swatchTile(theme(), "Color::accent()", Color::accent(), "Primary interaction tint.", Color::accent()),
                    swatchTile(theme(), "Color::selectedContentBackground()", Color::selectedContentBackground(), "Selected rows and ranges."),
                    swatchTile(theme(), "Color::focusRing()", Color::focusRing(), "Keyboard focus affordance.", Color::focusRing()),
                    swatchTile(theme(), "Color::separator()", Color::separator(), "Subtle chrome boundaries.", Color::secondary()),
                    swatchTile(theme(), "Color::success()", Color::success(), "Positive confirmation.", Color::success()),
                    swatchTile(theme(), "Color::warning()", Color::warning(), "Caution without failure.", Color::warning()),
                    swatchTile(theme(), "Color::danger()", Color::danger(), "Destructive or failed state.", Color::danger()),
                    swatchTile(theme(), "Color::scrim()", Color::scrim(), "Modal backdrop tone.", Color::secondary())
                )
            )
        );

        Element content = VStack {
            .spacing = theme().space5,
            .alignment = Alignment::Start,
            .children = children(
                Text {
                    .text = "Semantic Theme Tokens",
                    .font = Font::largeTitle(),
                    .color = Color::primary(),
                },
                Text {
                    .text = "A macOS-inspired first pass: late-resolved colors and fonts, one vocabulary for light and dark, and a demo that renders only through `Color::...` and `Font::...`.",
                    .font = Font::body(),
                    .color = Color::secondary(),
                    .wrapping = TextWrapping::Wrap,
                },
                std::move(previews),
                std::move(scaleSection),
                std::move(textColors),
                std::move(surfaces),
                std::move(states)
            )
        }
            .padding(theme().space5);

        return ScrollView {
            .axis = ScrollAxis::Vertical,
            .children = children(std::move(content)),
        }
            .fill(Color::windowBackground());
    }
};

int main(int argc, char *argv[]) {
    Application app(argc, argv);

    auto &w = app.createWindow<Window>({
        .size = {1180, 960},
        .title = "Lambda — Semantic Theme Demo",
        .resizable = true,
    });

    w.setView<TypographyDemoRoot>();
    return app.exec();
}
