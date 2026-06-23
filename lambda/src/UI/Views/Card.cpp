#include <Lambda/UI/Views/Card.hpp>
#include <Lambda/UI/Hooks.hpp>

namespace lambdaui {

namespace {

Card::Style resolveStyle(Card::Style const &style, Theme const &theme) {
  float const padding = resolveFloat(style.padding, theme.space5);
  return Card::Style {
      .padding = padding,
      .paddingH = resolveFloat(style.paddingH, padding),
      .paddingV = resolveFloat(style.paddingV, padding),
      .cornerRadius = resolveFloat(style.cornerRadius, theme.radiusXLarge),
      .borderWidth = resolveFloat(style.borderWidth, 1.f),
      .backgroundColor = [background = style.backgroundColor, theme] {
        return resolveColor(background.evaluate(), theme.elevatedBackgroundColor, theme);
      },
      .borderColor = [border = style.borderColor, theme] {
        return resolveColor(border.evaluate(), theme.separatorColor, theme);
      },
      .shadow = style.shadow,
  };
}

} // namespace

Element Card::body() const {
  Card::Style const resolved = resolveStyle(style, useEnvironment<ThemeKey>()());

  Reactive::Bindable<StrokeStyle> const stroke{[borderWidth = resolved.borderWidth,
                                                borderColor = resolved.borderColor] {
    Color const color = borderColor.evaluate();
    return borderWidth <= 0.f || color.a <= 0.001f
        ? StrokeStyle::none()
        : StrokeStyle::solid(color, borderWidth);
  }};

  Element content = child;
  return std::move(content)
      .padding(resolved.paddingV, resolved.paddingH, resolved.paddingV, resolved.paddingH)
      .fill(resolved.backgroundColor)
      .stroke(stroke)
      .cornerRadius(CornerRadius {resolved.cornerRadius})
      .shadow(resolved.shadow);
}

} // namespace lambdaui
