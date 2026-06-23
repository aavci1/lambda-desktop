#include <Lambda/UI/Views/Dialog.hpp>

#include <Lambda/UI/Hooks.hpp>
#include <Lambda/UI/IconName.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/Views/HStack.hpp>
#include <Lambda/UI/Views/Icon.hpp>
#include <Lambda/UI/Views/Rectangle.hpp>
#include <Lambda/UI/Views/ScaleAroundCenter.hpp>
#include <Lambda/UI/Views/Text.hpp>
#include <Lambda/UI/Views/VStack.hpp>
#include <Lambda/UI/Views/ZStack.hpp>

#include <utility>

namespace lambdaui {

namespace {

EdgeInsets resolveInsets(EdgeInsets insets, EdgeInsets themeInsets) {
  return EdgeInsets{
      resolveFloat(insets.top, themeInsets.top),
      resolveFloat(insets.right, themeInsets.right),
      resolveFloat(insets.bottom, themeInsets.bottom),
      resolveFloat(insets.left, themeInsets.left),
  };
}

Dialog::Style resolveStyle(Dialog::Style const& style, Theme const& theme) {
  return Dialog::Style{
      .width = resolveFloat(style.width, theme.dialogWidth),
      .headerSpacing = resolveFloat(style.headerSpacing, theme.dialogHeaderSpacing),
      .contentSpacing = resolveFloat(style.contentSpacing, theme.dialogContentSpacing),
      .footerSpacing = resolveFloat(style.footerSpacing, theme.dialogFooterSpacing),
      .headerPadding = resolveInsets(style.headerPadding, theme.dialogHeaderPadding),
      .contentPadding = resolveInsets(style.contentPadding, theme.dialogContentPadding),
      .footerPadding = resolveInsets(style.footerPadding, theme.dialogFooterPadding),
      .titleFont = resolveFont(style.titleFont, theme.dialogTitleFont, theme),
      .titleColor = resolveColor(style.titleColor, theme.dialogTitleColor, theme),
      .surfaceColor = resolveColor(style.surfaceColor, theme.dialogSurfaceColor, theme),
      .surfaceStrokeColor = resolveColor(style.surfaceStrokeColor, theme.dialogSurfaceStrokeColor, theme),
      .dividerColor = resolveColor(style.dividerColor, theme.dialogDividerColor, theme),
      .footerColor = resolveColor(style.footerColor, theme.dialogFooterColor, theme),
      .surfaceStrokeWidth = resolveFloat(style.surfaceStrokeWidth, theme.dialogSurfaceStrokeWidth),
      .dividerThickness = resolveFloat(style.dividerThickness, theme.dialogDividerThickness),
      .cornerRadius = resolveFloat(style.cornerRadius, theme.dialogCornerRadius),
      .shadowRadius = resolveFloat(style.shadowRadius, theme.dialogShadowRadius),
      .shadowOffsetX = resolveFloat(style.shadowOffsetX, theme.dialogShadowOffsetX),
      .shadowOffsetY = resolveFloat(style.shadowOffsetY, theme.dialogShadowOffsetY),
      .shadowColor = resolveColor(style.shadowColor, theme.dialogShadowColor, theme),
      .closeButtonSize = resolveFloat(style.closeButtonSize, theme.dialogCloseButtonSize),
      .closeButtonCornerRadius =
          resolveFloat(style.closeButtonCornerRadius, theme.dialogCloseButtonCornerRadius),
      .closeIconSize = resolveFloat(style.closeIconSize, theme.dialogCloseIconSize),
      .closeIconWeight = resolveFloat(style.closeIconWeight, theme.dialogCloseIconWeight),
      .closeIconColor = resolveColor(style.closeIconColor, theme.dialogCloseIconColor, theme),
      .closeHoverColor = resolveColor(style.closeHoverColor, theme.dialogCloseHoverColor, theme),
  };
}

StrokeStyle strokeStyle(Color color, float width) {
  return width <= 0.f || color.a <= 0.001f ? StrokeStyle::none() : StrokeStyle::solid(color, width);
}

ShadowStyle shadowStyle(Dialog::Style const& style) {
  return ShadowStyle{
      .radius = style.shadowRadius,
      .offset = {style.shadowOffsetX, style.shadowOffsetY},
      .color = style.shadowColor,
  };
}

struct DialogCloseButton : ViewModifiers<DialogCloseButton> {
  std::function<void()> onTap;
  Dialog::Style style {};

  Element body() const {
    Reactive::Signal<bool> hovered = useHover();
    Reactive::Signal<bool> pressed = usePress();
    auto handleTap = [onTap = onTap] {
      if (onTap) {
        onTap();
      }
    };

    return ScaleAroundCenter{
        .scale = [pressed] { return pressed() ? 0.94f : 1.f; },
        .child = ZStack{
            .horizontalAlignment = Alignment::Center,
            .verticalAlignment = Alignment::Center,
            .children = children(
                Rectangle{}
                    .size(style.closeButtonSize, style.closeButtonSize)
                    .fill([hovered, hoverColor = style.closeHoverColor] {
                      return hovered() ? FillStyle::solid(hoverColor) : FillStyle::none();
                    })
                    .cornerRadius(style.closeButtonCornerRadius),
                Icon{
                    .name = IconName::Close,
                    .size = style.closeIconSize,
                    .weight = style.closeIconWeight,
                    .color = style.closeIconColor,
                }),
        }.size(style.closeButtonSize, style.closeButtonSize)
             .cursor(Cursor::Hand)
             .focusable(true)
             .onTap(std::function<void()>{handleTap}),
    };
  }
};

Element dialogDivider(Dialog::Style const& style) {
  return Rectangle{}.height(style.dividerThickness).fill(style.dividerColor);
}

} // namespace

Element Dialog::body() const {
  auto theme = useEnvironment<ThemeKey>();
  Dialog::Style const resolved = resolveStyle(style, theme());

  std::vector<Element> rows;
  rows.reserve(footer.empty() ? 3 : 5);

  std::vector<Element> headerChildren = children(
      Text{
          .text = title,
          .font = resolved.titleFont,
          .color = resolved.titleColor,
      }.flex(1.f, 1.f));
  if (onClose) {
    headerChildren.push_back(DialogCloseButton{.onTap = onClose, .style = resolved});
  }

  rows.push_back(
      HStack{
          .spacing = resolved.headerSpacing,
          .alignment = Alignment::Center,
          .children = std::move(headerChildren),
      }.padding(resolved.headerPadding));

  rows.push_back(dialogDivider(resolved));

  rows.push_back(
      VStack{
          .spacing = resolved.contentSpacing,
          .alignment = Alignment::Stretch,
          .children = content,
      }.padding(resolved.contentPadding));

  if (!footer.empty()) {
    rows.push_back(dialogDivider(resolved));
    rows.push_back(
        HStack{
            .spacing = resolved.footerSpacing,
            .alignment = Alignment::Center,
            .children = footer,
        }.padding(resolved.footerPadding)
             .fill(resolved.footerColor));
  }

  return ZStack{
      .horizontalAlignment = Alignment::Center,
      .verticalAlignment = Alignment::Center,
      .children = children(
          VStack{
              .spacing = 0.f,
              .alignment = Alignment::Stretch,
              .children = std::move(rows),
          }.width(resolved.width)
               .fill(resolved.surfaceColor)
               .stroke(strokeStyle(resolved.surfaceStrokeColor, resolved.surfaceStrokeWidth))
               .cornerRadius(resolved.cornerRadius)
               .shadow(shadowStyle(resolved))
               .clipContent(true)),
  };
}

} // namespace lambdaui
