#include <Lambda.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/UI.hpp>
#include <Lambda/UI/Views/Card.hpp>
#include <Lambda/UI/Views/Checkbox.hpp>
#include <Lambda/UI/Views/HStack.hpp>
#include <Lambda/UI/Views/ScrollView.hpp>
#include <Lambda/UI/Views/Text.hpp>
#include <Lambda/UI/Views/VStack.hpp>

#include <cstdio>
#include <string>

using namespace lambda;

namespace {

Element makeSectionCard(Theme const &theme, std::string title, std::string caption, Element content) {
    return Card {
        .child = VStack {
            .spacing = theme.space3,
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

Element checkboxRow(Theme const &theme, std::string label, std::string detail, Element control) {
    return HStack {
        .spacing = theme.space3,
        .alignment = Alignment::Center,
        .children = children(
            std::move(control),
            VStack {
                .spacing = theme.space1,
                .alignment = Alignment::Start,
                .children = children(
                    Text {
                        .text = std::move(label),
                        .font = Font::headline(),
                        .color = Color::primary(),
                        .horizontalAlignment = HorizontalAlignment::Leading,
                    },
                    Text {
                        .text = std::move(detail),
                        .font = Font::footnote(),
                        .color = Color::secondary(),
                        .horizontalAlignment = HorizontalAlignment::Leading,
                        .wrapping = TextWrapping::Wrap,
                    }
                )
            } //
                .flex(1.f, 1.f, 0.f)
        )
    } //
        .padding(theme.space3)
        .fill(FillStyle::solid(Color::windowBackground()))
        .cornerRadius(CornerRadius {theme.radiusMedium});
}

Element summaryTile(Theme const &theme,
                    Bindable<std::string> value,
                    std::string label,
                    Bindable<Color> accent) {
    return VStack {
        .spacing = theme.space1,
        .alignment = Alignment::Start,
        .children = children(
            Text {.text = std::move(value), .font = Font::title2(), .color = std::move(accent)},
            Text {.text = std::move(label), .font = Font::footnote(), .color = Color::secondary()}
        )
    } //
        .padding(theme.space3)
        .fill(FillStyle::solid(Color::windowBackground()))
        .cornerRadius(CornerRadius {theme.radiusMedium})
        .flex(1.f, 1.f, 0.f);
}

} // namespace

struct CheckboxDemoRoot {
    Element body() const {
        auto theme = useEnvironment<ThemeKey>();

        auto termsAccepted = useState(false);
        auto newsletter = useState(true);
        auto disabledState = useState(true);
        auto itemA = useState(true);
        auto itemB = useState(false);
        auto itemC = useState(true);
        auto greenCheck = useState(true);

        auto selectAll = useState(false);
        Bindable<bool> const allChecked {[itemA, itemB, itemC] {
            return itemA() && itemB() && itemC();
        }};
        Bindable<bool> const noneChecked {[itemA, itemB, itemC] {
            return !itemA() && !itemB() && !itemC();
        }};
        Bindable<bool> const selectAllIndeterminate {[allChecked, noneChecked] {
            return !allChecked.evaluate() && !noneChecked.evaluate();
        }};
        Bindable<std::string> const selectedCount {[itemA, itemB, itemC] {
            int const count = static_cast<int>(itemA()) + static_cast<int>(itemB()) +
                              static_cast<int>(itemC());
            return std::to_string(count);
        }};
        Bindable<std::string> const aggregateState {[allChecked, selectAllIndeterminate] {
            if (selectAllIndeterminate.evaluate()) {
                return std::string {"Mixed"};
            }
            return allChecked.evaluate() ? std::string {"All"} : std::string {"None"};
        }};
        Bindable<Color> const aggregateColor {[selectAllIndeterminate] {
            return selectAllIndeterminate.evaluate() ? Color::warning() : Color::success();
        }};

        useEffect([selectAll, allChecked] {
            bool const next = allChecked.evaluate();
            if (selectAll.peek() != next) {
                selectAll.set(next);
            }
        });

        return ScrollView {
            .axis = ScrollAxis::Vertical,
            .children = children(
                VStack {
                    .spacing = theme().space4,
                    .children = children(
                        Text {
                            .text = "Checkbox Demo",
                            .font = Font::largeTitle(),
                            .color = Color::primary(),
                            .horizontalAlignment = HorizontalAlignment::Leading,
                        },
                        Text {
                            .text = "A more realistic checkbox showcase with form consent, tri-state selection, disabled rows, and a restrained style variation.",
                            .font = Font::body(),
                            .color = Color::secondary(),
                            .horizontalAlignment = HorizontalAlignment::Leading,
                            .wrapping = TextWrapping::Wrap,
                        },
                        makeSectionCard(
                            theme(), "Form Consent",
                            "Checkboxes fit best when users are confirming independent choices rather than flipping live settings.",
                            VStack {
                                .spacing = theme().space2,
                                .children = children(
                                    checkboxRow(
                                        theme(), "Accept terms and conditions",
                                        "Required before creating the workspace.",
                                        Checkbox {
                                            .value = termsAccepted,
                                            .onChange = [](bool v) {
                                                std::fprintf(stderr, "[checkbox-demo] Terms -> %s\n",
                                                             v ? "checked" : "unchecked");
                                            },
                                        }
                                    ),
                                    checkboxRow(
                                        theme(), "Subscribe to newsletter",
                                        "An optional follow-up preference that doesn’t block the primary task.",
                                        Checkbox {
                                            .value = newsletter,
                                            .onChange = [](bool v) {
                                                std::fprintf(stderr, "[checkbox-demo] Newsletter -> %s\n",
                                                             v ? "checked" : "unchecked");
                                            },
                                        }
                                    ),
                                    checkboxRow(
                                        theme(), "Disabled example",
                                        "Useful for unavailable or inherited defaults that still need to remain visible.",
                                        Checkbox {
                                            .value = disabledState,
                                            .disabled = true,
                                        }
                                    )
                                )
                            }
                        ),
                        makeSectionCard(
                            theme(), "Tri-State Selection",
                            "The parent checkbox reflects the aggregate state of its children. This is where the indeterminate state earns its keep.",
                            VStack {
                                .spacing = theme().space2,
                                .children = children(
                                    checkboxRow(
                                        theme(), "Select all assets",
                                        "Toggles the full selection at once.",
                                        Checkbox {
                                            .value = selectAll,
                                            .indeterminate = selectAllIndeterminate,
                                            .onChange = [itemA, itemB, itemC](bool v) {
                                                itemA = v;
                                                itemB = v;
                                                itemC = v;
                                            },
                                        }
                                    ),
                                    HStack {
                                        .spacing = theme().space3,
                                        .alignment = Alignment::Stretch,
                                        .children = children(
                                            summaryTile(theme(), selectedCount, "Selected", Color::accent()),
                                            summaryTile(theme(), aggregateState, "State", aggregateColor)
                                        )
                                    },
                                    checkboxRow(
                                        theme(), "Homepage illustrations",
                                        "Ready for this week’s rollout.",
                                        Checkbox {.value = itemA}
                                    ),
                                    checkboxRow(
                                        theme(), "Campaign copy deck",
                                        "Still waiting on final review.",
                                        Checkbox {.value = itemB}
                                    ),
                                    checkboxRow(
                                        theme(), "Pricing screenshots",
                                        "Approved and ready to export.",
                                        Checkbox {.value = itemC}
                                    )
                                )
                            }
                        ),
                        makeSectionCard(
                            theme(), "Style Variation",
                            "A small token change can adapt the control without changing its meaning.",
                            checkboxRow(
                                theme(), "Success accent",
                                "A green checked state works for positive checklist flows like “completed” or “verified”.",
                                Checkbox {
                                    .value = greenCheck,
                                    .style = Checkbox::Style {.checkedColor = Color::success()},
                                }
                            )
                        ),
                        Text {
                            .text = "Try keyboard focus here too: Tab onto a checkbox and use Space or Return to toggle it.",
                            .font = Font::footnote(),
                            .color = Color::tertiary(),
                            .horizontalAlignment = HorizontalAlignment::Leading,
                            .wrapping = TextWrapping::Wrap,
                        }
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
    auto &w = app.createWindow<Window>({
        .size = {800, 800},
        .title = "Lambda - Checkbox demo",
        .resizable = true,
    });
    w.setView<CheckboxDemoRoot>();
    return app.exec();
}
