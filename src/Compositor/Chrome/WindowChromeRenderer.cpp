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
                              ? chrome.glassTint
                              : Color{chrome.glassTint.r, chrome.glassTint.g, chrome.glassTint.b, 1.f};
  canvas.drawRect(frameRect,
                  frameRadius,
                  FillStyle::solid(frameTint),
                  StrokeStyle::none(),
                  windowShadow(chrome, surface.focused));
  if (chrome.windowGlassEnabled && chrome.glassBlurRadius > 0.f) {
    canvas.drawBackdropBlur(frameRect, chrome.glassBlurRadius, chrome.glassTint, frameRadius);
  }

  canvas.drawLine({windowX, windowY}, {windowX + windowWidth, windowY},
                  StrokeStyle::solid(chrome.borderLineColor, 0.5f));
  float const topInsetLeft = windowX + std::min(frameRadius.topLeft, windowWidth * 0.5f);
  float const topInsetRight = windowX + windowWidth - std::min(frameRadius.topRight, windowWidth * 0.5f);
  if (topInsetRight > topInsetLeft) {
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

void drawSnapPreview(Canvas& canvas, SnapPreviewSnapshot const& preview) {
  Rect const previewRect = Rect::sharp(static_cast<float>(preview.x),
                                      static_cast<float>(preview.y),
                                      static_cast<float>(preview.width),
                                      static_cast<float>(preview.height));
  canvas.drawRect(previewRect,
                  CornerRadius{0.f},
                  FillStyle::solid(Color{0.86f, 0.93f, 1.0f, 0.22f}),
                  StrokeStyle::solid(Color{0.92f, 0.97f, 1.0f, 0.82f}, 2.f),
                  ShadowStyle::none());
}

void drawCommandLauncher(Canvas& canvas,
                         TextSystem& textSystem,
                         CommandLauncherSnapshot const& launcher,
                         std::int32_t outputWidth,
                         std::int32_t outputHeight) {
  if (!launcher.visible) return;
  float const width = std::min(680.f, std::max(280.f, static_cast<float>(outputWidth) - 80.f));
  float const height = 78.f;
  float const x = (static_cast<float>(outputWidth) - width) * 0.5f;
  float const y = std::max(34.f, static_cast<float>(outputHeight) * 0.18f);
  Rect const panel = Rect::sharp(x, y, width, height);
  CornerRadius const radius{16.f};

  canvas.drawRect(panel,
                  radius,
                  FillStyle::solid(Color{0.94f, 0.95f, 0.97f, 0.96f}),
                  StrokeStyle::solid(Color{0.62f, 0.64f, 0.68f, 0.85f}, 1.f),
                  ShadowStyle{.radius = 30.f, .offset = {0.f, 18.f}, .color = Color{0.f, 0.f, 0.f, 0.38f}});

  Font commandFont{};
  commandFont.size = 24.f;
  commandFont.weight = 480.f;
  TextLayoutOptions commandOptions{
      .verticalAlignment = VerticalAlignment::Center,
      .wrapping = TextWrapping::NoWrap,
      .maxLines = 1,
  };
  std::string command = launcher.command.empty() ? std::string{"Run command"} : launcher.command;
  Color commandColor = launcher.command.empty() ? Color{0.47f, 0.49f, 0.53f, 1.f}
                                                : Color{0.08f, 0.09f, 0.11f, 1.f};
  Rect const commandRect = Rect::sharp(x + 26.f, y + 12.f, width - 52.f, 38.f);
  if (auto layout = textSystem.layout(command, commandFont, commandColor, commandRect, commandOptions)) {
    canvas.save();
    canvas.clipRect(commandRect);
    canvas.drawTextLayout(*layout, {0.f, 0.f});
    canvas.restore();
  }

  Font hintFont{};
  hintFont.size = 12.f;
  hintFont.weight = 420.f;
  TextLayoutOptions hintOptions{
      .verticalAlignment = VerticalAlignment::Center,
      .wrapping = TextWrapping::NoWrap,
      .maxLines = 1,
  };
  std::string hint = launcher.message.empty() ? std::string{"Enter to run, Escape to cancel"} : launcher.message;
  Rect const hintRect = Rect::sharp(x + 28.f, y + 51.f, width - 56.f, 18.f);
  if (auto layout = textSystem.layout(hint, hintFont, Color{0.40f, 0.42f, 0.46f, 1.f}, hintRect, hintOptions)) {
    canvas.drawTextLayout(*layout, {0.f, 0.f});
  }
}

} // namespace flux::compositor
