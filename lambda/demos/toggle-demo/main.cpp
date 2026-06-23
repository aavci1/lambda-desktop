#include <Lambda.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/UI.hpp>
#include <Lambda/UI/Views/Card.hpp>
#include <Lambda/UI/Views/HStack.hpp>
#include <Lambda/UI/Views/Rectangle.hpp>
#include <Lambda/UI/Views/ScrollView.hpp>
#include <Lambda/UI/Views/Spacer.hpp>
#include <Lambda/UI/Views/Text.hpp>
#include <Lambda/UI/Views/Toggle.hpp>
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
                    .font = Font::body(),
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

Element settingRow(Theme const &theme, std::string title, std::string detail, Element control) {
    return HStack {
        .spacing = theme.space3,
        .alignment = Alignment::Center,
        .children = children(
            VStack {
                .spacing = theme.space1,
                .alignment = Alignment::Start,
                .children = children(
                    Text {
                        .text = std::move(title),
                        .font = Font::headline(),
                        .color = Color::primary(),
                        .horizontalAlignment = HorizontalAlignment::Leading,
                    },
                    Text {
                        .text = std::move(detail),
                        .font = Font::callout(),
                        .color = Color::secondary(),
                        .horizontalAlignment = HorizontalAlignment::Leading,
                        .wrapping = TextWrapping::Wrap,
                    }
                )
            } //
                .flex(1.f, 1.f, 0.f),
            std::move(control)
        )
    } //
        .padding(theme.space3)
        .fill(FillStyle::solid(Color::windowBackground()))
        .cornerRadius(CornerRadius {theme.radiusMedium});
}

Element metricTile(Theme const &theme,
                   Bindable<std::string> value,
                   std::string label,
                   Bindable<Color> accent) {
    return VStack {
        .spacing = theme.space1,
        .alignment = Alignment::Start,
        .children = children(
            Text {
                .text = std::move(value),
                .font = Font::title2(),
                .color = accent,
            },
            Text {
                .text = std::move(label),
                .font = Font::footnote(),
                .color = Color::secondary(),
            }
        )
    } //
        .padding(theme.space3)
        .fill(FillStyle::solid(Color::windowBackground()))
        .cornerRadius(CornerRadius {theme.radiusMedium})
        .flex(1.f, 1.f, 0.f);
}

} // namespace

struct ToggleDemoRoot {
    Element body() const {
        auto theme = useEnvironment<ThemeKey>();

        auto wifiEnabled = useState(true);
        auto bluetoothEnabled = useState(false);
        auto syncEnabled = useState(true);
        auto notificationsEnabled = useState(false);
        auto compactEnabled = useState(false);
        auto greenAccent = useState(true);

        Bindable<std::string> enabledCount {[wifiEnabled, bluetoothEnabled, syncEnabled, notificationsEnabled] {
            int const count = static_cast<int>(wifiEnabled()) + static_cast<int>(bluetoothEnabled()) +
                              static_cast<int>(syncEnabled()) + static_cast<int>(notificationsEnabled());
            return std::to_string(count);
        }};

        return ScrollView {
            .axis = ScrollAxis::Vertical,
            .children = children(
                VStack {
                    .spacing = theme().space4,
                    .children = children(
                        Text {
                            .text = "Toggle Demo",
                            .font = Font::largeTitle(),
                            .color = Color::primary(),
                            .horizontalAlignment = HorizontalAlignment::Leading,
                        },
                        Text {
                            .text = "A cleaner toggle showcase with realistic settings rows, styling variations, and compact control density.",
                            .font = Font::body(),
                            .color = Color::secondary(),
                            .horizontalAlignment = HorizontalAlignment::Leading,
                            .wrapping = TextWrapping::Wrap,
                        },
                        makeSectionCard(
                            theme(), "Preferences",
                            "Toggles work best in quiet settings rows where the label carries the meaning and the switch only answers yes or no.",
                            VStack {
                                .spacing = theme().space2,
                                .children = children(
                                    settingRow(
                                        theme(), "Wi-Fi", "Keep the workspace online for syncing and collaboration.",
                                        Toggle {
                                            .value = wifiEnabled,
                                            .onChange = [](bool v) {
                                                std::fprintf(stderr, "[toggle-demo] Wi-Fi -> %s\n", v ? "on" : "off");
                                            },
                                        }
                                    ),
                                    settingRow(
                                        theme(),
                                        "Bluetooth",
                                        "Enable accessory pairing for keyboards, trackpads, and audio.",
                                        Toggle {
                                            .value = bluetoothEnabled,
                                            .onChange = [](bool v) {
                                                std::fprintf(stderr, "[toggle-demo] Bluetooth -> %s\n", v ? "on" : "off");
                                            },
                                        }
                                    ),
                                    settingRow(
                                        theme(),
                                        "Background sync",
                                        "This row is intentionally disabled to show the non-interactive state.",
                                        Toggle {
                                            .value = syncEnabled,
                                            .disabled = true,
                                        }
                                    ),
                                    settingRow(
                                        theme(),
                                        "Notifications",
                                        "Promote status changes without pushing users into a modal flow.",
                                        Toggle {
                                            .value = notificationsEnabled,
                                            .onChange = [](bool v) {
                                                std::fprintf(stderr, "[toggle-demo] Notifications -> %s\n", v ? "on" : "off");
                                            },
                                        }
                                    )
                                ),
                            }
                        ),
                        makeSectionCard(
                            theme(),
                            "States",
                            "A small summary helps show the control in a real context instead of as an isolated widget.",
                            HStack {
                                .spacing = theme().space3,
                                .alignment = Alignment::Stretch,
                                .children = children(
                                    metricTile(theme(), enabledCount, "Enabled settings", Color::accent()),
                                    metricTile(
                                        theme(),
                                        [notificationsEnabled] {
                                            return notificationsEnabled() ? "Live" : "Quiet";
                                        },
                                        "Notifications",
                                        [notificationsEnabled] {
                                            return notificationsEnabled() ? Color::success() : Color::warning();
                                        }
                                    ),
                                    metricTile(
                                        theme(),
                                        [wifiEnabled] {
                                            return wifiEnabled() ? "Online" : "Offline";
                                        },
                                        "Connectivity",
                                        [wifiEnabled] {
                                            return wifiEnabled() ? Color::success() : Color::secondary();
                                        }
                                    )
                                )
                            }
                        ),
                        makeSectionCard(
                            theme(),
                            "Styling",
                            "Style tokens should support subtle variations without turning the control into a different component.",
                            VStack {
                                .spacing = theme().space2,
                                .children = children(
                                    settingRow(
                                        theme(),
                                        "Success accent",
                                        "Useful when a toggle implies a positive enabled state.",
                                        Toggle {
                                            .value = greenAccent,
                                            .style = Toggle::Style {
                                                .onColor = Color::success(),
                                            },
                                        }
                                    ),
                                    settingRow(
                                        theme(),
                                        "Compact density",
                                        "A narrower track works for table rows and denser settings surfaces.",
                                        Toggle {
                                            .value = compactEnabled,
                                            .style = Toggle::Style {
                                                .trackWidth = 34.f,
                                                .trackHeight = 20.f,
                                                .thumbInset = 2.f,
                                            },
                                        }
                                    )
                                )
                            }
                        ),
                        Text {
                            .text = "Try keyboard focus as well: Tab to a toggle, then use Space or Return.",
                            .font = Font::footnote(),
                            .color = Color::tertiary(),
                            .horizontalAlignment = HorizontalAlignment::Leading,
                            .wrapping = TextWrapping::Wrap,
                        }
                    )
                } //
                    .padding(theme().space5)
            )
        };
    }
};

int main(int argc, char *argv[]) {
    Application app(argc, argv);
    auto &w = app.createWindow<Window>({
        .size = {800, 800},
        .title = "Lambda - Toggle demo",
        .resizable = true,
    });
    w.setView<ToggleDemoRoot>();
    return app.exec();
}
