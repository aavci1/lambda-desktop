#include <Lambda.hpp>
#include <Lambda/UI/Window.hpp>
#include <Lambda/UI/WindowUI.hpp>
#include <Lambda/Graphics/TextSystem.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/UI.hpp>
#include <Lambda/UI/Views/Button.hpp>
#include <Lambda/UI/Views/Card.hpp>
#include <Lambda/UI/Views/HStack.hpp>
#include <Lambda/UI/Views/Icon.hpp>
#include <Lambda/UI/Views/ScrollView.hpp>
#include <Lambda/UI/Views/Spacer.hpp>
#include <Lambda/UI/Views/Text.hpp>
#include <Lambda/UI/Views/Toggle.hpp>
#include <Lambda/UI/Views/VStack.hpp>

#include <string>
#include <vector>

using namespace lambda;

namespace {

struct ExpandableCard : ViewModifiers<ExpandableCard> {
    IconName icon = IconName::Dashboard;
    Color accent = Color::accent();
    std::string title;
    std::string summary;
    std::string detail;

    bool operator==(ExpandableCard const &) const = default;

    Element body() const {
        auto theme = useEnvironment<ThemeKey>();
        auto expanded = useState(false);
        auto detailHeight = useAnimated(0.f);
        auto detailOpacity = useAnimated(0.f);
        Color const accentColor = accent;

        auto bounds = useBounds();
        auto layout = Application::instance().textSystem().layout(detail, Font::body(), Color::secondary(), bounds.width - 2 * theme().space3, TextLayoutOptions {.wrapping = TextWrapping::Wrap});
        auto numLines = layout->lines.size();

        return Card {
            .child = VStack {
                .spacing = theme().space3,
                .alignment = Alignment::Stretch,
                .children = children(
                    HStack {
                        .spacing = theme().space3,
                        .alignment = Alignment::Center,
                        .children = children(
                            Icon {
                                .name = icon,
                                .size = 18.f,
                                .color = accent,
                            },
                            VStack {
                                .spacing = theme().space1,
                                .alignment = Alignment::Start,
                                .children = children(
                                    Text {
                                        .text = title,
                                        .font = Font::headline(),
                                        .color = Color::primary(),
                                    },
                                    Text {
                                        .text = summary,
                                        .font = Font::footnote(),
                                        .color = Color::secondary(),
                                        .wrapping = TextWrapping::Wrap,
                                    }
                                )
                            }
                                .flex(1.f, 1.f, 0.f),
                            Icon {
                                .name = [expanded] {
                                    return expanded() ? IconName::ExpandLess : IconName::ExpandMore;
                                },
                                .size = 18.f,
                                .color = Color::tertiary(),
                            }
                        )
                    },
                    Text {
                        .text = detail,
                        .font = Font::body(),
                        .color = Color::secondary(),
                        .wrapping = TextWrapping::Wrap,
                    }
                        .height([detailHeight] { return detailHeight(); })
                        .opacity([detailOpacity] { return detailOpacity; })
                )
            }.clipContent(true)
        }
            .cursor(Cursor::Hand)
            .onTap([expanded, theme, numLines, detailHeight, detailOpacity] {
                expanded = !*expanded;
                detailHeight.set(expanded() ? theme().bodyFont.size * numLines * 1.2f : -theme().space3, Transition::spring());
                detailOpacity.set(expanded() ? 1.f : 0.f, Transition::spring());
            });
    }
};

struct CardDemoView {
    Element body() const {
        auto theme = useEnvironment<ThemeKey>();

        auto accentBorder = useState(true);
        auto dropShadow = useState(true);

        return ScrollView {
            .axis = ScrollAxis::Vertical,
            .children = children(
                VStack {
                    .spacing = theme().space4,
                    .alignment = Alignment::Stretch,
                    .children = children(
                        Text {
                            .text = "Card",
                            .font = Font::largeTitle(),
                            .color = Color::primary(),
                        },
                        Text {
                            .text =
                                "Reusable surface container for elevated content. This demo shows default styling, "
                                "custom borders, shadows, and composition with controls.",
                            .font = Font::body(),
                            .color = Color::secondary(),
                            .wrapping = TextWrapping::Wrap,
                        },
                        Card {
                            .child = HStack {
                                .spacing = theme().space4,
                                .alignment = Alignment::Center,
                                .children = children(
                                    VStack {
                                        .spacing = theme().space1,
                                        .alignment = Alignment::Start,
                                        .children = children(
                                            Text {
                                                .text = "Style Knobs",
                                                .font = Font::headline(),
                                                .color = Color::primary(),
                                            },
                                            Text {
                                                .text = "These toggles feed the next card's border and shadow.",
                                                .font = Font::footnote(),
                                                .color = Color::secondary(),
                                                .wrapping = TextWrapping::Wrap,
                                            })
                                    }
                                        .flex(1.f, 1.f, 0.f),
                                    VStack {
                                        .spacing = theme().space2,
                                        .alignment = Alignment::Start,
                                        .children = children(
                                            HStack {
                                                .spacing = theme().space2,
                                                .alignment = Alignment::Center,
                                                .children = children(
                                                    Toggle {.value = accentBorder},
                                                    Text {
                                                        .text = "Accent border",
                                                        .font = Font::footnote(),
                                                        .color = Color::primary(),
                                                    })
                                            },
                                            HStack {
                                                .spacing = theme().space2,
                                                .alignment = Alignment::Center,
                                                .children = children(
                                                    Toggle {.value = dropShadow},
                                                    Text {
                                                        .text = "Drop shadow",
                                                        .font = Font::footnote(),
                                                        .color = Color::primary(),
                                                    })
                                            })
                                    })
                            },
                        },
                        Card {
                            .child = VStack {
                                .spacing = theme().space2,
                                .alignment = Alignment::Start,
                                .children = children(
                                    Text {
                                        .text = "Default card",
                                        .font = Font::headline(),
                                        .color = Color::primary(),
                                    },
                                    Text {
                                        .text = "Uses the framework defaults: elevated background, separator border, large radius, and standard padding.",
                                        .font = Font::body(),
                                        .color = Color::secondary(),
                                        .wrapping = TextWrapping::Wrap,
                                    })
                            },
                        },
                        Card {
                            .child = VStack {
                                .spacing = theme().space3,
                                .alignment = Alignment::Stretch,
                                .children = children(
                                    Text {
                                        .text = "Customized card",
                                        .font = Font::headline(),
                                        .color = Color::primary(),
                                    },
                                    Text {
                                        .text = "Same component, different tokens. This is the replacement path for the demo-specific surface wrappers.",
                                        .font = Font::body(),
                                        .color = Color::secondary(),
                                        .wrapping = TextWrapping::Wrap,
                                    },
                                    HStack {
                                        .spacing = theme().space2,
                                        .alignment = Alignment::Center,
                                        .children = children(
                                            Icon {
                                                .name = IconName::Palette,
                                                .size = 18.f,
                                                .color = Color::accent(),
                                            },
                                            Text {
                                                .text = [accentBorder] {
                                                    return accentBorder()
                                                               ? std::string {"Accent border enabled"}
                                                               : std::string {"Neutral border enabled"};
                                                },
                                                .font = Font::footnote(),
                                                .color = Color::secondary(),
                                            },
                                            Spacer {},
                                            Text {
                                                .text = [dropShadow] {
                                                    return dropShadow()
                                                               ? std::string {"Shadow on"}
                                                               : std::string {"Shadow off"};
                                                },
                                                .font = Font::footnote(),
                                                .color = Color::tertiary(),
                                            })
                                    })
                            },
                            .style = Card::Style {
                                .padding = theme().space4,
                                .cornerRadius = theme().radiusXLarge,
                                .borderColor = [accentBorder, theme] {
                                    return accentBorder() ? theme().accentColor : theme().separatorColor;
                                },
                                .shadow = [dropShadow, theme] {
                                    return dropShadow()
                                               ? ShadowStyle {
                                                     .radius = theme().shadowRadiusPopover,
                                                     .offset = {0.f, theme().shadowOffsetYPopover},
                                                     .color = Color {0.f, 0.f, 0.f, 0.12f},
                                                 }
                                               : ShadowStyle::none();
                                },
                            },
                        },
                        Card {
                            .child = VStack {
                                .spacing = theme().space3,
                                .alignment = Alignment::Stretch,
                                .children = children(
                                    Text {
                                        .text = "Composed with other controls",
                                        .font = Font::headline(),
                                        .color = Color::primary(),
                                    },
                                    Text {
                                        .text = "Cards are just containers. Put buttons, toggles, metrics, or custom layouts inside them.",
                                        .font = Font::body(),
                                        .color = Color::secondary(),
                                        .wrapping = TextWrapping::Wrap,
                                    },
                                    HStack {
                                        .spacing = theme().space2,
                                        .alignment = Alignment::Center,
                                        .children = children(
                                            Button {
                                                .label = "Primary",
                                                .onTap = [] {},
                                            },
                                            Button {
                                                .label = "Secondary",
                                                .variant = ButtonVariant::Secondary,
                                                .onTap = [] {},
                                            })
                                    })
                            },
                            .style = Card::Style {
                                .padding = theme().space4,
                                .cornerRadius = theme().radiusLarge,
                                .backgroundColor = theme().controlBackgroundColor,
                            },
                        },
                        ExpandableCard {
                            .icon = IconName::AutoAwesome,
                            .accent = theme().accentColor,
                            .title = "Interactive card",
                            .summary = "Hover or tap to emphasize the card surface.",
                            .detail = "The component does not own higher-level behavior. State, hover feedback, "
                                      "and expansion remain in user code while the framework handles the surface.",
                        },
                        ExpandableCard {
                            .icon = IconName::DashboardCustomize,
                            .accent = theme().successColor,
                            .title = "Another surface variant",
                            .summary = "Same primitive, different accent and content.",
                            .detail = "This is the intended replacement for demo-local section cards, stat cards, "
                                      "and other repeated panel wrappers that only differed by border or shadow.",
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
        .size = {820, 860},
        .title = "Lambda — Card demo",
        .resizable = true,
    });
    w.setView<CardDemoView>();
    return app.exec();
}
