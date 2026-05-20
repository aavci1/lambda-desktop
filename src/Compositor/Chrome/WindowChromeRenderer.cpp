#include "Compositor/Chrome/WindowChromeRenderer.hpp"

#include "Compositor/Chrome/ChromeMetrics.hpp"

#include <Flux/Graphics/Font.hpp>
#include <Flux/Graphics/TextLayoutOptions.hpp>

#include <algorithm>

namespace flux::compositor {
namespace {

Color withOpacity(Color color, float opacity) {
  color.a *= std::clamp(opacity, 0.f, 1.f);
  return color;
}

StrokeStyle visibleStroke(Color color, float width) {
  return color.a > 0.f && width > 0.f ? StrokeStyle::solid(color, width) : StrokeStyle::none();
}

ShadowStyle windowShadow(ChromeConfig const& chrome, bool focused) {
  return ShadowStyle{
      .radius = focused ? 30.f : 18.f,
      .offset = {0.f, focused ? 14.f : 9.f},
      .color = focused ? chrome.focusedShadowColor : chrome.unfocusedShadowColor,
  };
}

bool usesCutoutChrome(CommittedSurfaceSnapshot const& surface) {
  return surface.serverSideDecorated && surface.cutoutsBound && !surface.cutoutsRejected;
}

void drawControls(Canvas& canvas,
                  CommittedSurfaceSnapshot const& surface,
                  ChromeConfig const& chrome,
                  float titleTop,
                  float titleBarHeight) {
  float const windowX = static_cast<float>(surface.x);
  float const windowWidth = static_cast<float>(surface.width);
  ChromeControlsMetrics const metrics = chromeControlsMetrics(chrome, titleBarHeight);
  ChromeControlRects const rects = chromeControlRects(chrome, windowX, titleTop, windowWidth, titleBarHeight);
  float const buttonSize = metrics.buttonSize;
  float const buttonY = rects.closeButton.y;
  float const groupOpacity = surface.focused ? 1.f : 0.6f;
  float const glyphInset = std::max(3.f, buttonSize * 0.32f);
  float const glyphMin = glyphInset;
  float const glyphMax = buttonSize - glyphInset;
  float const minimizeY = buttonY + buttonSize * 0.66f;

  auto drawButton = [&](Rect const& rect, bool hovered, bool pressed, bool close) {
    bool const active = hovered || pressed;
    if (active) {
      Color const background = close ? chrome.closeHoverBackground : chrome.minimizeHoverBackground;
      canvas.drawRect(rect,
                      CornerRadius{metrics.buttonRadius},
                      FillStyle::solid(withOpacity(background, groupOpacity)),
                      StrokeStyle::none(),
                      ShadowStyle::none());
    }

    Color glyph = close
                      ? (active ? chrome.closeGlyphHoverColor : chrome.closeGlyphColor)
                      : (active ? chrome.minimizeGlyphHoverColor : chrome.minimizeGlyphColor);
    glyph = withOpacity(glyph, groupOpacity);
    StrokeStyle stroke = StrokeStyle::solid(glyph, 1.6f);
    stroke.cap = StrokeCap::Round;
    if (close) {
      canvas.drawLine({rect.x + glyphMin, buttonY + glyphMin}, {rect.x + glyphMax, buttonY + glyphMax}, stroke);
      canvas.drawLine({rect.x + glyphMax, buttonY + glyphMin}, {rect.x + glyphMin, buttonY + glyphMax}, stroke);
    } else {
      canvas.drawLine({rect.x + glyphMin, minimizeY}, {rect.x + glyphMax, minimizeY}, stroke);
    }
  };

  drawButton(rects.minimizeButton, surface.minimizeButtonHovered, surface.minimizeButtonPressed, false);
  drawButton(rects.closeButton, surface.closeButtonHovered, surface.closeButtonPressed, true);
}

void drawDefaultChrome(Canvas& canvas,
                       TextSystem& textSystem,
                       CommittedSurfaceSnapshot const& surface,
                       ChromeConfig const& chrome) {
  float const windowX = static_cast<float>(surface.x);
  float const windowY = static_cast<float>(surface.y);
  float const windowWidth = static_cast<float>(surface.width);
  float const windowHeight = static_cast<float>(surface.height);
  float const titleBarHeight = static_cast<float>(surface.titleBarHeight);
  if (titleBarHeight <= 0.f) return;

  float const titleTop = windowY - titleBarHeight;
  Rect const frameRect = Rect::sharp(windowX, titleTop, windowWidth, windowHeight + titleBarHeight);
  CornerRadius const frameRadius = chrome.windowCornerRadius;
  Color const frameTint = chrome.windowGlassEnabled
                              ? withOpacity(chrome.glassTint, chrome.windowGlassOpacity)
                              : Color{chrome.glassTint.r, chrome.glassTint.g, chrome.glassTint.b, 1.f};
  if (chrome.windowGlassEnabled && chrome.glassBlurRadius > 0.f) {
    Rect const titleRect = Rect::sharp(windowX, titleTop, windowWidth, titleBarHeight);
    CornerRadius const titleRadius{frameRadius.topLeft, frameRadius.topRight, 0.f, 0.f};
    canvas.drawBackdropBlur(titleRect, chrome.glassBlurRadius, Colors::transparent, titleRadius);
  }
  canvas.drawRect(frameRect,
                  frameRadius,
                  FillStyle::solid(frameTint),
                  StrokeStyle::none(),
                  windowShadow(chrome, surface.focused));

  canvas.drawLine({windowX, windowY}, {windowX + windowWidth, windowY},
                  StrokeStyle::solid(chrome.borderLineColor, 0.5f));
  float const topInsetLeft = windowX + std::min(frameRadius.topLeft, windowWidth * 0.5f);
  float const topInsetRight = windowX + windowWidth - std::min(frameRadius.topRight, windowWidth * 0.5f);
  if (chrome.insetHighlightColor.a > 0.f && topInsetRight > topInsetLeft) {
    canvas.drawLine({topInsetLeft, titleTop + 0.5f},
                    {topInsetRight, titleTop + 0.5f},
                    StrokeStyle::solid(chrome.insetHighlightColor, 1.f));
  }

  float const controlsWidth = chromeControlsMetrics(chrome, titleBarHeight).controlsWidth;
  float const titleLeft = windowX + controlsWidth;
  float const titleWidth = std::max(0.f, windowWidth - controlsWidth * 2.f);
  if (titleWidth > 0.f && !surface.title.empty()) {
    Font titleFont{};
    titleFont.size = chrome.titleTextFontSize;
    titleFont.weight = chrome.titleTextFontWeight;
    TextLayoutOptions titleOptions{
        .horizontalAlignment = HorizontalAlignment::Center,
        .verticalAlignment = VerticalAlignment::Center,
        .wrapping = TextWrapping::NoWrap,
        .maxLines = 1,
    };
    Color const titleColor = withOpacity(chrome.titleTextColor, surface.focused ? 1.f : 0.6f);
    auto titleLayout =
        textSystem.layout(surface.title,
                          titleFont,
                          titleColor,
                          Rect::sharp(titleLeft,
                                      titleTop,
                                      titleWidth,
                                      titleBarHeight),
                          titleOptions);
    if (titleLayout) {
      canvas.save();
      canvas.clipRect(Rect::sharp(titleLeft,
                                  titleTop,
                                  titleWidth,
                                  titleBarHeight));
      canvas.drawTextLayout(*titleLayout, {0.f, 0.f});
      canvas.restore();
    }
  }

  drawControls(canvas, surface, chrome, titleTop, titleBarHeight);
}

} // namespace

void drawWindowChrome(Canvas& canvas,
                      TextSystem& textSystem,
                      CommittedSurfaceSnapshot const& surface,
                      ChromeConfig const& chrome) {
  if (!surface.serverSideDecorated) return;
  if (usesCutoutChrome(surface)) {
    drawControls(canvas, surface, chrome, static_cast<float>(surface.y), static_cast<float>(chrome.titleBarHeight));
    return;
  }
  drawDefaultChrome(canvas, textSystem, surface, chrome);
}

void drawWindowFrameBorder(Canvas& canvas, CommittedSurfaceSnapshot const& surface, ChromeConfig const& chrome) {
  if (!surface.serverSideDecorated) return;
  StrokeStyle const border = visibleStroke(chrome.windowBorderColor, chrome.windowBorderWidth);
  if (border.isNone()) return;

  bool const cutoutChrome = usesCutoutChrome(surface);
  float const windowX = static_cast<float>(surface.x);
  float const windowY = static_cast<float>(surface.y);
  float const windowWidth = static_cast<float>(surface.width);
  float const windowHeight = static_cast<float>(surface.height);
  float const titleBarHeight = cutoutChrome ? 0.f : static_cast<float>(surface.titleBarHeight);
  Rect const frameRect = Rect::sharp(windowX, windowY - titleBarHeight, windowWidth, windowHeight + titleBarHeight);
  canvas.drawRect(frameRect,
                  chrome.windowCornerRadius,
                  FillStyle::none(),
                  border,
                  ShadowStyle::none());
}

void drawSnapPreview(Canvas& canvas, SnapPreviewSnapshot const& preview, ChromeConfig const& chrome) {
  Rect const previewRect = Rect::sharp(static_cast<float>(preview.x),
                                      static_cast<float>(preview.y),
                                      static_cast<float>(preview.width),
                                      static_cast<float>(preview.height));
  CornerRadius const radius{10.f};
  if (chrome.windowGlassEnabled && chrome.glassBlurRadius > 0.f) {
    canvas.drawBackdropBlur(previewRect, chrome.glassBlurRadius, Colors::transparent, radius);
  }
  canvas.drawRect(previewRect,
                  radius,
                  FillStyle::solid(withOpacity(chrome.glassTint, chrome.windowGlassOpacity * 0.48f)),
                  StrokeStyle::solid(Color{0.92f, 0.97f, 1.0f, 0.78f}, 1.f),
                  ShadowStyle::none());
}

void drawCommandLauncher(Canvas& canvas,
                         TextSystem& textSystem,
                         CommandLauncherSnapshot const& launcher,
                         ChromeConfig const& chrome,
                         std::int32_t outputWidth,
                         std::int32_t outputHeight) {
  if (!launcher.visible) return;
  float const width = std::min(680.f, std::max(280.f, static_cast<float>(outputWidth) - 80.f));
  float const height = 72.f;
  float const x = (static_cast<float>(outputWidth) - width) * 0.5f;
  float const y = std::max(34.f, static_cast<float>(outputHeight) * 0.18f);
  Rect const panel = Rect::sharp(x, y, width, height);
  CornerRadius const radius{16.f};

  if (chrome.windowGlassEnabled && chrome.glassBlurRadius > 0.f) {
    canvas.drawBackdropBlur(panel, chrome.glassBlurRadius, Colors::transparent, radius);
  }
  canvas.drawRect(panel,
                  radius,
                  FillStyle::solid(withOpacity(chrome.glassTint, chrome.windowGlassOpacity)),
                  visibleStroke(chrome.windowBorderColor, chrome.windowBorderWidth),
                  ShadowStyle{.radius = 30.f, .offset = {0.f, 18.f}, .color = Color{0.f, 0.f, 0.f, 0.38f}});

  Font commandFont{};
  commandFont.size = 24.f;
  commandFont.weight = 520.f;
  TextLayoutOptions commandOptions{
      .verticalAlignment = VerticalAlignment::Center,
      .wrapping = TextWrapping::NoWrap,
      .maxLines = 1,
  };
  std::string command = launcher.command.empty() ? std::string{"Run command"} : launcher.command;
  Color commandColor = launcher.command.empty() ? Color{0.25f, 0.27f, 0.31f, 1.f}
                                                : Color{0.04f, 0.05f, 0.07f, 1.f};
  float constexpr panelInset = 22.f;
  Rect const commandRect = Rect::sharp(x + panelInset,
                                       y + panelInset,
                                       width - panelInset * 2.f,
                                       height - panelInset * 2.f);
  if (auto layout = textSystem.layout(command, commandFont, commandColor, commandRect, commandOptions)) {
    canvas.save();
    canvas.clipRect(commandRect);
    canvas.drawTextLayout(*layout, {0.f, 0.f});
    canvas.restore();
  }
}

} // namespace flux::compositor
