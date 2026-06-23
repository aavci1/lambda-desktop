#include <Lambda.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/UI.hpp>
#include <Lambda/UI/Views/Card.hpp>
#include <Lambda/UI/Views/HStack.hpp>
#include <Lambda/UI/Views/Rectangle.hpp>
#include <Lambda/UI/Views/ScrollView.hpp>
#include <Lambda/UI/Views/Text.hpp>
#include <Lambda/UI/Views/VStack.hpp>

#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace lambdaui;

namespace {

Element makeSectionCard(Theme const& theme, std::string title, std::string caption, Element content) {
    return Card{
        .child = VStack{
            .spacing = theme.space3,
            .alignment = Alignment::Stretch,
            .children = children(
                Text{
                    .text = std::move(title),
                    .font = Font::title2(),
                    .color = Color::primary(),
                    .horizontalAlignment = HorizontalAlignment::Leading,
                },
                Text{
                    .text = std::move(caption),
                    .font = Font::footnote(),
                    .color = Color::secondary(),
                    .horizontalAlignment = HorizontalAlignment::Leading,
                    .wrapping = TextWrapping::Wrap,
                },
                std::move(content)
            ),
        },
        .style = Card::Style{
            .padding = theme.space4,
            .cornerRadius = theme.radiusLarge,
        },
    };
}

Element makeVerticalRow(Theme const& theme, int index) {
    std::string const rowKey = "vertical-row-" + std::to_string(index);
    std::ostringstream title;
    title << "Activity row " << index;

    std::ostringstream body;
    body << "Vertical scrolling should only show the trailing indicator even "
         << "if this line stretches a bit wider than the viewport.";

    Color const markerColor = index % 3 == 0 ? Color::accent()
                            : index % 3 == 1 ? Color::success()
                                             : Color::warning();

    return HStack{
        .spacing = theme.space3,
        .alignment = Alignment::Stretch,
        .children = children(
            Rectangle{}
                .width(8.f)
                .fill(FillStyle::solid(markerColor))
                .cornerRadius(CornerRadius{theme.radiusFull})
                .key(rowKey + "-marker"),
            VStack{
                .spacing = theme.space1,
                .alignment = Alignment::Stretch,
                .children = children(
                    Text{
                        .text = title.str(),
                        .font = Font::headline(),
                        .color = Color::primary(),
                        .horizontalAlignment = HorizontalAlignment::Leading,
                    }
                        .key(rowKey + "-title"),
                    Text{
                        .text = body.str(),
                        .font = Font::footnote(),
                        .color = Color::secondary(),
                        .horizontalAlignment = HorizontalAlignment::Leading,
                        .wrapping = TextWrapping::Wrap,
                    }
                        .key(rowKey + "-body")
                ),
            }
                .flex(1.f, 1.f, 0.f)
                .key(rowKey + "-content")
        ),
    }
        .padding(theme.space3)
        .fill(FillStyle::solid(Color::controlBackground()))
        .stroke(index % 4 == 0 ? StrokeStyle::solid(Color::separator(), 1.f) : StrokeStyle::none())
        .cornerRadius(CornerRadius{theme.radiusMedium})
        .key(std::move(rowKey));
}

Element makeVerticalDemo(Theme const& theme) {
    std::vector<Element> rows;
    rows.reserve(24);

    for (int i = 1; i <= 24; ++i) {
        rows.push_back(makeVerticalRow(theme, i));
    }

    return makeSectionCard(
        theme,
        "Vertical",
        "Wheel, trackpad, or drag to scroll. Horizontal overflow should stay quiet here.",
        ScrollView{
            .axis = ScrollAxis::Vertical,
            .children = children(
                VStack{
                    .spacing = theme.space2,
                    .alignment = Alignment::Stretch,
                    .children = std::move(rows),
                }
                    .padding(theme.space3)
            ),
        }
            .height(220.f)
            .fill(FillStyle::solid(Color::windowBackground()))
            .cornerRadius(CornerRadius{theme.radiusMedium})
    );
}

Element makeHorizontalCard(Theme const& theme, int index, Color fillColor, Color foregroundColor) {
    std::ostringstream title;
    title << "Lane " << index;

    std::ostringstream body;
    body << "Horizontal content card " << index << " with enough width to force scrolling.";

    return VStack{
        .spacing = theme.space2,
        .alignment = Alignment::Stretch,
        .children = children(
            Text{
                .text = title.str(),
                .font = Font::headline(),
                .color = foregroundColor,
                .horizontalAlignment = HorizontalAlignment::Leading,
            },
            Text{
                .text = body.str(),
                .font = Font::footnote(),
                .color = foregroundColor,
                .horizontalAlignment = HorizontalAlignment::Leading,
                .wrapping = TextWrapping::Wrap,
            }
        ),
    }
        .padding(theme.space4)
        .width(220.f)
        .fill(FillStyle::solid(fillColor))
        .cornerRadius(CornerRadius{theme.radiusLarge})
        .key("horizontal-card-" + std::to_string(index));
}

Element makeHorizontalDemo(Theme const& theme) {
    std::vector<Element> cards;
    cards.reserve(10);

    for (int i = 1; i <= 10; ++i) {
        if (i % 3 == 1) {
            cards.push_back(makeHorizontalCard(theme, i, Color::accent(), Color::accentForeground()));
        } else if (i % 3 == 2) {
            cards.push_back(makeHorizontalCard(theme, i, Color::success(), Color::successForeground()));
        } else {
            cards.push_back(makeHorizontalCard(theme, i, Color::warning(), Color::warningForeground()));
        }
    }

    return makeSectionCard(
        theme,
        "Horizontal",
        "A single row of fixed-width cards exercises the bottom indicator and horizontal dragging.",
        ScrollView{
            .axis = ScrollAxis::Horizontal,
            .children = children(
                HStack{
                    .spacing = theme.space3,
                    .children = std::move(cards),
                }
                    .padding(theme.space3)
            ),
        }
            .fill(FillStyle::solid(Color::windowBackground()))
            .cornerRadius(CornerRadius{theme.radiusMedium})
    );
}

Element makeMatrixCell(Theme const& theme, int row, int col) {
    std::ostringstream label;
    label << "Cell " << row << ":" << col;

    Color const previewColor = row % 2 == 0 ? Color::warningBackground() : Color::selectedContentBackground();

    return VStack{
        .spacing = theme.space1,
        .alignment = Alignment::Stretch,
        .children = children(
            Text{
                .text = label.str(),
                .font = Font::caption(),
                .color = Color::primary(),
                .horizontalAlignment = HorizontalAlignment::Leading,
            },
            Rectangle{}
                .size(140.f, 28.f + static_cast<float>(((row - 1) % 3) * 12))
                .cornerRadius(CornerRadius{theme.radiusSmall})
                .fill(FillStyle::solid(previewColor))
        ),
    }
        .padding(theme.space2)
        .fill(FillStyle::solid(Color::elevatedBackground()))
        .stroke(StrokeStyle::solid(Color::separator(), 1.f))
        .cornerRadius(CornerRadius{theme.radiusMedium})
        .key("matrix-cell-" + std::to_string(row) + "-" + std::to_string(col));
}

Element makeMatrixColumn(Theme const& theme, int index) {
    std::vector<Element> cells;
    cells.reserve(7);

    for (int row = 1; row <= 7; ++row) {
        cells.push_back(makeMatrixCell(theme, row, index));
    }

    std::ostringstream title;
    title << "Column " << index;

    return VStack{
        .spacing = theme.space2,
        .alignment = Alignment::Stretch,
        .children = children(
            Text{
                .text = title.str(),
                .font = Font::title3(),
                .color = Color::primary(),
                .horizontalAlignment = HorizontalAlignment::Leading,
            },
            VStack{
                .spacing = theme.space2,
                .alignment = Alignment::Stretch,
                .children = std::move(cells),
            }
        ),
    }
        .padding(theme.space3)
        .width(210.f)
        .fill(FillStyle::solid(Color::controlBackground()))
        .stroke(StrokeStyle::solid(Color::separator(), 1.f))
        .cornerRadius(CornerRadius{theme.radiusLarge})
        .key("matrix-column-" + std::to_string(index));
}

Element makeBothAxesDemo(Theme const& theme) {
    std::vector<Element> columns;
    columns.reserve(6);

    for (int col = 1; col <= 6; ++col) {
        columns.push_back(makeMatrixColumn(theme, col));
    }

    return makeSectionCard(
        theme,
        "Both Axes",
        "This oversized canvas keeps both indicators pinned to the viewport while the content moves underneath.",
        ScrollView{
            .axis = ScrollAxis::Both,
            .children = children(
                HStack{
                    .spacing = theme.space3,
                    .children = std::move(columns),
                }
                    .padding(theme.space3)
            ),
        }
            .height(260.f)
            .fill(FillStyle::solid(Color::windowBackground()))
            .cornerRadius(CornerRadius{theme.radiusMedium})
    );
}

} // namespace

struct ScrollDemoRoot {
    Element body() const {
        auto theme = useEnvironment<ThemeKey>();

        return ScrollView{
            .axis = ScrollAxis::Vertical,
            .children = children(
                VStack{
                    .spacing = theme().space4,
                    .alignment = Alignment::Stretch,
                    .children = children(
                        Text{
                            .text = "ScrollView Demo",
                            .font = Font::largeTitle(),
                            .color = Color::primary(),
                            .horizontalAlignment = HorizontalAlignment::Leading,
                        },
                        Text{
                            .text = "Three focused examples for vertical, horizontal, and two-axis scrolling with overlay indicators.",
                            .font = Font::body(),
                            .color = Color::secondary(),
                            .horizontalAlignment = HorizontalAlignment::Leading,
                            .wrapping = TextWrapping::Wrap,
                        },
                        makeVerticalDemo(theme()),
                        makeHorizontalDemo(theme()),
                        makeBothAxesDemo(theme())
                    ),
                }
                    .padding(theme().space5)
            ),
        }
            .fill(FillStyle::solid(Color::windowBackground()));
    }
};

int main(int argc, char* argv[]) {
    Application app(argc, argv);

    auto& w = app.createWindow({
        .size = {960, 920},
        .title = "Lambda - Scroll demo",
    });

    w.setView<ScrollDemoRoot>();

    return app.exec();
}
