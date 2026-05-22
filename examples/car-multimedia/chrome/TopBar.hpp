#pragma once

#include "../AppState.hpp"
#include "../Common.hpp"
#include "../components/Avatar.hpp"

namespace car {

struct TopBar : ViewModifiers<TopBar> {
    Reactive::Signal<State> state;

    auto body() const {
        auto theme = useEnvironment<ThemeKey>();
        auto statusItem = [](IconName icon, std::string label) -> Element {
            if (label.empty()) {
                return Icon {.name = icon, .size = 16.f, .color = Color::secondary()};
            }
            return HStack {
                .spacing = 4.f,
                .alignment = Alignment::Center,
                .children = children(
                    Icon {.name = icon, .size = 16.f, .color = Color::secondary()},
                    Text {.text = std::move(label), .font = Font {.size = 11.f, .weight = 500.f}, .color = Color::secondary()}
                ),
            };
        };
        return HStack {
            .spacing = theme().space6,
            .alignment = Alignment::Center,
            .children = children(
                HStack {
                    .spacing = theme().space2,
                    .alignment = Alignment::Center,
                    .children = children(
                        Avatar {.initials = [s = state] { return s().profile.initials; }, .sizeP = 28.f, .useAccent = true},
                        Text {.text = [s = state] { return s().profile.name; }, .font = Font {.size = 13.f, .weight = 500.f}, .color = Color::primary()}
                    ),
                },
                Spacer {}.flex(1.f, 1.f),
                HStack {
                    .spacing = theme().space3,
                    .alignment = Alignment::Stretch,
                    .children = children(
                        statusItem(IconName::BluetoothConnected, "iPhone 16 Pro"),
                        statusItem(IconName::SignalCellularAlt, "LTE"),
                        statusItem(IconName::Wifi, ""),
                        Divider {.orientation = Divider::Orientation::Vertical},

                        Text {.text = [s = state] { return s().clock; }, .font = Font {.size = 11.f, .weight = 500.f}, .color = Color::secondary()}
                    ),
                }
            ),
        }
            .padding(0.f, 28.f, 0.f, 28.f)
            .fill(Color::elevatedBackground());
    }
};

} // namespace car
