#include <Lambda.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/UI.hpp>
#include <Lambda/UI/Views/Card.hpp>
#include <Lambda/UI/Views/HStack.hpp>
#include <Lambda/UI/Views/Rectangle.hpp>
#include <Lambda/UI/Views/ScrollView.hpp>
#include <Lambda/UI/Views/SegmentedControl.hpp>
#include <Lambda/UI/Views/Spacer.hpp>
#include <Lambda/UI/Views/Text.hpp>
#include <Lambda/UI/Views/VStack.hpp>
#include <Lambda/UI/Views/ZStack.hpp>

#include <cstdio>
#include <string>
#include <vector>

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

Element metricTile(Theme const &theme, Bindable<std::string> value, std::string label, Color accent) {
    return VStack {
        .spacing = theme.space1,
        .alignment = Alignment::Start,
        .children = children(
            Text {
                .text = std::move(value),
                .font = Font::title2(),
                .color = accent,
                .horizontalAlignment = HorizontalAlignment::Leading,
            },
            Text {
                .text = std::move(label),
                .font = Font::footnote(),
                .color = Color::secondary(),
                .horizontalAlignment = HorizontalAlignment::Leading,
            }
        )
    }
        .padding(theme.space3)
        .fill(FillStyle::solid(Color::windowBackground()))
        .cornerRadius(CornerRadius {theme.radiusMedium})
        .flex(1.f, 1.f, 0.f);
}

Element statusPill(Theme const &theme, Bindable<std::string> text, Color fill, Color label) {
    return Text {
        .text = std::move(text),
        .font = Font::caption(),
        .color = label,
        .horizontalAlignment = HorizontalAlignment::Center,
        .verticalAlignment = VerticalAlignment::Center,
    }
        .padding(6.f, 10.f, 6.f, 10.f)
        .fill(FillStyle::solid(fill))
        .cornerRadius(CornerRadius {theme.radiusLarge});
}

Element surfacePreview(Theme const &theme,
                       Bindable<std::string> title,
                       Bindable<std::string> body,
                       Color accent) {
    return VStack {
        .spacing = theme.space2,
        .alignment = Alignment::Start,
        .children = children(
            Text {
                .text = std::move(title),
                .font = Font::title3(),
                .color = Color::primary(),
                .horizontalAlignment = HorizontalAlignment::Leading,
            },
            Text {
                .text = std::move(body),
                .font = Font::footnote(),
                .color = Color::secondary(),
                .horizontalAlignment = HorizontalAlignment::Leading,
                .wrapping = TextWrapping::Wrap,
            },
            Rectangle {}
                .size(56.f, 4.f)
                .fill(FillStyle::solid(accent))
                .cornerRadius(CornerRadius {2.f})
        )
    }
        .padding(theme.space3)
        .fill(FillStyle::solid(Color::windowBackground()))
        .cornerRadius(CornerRadius {theme.radiusMedium})
        .flex(1.f, 1.f, 0.f);
}

std::string workspaceLabel(int index) {
    switch (index) {
    case 0:
        return "Overview";
    case 1:
        return "Deployments";
    case 2:
        return "Logs";
    default:
        return "Overview";
    }
}

std::string channelLabel(int index) {
    switch (index) {
    case 0:
        return "Email";
    case 1:
        return "Push";
    case 2:
        return "SMS";
    default:
        return "Email";
    }
}

std::string densityLabel(int index) {
    switch (index) {
    case 0:
        return "Comfortable";
    case 1:
        return "Balanced";
    case 2:
        return "Compact";
    default:
        return "Balanced";
    }
}

Element makeHeroDemo(Theme const &theme, Signal<int> workspace, Signal<std::string> lastEvent) {
    Bindable<std::string> current {[workspace] {
        return workspaceLabel(workspace());
    }};
    Bindable<std::string> title {[workspace] {
        return workspace() == 0 ? std::string {"Weekly performance digest"} :
               workspace() == 1 ? std::string {"Production rollout checklist"} :
                                      std::string {"Live incident timeline"};
    }};
    Bindable<std::string> body {[workspace] {
        return workspace() == 0 ? std::string {"A broader summary surface where the segmented control behaves like top-level workspace navigation."} :
               workspace() == 1 ? std::string {"A task-oriented view with focused rollout actions and environment status."} :
                                      std::string {"A denser operational surface with event streams, traces, and recent alerts."};
    }};

    return makeSectionCard(
        theme,
        "App Shell Navigation",
        "Segmented controls work best when the set is short, mutually exclusive, and switching should reuse the same screen real estate instead of opening another layer.",
        VStack {
            .spacing = theme.space3,
            .children = children(
                HStack {
                    .spacing = theme.space3,
                    .alignment = Alignment::Center,
                    .children = children(
                        SegmentedControl {
                            .selectedIndex = workspace,
                            .options = {
                                SegmentedControlOption {.label = "Overview"},
                                SegmentedControlOption {.label = "Deployments"},
                                SegmentedControlOption {.label = "Logs"},
                            },
                            .onChange = [lastEvent](int index) {
                                std::string const next = workspaceLabel(index);
                                lastEvent = "Primary workspace switched to " + next + ".";
                                std::fprintf(stderr, "[segmented-demo] workspace -> %s\n", next.c_str());
                            },
                        }
                            .width(360.f),
                        Spacer {}.flex(1.f, 1.f),
                        statusPill(theme, current, Color::selectedContentBackground(), Color::accent())
                    )
                },
                HStack {
                    .spacing = theme.space3,
                    .alignment = Alignment::Stretch,
                    .children = children(
                        surfacePreview(theme, title, body, Color::accent()),
                        surfacePreview(theme, "Why it works",
                                       "The selected segment owns the same content region, so the control reads as a mode switch rather than a loose set of buttons.",
                                       Color::success())
                    )
                }
            )
        }
    );
}

Element makeToolbarDemo(Theme const &theme, Signal<int> channel, Signal<int> density, Signal<std::string> lastEvent) {
    return makeSectionCard(
        theme,
        "Utility Toolbar",
        "The same control can shrink into tighter toolbars for filters, output modes, or presentation density without turning into tabs.",
        VStack {
            .spacing = theme.space3,
            .children = children(
                HStack {
                    .spacing = theme.space3,
                    .alignment = Alignment::Center,
                    .children = children(
                        Text {
                            .text = "Notifications",
                            .font = Font::headline(),
                            .color = Color::primary(),
                        },
                        SegmentedControl {
                            .selectedIndex = channel,
                            .options = {
                                SegmentedControlOption {.label = "Email"},
                                SegmentedControlOption {.label = "Push"},
                                SegmentedControlOption {.label = "SMS"},
                            },
                            .onChange = [lastEvent](int index) {
                                std::string const next = channelLabel(index);
                                lastEvent = "Delivery mode changed to " + next + ".";
                                std::fprintf(stderr, "[segmented-demo] channel -> %s\n", next.c_str());
                            },
                        }
                            .width(220.f),
                        Spacer {}.flex(1.f, 1.f),
                        Text {
                            .text = "Density",
                            .font = Font::headline(),
                            .color = Color::primary(),
                        },
                        SegmentedControl {
                            .selectedIndex = density,
                            .options = {
                                SegmentedControlOption {.label = "Comfortable"},
                                SegmentedControlOption {.label = "Balanced"},
                                SegmentedControlOption {.label = "Compact"},
                            },
                            .style = SegmentedControl::Style {
                                .paddingH = 12.f,
                            },
                            .onChange = [lastEvent](int index) {
                                std::string const next = densityLabel(index);
                                lastEvent = "Density set to " + next + ".";
                                std::fprintf(stderr, "[segmented-demo] density -> %s\n", next.c_str());
                            },
                        }
                            .width(320.f)
                    )
                },
                HStack {
                    .spacing = theme.space3,
                    .alignment = Alignment::Stretch,
                    .children = children(
                        metricTile(theme, [channel] {
                            return channelLabel(channel());
                        }, "Delivery channel", Color::accent()),
                        metricTile(theme, [density] {
                            return densityLabel(density());
                        }, "Interface density", Color::success()),
                        metricTile(theme, "3", "Choices per group", Color::warning())
                    )
                }
            )
        }
    );
}

Element makeStateDemo(Theme const &theme, Signal<int> branch, Signal<std::string> lastEvent) {
    return makeSectionCard(
        theme,
        "States And Availability",
        "Disabled options still belong in the group when they explain what the full system can do, but the active choice should stay on an enabled segment.",
        VStack {
            .spacing = theme.space3,
            .children = children(
                HStack {
                    .spacing = theme.space3,
                    .alignment = Alignment::Center,
                    .children = children(
                        Text {
                            .text = "Release branch",
                            .font = Font::headline(),
                            .color = Color::primary(),
                        },
                        SegmentedControl {
                            .selectedIndex = branch,
                            .options = {
                                SegmentedControlOption {.label = "Stable"},
                                SegmentedControlOption {.label = "Beta"},
                                SegmentedControlOption {.label = "Canary", .disabled = true},
                            },
                            .onChange = [lastEvent](int index) {
                                char const *labels[] = {"Stable", "Beta", "Canary"};
                                lastEvent = std::string("Branch switched to ") + labels[index] + ".";
                                std::fprintf(stderr, "[segmented-demo] branch -> %s\n", labels[index]);
                            },
                        }
                            .width(280.f),
                        statusPill(theme, "Canary locked", Color::warningBackground(), Color::warning())
                    )
                },
                Text {
                    .text = "Try using arrow keys after focusing the control. Navigation skips disabled segments and keeps focus inside the same group.",
                    .font = Font::footnote(),
                    .color = Color::secondary(),
                    .wrapping = TextWrapping::Wrap,
                },
                HStack {
                    .spacing = theme.space3,
                    .alignment = Alignment::Stretch,
                    .children = children(
                        surfacePreview(theme, "Enabled segment",
                                       "Stable and Beta are available today, so switching is instant and uses the same interaction pattern as pointer taps.",
                                       Color::accent()),
                        surfacePreview(theme, "Disabled segment",
                                       "Canary remains visible to communicate roadmap and access gating without behaving like a broken control.",
                                       Color::warning())
                    )
                }
            )
        }
    );
}

Element makeLayoutDemo(Theme const &theme, Signal<int> compactMode, Signal<std::string> lastEvent) {
    return makeSectionCard(
        theme,
        "Layout Variations",
        "A segmented control can span a broad header or tighten into a narrow utility card. The component should stretch cleanly without changing its semantics.",
        VStack {
            .spacing = theme.space3,
            .children = children(
                VStack {
                    .spacing = theme.space2,
                    .alignment = Alignment::Start,
                    .children = children(
                        Text {
                            .text = "Full-width usage",
                            .font = Font::headline(),
                            .color = Color::primary(),
                        },
                        SegmentedControl {
                            .selectedIndex = compactMode,
                            .options = {
                                SegmentedControlOption {.label = "Summary"},
                                SegmentedControlOption {.label = "Details"},
                                SegmentedControlOption {.label = "Activity"},
                                SegmentedControlOption {.label = "Files"},
                            },
                            .onChange = [lastEvent](int index) {
                                char const *labels[] = {"Summary", "Details", "Activity", "Files"};
                                lastEvent = std::string("Preview switched to ") + labels[index] + ".";
                                std::fprintf(stderr, "[segmented-demo] preview -> %s\n", labels[index]);
                            },
                        }
                        .flex(1.f, 1.f)
                    )
                }
                    .padding(theme.space3)
                    .fill(FillStyle::solid(Color::windowBackground()))
                    .cornerRadius(CornerRadius {theme.radiusMedium}),
                HStack {
                    .spacing = theme.space3,
                    .alignment = Alignment::Stretch,
                    .children = children(
                        VStack {
                            .spacing = theme.space2,
                            .alignment = Alignment::Start,
                            .children = children(
                                Text {
                                    .text = "Compact card",
                                    .font = Font::headline(),
                                    .color = Color::primary(),
                                },
                                SegmentedControl {
                                    .selectedIndex = compactMode,
                                    .options = {
                                        SegmentedControlOption {.label = "Day"},
                                        SegmentedControlOption {.label = "Week"},
                                        SegmentedControlOption {.label = "Month"},
                                    },
                                }
                            )
                        }
                            .padding(theme.space3)
                            .fill(FillStyle::solid(Color::windowBackground()))
                            .cornerRadius(CornerRadius {theme.radiusMedium})
                            .flex(1.f, 1.f, 0.f),
                        VStack {
                            .spacing = theme.space2,
                            .alignment = Alignment::Start,
                            .children = children(
                                Text {
                                    .text = "Disabled whole group",
                                    .font = Font::headline(),
                                    .color = Color::primary(),
                                },
                                SegmentedControl {
                                    .selectedIndex = compactMode,
                                    .options = {
                                        SegmentedControlOption {.label = "Draft"},
                                        SegmentedControlOption {.label = "Review"},
                                        SegmentedControlOption {.label = "Live"},
                                    },
                                    .disabled = true,
                                }
                            )
                        }
                            .padding(theme.space3)
                            .fill(FillStyle::solid(Color::windowBackground()))
                            .cornerRadius(CornerRadius {theme.radiusMedium})
                            .flex(1.f, 1.f, 0.f)
                    )
                }
            )
        }
    );
}

} // namespace

struct SegmentedDemoRoot {
    Element body() const {
        auto theme = useEnvironment<ThemeKey>();

        auto workspace = useState<int>(0);
        auto channel = useState<int>(1);
        auto density = useState<int>(1);
        auto branch = useState<int>(0);
        auto layoutMode = useState<int>(1);
        auto lastEvent = useState<std::string>(
            "Use the segmented controls below to switch contexts and watch how the surrounding surface updates."
        );

        return ScrollView {
            .axis = ScrollAxis::Vertical,
            .children = children(
                VStack {
                    .spacing = theme().space4,
                    .children = children(
                        Text {
                            .text = "Segmented Control Demo",
                            .font = Font::largeTitle(),
                            .color = Color::primary(),
                            .horizontalAlignment = HorizontalAlignment::Leading,
                        },
                        Text {
                            .text = "A segmented control for short, exclusive mode switches. This demo covers app-shell navigation, compact toolbars, disabled segments, and width variations similar to real product surfaces.",
                            .font = Font::body(),
                            .color = Color::secondary(),
                            .horizontalAlignment = HorizontalAlignment::Leading,
                            .wrapping = TextWrapping::Wrap,
                        },
                        HStack {
                            .spacing = theme().space3,
                            .alignment = Alignment::Stretch,
                            .children = children(
                                metricTile(theme(), [workspace] {
                                    return workspaceLabel(workspace());
                                }, "Primary workspace", Color::accent()),
                                metricTile(theme(), [channel] {
                                    return channelLabel(channel());
                                }, "Delivery mode", Color::success()),
                                metricTile(theme(), [density] {
                                    return densityLabel(density());
                                }, "Density preset", Color::warning())
                            )
                        },
                        makeHeroDemo(theme(), workspace, lastEvent),
                        makeToolbarDemo(theme(), channel, density, lastEvent),
                        makeStateDemo(theme(), branch, lastEvent),
                        makeLayoutDemo(theme(), layoutMode, lastEvent),
                        Text {
                            .text = [lastEvent] {
                                return lastEvent();
                            },
                            .font = Font::footnote(),
                            .color = Color::tertiary(),
                            .horizontalAlignment = HorizontalAlignment::Leading,
                            .wrapping = TextWrapping::Wrap,
                        }
                    )
                }
                    .padding(theme().space5)
            )
        }
            .fill(FillStyle::solid(Color::windowBackground()));
    }
};

int main(int argc, char *argv[]) {
    Application app(argc, argv);

    auto &w = app.createWindow<Window>({
        .size = {800, 800},
        .title = "Lambda - Segmented control demo",
        .resizable = true,
    });
    w.setView<SegmentedDemoRoot>();
    return app.exec();
}
