#include <Lambda.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/UI.hpp>
#include <Lambda/UI/Views/Card.hpp>
#include <Lambda/UI/Views/HStack.hpp>
#include <Lambda/UI/Views/Rectangle.hpp>
#include <Lambda/UI/Views/ScrollView.hpp>
#include <Lambda/UI/Views/Text.hpp>
#include <Lambda/UI/Views/VStack.hpp>
#include <Lambda/UI/Views/ZStack.hpp>

#include <string>

using namespace lambdaui;

namespace {

char const *kWrapSample =
    "Lambda uses the same text constraints for measurement and layout, so wrapped paragraphs reflow "
    "predictably as the window changes size.";

char const *kLongToken =
    "Supercalifragilisticexpialidocious_pseudopseudohypoparathyroidism_rendering_pipeline";

Element sectionCard(Theme const &theme, std::string title, std::string body, Element content) {
    return Card {
        .child = VStack {
            .spacing = theme.space3,
            .alignment = Alignment::Start,
            .children = children(
                Text {
                    .text = std::move(title),
                    .font = Font::title2(),
                    .color = Color::primary(),
                },
                Text {
                    .text = std::move(body),
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

Element alignmentBand(Theme const &theme, std::string label, HorizontalAlignment alignment) {
    Element band = Element {ZStack {
                                .children = children(
                                    Rectangle {}
                                        .height(44.f)
                                        .fill(FillStyle::solid(theme.hoveredControlBackgroundColor))
                                        .stroke(StrokeStyle::solid(Color::separator(), 1.f))
                                        .cornerRadius(theme.radiusMedium),
                                    Text {
                                        .text = "Alignment sample",
                                        .font = Font::body(),
                                        .color = Color::primary(),
                                        .horizontalAlignment = alignment,
                                        .verticalAlignment = VerticalAlignment::Center,
                                    }
                                        .flex(1.f, 0.f, 0.f)
                                        .size(0.f, 44.f)
                                )
                            }}
                       .size(0.f, 44.f);

    return VStack {
        .spacing = theme.space2,
        .alignment = Alignment::Start,
        .children = children(
            Text {
                .text = std::move(label),
                .font = Font::caption(),
                .color = Color::tertiary(),
            },
            std::move(band)
        )
    }
        .flex(1.f);
}

Element wrappingExamples(Theme const &theme) {
    return VStack {
        .spacing = theme.space3,
        .alignment = Alignment::Start,
        .children = children(
            Text {
                .text = "Wrap",
                .font = Font::headline(),
                .color = Color::primary(),
            },
            Text {
                .text = kWrapSample,
                .font = Font::body(),
                .color = Color::primary(),
                .wrapping = TextWrapping::Wrap,
            }
                .padding(theme.space3)
                .fill(FillStyle::solid(theme.hoveredControlBackgroundColor))
                .stroke(StrokeStyle::solid(Color::separator(), 1.f))
                .cornerRadius(theme.radiusMedium),
            Text {
                .text = "NoWrap",
                .font = Font::headline(),
                .color = Color::primary(),
            },
            Text {
                .text = kLongToken,
                .font = Font::footnote(),
                .color = Color::secondary(),
                .wrapping = TextWrapping::NoWrap,
            }
                .padding(theme.space3)
                .fill(FillStyle::solid(theme.hoveredControlBackgroundColor))
                .stroke(StrokeStyle::solid(Color::separator(), 1.f))
                .cornerRadius(theme.radiusMedium),
            Text {
                .text = "WrapAnywhere",
                .font = Font::headline(),
                .color = Color::primary(),
            },
            Text {
                .text = kLongToken,
                .font = Font::footnote(),
                .color = Color::secondary(),
                .wrapping = TextWrapping::WrapAnywhere,
            }
                .padding(theme.space3)
                .fill(FillStyle::solid(theme.hoveredControlBackgroundColor))
                .stroke(StrokeStyle::solid(Color::separator(), 1.f))
                .cornerRadius(theme.radiusMedium)
        )
    };
}

} // namespace

struct TextDemoRoot {
    auto body() const {
        auto theme = useEnvironment<ThemeKey>();

        Element intro = VStack {
            .spacing = theme().space3,
            .alignment = Alignment::Start,
            .children = children(
                Text {
                    .text = "Text Demo",
                    .font = Font::largeTitle(),
                    .color = Color::primary(),
                },
                Text {
                    .text = "A compact tour of wrapping, alignment, truncation, and semantic emphasis.",
                    .font = Font::body(),
                    .color = Color::secondary(),
                    .wrapping = TextWrapping::Wrap,
                }
            )
        };

        Element alignmentSection = sectionCard(
            theme(), "Alignment", "The same text view can be positioned differently inside its layout box.",
            HStack {
                .spacing = theme().space3,
                .alignment = Alignment::Start,
                .children = children(
                    alignmentBand(theme(), "Leading", HorizontalAlignment::Leading),
                    alignmentBand(theme(), "Center", HorizontalAlignment::Center),
                    alignmentBand(theme(), "Trailing", HorizontalAlignment::Trailing)
                )
            }
        );

        Element wrappingSection =
            sectionCard(theme(), "Wrapping Modes",
                        "These examples show the three supported wrapping behaviors under the same width.",
                        wrappingExamples(theme()));

        Element maxLinesSection = sectionCard(
            theme(), "Line Limits",
            "Use maxLines to keep a layout compact while leaving measurement and line geometry consistent.",
            VStack {
                .spacing = theme().space3,
                .alignment = Alignment::Start,
                .children = children(
                    Text {
                        .text = "Full paragraph",
                        .font = Font::headline(),
                        .color = Color::primary(),
                    },
                    Text {
                        .text = std::string(kWrapSample) + " " + kWrapSample,
                        .font = Font::body(),
                        .color = Color::primary(),
                        .wrapping = TextWrapping::Wrap,
                    }
                        .padding(theme().space3)
                        .fill(FillStyle::solid(theme().hoveredControlBackgroundColor))
                        .stroke(StrokeStyle::solid(Color::separator(), 1.f))
                        .cornerRadius(theme().radiusMedium),
                    Text {
                        .text = "Same text with maxLines = 2",
                        .font = Font::headline(),
                        .color = Color::primary(),
                    },
                    Text {
                        .text = std::string(kWrapSample) + " " + kWrapSample,
                        .font = Font::body(),
                        .color = Color::primary(),
                        .wrapping = TextWrapping::Wrap,
                        .maxLines = 2,
                    }
                        .padding(theme().space3)
                        .fill(FillStyle::solid(theme().hoveredControlBackgroundColor))
                        .stroke(StrokeStyle::solid(Color::separator(), 1.f))
                        .cornerRadius(theme().radiusMedium)
                )
            }
        );

        Element emphasisSection = sectionCard(
            theme(), "Semantic Emphasis",
            "Text styles and semantic colors are meant to be mixed deliberately, not just resized mechanically.",
            HStack {
                .spacing = theme().space3,
                .alignment = Alignment::Start,
                .children = children(
                    VStack {
                        .spacing = theme().space2,
                        .alignment = Alignment::Start,
                        .children = children(
                            Text {.text = "Primary", .font = Font::body(), .color = Color::primary()},
                            Text {.text = "Secondary", .font = Font::body(), .color = Color::secondary()},
                            Text {.text = "Muted", .font = Font::body(), .color = Color::tertiary()}
                        )
                    }
                        .flex(1.f),
                    VStack {
                        .spacing = theme().space2,
                        .alignment = Alignment::Start,
                        .children = children(
                            Text {.text = "Accent", .font = Font::body(), .color = Color::accent()},
                            Text {.text = "Success", .font = Font::body(), .color = Color::success()},
                            Text {.text = "Danger", .font = Font::body(), .color = Color::danger()}
                        )
                    }
                        .flex(1.f)
                )
            }
        );

        Element content = VStack {
            .spacing = theme().space5,
            .alignment = Alignment::Start,
            .children = children(
                std::move(intro),
                std::move(alignmentSection),
                std::move(wrappingSection),
                std::move(maxLinesSection),
                std::move(emphasisSection)
            )
        }
                              .padding(theme().space5);

        return ScrollView {
            .axis = ScrollAxis::Vertical,
            .children = children(std::move(content)),
        }
            .fill(FillStyle::solid(Color::windowBackground()));
    }
};

int main(int argc, char *argv[]) {
    Application app(argc, argv);

    auto &w = app.createWindow<Window>({
        .size = {800, 800},
        .title = "Lambda — Text Demo",
        .resizable = true,
    });

    w.setView<TextDemoRoot>();
    return app.exec();
}
