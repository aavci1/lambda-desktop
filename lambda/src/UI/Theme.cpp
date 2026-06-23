#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/Environment.hpp>

#include <algorithm>

namespace lambdaui {

Font resolveFont(Font const& override, Font const& themeValue) {
  if (override.semanticToken() == 1) {
    return themeValue;
  }
  return override;
}

namespace {

Theme const& defaultTheme() {
  static Theme const fallback = Theme::light();
  return fallback;
}

Color resolveSemanticColorToken(int token, Theme const& theme) {
  switch (token) {
  case 1:
  case 2:
    return theme.labelColor;
  case 3:
    return theme.secondaryLabelColor;
  case 4:
    return theme.tertiaryLabelColor;
  case 5:
    return theme.quaternaryLabelColor;
  case 6:
    return theme.placeholderTextColor;
  case 7:
    return theme.disabledTextColor;
  case 8:
    return theme.accentColor;
  case 9:
    return theme.accentForegroundColor;
  case 10:
    return theme.windowBackgroundColor;
  case 11:
    return theme.controlBackgroundColor;
  case 12:
    return theme.elevatedBackgroundColor;
  case 13:
    return theme.textBackgroundColor;
  case 14:
    return theme.separatorColor;
  case 15:
    return theme.opaqueSeparatorColor;
  case 16:
    return theme.selectedContentBackgroundColor;
  case 17:
    return theme.keyboardFocusIndicatorColor;
  case 18:
    return theme.modalScrimColor;
  case 19:
    return theme.popoverScrimColor;
  case 20:
    return theme.successColor;
  case 21:
    return theme.successForegroundColor;
  case 22:
    return theme.successBackgroundColor;
  case 23:
    return theme.warningColor;
  case 24:
    return theme.warningForegroundColor;
  case 25:
    return theme.warningBackgroundColor;
  case 26:
    return theme.dangerColor;
  case 27:
    return theme.dangerForegroundColor;
  case 28:
    return theme.dangerBackgroundColor;
  default:
    return theme.labelColor;
  }
}

Font resolveSemanticFontToken(int token, Theme const& theme) {
  switch (token) {
  case 1:
  case 8:
    return theme.bodyFont;
  case 2:
    return theme.largeTitleFont;
  case 3:
    return theme.titleFont;
  case 4:
    return theme.title2Font;
  case 5:
    return theme.title3Font;
  case 6:
    return theme.headlineFont;
  case 7:
    return theme.subheadlineFont;
  case 9:
    return theme.calloutFont;
  case 10:
    return theme.footnoteFont;
  case 11:
    return theme.captionFont;
  case 12:
    return theme.caption2Font;
  case 13:
    return theme.monospacedBodyFont;
  default:
    return theme.bodyFont;
  }
}

} // namespace

Color Color::theme() {
  Color color = resolveSemanticColorToken(1, defaultTheme());
  color.semantic = 1;
  return color;
}

Color Color::primary() {
  Color color = resolveSemanticColorToken(2, defaultTheme());
  color.semantic = 2;
  return color;
}

Color Color::secondary() {
  Color color = resolveSemanticColorToken(3, defaultTheme());
  color.semantic = 3;
  return color;
}

Color Color::tertiary() {
  Color color = resolveSemanticColorToken(4, defaultTheme());
  color.semantic = 4;
  return color;
}

Color Color::quaternary() {
  Color color = resolveSemanticColorToken(5, defaultTheme());
  color.semantic = 5;
  return color;
}

Color Color::placeholder() {
  Color color = resolveSemanticColorToken(6, defaultTheme());
  color.semantic = 6;
  return color;
}

Color Color::disabled() {
  Color color = resolveSemanticColorToken(7, defaultTheme());
  color.semantic = 7;
  return color;
}

Color Color::accent() {
  Color color = resolveSemanticColorToken(8, defaultTheme());
  color.semantic = 8;
  return color;
}

Color Color::accentForeground() {
  Color color = resolveSemanticColorToken(9, defaultTheme());
  color.semantic = 9;
  return color;
}

Color Color::windowBackground() {
  Color color = resolveSemanticColorToken(10, defaultTheme());
  color.semantic = 10;
  return color;
}

Color Color::controlBackground() {
  Color color = resolveSemanticColorToken(11, defaultTheme());
  color.semantic = 11;
  return color;
}

Color Color::elevatedBackground() {
  Color color = resolveSemanticColorToken(12, defaultTheme());
  color.semantic = 12;
  return color;
}

Color Color::textBackground() {
  Color color = resolveSemanticColorToken(13, defaultTheme());
  color.semantic = 13;
  return color;
}

Color Color::separator() {
  Color color = resolveSemanticColorToken(14, defaultTheme());
  color.semantic = 14;
  return color;
}

Color Color::opaqueSeparator() {
  Color color = resolveSemanticColorToken(15, defaultTheme());
  color.semantic = 15;
  return color;
}

Color Color::selectedContentBackground() {
  Color color = resolveSemanticColorToken(16, defaultTheme());
  color.semantic = 16;
  return color;
}

Color Color::focusRing() {
  Color color = resolveSemanticColorToken(17, defaultTheme());
  color.semantic = 17;
  return color;
}

Color Color::scrim() {
  Color color = resolveSemanticColorToken(18, defaultTheme());
  color.semantic = 18;
  return color;
}

Color Color::popoverScrim() {
  Color color = resolveSemanticColorToken(19, defaultTheme());
  color.semantic = 19;
  return color;
}

Color Color::success() {
  Color color = resolveSemanticColorToken(20, defaultTheme());
  color.semantic = 20;
  return color;
}

Color Color::successForeground() {
  Color color = resolveSemanticColorToken(21, defaultTheme());
  color.semantic = 21;
  return color;
}

Color Color::successBackground() {
  Color color = resolveSemanticColorToken(22, defaultTheme());
  color.semantic = 22;
  return color;
}

Color Color::warning() {
  Color color = resolveSemanticColorToken(23, defaultTheme());
  color.semantic = 23;
  return color;
}

Color Color::warningForeground() {
  Color color = resolveSemanticColorToken(24, defaultTheme());
  color.semantic = 24;
  return color;
}

Color Color::warningBackground() {
  Color color = resolveSemanticColorToken(25, defaultTheme());
  color.semantic = 25;
  return color;
}

Color Color::danger() {
  Color color = resolveSemanticColorToken(26, defaultTheme());
  color.semantic = 26;
  return color;
}

Color Color::dangerForeground() {
  Color color = resolveSemanticColorToken(27, defaultTheme());
  color.semantic = 27;
  return color;
}

Color Color::dangerBackground() {
  Color color = resolveSemanticColorToken(28, defaultTheme());
  color.semantic = 28;
  return color;
}

Font Font::theme() {
  Font font = resolveSemanticFontToken(1, defaultTheme());
  font.semantic = 1;
  return font;
}

Font Font::largeTitle() {
  Font font = resolveSemanticFontToken(2, defaultTheme());
  font.semantic = 2;
  return font;
}

Font Font::title() {
  Font font = resolveSemanticFontToken(3, defaultTheme());
  font.semantic = 3;
  return font;
}

Font Font::title2() {
  Font font = resolveSemanticFontToken(4, defaultTheme());
  font.semantic = 4;
  return font;
}

Font Font::title3() {
  Font font = resolveSemanticFontToken(5, defaultTheme());
  font.semantic = 5;
  return font;
}

Font Font::headline() {
  Font font = resolveSemanticFontToken(6, defaultTheme());
  font.semantic = 6;
  return font;
}

Font Font::subheadline() {
  Font font = resolveSemanticFontToken(7, defaultTheme());
  font.semantic = 7;
  return font;
}

Font Font::body() {
  Font font = resolveSemanticFontToken(8, defaultTheme());
  font.semantic = 8;
  return font;
}

Font Font::callout() {
  Font font = resolveSemanticFontToken(9, defaultTheme());
  font.semantic = 9;
  return font;
}

Font Font::footnote() {
  Font font = resolveSemanticFontToken(10, defaultTheme());
  font.semantic = 10;
  return font;
}

Font Font::caption() {
  Font font = resolveSemanticFontToken(11, defaultTheme());
  font.semantic = 11;
  return font;
}

Font Font::caption2() {
  Font font = resolveSemanticFontToken(12, defaultTheme());
  font.semantic = 12;
  return font;
}

Font Font::monospacedBody() {
  Font font = resolveSemanticFontToken(13, defaultTheme());
  font.semantic = 13;
  return font;
}

Color resolveColor(Color value, Theme const& theme) {
  if (!value.isSemantic()) {
    return value;
  }
  return resolveSemanticColorToken(value.semanticToken(), theme);
}

Color resolveColor(Color override, Color themeValue, Theme const& theme) {
  int const token = override.semanticToken();
  if (token == 0) {
    return override;
  }
  if (token == 1) {
    return themeValue;
  }
  return resolveSemanticColorToken(token, theme);
}

Font resolveFont(Font const& value, Theme const& theme) {
  if (!value.isSemantic()) {
    return value;
  }
  return resolveSemanticFontToken(value.semanticToken(), theme);
}

Font resolveFont(Font const& override, Font const& themeValue, Theme const& theme) {
  int const token = override.semanticToken();
  if (token == 0) {
    return override;
  }
  if (token == 1) {
    return themeValue;
  }
  return resolveSemanticFontToken(token, theme);
}

Theme Theme::light() { return Theme{}; }

Theme Theme::dark() {
  Theme t;

  t.accentColor = Color::hex(0x0A84FF);
  t.accentForegroundColor = Color::hex(0xFFFFFF);
  t.selectedContentBackgroundColor = Color{0.04f, 0.52f, 1.f, 0.28f};

  t.successColor = Color::hex(0x32D74B);
  t.successForegroundColor = Color::hex(0x08120A);
  t.successBackgroundColor = Color{0.20f, 0.84f, 0.29f, 0.25f};

  t.warningColor = Color::hex(0xFFD60A);
  t.warningForegroundColor = Color::hex(0x111118);
  t.warningBackgroundColor = Color{1.f, 0.84f, 0.04f, 0.22f};

  t.dangerColor = Color::hex(0xFF6961);
  t.dangerForegroundColor = Color::hex(0x111118);
  t.dangerBackgroundColor = Color{1.f, 0.41f, 0.38f, 0.22f};

  t.windowBackgroundColor = Color::hex(0x1C1C1E);
  t.controlBackgroundColor = Color::hex(0x242426);
  t.elevatedBackgroundColor = Color::hex(0x2C2C2E);
  t.textBackgroundColor = Color::hex(0x2C2C2E);
  t.hoveredControlBackgroundColor = Color::hex(0x343437);
  t.rowHoverBackgroundColor = Color::hex(0x3A3A3C);
  t.disabledControlBackgroundColor = Color::hex(0x2A2A2C);

  t.separatorColor = Color::hex(0x3A3A3C);
  t.opaqueSeparatorColor = Color::hex(0x545458);
  t.keyboardFocusIndicatorColor = Color::hex(0x0A84FF);

  t.labelColor = Color::hex(0xF5F5F7);
  t.secondaryLabelColor = Color::hex(0xAEAEB2);
  t.tertiaryLabelColor = Color::hex(0x8E8E93);
  t.quaternaryLabelColor = Color::hex(0x636366);
  t.placeholderTextColor = Color::hex(0x6E6E73);
  t.disabledTextColor = Color::hex(0x636366);

  t.modalScrimColor = Color{0.f, 0.f, 0.f, 0.55f};
  t.popoverScrimColor = Color{0.f, 0.f, 0.f, 0.f};
  t.modalBackdropBlurRadius = 18.f;
  t.popoverBackdropBlurRadius = 0.f;

  t.toggleOnColor = t.accentColor;
  t.toggleOffColor = t.opaqueSeparatorColor;
  t.toggleBorderColor = t.separatorColor;

  t.checkboxCheckedColor = t.accentColor;
  t.checkboxUncheckedColor = t.opaqueSeparatorColor;
  t.checkboxBorderColor = t.opaqueSeparatorColor;

  t.dialogTitleColor = t.labelColor;
  t.dialogSurfaceColor = t.elevatedBackgroundColor;
  t.dialogSurfaceStrokeColor = Color{1.f, 1.f, 1.f, 0.10f};
  t.dialogDividerColor = t.separatorColor;
  t.dialogFooterColor = t.controlBackgroundColor;
  t.dialogShadowColor = Color{0.f, 0.f, 0.f, 0.45f};
  t.dialogCloseIconColor = t.secondaryLabelColor;
  t.dialogCloseHoverColor = Color{1.f, 1.f, 1.f, 0.08f};

  t.shadowColor = Color{0.f, 0.f, 0.f, 0.35f};

  return t;
}

Theme Theme::compact() { return Theme::light().withDensity(0.75f); }

Theme Theme::comfortable() { return Theme::light().withDensity(1.25f); }

namespace {

// Nominal 8 pt grid at density = 1.0 (matches `Theme` defaults in Theme.hpp).
inline constexpr float kSpace1 = 4.f;
inline constexpr float kSpace2 = 8.f;
inline constexpr float kSpace3 = 12.f;
inline constexpr float kSpace4 = 16.f;
inline constexpr float kSpace5 = 20.f;
inline constexpr float kSpace6 = 24.f;
inline constexpr float kSpace7 = 32.f;
inline constexpr float kSpace8 = 48.f;

EdgeInsets scaleInsets(EdgeInsets insets, float scale) {
  return EdgeInsets{
      insets.top * scale,
      insets.right * scale,
      insets.bottom * scale,
      insets.left * scale,
  };
}

} // namespace

Theme Theme::withDensity(float d) const {
  Theme t = *this;
  t.density = d;
  // Scale the full space scale so components using `theme.spaceN` (Button, layouts, etc.)
  // participate in compact/comfortable, not only paddingFieldH/V.
  // radius* and typography tokens are left unchanged; motion/icon tokens unchanged.
  t.space1 = kSpace1 * d;
  t.space2 = kSpace2 * d;
  t.space3 = kSpace3 * d;
  t.space4 = kSpace4 * d;
  t.space5 = kSpace5 * d;
  t.space6 = kSpace6 * d;
  t.space7 = kSpace7 * d;
  t.space8 = kSpace8 * d;
  t.paddingFieldH = t.space3;
  t.paddingFieldV = t.space2;
  t.controlHeightSmall = std::max(24.f, 28.f * d);
  t.controlHeightMedium = std::max(28.f, 36.f * d);
  t.controlHeightLarge = std::max(36.f, 44.f * d);
  t.dialogHeaderSpacing = dialogHeaderSpacing * d;
  t.dialogContentSpacing = dialogContentSpacing * d;
  t.dialogFooterSpacing = dialogFooterSpacing * d;
  t.dialogHeaderPadding = scaleInsets(dialogHeaderPadding, d);
  t.dialogContentPadding = scaleInsets(dialogContentPadding, d);
  t.dialogFooterPadding = scaleInsets(dialogFooterPadding, d);
  return t;
}

} // namespace lambdaui
