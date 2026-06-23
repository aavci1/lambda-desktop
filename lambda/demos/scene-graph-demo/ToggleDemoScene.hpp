#pragma once

#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Core/Color.hpp>
#include <Lambda/Graphics/Path.hpp>
#include <Lambda/Graphics/TextLayoutOptions.hpp>
#include <Lambda/Graphics/TextSystem.hpp>
#include <Lambda/SceneGraph/SceneNode.hpp>
#include <Lambda/SceneGraph/PathNode.hpp>
#include <Lambda/SceneGraph/RectNode.hpp>
#include <Lambda/SceneGraph/TextNode.hpp>
#include <Lambda/UI/Theme.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lambdaui::examples::scenegraphdemo {

using scenegraph::PathNode;
using scenegraph::RectNode;
using scenegraph::SceneNode;
using scenegraph::TextNode;

constexpr Size kWindowSize {800.f, 960.f};

struct BuiltNode {
    std::unique_ptr<SceneNode> node;
    Size size {};
};

struct ToggleVisual {
    bool value = false;
    bool disabled = false;
    float trackWidth = 44.f;
    float trackHeight = 26.f;
    float thumbInset = 4.f;
    Color onColor = Colors::blue;
};

struct TextMeasure {
    std::shared_ptr<TextLayout const> layout;
    Size size {};
};

inline Color withAlpha(Color color, float alpha) {
    color.a = alpha;
    return color;
}

inline TextMeasure layoutText(TextSystem &textSystem, std::string_view text, Font const &font,
                              Color const &color, float maxWidth) {
    TextLayoutOptions const options {};
    Size const measuredSize = textSystem.measure(text, font, color, maxWidth, options);
    auto layout = textSystem.layout(
        text,
        font,
        color,
        Rect {0.f, 0.f, maxWidth > 0.f ? maxWidth : measuredSize.width, measuredSize.height},
        options
    );
    return {
        .layout = std::move(layout),
        .size = measuredSize,
    };
}

inline TextNode *appendTextNode(SceneNode &parent, Point position, TextMeasure text) {
    auto node = std::make_unique<TextNode>(
        Rect {position.x, position.y, text.size.width, text.size.height},
        std::move(text.layout)
    );
    TextNode *raw = node.get();
    parent.appendChild(std::move(node));
    return raw;
}

inline RectNode *appendRectNode(SceneNode &parent, Rect bounds, FillStyle fill = FillStyle::none(),
                                StrokeStyle stroke = StrokeStyle::none(),
                                CornerRadius cornerRadius = {},
                                ShadowStyle shadow = ShadowStyle::none()) {
    auto node = std::make_unique<RectNode>(bounds, fill, stroke, cornerRadius, shadow);
    RectNode *raw = node.get();
    parent.appendChild(std::move(node));
    return raw;
}

inline PathNode *appendPathNode(SceneNode &parent, Rect bounds, Path path,
                                FillStyle fill = FillStyle::none(),
                                StrokeStyle stroke = StrokeStyle::none(),
                                ShadowStyle shadow = ShadowStyle::none()) {
    auto node =
        std::make_unique<PathNode>(bounds, std::move(path), fill, stroke, shadow);
    PathNode *raw = node.get();
    parent.appendChild(std::move(node));
    return raw;
}

inline BuiltNode buildToggle(Theme const &theme, ToggleVisual visual) {
    auto toggle = std::make_unique<SceneNode>(Rect {0.f, 0.f, visual.trackWidth, visual.trackHeight});

    Color trackColor = visual.value ? visual.onColor : theme.toggleOffColor;
    if (visual.disabled) {
        trackColor = visual.value ? withAlpha(visual.onColor, 0.35f)
                                  : theme.disabledControlBackgroundColor;
    }

    StrokeStyle trackStroke = StrokeStyle::none();
    if (!visual.value && !visual.disabled && theme.toggleBorderWidth > 0.f) {
        trackStroke = StrokeStyle::solid(theme.toggleBorderColor, theme.toggleBorderWidth);
    }

    appendRectNode(
        *toggle,
        Rect {0.f, 0.f, visual.trackWidth, visual.trackHeight},
        FillStyle::solid(trackColor),
        trackStroke,
        CornerRadius {visual.trackHeight * 0.5f}
    );

    float const thumbSize = std::max(visual.trackHeight - 2.f * visual.thumbInset, 0.f);
    float const thumbX = visual.value ? visual.trackWidth - visual.thumbInset - thumbSize
                                      : visual.thumbInset;

    StrokeStyle thumbStroke = StrokeStyle::none();
    if (theme.toggleThumbBorderWidth > 0.f) {
        thumbStroke = StrokeStyle::solid(theme.toggleThumbBorderColor, theme.toggleThumbBorderWidth);
    }

    ShadowStyle thumbShadow = ShadowStyle::none();
    if (!visual.disabled) {
        thumbShadow = ShadowStyle {
            .radius = theme.shadowRadiusControl,
            .offset = {0.f, theme.shadowOffsetYControl},
            .color = theme.shadowColor,
        };
    }

    appendRectNode(
        *toggle,
        Rect {thumbX, visual.thumbInset, thumbSize, thumbSize},
        FillStyle::solid(theme.toggleThumbColor),
        thumbStroke,
        CornerRadius {thumbSize * 0.5f},
        thumbShadow
    );

    return {
        .node = std::move(toggle),
        .size = Size {visual.trackWidth, visual.trackHeight},
    };
}

inline BuiltNode buildSettingRow(TextSystem &textSystem, Theme const &theme, float width,
                                 std::string_view title, std::string_view detail,
                                 ToggleVisual toggleVisual) {
    float const padding = theme.space3;
    float const spacing = theme.space3;

    BuiltNode toggle = buildToggle(theme, toggleVisual);
    float const textWidth = std::max(width - padding * 2.f - spacing - toggle.size.width, 0.f);

    TextMeasure titleText =
        layoutText(textSystem, title, theme.headlineFont, theme.labelColor, textWidth);
    TextMeasure detailText =
        layoutText(textSystem, detail, theme.calloutFont, theme.secondaryLabelColor, textWidth);

    float const textStackHeight = titleText.size.height + theme.space1 + detailText.size.height;
    float const contentHeight = std::max(textStackHeight, toggle.size.height);
    float const height = padding * 2.f + contentHeight;

    auto row = std::make_unique<RectNode>(
        Rect {0.f, 0.f, width, height},
        FillStyle::solid(theme.windowBackgroundColor),
        StrokeStyle::none(),
        CornerRadius {theme.radiusMedium}
    );

    float const textTop = padding + (contentHeight - textStackHeight) * 0.5f;
    float const detailY = textTop + titleText.size.height + theme.space1;
    appendTextNode(*row, Point {padding, textTop}, std::move(titleText));
    appendTextNode(*row, Point {padding, detailY}, std::move(detailText));

    toggle.node->setPosition(Point {
        width - padding - toggle.size.width,
        padding + (contentHeight - toggle.size.height) * 0.5f,
    });
    row->appendChild(std::move(toggle.node));

    return {
        .node = std::move(row),
        .size = Size {width, height},
    };
}

inline BuiltNode buildMetricTile(TextSystem &textSystem, Theme const &theme, float width,
                                 std::string_view value, std::string_view label, Color accent) {
    float const padding = theme.space3;
    float const maxWidth = std::max(width - padding * 2.f, 0.f);

    TextMeasure valueText = layoutText(textSystem, value, theme.title2Font, accent, maxWidth);
    TextMeasure labelText =
        layoutText(textSystem, label, theme.footnoteFont, theme.secondaryLabelColor, maxWidth);

    float const height = padding * 2.f + valueText.size.height + theme.space1 + labelText.size.height;

    auto tile = std::make_unique<RectNode>(
        Rect {0.f, 0.f, width, height},
        FillStyle::solid(theme.windowBackgroundColor),
        StrokeStyle::none(),
        CornerRadius {theme.radiusMedium}
    );

    appendTextNode(*tile, Point {padding, padding}, std::move(valueText));
    appendTextNode(*tile, Point {padding, padding + valueText.size.height + theme.space1},
                   std::move(labelText));

    return {
        .node = std::move(tile),
        .size = Size {width, height},
    };
}

inline BuiltNode buildMetricsRow(TextSystem &textSystem, Theme const &theme, float width) {
    bool const wifiEnabled = true;
    bool const bluetoothEnabled = false;
    bool const syncEnabled = true;
    bool const notificationsEnabled = false;

    int const enabledCount = static_cast<int>(wifiEnabled) + static_cast<int>(bluetoothEnabled) +
                             static_cast<int>(syncEnabled) + static_cast<int>(notificationsEnabled);

    float const spacing = theme.space3;
    float const tileWidth = std::max((width - spacing * 2.f) / 3.f, 0.f);

    BuiltNode enabled = buildMetricTile(
        textSystem,
        theme,
        tileWidth,
        std::to_string(enabledCount),
        "Enabled settings",
        theme.accentColor
    );
    BuiltNode notifications = buildMetricTile(
        textSystem,
        theme,
        tileWidth,
        notificationsEnabled ? "Live" : "Quiet",
        "Notifications",
        notificationsEnabled ? theme.successColor : theme.warningColor
    );
    BuiltNode connectivity = buildMetricTile(
        textSystem,
        theme,
        tileWidth,
        wifiEnabled ? "Online" : "Offline",
        "Connectivity",
        wifiEnabled ? theme.successColor : theme.secondaryLabelColor
    );

    float const height =
        std::max(enabled.size.height, std::max(notifications.size.height, connectivity.size.height));

    auto row = std::make_unique<SceneNode>(Rect {0.f, 0.f, width, height});

    enabled.node->setPosition(Point {0.f, 0.f});
    notifications.node->setPosition(Point {tileWidth + spacing, 0.f});
    connectivity.node->setPosition(Point {(tileWidth + spacing) * 2.f, 0.f});

    row->appendChild(std::move(enabled.node));
    row->appendChild(std::move(notifications.node));
    row->appendChild(std::move(connectivity.node));

    return {
        .node = std::move(row),
        .size = Size {width, height},
    };
}

inline BuiltNode buildSectionCard(TextSystem &textSystem, Theme const &theme, float width,
                                  std::string_view title, std::string_view caption,
                                  std::vector<BuiltNode> contentNodes, float contentSpacing) {
    float const padding = theme.space4;
    float const maxWidth = std::max(width - padding * 2.f, 0.f);

    TextMeasure titleText = layoutText(textSystem, title, theme.title2Font, theme.labelColor, maxWidth);
    TextMeasure captionText =
        layoutText(textSystem, caption, theme.bodyFont, theme.secondaryLabelColor, maxWidth);

    float contentHeight = 0.f;
    for (std::size_t i = 0; i < contentNodes.size(); ++i) {
        contentHeight += contentNodes[i].size.height;
        if (i + 1 < contentNodes.size()) {
            contentHeight += contentSpacing;
        }
    }

    float const bodyTop = padding + titleText.size.height + theme.space3 + captionText.size.height;
    float const height = bodyTop + (contentNodes.empty() ? 0.f : theme.space3 + contentHeight) + padding;

    auto card = std::make_unique<RectNode>(
        Rect {0.f, 0.f, width, height},
        FillStyle::solid(theme.elevatedBackgroundColor),
        StrokeStyle::solid(theme.separatorColor, 1.f),
        CornerRadius {theme.radiusLarge},
        ShadowStyle {
            .radius = theme.shadowRadiusPopover,
            .offset = {0.f, theme.shadowOffsetYPopover},
            .color = theme.shadowColor,
        }
    );

    appendTextNode(*card, Point {padding, padding}, std::move(titleText));
    appendTextNode(*card, Point {padding, padding + titleText.size.height + theme.space3},
                   std::move(captionText));

    float cursorY = bodyTop;
    if (!contentNodes.empty()) {
        cursorY += theme.space3;
    }
    for (BuiltNode &content : contentNodes) {
        content.node->setPosition(Point {padding, cursorY});
        card->appendChild(std::move(content.node));
        cursorY += content.size.height + contentSpacing;
    }

    return {
        .node = std::move(card),
        .size = Size {width, height},
    };
}

inline std::unique_ptr<SceneNode> buildHeroAccent(Theme const &theme, float contentWidth) {
    constexpr float badgeSize = 74.f;
    constexpr float glyphSize = 30.f;

    auto accent = std::make_unique<SceneNode>(Rect {0.f, 0.f, badgeSize, badgeSize});

    auto *badge = appendRectNode(
        *accent,
        Rect {0.f, 0.f, badgeSize, badgeSize},
        FillStyle::solid(withAlpha(theme.successColor, 0.14f)),
        StrokeStyle::solid(withAlpha(theme.successColor, 0.42f), 1.f),
        CornerRadius {22.f}
    );
    badge->setOpacity(0.88f);
    badge->setTransform(Mat3::rotate(0.2f, Point {badgeSize * 0.5f, badgeSize * 0.5f}));

    appendRectNode(
        *badge,
        Rect {11.f, 11.f, badgeSize - 22.f, badgeSize - 22.f},
        FillStyle::solid(withAlpha(theme.successColor, 0.08f)),
        StrokeStyle::none(),
        CornerRadius {18.f}
    );

    Path glyph;
    glyph.moveTo(Point {4.f, 17.f});
    glyph.lineTo(Point {12.f, 25.f});
    glyph.lineTo(Point {26.f, 8.f});

    auto *check = appendPathNode(
        *badge,
        Rect {
            (badgeSize - glyphSize) * 0.5f,
            (badgeSize - glyphSize) * 0.5f,
            glyphSize,
            glyphSize,
        },
        std::move(glyph),
        FillStyle::none(),
        StrokeStyle::solid(theme.successColor, 4.f)
    );
    check->setTransform(Mat3::rotate(-0.12f, Point {glyphSize * 0.5f, glyphSize * 0.5f}));

    return accent;
}

inline std::unique_ptr<SceneNode> buildToggleDemoScene(TextSystem &textSystem, Theme const &theme) {
    float const viewportWidth = kWindowSize.width;
    float const viewportHeight = kWindowSize.height;
    float const outerPadding = theme.space5;
    float const contentWidth = viewportWidth - outerPadding * 2.f;

    auto root = std::make_unique<SceneNode>(Rect {0.f, 0.f, viewportWidth, viewportHeight});
    auto viewport = std::make_unique<RectNode>(Rect {0.f, 0.f, viewportWidth, viewportHeight});
    viewport->setClipsContents(true);

    auto content = std::make_unique<SceneNode>(Rect {0.f, 0.f, viewportWidth, viewportHeight});

    TextMeasure heroTitle =
        layoutText(textSystem, "Toggle Demo", theme.largeTitleFont, theme.labelColor, contentWidth);
    TextMeasure heroCaption = layoutText(
        textSystem,
        "A cleaner toggle showcase with realistic settings rows, styling variations, and compact control density.",
        theme.bodyFont,
        theme.secondaryLabelColor,
        contentWidth
    );

    std::vector<BuiltNode> preferenceRows;
    preferenceRows.push_back(buildSettingRow(
        textSystem,
        theme,
        contentWidth - theme.space4 * 2.f,
        "Wi-Fi",
        "Keep the workspace online for syncing and collaboration.",
        ToggleVisual {
            .value = true,
            .onColor = theme.toggleOnColor,
        }
    ));
    preferenceRows.push_back(buildSettingRow(
        textSystem,
        theme,
        contentWidth - theme.space4 * 2.f,
        "Bluetooth",
        "Enable accessory pairing for keyboards, trackpads, and audio.",
        ToggleVisual {
            .value = false,
            .onColor = theme.toggleOnColor,
        }
    ));
    preferenceRows.push_back(buildSettingRow(
        textSystem,
        theme,
        contentWidth - theme.space4 * 2.f,
        "Background sync",
        "This row is intentionally disabled to show the non-interactive state.",
        ToggleVisual {
            .value = true,
            .disabled = true,
            .onColor = theme.toggleOnColor,
        }
    ));
    preferenceRows.push_back(buildSettingRow(
        textSystem,
        theme,
        contentWidth - theme.space4 * 2.f,
        "Notifications",
        "Promote status changes without pushing users into a modal flow.",
        ToggleVisual {
            .value = false,
            .onColor = theme.toggleOnColor,
        }
    ));

    BuiltNode preferencesCard = buildSectionCard(
        textSystem,
        theme,
        contentWidth,
        "Preferences",
        "Toggles work best in quiet settings rows where the label carries the meaning and the switch only answers yes or no.",
        std::move(preferenceRows),
        theme.space2
    );

    std::vector<BuiltNode> stateContent;
    stateContent.push_back(buildMetricsRow(textSystem, theme, contentWidth - theme.space4 * 2.f));
    BuiltNode statesCard = buildSectionCard(
        textSystem,
        theme,
        contentWidth,
        "States",
        "A small summary helps show the control in a real context instead of as an isolated widget.",
        std::move(stateContent),
        theme.space2
    );

    std::vector<BuiltNode> stylingRows;
    stylingRows.push_back(buildSettingRow(
        textSystem,
        theme,
        contentWidth - theme.space4 * 2.f,
        "Success accent",
        "Useful when a toggle implies a positive enabled state.",
        ToggleVisual {
            .value = true,
            .onColor = theme.successColor,
        }
    ));
    stylingRows.push_back(buildSettingRow(
        textSystem,
        theme,
        contentWidth - theme.space4 * 2.f,
        "Compact density",
        "A narrower track works for table rows and denser settings surfaces.",
        ToggleVisual {
            .value = false,
            .trackWidth = 34.f,
            .trackHeight = 20.f,
            .thumbInset = 2.f,
            .onColor = theme.toggleOnColor,
        }
    ));

    BuiltNode stylingCard = buildSectionCard(
        textSystem,
        theme,
        contentWidth,
        "Styling",
        "Style tokens should support subtle variations without turning the control into a different component.",
        std::move(stylingRows),
        theme.space2
    );

    TextMeasure footer = layoutText(
        textSystem,
        "Try keyboard focus as well: Tab to a toggle, then use Space or Return.",
        theme.footnoteFont,
        theme.tertiaryLabelColor,
        contentWidth
    );

    float cursorY = outerPadding;
    appendTextNode(*content, Point {outerPadding, cursorY}, std::move(heroTitle));
    auto heroAccent = buildHeroAccent(theme, contentWidth);
    heroAccent->setPosition(Point {outerPadding + contentWidth - 74.f, cursorY - 4.f});
    content->appendChild(std::move(heroAccent));
    cursorY += heroTitle.size.height + theme.space3;

    appendTextNode(*content, Point {outerPadding, cursorY}, std::move(heroCaption));
    cursorY += heroCaption.size.height + theme.space4;

    preferencesCard.node->setPosition(Point {outerPadding, cursorY});
    content->appendChild(std::move(preferencesCard.node));
    cursorY += preferencesCard.size.height + theme.space4;

    statesCard.node->setPosition(Point {outerPadding, cursorY});
    content->appendChild(std::move(statesCard.node));
    cursorY += statesCard.size.height + theme.space4;

    stylingCard.node->setPosition(Point {outerPadding, cursorY});
    content->appendChild(std::move(stylingCard.node));
    cursorY += stylingCard.size.height + theme.space4;

    appendTextNode(*content, Point {outerPadding, cursorY}, std::move(footer));
    cursorY += footer.size.height + outerPadding;

    content->setSize(Size {viewportWidth, cursorY});

    viewport->appendChild(std::move(content));
    root->appendChild(std::move(viewport));
    return root;
}

} // namespace lambdaui::examples::scenegraphdemo
