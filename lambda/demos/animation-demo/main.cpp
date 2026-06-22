#include <Lambda.hpp>
#include <Lambda/UI/WindowUI.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/UI.hpp>
#include <Lambda/UI/Views/Button.hpp>
#include <Lambda/UI/Views/Card.hpp>
#include <Lambda/UI/Views/HStack.hpp>
#include <Lambda/UI/Views/Rectangle.hpp>
#include <Lambda/UI/Views/Render.hpp>
#include <Lambda/UI/Views/ScrollView.hpp>
#include <Lambda/UI/Views/Slider.hpp>
#include <Lambda/UI/Views/Text.hpp>
#include <Lambda/UI/Views/VStack.hpp>
#include <Lambda/UI/Views/ZStack.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace lambda;

namespace {

std::string formatFloat(float value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.2f", value);
    return buffer;
}

double nowSeconds() {
    auto const nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch());
    return static_cast<double>(nanos.count()) * 1e-9;
}

float loopBarEmphasis(float phase, float anchor) {
    return std::clamp(1.f - std::abs(phase - anchor) * 2.8f, 0.f, 1.f);
}

Element makeSectionCard(Theme const &theme, std::string title, std::string caption, Element content) {
    return Card {
        .child = VStack {
            .spacing = theme.space3,
            .alignment = Alignment::Start,
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
        .fill(Color::windowBackground())
        .cornerRadius(theme.radiusMedium)
        .flex(1.f, 1.f, 0.f);
}

Element buttonRow(Theme const &theme, std::vector<Element> buttons) {
    return HStack {
        .spacing = theme.space2,
        .alignment = Alignment::Center,
        .children = std::move(buttons),
    };
}

struct PlaybackLab : ViewModifiers<PlaybackLab> {
    auto body() const {
        auto theme = useEnvironment<ThemeKey>();

        auto progress = useAnimated<float>(0.f);

        auto playOnce = [progress, theme] {
            progress.set(0.f, Transition::instant());
            progress.play(1.f, Transition::ease(std::max(0.01f, theme().durationSlow)).delayed(0.12f));
        };
        auto loopForever = [progress] {
            progress.set(0.f, Transition::instant());
            progress.play(1.f, AnimationOptions {
                                    .transition = Transition::linear(1.2f),
                                    .repeat = AnimationOptions::kRepeatForever,
                                    .autoreverse = true,
                                });
        };
        auto bounceFour = [progress] {
            progress.set(0.f, Transition::instant());
            progress.play(1.f, AnimationOptions {
                                    .transition = Transition::ease(0.28f),
                                    .repeat = 4,
                                    .autoreverse = true,
                                });
        };
        auto pause = [progress] { progress.pause(); };
        auto resume = [progress] { progress.resume(); };
        auto stop = [progress] { progress.stop(); };
        auto reset = [progress] {
            progress.stop();
            progress.set(0.f, Transition::instant());
        };

        float clamped = std::clamp(progress(), 0.f, 1.f);
        auto previewValue = useState(clamped);
        useEffect([progress, previewValue] {
            previewValue = std::clamp(progress(), 0.f, 1.f);
        });

        return makeSectionCard(
            theme(), "Playback Controls",
            "Drive one handle with play, pause, resume, stop, finite repeats, infinite repeats, and delayed starts.",
            VStack {
                .spacing = theme().space3,
                .alignment = Alignment::Stretch,
                .children = children(
                    Slider {
                        .value = previewValue,
                        .min = 0.f,
                        .max = 1.f,
                        .disabled = true
                    },
                    HStack {
                        .spacing = theme().space3,
                        .alignment = Alignment::Stretch,
                        .children = children(
                            metricTile(theme(), [progress] {
                                float const value = progress();
                                if (value <= 0.001f) {
                                    return std::string {"Idle"};
                                }
                                if (value >= 0.999f) {
                                    return std::string {"Settled"};
                                }
                                return std::string {"Running"};
                            }, "State", Color::accent()),
                            metricTile(theme(), [progress] {
                                return formatFloat(progress());
                            }, "Progress", Color::success())
                        )
                    },
                    buttonRow(
                        theme(),
                        std::vector<Element> {
                            Button {.label = "Play Once", .variant = ButtonVariant::Primary, .onTap = playOnce},
                            Button {.label = "Loop Forever", .variant = ButtonVariant::Secondary, .onTap = loopForever},
                            Button {.label = "Bounce 4x", .variant = ButtonVariant::Secondary, .onTap = bounceFour},
                        }
                    ),
                    buttonRow(
                        theme(),
                        std::vector<Element> {
                            Button {.label = "Pause", .variant = ButtonVariant::Ghost, .disabled = !progress.isRunning() || progress.isPaused(), .onTap = pause},
                            Button {.label = "Resume", .variant = ButtonVariant::Ghost, .disabled = !progress.isPaused(), .onTap = resume},
                            Button {.label = "Stop", .variant = ButtonVariant::Ghost, .disabled = !progress.isRunning() && !progress.isPaused(), .onTap = stop},
                            Button {.label = "Reset", .variant = ButtonVariant::Ghost, .onTap = reset},
                        }
                    )
                )
            }
        );
    }
};

struct MorphLab : ViewModifiers<MorphLab> {
    auto body() const {
        auto theme = useEnvironment<ThemeKey>();

        auto travel = useAnimated<float>(0.f);
        auto width = useAnimated<float>(132.f);
        auto height = useAnimated<float>(52.f);
        auto radius = useAnimated<float>(16.f);
        auto lift = useAnimated<float>(0.f);
        auto rotation = useAnimated<float>(0.f);
        auto fill = useAnimated<Color>(Color::accent());

        auto calmPreset = [travel, width, height, radius, lift, rotation, fill, theme] {
            WithTransition transition {Transition::ease(std::max(0.01f, theme().durationSlow))};
            travel = 0.f;
            width = 132.f;
            height = 52.f;
            radius = theme().radiusLarge;
            lift = 0.f;
            rotation = 0.f;
            fill = Color::accent();
        };
        auto springPreset = [travel, width, height, radius, lift, rotation, fill, theme] {
            WithTransition transition {Transition::spring(420.f, 24.f, 0.70f)};
            travel = 1.f;
            width = 194.f;
            height = 74.f;
            radius = 30.f;
            lift = -10.f;
            rotation = 0.42f;
            fill = Color::warning();
        };

        return makeSectionCard(
            theme(), "WithTransition Scope",
            "A single WithTransition scope lets several useAnimated handles share one easing or spring without repeating the transition at each call site.",
            VStack {
                .spacing = theme().space3,
                .alignment = Alignment::Stretch,
                .children = children(
                    Render {
                        .measureFn = [](LayoutConstraints const& constraints, LayoutHints const&) {
                            float const width = std::isfinite(constraints.maxWidth) && constraints.maxWidth > 0.f
                                                  ? constraints.maxWidth
                                                  : 520.f;
                            return Size {std::max(260.f, width), 118.f};
                        },
                        .draw = [theme, travel, width, height, radius, lift, rotation, fill](
                                    Canvas& canvas, Rect frame) {
                            Theme const currentTheme = theme.evaluate();
                            Rect const track = Rect::sharp(frame.x, frame.y, frame.width, frame.height);
                            canvas.drawRect(track, CornerRadius {currentTheme.radiusLarge},
                                            FillStyle::solid(Color::windowBackground()),
                                            StrokeStyle::solid(Color::separator(), 1.f));
                            canvas.drawRect(Rect::sharp(frame.x + 16.f, frame.y + 58.f,
                                                        std::max(0.f, frame.width - 32.f), 2.f),
                                            CornerRadius {1.f}, FillStyle::solid(Color::separator()),
                                            StrokeStyle::none());

                            float const boxW = width.evaluate();
                            float const boxH = height.evaluate();
                            float const boxX = frame.x + 18.f + std::max(0.f, frame.width - boxW - 36.f) *
                                                          std::clamp(travel.evaluate(), 0.f, 1.f);
                            float const boxY = frame.y + 32.f + lift.evaluate();
                            Rect const box = Rect::sharp(boxX, boxY, boxW, boxH);
                            Point const center = box.center();
                            canvas.save();
                            canvas.translate(center);
                            canvas.rotate(rotation.evaluate());
                            canvas.drawRect(Rect::sharp(-boxW * 0.5f, -boxH * 0.5f, boxW, boxH),
                                            CornerRadius {radius.evaluate()},
                                            FillStyle::solid(fill.evaluate()),
                                            StrokeStyle::none(),
                                            ShadowStyle {.radius = 16.f,
                                                         .offset = {0.f, 10.f},
                                                         .color = Color {0.f, 0.f, 0.f, 0.16f}});
                            canvas.restore();
                        },
                    },
                    HStack {
                        .spacing = theme().space3,
                        .alignment = Alignment::Stretch,
                        .children = children(
                            metricTile(theme(), [travel] {
                                return formatFloat(travel());
                            }, "Travel", Color::accent()),
                            metricTile(theme(), [width] {
                                return formatFloat(width());
                            }, "Width", Color::warning()),
                            metricTile(theme(), [rotation] {
                                return formatFloat(rotation());
                            }, "Rotation", Color::success())
                        )
                    },
                    buttonRow(
                        theme(),
                        std::vector<Element> {
                            Button {.label = "Calm Ease", .variant = ButtonVariant::Primary, .onTap = calmPreset},
                            Button {.label = "Spring Burst", .variant = ButtonVariant::Secondary, .onTap = springPreset},
                        }
                    )
                )
            }
        );
    }
};

struct AmbientLoopLab : ViewModifiers<AmbientLoopLab> {
    auto body() const {
        auto theme = useEnvironment<ThemeKey>();

        auto phase = useState(0.f);
        double const startedAt = nowSeconds();
        useFrame([phase, startedAt](AnimationTick const& tick) {
                double const cycle = 2.8;
                double local = std::fmod(std::max(0.0, tick.nowSeconds - startedAt), cycle) / 1.4;
                if (local > 1.0) {
                    local = 2.0 - local;
                }
                phase = static_cast<float>(std::clamp(local, 0.0, 1.0));
            });

        std::vector<Element> bars;
        bars.reserve(5);
        for (int i = 0; i < 5; ++i) {
            float const anchor = static_cast<float>(i) / 4.f;
            bars.push_back(
                Rectangle {}
                    .size(22.f, [phase, anchor] {
                        float const emphasis = loopBarEmphasis(phase(), anchor);
                        return 18.f + emphasis * 34.f;
                    })
                    .cornerRadius(11.f)
                    .fill([theme, phase, anchor] {
                        float const emphasis = loopBarEmphasis(phase(), anchor);
                        Color color = theme().accentColor;
                        color.a = 0.30f + emphasis * 0.70f;
                        return color;
                    })
            );
        }

        return makeSectionCard(
            theme(), "Auto-Running Loop",
            "This panel starts a repeating autoreversing animation from body() and keeps the loop owned by the component scope.",
            VStack {
                .spacing = theme().space3,
                .children = children(
                    ZStack {
                        .horizontalAlignment = Alignment::Center,
                        .verticalAlignment = Alignment::Center,
                        .children = children(
                            Rectangle {}
                                .size(260.f, 104.f)
                                .cornerRadius(theme().radiusLarge)
                                .fill(Color::windowBackground())
                                .stroke(Color::separator(), 1.f),
                            HStack {
                                .spacing = theme().space2,
                                .alignment = Alignment::Center,
                                .children = std::move(bars),
                            }
                        )
                    }
,
                    HStack {
                        .spacing = theme().space3,
                        .alignment = Alignment::Stretch,
                        .children = children(
                            metricTile(theme(), [phase] {
                                return formatFloat(phase());
                            }, "Phase", Color::accent()),
                            metricTile(theme(), [] {
                                return std::string {"Looping"};
                            }, "Runtime state", Color::success())
                        )
                    }
                )
            }
        );
    }
};

struct AnimationDemoRoot {
    auto body() const {
        auto windowTheme = useEnvironment<ThemeKey>();

        Theme demoTheme = windowTheme();

        return ScrollView {
            .axis = ScrollAxis::Vertical,
            .children = children(
                VStack {
                    .spacing = demoTheme.space4,
                    .alignment = Alignment::Start,
                    .children = children(
                        Text {
                            .text = "Animation Demo",
                            .font = Font::largeTitle(),
                            .color = Color::primary(),
                            .horizontalAlignment = HorizontalAlignment::Leading,
                        },
                        Text {
                            .text = "Detailed useAnimated walkthrough: explicit play/set controls, repeat and autoreverse, synchronized WithTransition updates, and scoped frame callbacks.",
                            .font = Font::body(),
                            .color = Color::secondary(),
                            .horizontalAlignment = HorizontalAlignment::Leading,
                            .wrapping = TextWrapping::Wrap,
                        },
                        PlaybackLab {},
                        MorphLab {},
                        AmbientLoopLab {},
                        Text {
                            .text = "Tip: the playback panel demonstrates imperative control; the morph panel demonstrates scoped transitions; the ambient panel demonstrates a loop owned by the component body.",
                            .font = demoTheme.footnoteFont,
                            .color = demoTheme.tertiaryLabelColor,
                            .horizontalAlignment = HorizontalAlignment::Leading,
                            .wrapping = TextWrapping::Wrap,
                        }
                    )
                }
                    .padding(demoTheme.space5)
            )
        }
            .fill(demoTheme.windowBackgroundColor)
            .environment<ThemeKey>(demoTheme);
    }
};

} // namespace

int main(int argc, char *argv[]) {
    Application app(argc, argv);
    auto &w = app.createWindow<Window>({
        .size = {800, 800},
        .title = "Lambda - Animation demo",
        .resizable = true,
    });
    w.setView<AnimationDemoRoot>();
    return app.exec();
}
