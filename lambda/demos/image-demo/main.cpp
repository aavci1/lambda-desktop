#include <Lambda.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/UI.hpp>
#include <Lambda/UI/Views/Views.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace lambdaui;

namespace {

std::string defaultImagePath() {
    namespace fs = std::filesystem;
    return fs::absolute(fs::path(__FILE__).parent_path() / "test.png").string();
}

std::string fileNameForPath(std::string const &path) {
    namespace fs = std::filesystem;
    return fs::path(path).filename().string();
}

std::string pixelSizeLabel(Size size) {
    int const width = static_cast<int>(std::round(size.width));
    int const height = static_cast<int>(std::round(size.height));
    return std::to_string(width) + " × " + std::to_string(height);
}

std::string aspectRatioLabel(Size size) {
    if (size.width <= 0.f || size.height <= 0.f) {
        return "Unknown";
    }
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.2f : 1", static_cast<double>(size.width / size.height));
    return buffer;
}

Color withAlpha(Color color, float alpha) {
    color.a = alpha;
    return color;
}

Element makeSectionCard(Theme const &theme, std::string title, std::string caption, Element content) {
    return Card{
        .child = VStack{
            .spacing = theme.space3,
            .alignment = Alignment::Start,
            .children = children(
                Text{
                    .text = std::move(title),
                    .font = Font::title2(),
                    .color = Color::primary(),
                    .horizontalAlignment = HorizontalAlignment::Leading,
                },
                Text{
                    .text = std::move(caption),
                    .font = Font::footnote(),
                    .color = Color::secondary(),
                    .horizontalAlignment = HorizontalAlignment::Leading,
                    .wrapping = TextWrapping::Wrap,
                },
                std::move(content))
        },
        .style = Card::Style{
            .padding = theme.space4,
            .cornerRadius = theme.radiusLarge,
        },
    };
}

Element makeMetricTile(Theme const &theme, std::string value, std::string label, Color accent) {
    return VStack{
        .spacing = theme.space1,
        .alignment = Alignment::Start,
        .children = children(
            Text{
                .text = std::move(value),
                .font = Font::title2(),
                .color = accent,
                .horizontalAlignment = HorizontalAlignment::Leading,
            },
            Text{
                .text = std::move(label),
                .font = Font::footnote(),
                .color = Color::secondary(),
                .horizontalAlignment = HorizontalAlignment::Leading,
            })
    }
        .padding(theme.space3)
        .fill(FillStyle::solid(Color::windowBackground()))
        .cornerRadius(CornerRadius{theme.radiusMedium})
        .flex(1.f, 1.f, 0.f);
}

Element makeBadge(Theme const &theme, std::string label, Color fill, Color textColor) {
    return Text{
        .text = std::move(label),
        .font = Font::caption(),
        .color = textColor,
        .horizontalAlignment = HorizontalAlignment::Center,
        .verticalAlignment = VerticalAlignment::Center,
    }
        .padding(6.f, 10.f, 6.f, 10.f)
        .fill(FillStyle::solid(fill))
        .cornerRadius(CornerRadius{theme.radiusFull});
}

Element makeImageSurface(Theme const &theme, std::shared_ptr<lambdaui::Image> const &image,
                         ImageFillMode fillMode, float height, std::optional<Element> overlay = std::nullopt) {
    Element media = Element{views::Image{
        .source = image,
        .fillMode = fillMode,
    }}
                        .height(height)
                        .cornerRadius(CornerRadius{theme.radiusMedium});

    if (overlay.has_value()) {
        media = ZStack{
            .children = children(
                std::move(media),
                VStack{
                    .children = children(
                        Spacer{},
                        HStack{
                            .alignment = Alignment::End,
                            .children = children(
                                std::move(*overlay),
                                Spacer{})
                        })
                }
                    .padding(theme.space3)
                    .height(height))
        };
    }

    return ZStack{
        .children = children(
            Rectangle{}
                .fill(FillStyle::solid(Color::windowBackground()))
                .stroke(StrokeStyle::solid(Color::separator(), 1.f))
                .cornerRadius(CornerRadius{theme.radiusMedium}),
            std::move(media))
    }
        .height(height);
}

Element makeModeTile(Theme const &theme, std::shared_ptr<lambdaui::Image> const &image, std::string title,
                     std::string caption, ImageFillMode fillMode, float height = 168.f) {
    return VStack{
        .spacing = theme.space2,
        .alignment = Alignment::Start,
        .children = children(
            makeImageSurface(theme, image, fillMode, height),
            Text{
                .text = std::move(title),
                .font = Font::headline(),
                .color = Color::primary(),
                .horizontalAlignment = HorizontalAlignment::Leading,
            },
            Text{
                .text = std::move(caption),
                .font = Font::footnote(),
                .color = Color::secondary(),
                .horizontalAlignment = HorizontalAlignment::Leading,
                .wrapping = TextWrapping::Wrap,
            })
    }
        .padding(theme.space3)
        .fill(FillStyle::solid(Color::windowBackground()))
        .cornerRadius(CornerRadius{theme.radiusMedium});
}

Element makeUsageRow(Theme const &theme, std::shared_ptr<lambdaui::Image> const &image, std::string title,
                     std::string caption, ImageFillMode fillMode, float thumbWidth) {
    return HStack{
        .spacing = theme.space3,
        .alignment = Alignment::Center,
        .children = children(
            makeImageSurface(theme, image, fillMode, 72.f)
                .width(thumbWidth)
                .flex(0.f, 0.f, thumbWidth),
            VStack{
                .spacing = theme.space1,
                .alignment = Alignment::Start,
                .children = children(
                    Text{
                        .text = std::move(title),
                        .font = Font::headline(),
                        .color = Color::primary(),
                        .horizontalAlignment = HorizontalAlignment::Leading,
                    },
                    Text{
                        .text = std::move(caption),
                        .font = Font::footnote(),
                        .color = Color::secondary(),
                        .horizontalAlignment = HorizontalAlignment::Leading,
                        .wrapping = TextWrapping::Wrap,
                    })
            }
                .flex(1.f, 1.f, 0.f))
    }
        .padding(theme.space3)
        .fill(FillStyle::solid(Color::windowBackground()))
        .cornerRadius(CornerRadius{theme.radiusMedium});
}

struct ImageDemoRoot {
    std::shared_ptr<lambdaui::Image> image;
    std::string imagePath;

    Element body() const {
        auto theme = useEnvironment<ThemeKey>();

        if (!image) {
            return ScrollView{
                .axis = ScrollAxis::Vertical,
                .children = children(
                    VStack{
                        .spacing = theme().space4,
                        .children = children(
                            Text{
                                .text = "Image Demo",
                                .font = Font::largeTitle(),
                                .color = Color::primary(),
                                .horizontalAlignment = HorizontalAlignment::Leading,
                            },
                            makeSectionCard(
                                theme(), "Image load failed",
                                "The demo needs a bitmap asset to render the image element showcase.",
                                VStack{
                                    .spacing = theme().space2,
                                    .alignment = Alignment::Start,
                                    .children = children(
                                        Text{
                                            .text = "Tried to load: " + imagePath,
                                            .font = Font::body(),
                                            .color = Color::secondary(),
                                            .horizontalAlignment = HorizontalAlignment::Leading,
                                            .wrapping = TextWrapping::Wrap,
                                        },
                                        Text{
                                            .text = "Pass a different path as the first CLI argument to preview another image.",
                                            .font = Font::footnote(),
                                            .color = Color::tertiary(),
                                            .horizontalAlignment = HorizontalAlignment::Leading,
                                            .wrapping = TextWrapping::Wrap,
                                        })
                                }))
                    }
                        .padding(theme().space5))
            };
        }

        Size const size = image->size();
        std::vector<Element> modeTiles;
        modeTiles.reserve(5);
        modeTiles.emplace_back(makeModeTile(
            theme(), image, "Cover",
            "Fills the frame edge to edge and crops when the aspect ratio does not match.",
            ImageFillMode::Cover));
        modeTiles.emplace_back(makeModeTile(
            theme(), image, "Fit",
            "Preserves the whole image inside the frame and leaves empty space when needed.",
            ImageFillMode::Fit));
        modeTiles.emplace_back(makeModeTile(
            theme(), image, "Stretch",
            "Matches the frame exactly and ignores the source aspect ratio.",
            ImageFillMode::Stretch));
        modeTiles.emplace_back(makeModeTile(
            theme(), image, "Center",
            "Keeps the image at its natural size and centers it without scaling.",
            ImageFillMode::Center));
        modeTiles.emplace_back(makeModeTile(
            theme(), image, "Tile",
            "Repeats the source to turn a single image asset into a textured surface.",
            ImageFillMode::Tile, 120.f));

        Element heroOverlay = VStack{
            .spacing = theme().space2,
            .alignment = Alignment::Start,
            .children = children(
                makeBadge(theme(), "Cover hero", withAlpha(Color::accent(), 0.18f), Colors::white),
                Text{
                    .text = fileNameForPath(imagePath),
                    .font = Font::title3(),
                    .color = Colors::white,
                    .horizontalAlignment = HorizontalAlignment::Leading,
                },
                Text{
                    .text = "Image views inherit the same rounded corners, overlays, shadows, and layout behavior as the rest of the UI primitives.",
                    .font = Font::footnote(),
                    .color = Color{1.f, 1.f, 1.f, 0.82f},
                    .horizontalAlignment = HorizontalAlignment::Leading,
                    .wrapping = TextWrapping::Wrap,
                })
        };

        Element metrics = HStack{
            .spacing = theme().space3,
            .alignment = Alignment::Stretch,
            .children = children(
                makeMetricTile(theme(), pixelSizeLabel(size), "Source size", Color::accent()),
                makeMetricTile(theme(), aspectRatioLabel(size), "Aspect ratio", Color::success()),
                makeMetricTile(theme(), "5", "Image fill modes", Color::warning()))
        };

        Element heroSection = makeSectionCard(
            theme(), "Hero Treatment",
            "Large editorial-style media blocks are mostly just a cover image plus ordinary layout elements layered on top.",
            VStack{
                .spacing = theme().space3,
                .children = children(
                    makeImageSurface(theme(), image, ImageFillMode::Cover, 248.f, heroOverlay)
                        .shadow(ShadowStyle{
                            .radius = theme().shadowRadiusPopover + 6.f,
                            .offset = {0.f, theme().shadowOffsetYPopover + 2.f},
                            .color = withAlpha(theme().shadowColor, std::max(0.18f, theme().shadowColor.a)),
                        }),
                    Text{
                        .text = imagePath,
                        .font = Font::footnote(),
                        .color = Color::tertiary(),
                        .horizontalAlignment = HorizontalAlignment::Leading,
                        .wrapping = TextWrapping::Wrap,
                    })
            });

        Element fillModesSection = makeSectionCard(
            theme(), "Fill Modes",
            "These use the same image source and the same `views::Image` element, changing only the fill mode.",
            Grid{
                .columns = 2,
                .horizontalSpacing = theme().space3,
                .verticalSpacing = theme().space3,
                .children = std::move(modeTiles),
            });

        Element styledVariationsSection = makeSectionCard(
            theme(), "Styled Variations",
            "Because image is just another element, it composes with modifiers and neighboring content instead of needing a special container type.",
            Grid{
                .columns = 2,
                .horizontalSpacing = theme().space3,
                .verticalSpacing = theme().space3,
                .children = children(
                    VStack{
                        .spacing = theme().space2,
                        .alignment = Alignment::Start,
                        .children = children(
                            makeImageSurface(
                                theme(), image, ImageFillMode::Fit, 180.f,
                                makeBadge(theme(), "Rounded fit", withAlpha(Color::success(), 0.18f), Colors::white))
                                .padding(theme().space2)
                                .fill(FillStyle::solid(Color::windowBackground()))
                                .stroke(StrokeStyle::solid(Color::separator(), 1.f))
                                .cornerRadius(CornerRadius{theme().radiusMedium}),
                            Text{
                                .text = "Inset framing keeps the full asset visible inside a more editorial card treatment.",
                                .font = Font::footnote(),
                                .color = Color::secondary(),
                                .horizontalAlignment = HorizontalAlignment::Leading,
                                .wrapping = TextWrapping::Wrap,
                            })
                    }
                        .padding(theme().space3)
                        .fill(FillStyle::solid(Color::windowBackground()))
                        .cornerRadius(CornerRadius{theme().radiusMedium}),
                    VStack{
                        .spacing = theme().space2,
                        .alignment = Alignment::Start,
                        .children = children(
                            ZStack{
                                .children = children(
                                    makeImageSurface(theme(), image, ImageFillMode::Cover, 180.f),
                                    Rectangle{}
                                        .fill(FillStyle::solid(Color{0.f, 0.f, 0.f, 0.26f}))
                                        .cornerRadius(CornerRadius{theme().radiusMedium})
                                        .height(180.f),
                                    VStack{
                                        .children = children(
                                            Spacer{},
                                            Text{
                                                .text = "Overlay labels, focus rings, and other chrome can sit directly on top of the image view.",
                                                .font = Font::footnote(),
                                                .color = Colors::white,
                                                .horizontalAlignment = HorizontalAlignment::Leading,
                                                .wrapping = TextWrapping::Wrap,
                                            })
                                    }
                                        .padding(theme().space3)
                                        .height(180.f))
                            },
                            Text{
                                .text = "This uses ordinary stack composition rather than a special-purpose image card widget.",
                                .font = Font::footnote(),
                                .color = Color::secondary(),
                                .horizontalAlignment = HorizontalAlignment::Leading,
                                .wrapping = TextWrapping::Wrap,
                            })
                    }
                        .padding(theme().space3)
                        .fill(FillStyle::solid(Color::windowBackground()))
                        .cornerRadius(CornerRadius{theme().radiusMedium}))
            });

        Element usageSection = makeSectionCard(
            theme(), "In Product Layouts",
            "The same element also works in denser UI rows, not just large hero surfaces.",
            VStack{
                .spacing = theme().space3,
                .children = children(
                    makeUsageRow(
                        theme(), image, "Library card thumb",
                        "A cover crop works well when the row needs a strong visual anchor and the image can absorb some cropping.",
                        ImageFillMode::Cover, 116.f),
                    makeUsageRow(
                        theme(), image, "Inspection pane preview",
                        "Fit mode is a better default when the viewer needs to see the whole asset without losing context at the edges.",
                        ImageFillMode::Fit, 116.f),
                    makeUsageRow(
                        theme(), image, "Decorative texture strip",
                        "Tile mode can turn one file into a repeating background accent without additional assets.",
                        ImageFillMode::Tile, 116.f))
            });

        return ScrollView{
            .axis = ScrollAxis::Vertical,
            .children = children(
                VStack{
                    .spacing = theme().space4,
                    .children = children(
                        Text{
                            .text = "Image Demo",
                            .font = Font::largeTitle(),
                            .color = Color::primary(),
                            .horizontalAlignment = HorizontalAlignment::Leading,
                        },
                        Text{
                            .text = "A styled showcase for the image element: fill modes, chrome, overlays, and everyday layout patterns built on the same view system as the other demos.",
                            .font = Font::body(),
                            .color = Color::secondary(),
                            .horizontalAlignment = HorizontalAlignment::Leading,
                            .wrapping = TextWrapping::Wrap,
                        },
                        std::move(metrics),
                        std::move(heroSection),
                        std::move(fillModesSection),
                        std::move(styledVariationsSection),
                        std::move(usageSection))
                }
                    .padding(theme().space5))
        };
    }
};

} // namespace

int main(int argc, char *argv[]) {
    Application app(argc, argv);

    std::string const imagePath = argc > 1 ? std::string(argv[1]) : defaultImagePath();
    std::shared_ptr<lambdaui::Image> image = loadImage(imagePath);

    auto &w = app.createWindow<Window>({
        .size = {960, 920},
        .title = "Lambda - Image demo",
        .resizable = true,
    });
    w.setView(ImageDemoRoot{
        .image = std::move(image),
        .imagePath = imagePath,
    });
    return app.exec();
}
