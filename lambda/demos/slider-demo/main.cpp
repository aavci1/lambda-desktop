#include <Lambda.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/UI.hpp>
#include <Lambda/UI/Views/Card.hpp>
#include <Lambda/UI/Views/HStack.hpp>
#include <Lambda/UI/Views/Rectangle.hpp>
#include <Lambda/UI/Views/ScrollView.hpp>
#include <Lambda/UI/Views/Slider.hpp>
#include <Lambda/UI/Views/Spacer.hpp>
#include <Lambda/UI/Views/Text.hpp>
#include <Lambda/UI/Views/VStack.hpp>
#include <Lambda/UI/Views/ZStack.hpp>

#include <cstdio>
#include <string>

using namespace lambda;

namespace {

std::string fmtInt(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(v));
    return buf;
}

std::string fmtHex(float r, float g, float b) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", static_cast<int>(r), static_cast<int>(g),
                  static_cast<int>(b));
    return buf;
}

std::string fmtMinutes(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d min", static_cast<int>(v));
    return buf;
}

std::string fmtPercent(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(v));
    return buf;
}

Element makeSectionCard(Theme const &theme, std::string title, std::string caption, Element content) {
    return Card {
        .child = VStack {
            .spacing = theme.space3,
            .children = children(
                Text {.text = std::move(title), .font = Font::title2(), .color = Color::primary()},
                Text {
                    .text = std::move(caption),
                    .font = Font::footnote(),
                    .color = Color::secondary(),
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

Element labeledSlider(Theme const &theme,
                      std::string label,
                      Bindable<std::string> valueText,
                      Signal<float> value,
                      float min, float max, float step, Slider::Style style = {}) {
    return VStack {
        .spacing = theme.space2,
        .children = children(
            HStack {
                .spacing = theme.space2,
                .alignment = Alignment::Center,
                .children = children(
                    Text {.text = std::move(label), .font = Font::headline(), .color = Color::primary()},
                    Spacer {},
                    Text {.text = std::move(valueText), .font = Font::headline(), .color = Color::secondary()}
                )
            },
            Slider {
                .value = value,
                .min = min,
                .max = max,
                .step = step,
                .style = style,
            }
        )
    } //
        .padding(theme.space3)
        .fill(FillStyle::solid(Color::windowBackground()))
        .cornerRadius(CornerRadius {theme.radiusMedium});
}

float luminance(Color const &c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }

constexpr float kChannelScale = 1.f / 255.f;

} // namespace

struct SliderDemoRoot {
    Element body() const {
        auto theme = useEnvironment<ThemeKey>();

        auto red = useState(90.f);
        auto green = useState(120.f);
        auto blue = useState(200.f);
        auto volume = useState(68.f);
        auto scrubber = useState(42.f);

        Bindable<Color> preview {[red, green, blue] {
            return Color {red() * kChannelScale, green() * kChannelScale, blue() * kChannelScale, 1.f};
        }};
        Bindable<Color> previewTextColor {[red, green, blue] {
            Color const color {red() * kChannelScale, green() * kChannelScale, blue() * kChannelScale, 1.f};
            return luminance(color) > 0.55f ? Color::primary() : Color::accentForeground();
        }};

        Element page = Element {VStack {
                                    .spacing = theme().space4,
                                    .children = children(
                                        Text {
                                            .text = "Slider Demo",
                                            .font = Font::largeTitle(),
                                            .color = Color::primary(),
                                            .horizontalAlignment = HorizontalAlignment::Leading,
                                        },
                                        Text {
                                            .text = "Three focused examples: color mixing, media-style scrubbing, and compact percentage tuning.",
                                            .font = Font::body(),
                                            .color = Color::secondary(),
                                            .horizontalAlignment = HorizontalAlignment::Leading,
                                            .wrapping = TextWrapping::Wrap,
                                        },
                                        makeSectionCard(
                                            theme(), "RGB Mixer",
                                            "A slider should feel at home in a composed panel, not just as a lone horizontal line.",
                                            VStack {
                                                .spacing = theme().space3,
                                                .children = children(
                                                    ZStack {
                                                        .horizontalAlignment = Alignment::Center,
                                                        .verticalAlignment = Alignment::Center,
                                                        .children = children(
                                                            Rectangle {}
                                                                .fill(preview)
                                                                .stroke(StrokeStyle::solid(Color::opaqueSeparator(), 1.f))
                                                                .height(150.f)
                                                                .cornerRadius(CornerRadius {theme().radiusLarge})
                                                                .flex(1.f, 1.f, 0.f),
                                                            Text {
                                                                .text = [red, green, blue] {
                                                                    return fmtHex(red(), green(), blue());
                                                                },
                                                                .font = Font::title2(),
                                                                .color = previewTextColor,
                                                                .horizontalAlignment = HorizontalAlignment::Center,
                                                                .verticalAlignment = VerticalAlignment::Center,
                                                            }
                                                        )
                                                    },
                                                    labeledSlider(theme(), "Red", [red] {
                                                        return fmtInt(red());
                                                    }, red, 0.f, 255.f, 1.f,
                                                                  Slider::Style {.activeColor = Color::danger()}),
                                                    labeledSlider(theme(), "Green", [green] {
                                                        return fmtInt(green());
                                                    }, green, 0.f, 255.f, 1.f,
                                                                  Slider::Style {.activeColor = Color::success()}),
                                                    labeledSlider(theme(), "Blue", [blue] {
                                                        return fmtInt(blue());
                                                    }, blue, 0.f, 255.f, 1.f,
                                                                  Slider::Style {.activeColor = Color::accent()})
                                                )
                                            }
                                        ),
                                        makeSectionCard(
                                            theme(), "Media Scrubber",
                                            "The same primitive can read as a playback control when the surrounding layout explains what the value means.",
                                            VStack {
                                                .spacing = theme().space3,
                                                .children = children(
                                                    HStack {
                                                        .spacing = theme().space2,
                                                        .alignment = Alignment::Center,
                                                        .children = children(
                                                            Text {.text = "Playback position", .font = Font::headline(), .color = Color::primary()},
                                                            Spacer {},
                                                            Text {
                                                                .text = [scrubber] {
                                                                    return fmtMinutes(scrubber());
                                                                },
                                                                .font = Font::headline(),
                                                                .color = Color::secondary(),
                                                            }
                                                        )
                                                    },
                                                    Slider {
                                                        .value = scrubber,
                                                        .min = 0.f,
                                                        .max = 96.f,
                                                        .step = 1.f,
                                                        .style = Slider::Style {
                                                            .activeColor = Color::accent(),
                                                            .trackHeight = 6.f,
                                                            .thumbSize = 18.f,
                                                        },
                                                    },
                                                    Text {
                                                        .text = "Arrow keys nudge the value; hold Shift for larger jumps.",
                                                        .font = Font::footnote(),
                                                        .color = Color::secondary(),
                                                        .wrapping = TextWrapping::Wrap,
                                                    }
                                                )
                                            }
                                        ),
                                        makeSectionCard(
                                            theme(),
                                            "Compact Controls",
                                            "Sliders also work well in shorter utility rows when the value is the main feedback.",
                                            VStack {
                                                .spacing = theme().space2,
                                                .children = children(
                                                    labeledSlider(
                                                        theme(),
                                                        "Volume",
                                                        [volume] {
                                                            return fmtPercent(volume());
                                                        },
                                                        volume,
                                                        0.f,
                                                        100.f,
                                                        1.f,
                                                        Slider::Style {
                                                            .activeColor = Color::success(),
                                                            .trackHeight = 4.f,
                                                            .thumbSize = 16.f,
                                                        }
                                                    ),
                                                    Text {
                                                        .text = "The minimum-value fill workaround stays in place here to avoid the zero-width rendering bug you called out.",
                                                        .font = Font::footnote(),
                                                        .color = Color::tertiary(),
                                                        .wrapping = TextWrapping::Wrap,
                                                    }
                                                )
                                            }
                                        )
                                    )
                                }}
                           .padding(theme().space5);

        return ScrollView {
            .axis = ScrollAxis::Vertical,
            .children = children(std::move(page)),
        }
            .fill(FillStyle::solid(Color::windowBackground()));
    }
};

int main(int argc, char *argv[]) {
    Application app(argc, argv);
    auto &w = app.createWindow<Window>({
        .size = {800, 800},
        .title = "Lambda - Slider demo",
        .resizable = true,
    });
    w.setView<SliderDemoRoot>();
    return app.exec();
}
