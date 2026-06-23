// Demonstrates useTooltip: hover delay, placement, dismiss on tap, and toggle target.
#include <Lambda.hpp>
#include <Lambda/UI/WindowUI.hpp>
#include <Lambda/UI/UI.hpp>
#include <Lambda/UI/Views/Button.hpp>
#include <Lambda/UI/Views/HStack.hpp>
#include <Lambda/UI/Views/Icon.hpp>
#include <Lambda/UI/Views/Popover.hpp>
#include <Lambda/UI/Views/Spacer.hpp>
#include <Lambda/UI/Views/Text.hpp>
#include <Lambda/UI/Views/Toggle.hpp>
#include <Lambda/UI/Views/Tooltip.hpp>
#include <Lambda/UI/Views/VStack.hpp>

#include <cstdio>
#include <string>

using namespace lambdaui;

namespace {

Element attachTooltip(Element element, TooltipConfig config) {
    useTooltip(config);
    return std::move(element);
}

} // namespace

// ── Tooltip on a button ────────────────────────────────────────────────────

struct TooltipButton {
    std::string label;
    std::string tooltip;
    PopoverPlacement placement = PopoverPlacement::Above;

    bool operator==(TooltipButton const &) const = default;

    auto body() const {
        Element button = Button {
            .label = label,
            .variant = ButtonVariant::Secondary,
            .onTap = [label = label]() {
                std::fprintf(stderr, "[tooltip-demo] %s tapped\n", label.c_str());
            },
        };
        return attachTooltip(std::move(button), TooltipConfig {
            .text = tooltip,
            .placement = placement,
        });
    }
};

// ── Tooltip on an icon ─────────────────────────────────────────────────────

struct TooltipIcon {
    IconName name = IconName::Info;
    std::string tooltip;

    bool operator==(TooltipIcon const &) const = default;

    auto body() const {
        Element icon = Element {Icon {
            .name = name,
            .size = 24.f,
            .color = Color::secondary(),
        }}
            .padding(4.f)
            .cursor(Cursor::Arrow);
        return attachTooltip(std::move(icon), TooltipConfig {.text = tooltip});
    }
};

// ── Toggle wrapped with tooltip ────────────────────────────────────────────

struct TooltipToggle {
    auto body() const {
        auto value = useState(false);
        Element toggle = Toggle {
            .value = value,
        };
        return attachTooltip(std::move(toggle), TooltipConfig {.text = "Enable or disable notifications"});
    }
};

// ── Root view ────────────────────────────────────────────────────────────────

struct TooltipDemoRoot {
    auto body() const {
        auto theme = useEnvironment<ThemeKey>();

        return VStack {
            .spacing = 24.f,
            .alignment = Alignment::Start,
            .children = children(
                Text {
                    .text = "Tooltip",
                    .font = Font::largeTitle(),
                    .color = Color::primary(),
                },
                Text {
                    .text = "Hover over any control for 600 ms to "
                            "see its tooltip. Move the pointer away "
                            "to dismiss. Tapping also dismisses.",
                    .font = Font::body(),
                    .color = Color::secondary(),
                    .wrapping = TextWrapping::Wrap,
                },
                // ── Placement variants ──────────────────────────
                Text {
                    .text = "Placement",
                    .font = Font::title(),
                    .color = Color::primary(),
                },
                HStack {
                    .spacing = 12.f,
                    .alignment = Alignment::Center,
                    .children = children(
                        TooltipButton {
                            .label = "Above",
                            .tooltip = "Tooltip above the button",
                            .placement = PopoverPlacement::Above,
                        },
                        TooltipButton {
                            .label = "Below",
                            .tooltip = "Tooltip below the button",
                            .placement = PopoverPlacement::Below,
                        },
                        TooltipButton {
                            .label = "End",
                            .tooltip = "Tooltip to the right",
                            .placement = PopoverPlacement::End,
                        },
                        TooltipButton {
                            .label = "Start",
                            .tooltip = "Tooltip to the left",
                            .placement = PopoverPlacement::Start,
                        }
                    ),
                },

                // ── Icon tooltips ───────────────────────────────
                Text {
                    .text = "Icon tooltips",
                    .font = Font::title(),
                    .color = Color::primary(),
                },
                HStack {
                    .spacing = 16.f,
                    .alignment = Alignment::Center,
                    .children = children(
                        TooltipIcon {
                            .name = IconName::ContentCopy,
                            .tooltip = "Copy to clipboard",
                        },
                        TooltipIcon {
                            .name = IconName::Delete,
                            .tooltip = "Delete item",
                        },
                        TooltipIcon {
                            .name = IconName::Settings,
                            .tooltip = "Open settings",
                        },
                        TooltipIcon {
                            .name = IconName::Help,
                            .tooltip = "Help & documentation",
                        }
                    ),
                },

                // ── Long tooltip text ───────────────────────────
                Text {
                    .text = "Long text",
                    .font = Font::title(),
                    .color = Color::primary(),
                },
                TooltipButton {
                    .label = "Hover me",
                    .tooltip = "This is a longer tooltip that demonstrates "
                                "text wrapping within the 240 pt max width "
                                "constraint. It should wrap gracefully.",
                },

                // ── Toggle with tooltip ─────────────────────────
                Text {
                    .text = "On other controls",
                    .font = Font::title(),
                    .color = Color::primary(),
                },
                HStack {
                    .spacing = 12.f,
                    .alignment = Alignment::Center,
                    .children = children(
                        Text {
                            .text = "Notifications",
                            .font = Font::body(),
                            .color = Color::primary(),
                        },
                        Spacer {},
                        TooltipToggle {}
                    ),
                }
            ),
        }
            .padding(24.f);
    };
};

int main(int argc, char *argv[]) {
    Application app(argc, argv);
    auto &w = app.createWindow<Window>({
        .size = {800, 800},
        .title = "Lambda — Tooltip demo",
        .resizable = true,
    });
    w.setView<TooltipDemoRoot>();
    return app.exec();
}
