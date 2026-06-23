#include <Lambda.hpp>
#include <Lambda/UI/Window.hpp>
#include <Lambda/UI/WindowUI.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/UI.hpp>
#include <Lambda/UI/Views/Badge.hpp>
#include <Lambda/UI/Views/Card.hpp>
#include <Lambda/UI/Views/HStack.hpp>
#include <Lambda/UI/Views/Icon.hpp>
#include <Lambda/UI/Views/ProgressBar.hpp>
#include <Lambda/UI/Views/Rectangle.hpp>
#include <Lambda/UI/Views/ScrollView.hpp>
#include <Lambda/UI/Views/Show.hpp>
#include <Lambda/UI/Views/TableView.hpp>
#include <Lambda/UI/Views/Text.hpp>
#include <Lambda/UI/Views/VStack.hpp>

#include <cstdio>
#include <string>
#include <vector>

using namespace lambdaui;

namespace {

struct Deal {
    std::string id;
    std::string name;
    std::string owner;
    std::string stage;
    float progress = 0.f;
    double value = 0.0;
    std::string nextStep;
    bool atRisk = false;
};

std::vector<Deal> sampleDeals() {
    return {
        {.id = "aurora", .name = "Aurora Systems", .owner = "Nina", .stage = "Qualified", .progress = 0.28f, .value = 42000.0, .nextStep = "Confirm migration scope and security review.", .atRisk = false},
        {.id = "atlas", .name = "Atlas Bio", .owner = "Marco", .stage = "Proposal", .progress = 0.63f, .value = 118000.0, .nextStep = "Send revised pricing after procurement feedback.", .atRisk = true},
        {.id = "northstar", .name = "Northstar Retail", .owner = "Lena", .stage = "Negotiation", .progress = 0.82f, .value = 86000.0, .nextStep = "Legal redlines due Friday.", .atRisk = false},
        {.id = "helios", .name = "Helios Energy", .owner = "Samir", .stage = "Pilot", .progress = 0.48f, .value = 57000.0, .nextStep = "Collect first-week usage metrics from the field team.", .atRisk = false},
        {.id = "fable", .name = "Fable Health", .owner = "Jo", .stage = "Renewal", .progress = 0.91f, .value = 132000.0, .nextStep = "Finalize expansion seats and sign renewal order form.", .atRisk = true},
        {.id = "quarry", .name = "Quarry Finance", .owner = "Ada", .stage = "Discovery", .progress = 0.14f, .value = 26000.0, .nextStep = "Schedule stakeholder workshop with compliance and ops.", .atRisk = false},
    };
}

std::string formatCurrency(double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "$%.0f", value);
    return buffer;
}

Element emptyCell() {
    return Rectangle {}
        .size(0.f, 0.f)
        .fill(Colors::transparent);
}

Color stageForeground(Theme const &theme, std::string const &stage) {
    if (stage == "Renewal") {
        return theme.successColor;
    }
    if (stage == "Negotiation") {
        return theme.accentColor;
    }
    if (stage == "Proposal") {
        return theme.warningColor;
    }
    return theme.secondaryLabelColor;
}

Color stageBackground(Theme const &theme, std::string const &stage) {
    if (stage == "Renewal") {
        return theme.successBackgroundColor;
    }
    if (stage == "Negotiation") {
        return theme.selectedContentBackgroundColor;
    }
    if (stage == "Proposal") {
        return theme.warningBackgroundColor;
    }
    return theme.hoveredControlBackgroundColor;
}

Element stageBadge(Theme const &theme, std::string stage) {
    return Badge {
        .label = std::move(stage),
        .style = Badge::Style {
            .font = Font::caption(),
            .foregroundColor = stageForeground(theme, stage),
            .backgroundColor = stageBackground(theme, stage),
        },
    };
}

struct DealTableRow : ViewModifiers<DealTableRow> {
    Deal deal;

    Element body() const {
        auto theme = useEnvironment<ThemeKey>();
        auto expanded = useState(false);

        std::vector<Element> cells;
        cells.push_back(
            TableCell {
                .content = HStack {
                    .spacing = theme().space2,
                    .alignment = Alignment::Center,
                    .children = children(
                        Icon {
                            .name = deal.atRisk ? IconName::Warning : IconName::Circle,
                            .size = 14.f,
                            .color = deal.atRisk ? theme().warningColor : theme().successColor,
                        },
                        Text {
                            .text = deal.name,
                            .font = Font::headline(),
                            .color = Color::primary(),
                            .horizontalAlignment = HorizontalAlignment::Leading,
                        })
                },
                .style = TableCell::Style {
                    .width = 240.f,
                    .alignment = HorizontalAlignment::Leading,
                },
            });
        cells.push_back(
            TableCell {
                .content = Text {
                    .text = deal.owner,
                    .font = Font::body(),
                    .color = Color::secondary(),
                },
                .style = TableCell::Style {
                    .width = 110.f,
                    .alignment = HorizontalAlignment::Leading,
                },
            });
        cells.push_back(
            TableCell {
                .content = stageBadge(theme(), deal.stage),
                .style = TableCell::Style {
                    .width = 120.f,
                    .alignment = HorizontalAlignment::Center,
                },
            });
        cells.push_back(
            TableCell {
                .content = VStack {
                    .spacing = theme().space1,
                    .alignment = Alignment::Stretch,
                    .children = children(
                        ProgressBar {
                            .progress = deal.progress,
                            .style = ProgressBar::Style {
                                .activeColor = deal.atRisk ? theme().warningColor : theme().accentColor,
                                .inactiveColor = theme().separatorColor,
                                .trackHeight = 6.f,
                            },
                        },
                        Text {
                            .text = std::to_string(static_cast<int>(deal.progress * 100.f)) + "%",
                            .font = Font::caption(),
                            .color = Color::tertiary(),
                            .horizontalAlignment = HorizontalAlignment::Trailing,
                        })
                },
                .style = TableCell::Style {
                    .width = 150.f,
                    .alignment = HorizontalAlignment::Leading,
                },
            });
        cells.push_back(
            TableCell {
                .content = Text {
                    .text = formatCurrency(deal.value),
                    .font = Font::monospacedBody(),
                    .color = Color::primary(),
                    .horizontalAlignment = HorizontalAlignment::Trailing,
                },
                .style = TableCell::Style {
                    .width = 110.f,
                    .alignment = HorizontalAlignment::Trailing,
                },
            });
        cells.push_back(
            TableCell {
                .content = Icon {
                    .name = [expanded] {
                        return expanded() ? IconName::ExpandLess : IconName::ExpandMore;
                    },
                    .size = 18.f,
                    .color = Color::tertiary(),
                },
                .style = TableCell::Style {
                    .width = 32.f,
                    .alignment = HorizontalAlignment::Center,
                },
            });

        Element detail = Element {Show(
            [expanded] {
                return expanded();
            },
            [deal = deal, theme] {
                return Text {
                    .text = "Next step: " + deal.nextStep,
                    .font = Font::footnote(),
                    .color = deal.atRisk ? theme().warningColor : Color::secondary(),
                    .horizontalAlignment = HorizontalAlignment::Leading,
                    .wrapping = TextWrapping::Wrap,
                }
                    .padding(theme().space3, theme().space4, theme().space3, theme().space4)
                    .fill(deal.atRisk ? theme().warningBackgroundColor : theme().selectedContentBackgroundColor);
            }
        )};

        return TableRow {
            .cells = std::move(cells),
            .detail = std::move(detail),
            .style = TableRow::Style {
                .paddingH = theme().space4,
                .paddingV = theme().space3,
                .spacing = theme().space3,
                .backgroundColor = theme().elevatedBackgroundColor,
                .hoverBackgroundColor = theme().hoveredControlBackgroundColor,
                .selectedBackgroundColor = theme().selectedContentBackgroundColor,
            },
            .onTap = [expanded] {
                expanded = !expanded();
            },
        };
    }
};

struct TableDemoView {
    Element body() const {
        auto theme = useEnvironment<ThemeKey>();
        std::vector<Deal> deals = sampleDeals();
        double totalValue = 0.0;
        float totalProgress = 0.f;
        int riskCount = 0;
        for (Deal const &deal : deals) {
            totalValue += deal.value;
            totalProgress += deal.progress;
            if (deal.atRisk) {
                ++riskCount;
            }
        }
        int const averageProgress = deals.empty() ? 0 : static_cast<int>((totalProgress / static_cast<float>(deals.size())) * 100.f);

        Element headerRow = TableRow {
            .cells = {
                Element {TableCell {.content = Text {.text = "Account", .font = Font::headline(), .color = Color::tertiary()}, .style = TableCell::Style {.width = 240.f}}},
                Element {TableCell {.content = Text {.text = "Owner", .font = Font::headline(), .color = Color::tertiary()}, .style = TableCell::Style {.width = 110.f}}},
                Element {TableCell {.content = Text {.text = "Stage", .font = Font::headline(), .color = Color::tertiary()}, .style = TableCell::Style {.width = 120.f, .alignment = HorizontalAlignment::Center}}},
                Element {TableCell {.content = Text {.text = "Progress", .font = Font::headline(), .color = Color::tertiary()}, .style = TableCell::Style {.width = 150.f}}},
                Element {TableCell {.content = Text {.text = "Value", .font = Font::headline(), .color = Color::tertiary()}, .style = TableCell::Style {.width = 110.f, .alignment = HorizontalAlignment::Trailing}}},
                Element {TableCell {.content = emptyCell(), .style = TableCell::Style {.width = 32.f, .alignment = HorizontalAlignment::Center}}},
            },
            .style = TableRow::Style {
                .paddingH = theme().space4,
                .paddingV = theme().space2,
                .spacing = theme().space3,
                .backgroundColor = theme().windowBackgroundColor,
                .hoverBackgroundColor = theme().windowBackgroundColor,
                .selectedBackgroundColor = theme().windowBackgroundColor,
            },
        };

        std::vector<TableView::Item> items;
        for (Deal const &deal : deals) {
            items.push_back(TableView::Item {
                .row = Element {DealTableRow {.deal = deal}}.key(deal.id),
                .sortValues = {
                    {0u, deal.name},
                    {3u, deal.progress},
                    {4u, deal},
                },
            });
        }

        std::vector<Element> rows;
        rows.push_back(
            TableRow {
                .cells = {
                    Element {TableCell {.content = Text {.text = "Pipeline total", .font = Font::headline(), .color = Color::secondary()}}},
                    Element {TableCell {.content = Text {.text = std::to_string(riskCount) + " at risk", .font = Font::footnote(), .color = riskCount > 0 ? theme().warningColor : theme().successColor}}},
                    Element {TableCell {.content = emptyCell()}},
                    Element {TableCell {.content = emptyCell()}},
                    Element {TableCell {.content = Text {.text = formatCurrency(totalValue), .font = Font::monospacedBody(), .color = Color::primary(), .horizontalAlignment = HorizontalAlignment::Trailing}, .style = TableCell::Style {.alignment = HorizontalAlignment::Trailing}}},
                    Element {TableCell {.content = Icon {.name = riskCount > 0 ? IconName::Warning : IconName::CheckCircle, .size = 16.f, .color = riskCount > 0 ? theme().warningColor : theme().successColor}, .style = TableCell::Style {.alignment = HorizontalAlignment::Center}}},
                },
                .style = TableRow::Style {
                    .paddingH = theme().space4,
                    .paddingV = theme().space2,
                    .spacing = theme().space3,
                    .backgroundColor = theme().windowBackgroundColor,
                    .hoverBackgroundColor = theme().windowBackgroundColor,
                    .selectedBackgroundColor = theme().windowBackgroundColor,
                },
            });

        Element compactHeader = TableRow {
            .cells = {
                Element {TableCell {.content = Text {.text = "Metric", .font = Font::headline(), .color = Color::tertiary()}, .style = TableCell::Style {.width = 200.f}}},
                Element {TableCell {.content = Text {.text = "Value", .font = Font::headline(), .color = Color::tertiary()}, .style = TableCell::Style {.width = 140.f, .alignment = HorizontalAlignment::Trailing}}},
            },
            .style = TableRow::Style {
                .paddingH = theme().space4,
                .paddingV = theme().space2,
                .spacing = theme().space3,
                .backgroundColor = theme().controlBackgroundColor,
                .hoverBackgroundColor = theme().controlBackgroundColor,
                .selectedBackgroundColor = theme().controlBackgroundColor,
            },
        };

        std::vector<Element> compactRows;
        compactRows.push_back(
            TableRow {
                .cells = {
                    Element {TableCell {.content = Text {.text = "Deals in pipeline", .font = Font::body(), .color = Color::secondary()}, .style = TableCell::Style {.width = 200.f}}},
                    Element {TableCell {.content = Text {.text = std::to_string(deals.size()), .font = Font::headline(), .color = Color::primary(), .horizontalAlignment = HorizontalAlignment::Trailing}, .style = TableCell::Style {.width = 140.f, .alignment = HorizontalAlignment::Trailing}}},
                },
                .style = TableRow::Style {
                    .paddingH = theme().space4,
                    .paddingV = theme().space3,
                    .spacing = theme().space3,
                    .backgroundColor = Colors::transparent,
                    .hoverBackgroundColor = Colors::transparent,
                    .selectedBackgroundColor = Colors::transparent,
                },
            });
        compactRows.push_back(
            TableRow {
                .cells = {
                    Element {TableCell {.content = Text {.text = "Average progress", .font = Font::body(), .color = Color::secondary()}, .style = TableCell::Style {.width = 200.f}}},
                    Element {TableCell {.content = Text {.text = std::to_string(averageProgress) + "%", .font = Font::headline(), .color = Color::accent(), .horizontalAlignment = HorizontalAlignment::Trailing}, .style = TableCell::Style {.width = 140.f, .alignment = HorizontalAlignment::Trailing}}},
                },
                .style = TableRow::Style {
                    .paddingH = theme().space4,
                    .paddingV = theme().space3,
                    .spacing = theme().space3,
                    .backgroundColor = Colors::transparent,
                    .hoverBackgroundColor = Colors::transparent,
                    .selectedBackgroundColor = Colors::transparent,
                },
            });

        return ScrollView {
            .axis = ScrollAxis::Vertical,
            .children = children(
                VStack {
                    .spacing = theme().space4,
                    .alignment = Alignment::Stretch,
                    .children = children(
                        Text {
                            .text = "TableView",
                            .font = Font::largeTitle(),
                            .color = Color::primary(),
                        },
                        Text {
                            .text =
                                "Sticky header outside the scroll body, reusable rows and cells, arbitrary content inside each column, and opt-in sortable headers.",
                            .font = Font::body(),
                            .color = Color::secondary(),
                            .wrapping = TextWrapping::Wrap,
                        },
                        Card {
                            .child = TableView {
                                .header = std::move(headerRow),
                                .items = std::move(items),
                                .rows = std::move(rows),
                                .columns = {
                                    TableColumn {
                                        .width = 240.f,
                                        .sort = TableColumn::Sort::ascending<std::string>(),
                                    },
                                    TableColumn {.width = 110.f},
                                    TableColumn {.width = 120.f},
                                    TableColumn {
                                        .width = 150.f,
                                        .sort = TableColumn::Sort::descending<float>(),
                                    },
                                    TableColumn {
                                        .width = 110.f,
                                        .sort = TableColumn::Sort::by<Deal>(
                                            [](Deal const &lhs, Deal const &rhs) {
                                                return lhs.value < rhs.value;
                                            },
                                            false),
                                    },
                                    TableColumn {.width = 32.f},
                                },
                                .style = TableView::Style {
                                    .dividerInsetH = theme().space4,
                                    .backgroundColor = theme().windowBackgroundColor,
                                    .dividerColor = theme().separatorColor,
                                },
                            }
                                .height(380.f),
                            .style = Card::Style {
                                .padding = 0.f,
                                .cornerRadius = theme().radiusXLarge,
                            },
                        },
                        Card {
                            .child = VStack {
                                .spacing = theme().space3,
                                .alignment = Alignment::Stretch,
                                .children = children(
                                    Text {
                                        .text = "Non-scrolling inline table",
                                        .font = Font::headline(),
                                        .color = Color::primary(),
                                    },
                                    TableView {
                                        .header = std::move(compactHeader),
                                        .rows = std::move(compactRows),
                                        .columns = {
                                            TableColumn {.width = 200.f},
                                            TableColumn {.width = 140.f},
                                        },
                                        .scrollBody = false,
                                        .style = TableView::Style {
                                            .dividerInsetH = theme().space4,
                                            .backgroundColor = theme().controlBackgroundColor,
                                            .dividerColor = theme().separatorColor,
                                        },
                                    })
                            },
                        })
                }
                    .padding(theme().space6))
        };
    }
};

} // namespace

int main(int argc, char *argv[]) {
    Application app(argc, argv);
    auto &w = app.createWindow<Window>({
        .size = {940, 820},
        .title = "Lambda — Table demo",
        .resizable = true,
    });
    w.setView<TableDemoView>();
    return app.exec();
}
