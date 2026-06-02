#pragma once

/// \file Lambda/UI/Theme.hpp
///
/// Part of the Lambda public API.

#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Core/Color.hpp>
#include <Lambda/Graphics/Font.hpp>
#include <Lambda/Reactive/Computed.hpp>
#include <Lambda/Reactive/Signal.hpp>
#include <Lambda/UI/Environment.hpp>

#include <string>

namespace lambda {

struct Theme;

Font resolveFont(Font const &override, Font const &themeValue);
Font resolveFont(Font const &value, Theme const &theme);
Font resolveFont(Font const &override, Font const &themeValue, Theme const &theme);

/// Raw colour palette — 50 named swatches across 5 hues × 10 steps.
/// Used to build Theme presets; not consumed by components directly.
/// Step convention: 50 = lightest, 950 = darkest (same as Tailwind).
struct LambdaPalette {
    Color blue50 = Color::hex(0xEFF6FF);
    Color blue100 = Color::hex(0xDBEAFE);
    Color blue200 = Color::hex(0xBFD7FE);
    Color blue300 = Color::hex(0x93BBFD);
    Color blue400 = Color::hex(0x6098FA);
    Color blue500 = Color::hex(0x3A7BD5);
    Color blue600 = Color::hex(0x2563EB);
    Color blue700 = Color::hex(0x1D4ED8);
    Color blue800 = Color::hex(0x1E3FA8);
    Color blue900 = Color::hex(0x1E3A8A);

    Color red50 = Color::hex(0xFFF1F2);
    Color red100 = Color::hex(0xFFE4E6);
    Color red200 = Color::hex(0xFECDD3);
    Color red300 = Color::hex(0xFCA5A5);
    Color red400 = Color::hex(0xF87171);
    Color red500 = Color::hex(0xD94040);
    Color red600 = Color::hex(0xDC2626);
    Color red700 = Color::hex(0xB91C1C);
    Color red800 = Color::hex(0x991B1B);
    Color red900 = Color::hex(0x7F1D1D);

    Color green50 = Color::hex(0xF0FDF4);
    Color green100 = Color::hex(0xDCFCE7);
    Color green200 = Color::hex(0xBBF7D0);
    Color green300 = Color::hex(0x86EFAC);
    Color green400 = Color::hex(0x4ADE80);
    Color green500 = Color::hex(0x22C55E);
    Color green600 = Color::hex(0x16A34A);
    Color green700 = Color::hex(0x15803D);
    Color green800 = Color::hex(0x166534);
    Color green900 = Color::hex(0x14532D);

    Color amber50 = Color::hex(0xFFFBEB);
    Color amber100 = Color::hex(0xFEF3C7);
    Color amber200 = Color::hex(0xFDE68A);
    Color amber300 = Color::hex(0xFCD34D);
    Color amber400 = Color::hex(0xFBBF24);
    Color amber500 = Color::hex(0xF59E0B);
    Color amber600 = Color::hex(0xD97706);
    Color amber700 = Color::hex(0xB45309);
    Color amber800 = Color::hex(0x92400E);
    Color amber900 = Color::hex(0x78350F);

    Color neutral50 = Color::hex(0xF8F8FA);
    Color neutral100 = Color::hex(0xF2F2F7);
    Color neutral200 = Color::hex(0xE5E5EA);
    Color neutral300 = Color::hex(0xC8C8D0);
    Color neutral400 = Color::hex(0xAAAAAA);
    Color neutral500 = Color::hex(0x8E8E9A);
    Color neutral600 = Color::hex(0x6E6E80);
    Color neutral700 = Color::hex(0x48484A);
    Color neutral800 = Color::hex(0x3A3A3C);
    Color neutral900 = Color::hex(0x1C1C1E);
    Color neutral950 = Color::hex(0x111118);
};

struct Theme {
    Color accentColor = Color::hex(0x0A84FF);
    Color accentForegroundColor = Color::hex(0xFFFFFF);
    Color selectedContentBackgroundColor = Color {0.04f, 0.52f, 1.f, 0.18f};

    Color successColor = Color::hex(0x28CD41);
    Color successForegroundColor = Color::hex(0xFFFFFF);
    Color successBackgroundColor = Color {0.16f, 0.80f, 0.25f, 0.18f};

    Color warningColor = Color::hex(0xFF9F0A);
    Color warningForegroundColor = Color::hex(0x111118);
    Color warningBackgroundColor = Color {1.f, 0.62f, 0.04f, 0.18f};

    Color dangerColor = Color::hex(0xFF453A);
    Color dangerForegroundColor = Color::hex(0xFFFFFF);
    Color dangerBackgroundColor = Color {1.f, 0.27f, 0.23f, 0.18f};

    Color windowBackgroundColor = Color::hex(0xF4F4F7);
    Color controlBackgroundColor = Color::hex(0xFFFFFF);
    Color elevatedBackgroundColor = Color::hex(0xFFFFFF);
    Color textBackgroundColor = Color::hex(0xFFFFFF);
    Color hoveredControlBackgroundColor = Color::hex(0xFAFAFC);
    Color rowHoverBackgroundColor = Color::hex(0xF1F3F8);
    Color disabledControlBackgroundColor = Color::hex(0xE8E8ED);

    Color separatorColor = Color::hex(0xE5E5EA);
    Color opaqueSeparatorColor = Color::hex(0xD1D1D6);
    Color keyboardFocusIndicatorColor = Color::hex(0x0A84FF);

    Color labelColor = Color::hex(0x111118);
    Color secondaryLabelColor = Color::hex(0x6E6E73);
    Color tertiaryLabelColor = Color::hex(0x8E8E93);
    Color quaternaryLabelColor = Color::hex(0xAEAEB2);
    Color placeholderTextColor = Color::hex(0x8E8E93);
    Color disabledTextColor = Color::hex(0xAEAEB2);

    Color modalScrimColor = Color {0.f, 0.f, 0.f, 0.35f};
    Color popoverScrimColor = Color {0.f, 0.f, 0.f, 0.f};
    float modalBackdropBlurRadius = 18.f;
    float popoverBackdropBlurRadius = 0.f;

    /// Apple-style type roles. Use these directly or via `Font::...()` semantic tokens.
    Font largeTitleFont {.family = "", .size = 34.f, .weight = 400.f};
    Font titleFont {.family = "", .size = 28.f, .weight = 400.f};
    Font title2Font {.family = "", .size = 22.f, .weight = 400.f};
    Font title3Font {.family = "", .size = 20.f, .weight = 400.f};
    Font headlineFont {.family = "", .size = 13.f, .weight = 600.f};
    Font subheadlineFont {.family = "", .size = 12.f, .weight = 400.f};
    Font bodyFont {.family = "", .size = 13.f, .weight = 400.f};
    Font calloutFont {.family = "", .size = 12.f, .weight = 400.f};
    Font footnoteFont {.family = "", .size = 11.f, .weight = 400.f};
    Font captionFont {.family = "", .size = 11.f, .weight = 400.f};
    Font caption2Font {.family = "", .size = 10.f, .weight = 400.f};
    Font monospacedBodyFont {.family = "Menlo", .size = 13.f, .weight = 400.f};

    // Spacing scale (8 pt grid). At density 1.0, space3 is 12 pt, space4 is 16 pt, etc.
    // `withDensity(d)` scales space1–space8 by d (and updates paddingFieldH/V to match space3/space2).
    // After compact/comfortable, spaceN are no longer fixed “name = 12 pt” constants — always read from
    // `Theme` so layout participates in density; hardcoded literals (e.g. 24.f) do not.
    float space1 = 4.f;
    float space2 = 8.f;
    float space3 = 12.f;
    float space4 = 16.f;
    float space5 = 20.f;
    float space6 = 24.f;
    float space7 = 32.f;
    float space8 = 48.f;

    /// Multiplier recorded when using `withDensity`; spacing fields above include its effect after `withDensity`.
    float density = 1.0f;

    /// Horizontal / vertical padding for single-line fields; kept in sync with space3 / space2 by `withDensity`.
    float paddingFieldH = 12.f;
    float paddingFieldV = 8.f;

    // Corner radii (points). Not scaled by `withDensity` — shape stays stable across density presets.
    float radiusNone = 0.f;
    float radiusXSmall = 4.f;
    float radiusSmall = 6.f;
    float radiusMedium = 8.f;
    float radiusLarge = 10.f;
    float radiusXLarge = 14.f;
    float radiusFull = 9999.f;

    float controlHeightSmall = 28.f;
    float controlHeightMedium = 36.f;
    float controlHeightLarge = 44.f;

    float durationInstant = 0.00f;
    float durationFast = 0.10f;
    float durationMedium = 0.18f;
    float durationSlow = 0.30f;

    // Toggle
    float toggleTrackWidth = 44.f;
    float toggleTrackHeight = 26.f;
    float toggleThumbInset = 4.f;
    float toggleBorderWidth = 1.f;
    float toggleThumbBorderWidth = 0.f;
    Color toggleOnColor = Color::hex(0x0A84FF);
    Color toggleOffColor = Color::hex(0xD1D1D6);
    Color toggleThumbColor = Color::hex(0xFFFFFF);
    Color toggleThumbBorderColor = Color::hex(0xFFFFFF);
    Color toggleBorderColor = Color::hex(0xD1D1D6);

    // Checkbox
    float checkboxBoxSize = 20.f;
    float checkboxCornerRadius = 4.f;
    float checkboxBorderWidth = 2.0f;
    Color checkboxCheckedColor = Color::hex(0x0A84FF);
    Color checkboxUncheckedColor = Color::hex(0xD1D1D6);
    Color checkboxCheckColor = Color::hex(0xFFFFFF);
    Color checkboxBorderColor = Color::hex(0xD1D1D6);

    // Slider
    float sliderTrackHeight = 4.f;
    float sliderThumbSize = 20.f;

    // Dialog
    float dialogWidth = 440.f;
    float dialogHeaderSpacing = 8.f;
    float dialogContentSpacing = 22.f;
    float dialogFooterSpacing = 8.f;
    EdgeInsets dialogHeaderPadding {16.f, 20.f, 16.f, 20.f};
    EdgeInsets dialogContentPadding {18.f, 20.f, 20.f, 20.f};
    EdgeInsets dialogFooterPadding {12.f, 20.f, 12.f, 20.f};
    Font dialogTitleFont {.family = "", .size = 15.f, .weight = 600.f};
    Color dialogTitleColor = Color::hex(0x1A1A1A);
    Color dialogSurfaceColor = Color::hex(0xFFFFFF);
    Color dialogSurfaceStrokeColor = Color {0.f, 0.f, 0.f, 0.08f};
    Color dialogDividerColor = Color {0.f, 0.f, 0.f, 0.06f};
    Color dialogFooterColor = Color::hex(0xFAFAFA);
    float dialogSurfaceStrokeWidth = 1.f;
    float dialogDividerThickness = 1.f;
    float dialogCornerRadius = 12.f;
    float dialogShadowRadius = 40.f;
    float dialogShadowOffsetX = 0.f;
    float dialogShadowOffsetY = 12.f;
    Color dialogShadowColor = Color {0.f, 0.f, 0.f, 0.18f};
    float dialogCloseButtonSize = 26.f;
    float dialogCloseButtonCornerRadius = 6.f;
    float dialogCloseIconSize = 18.f;
    float dialogCloseIconWeight = 450.f;
    Color dialogCloseIconColor = Color {0.f, 0.f, 0.f, 0.55f};
    Color dialogCloseHoverColor = Color {0.f, 0.f, 0.f, 0.05f};

    /// Bundled Material Symbols Rounded (override to swap icon sets globally).
    std::string iconFontFamily = "Material Symbols Rounded";

    Color shadowColor = Color {0.f, 0.f, 0.f, 0.15f};
    /// Drop shadow blur radius (logical px) for control thumbs, buttons, etc.
    float shadowRadiusControl = 6.f;
    /// Drop shadow vertical offset (logical px); positive = downward.
    float shadowOffsetYControl = 0.f;
    /// Popover / tooltip card shadow (path fill uses offset pass; radius reserved for future blur).
    float shadowRadiusPopover = 4.f;
    float shadowOffsetYPopover = 3.f;

    static Theme light();
    static Theme dark();
    static Theme compact();
    static Theme comfortable();

    Theme withDensity(float d) const;

    bool operator==(Theme const& other) const = default;
};

} // namespace lambda
