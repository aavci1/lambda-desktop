#include "Compositor/Chrome/WindowChromeRenderer.hpp"

#include "Compositor/Chrome/ChromeMetrics.hpp"
#include "Compositor/Chrome/WindowFrameGeometry.hpp"

#include <Lambda/Graphics/Font.hpp>
#include <Lambda/Graphics/Path.hpp>
#include <Lambda/Graphics/TextLayoutOptions.hpp>

#include <algorithm>
#include <cmath>
#include <optional>

namespace lambda::compositor {
namespace {

Color withOpacity(Color color, float opacity) {
  color.a *= std::clamp(opacity, 0.f, 1.f);
  return color;
}

StrokeStyle visibleStroke(Color color, float width) {
  return color.a > 0.f && width > 0.f ? StrokeStyle::solid(color, width) : StrokeStyle::none();
}

std::optional<Rect> clippedShadowRect(Rect const& layerRect, CommittedSurfaceSnapshot const& surface) {
  float top = layerRect.y;
  float bottom = layerRect.y + layerRect.height;
  if (surface.shadowClipTop > 0) {
    top = std::max(top, static_cast<float>(surface.shadowClipTop));
  }
  if (surface.shadowClipBottom > 0) {
    bottom = std::min(bottom, static_cast<float>(surface.shadowClipBottom));
  }
  if (bottom <= top) return std::nullopt;
  return Rect::sharp(layerRect.x, top, layerRect.width, bottom - top);
}

ShadowStyle windowShadow(ChromeConfig const& chrome, bool focused) {
  return ShadowStyle{
      .radius = focused ? 18.f : 12.f,
      .offset = {0.f, focused ? 7.f : 4.f},
      .color = focused ? chrome.focusedShadowColor : chrome.unfocusedShadowColor,
  };
}

void drawGlassMaterialRect(Canvas& canvas, Rect const& rect, CornerRadius const& radius, ChromeConfig const& chrome) {
  if (rect.width <= 0.f || rect.height <= 0.f) return;
  float const blurRadius = chrome.glass.blurRadius;
  Color const baseColor = withOpacity(chrome.glass.baseColor, chrome.glass.opacity);
  Color const tintColor = withOpacity(chrome.glass.tintColor, chrome.glass.opacity);
  if (blurRadius > 0.f) {
    canvas.drawBackdropBlur(rect, blurRadius, Colors::transparent, radius);
  }
  if (baseColor.a > 0.f) {
    canvas.drawRect(rect, radius, FillStyle::solid(baseColor), StrokeStyle::none(), ShadowStyle::none());
  }
  if (tintColor.a > 0.f) {
    canvas.drawRect(rect, radius, FillStyle::solid(tintColor), StrokeStyle::none(), ShadowStyle::none());
  }
}

void drawGlassMaterialFrame(Canvas& canvas,
                            Rect const& frame,
                            CornerRadius const& frameRadius,
                            Rect const& cutout,
                            CornerRadius const& cutoutRadius,
                            ChromeConfig const& chrome) {
  if (frame.width <= 0.f || frame.height <= 0.f) return;
  if (cutout.width <= 0.f || cutout.height <= 0.f) {
    drawGlassMaterialRect(canvas, frame, frameRadius, chrome);
    return;
  }

  float const blurRadius = chrome.glass.blurRadius;
  float const left = std::max(0.f, cutout.x - frame.x);
  float const top = std::max(0.f, cutout.y - frame.y);
  float const right = std::max(0.f, frame.x + frame.width - (cutout.x + cutout.width));
  float const bottom = std::max(0.f, frame.y + frame.height - (cutout.y + cutout.height));
  if (blurRadius > 0.f) {
    if (top > 0.f) {
      canvas.drawBackdropBlurCached(Rect::sharp(frame.x, frame.y, frame.width, top),
                                    frame,
                                    blurRadius,
                                    Colors::transparent,
                                    CornerRadius{frameRadius.topLeft, frameRadius.topRight, 0.f, 0.f});
    }
    if (left > 0.f) {
      canvas.drawBackdropBlurCached(Rect::sharp(frame.x, cutout.y, left, cutout.height),
                                    frame,
                                    blurRadius,
                                    Colors::transparent,
                                    CornerRadius{});
    }
    if (right > 0.f) {
      canvas.drawBackdropBlurCached(Rect::sharp(cutout.x + cutout.width, cutout.y, right, cutout.height),
                                    frame,
                                    blurRadius,
                                    Colors::transparent,
                                    CornerRadius{});
    }
    if (bottom > 0.f) {
      canvas.drawBackdropBlurCached(Rect::sharp(frame.x, cutout.y + cutout.height, frame.width, bottom),
                                    frame,
                                    blurRadius,
                                    Colors::transparent,
                                    CornerRadius{0.f, 0.f, frameRadius.bottomRight, frameRadius.bottomLeft});
    }
  }

  Path glassFrame;
  glassFrame.rect(frame, frameRadius);
  glassFrame.rect(cutout, cutoutRadius);

  Color const baseColor = withOpacity(chrome.glass.baseColor, chrome.glass.opacity);
  if (baseColor.a > 0.f) {
    FillStyle fill = FillStyle::solid(baseColor);
    fill.fillRule = FillRule::EvenOdd;
    canvas.drawPath(glassFrame, fill, StrokeStyle::none(), ShadowStyle::none());
  }

  Color const tintColor = withOpacity(chrome.glass.tintColor, chrome.glass.opacity);
  if (tintColor.a > 0.f) {
    FillStyle fill = FillStyle::solid(tintColor);
    fill.fillRule = FillRule::EvenOdd;
    canvas.drawPath(glassFrame, fill, StrokeStyle::none(), ShadowStyle::none());
  }
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
  float const groupOpacity = surface.focused ? 1.f : 0.6f;
  float const glyphInset = std::max(3.f, buttonSize * 0.28f);

  auto glyphRect = [&](Rect const& rect) {
    return Rect::sharp(rect.x + (rect.width - buttonSize) * 0.5f,
                       rect.y + (rect.height - buttonSize) * 0.5f,
                       buttonSize,
                       buttonSize);
  };

  enum class ControlKind {
    Minimize,
    Maximize,
    Close,
  };

  auto hoverRadius = [&](Rect const& rect) {
    float const right = windowX + windowWidth;
    if (std::abs((rect.x + rect.width) - right) <= 0.5f) {
      return CornerRadius{0.f, chrome.windowCornerRadius.topRight, 0.f, 0.f};
    }
    return CornerRadius{};
  };

  auto drawButton = [&](Rect const& rect, bool hovered, bool pressed, ControlKind kind) {
    bool const active = hovered || pressed;
    if (active) {
      Color const background = kind == ControlKind::Close ? chrome.closeHoverBackground : chrome.minimizeHoverBackground;
      canvas.drawRect(rect,
                      hoverRadius(rect),
                      FillStyle::solid(withOpacity(background, groupOpacity)),
                      StrokeStyle::none(),
                      ShadowStyle::none());
    }

    Color glyph = kind == ControlKind::Close
                      ? (active ? chrome.closeGlyphHoverColor : chrome.closeGlyphColor)
                      : (active ? chrome.minimizeGlyphHoverColor : chrome.minimizeGlyphColor);
    glyph = withOpacity(glyph, groupOpacity);
    StrokeStyle stroke = StrokeStyle::solid(glyph, 1.6f);
    stroke.cap = StrokeCap::Round;
    Rect const glyphBounds = glyphRect(rect);
    float const glyphMinX = glyphBounds.x + glyphInset;
    float const glyphMaxX = glyphBounds.x + glyphBounds.width - glyphInset;
    float const glyphMinY = glyphBounds.y + glyphInset;
    float const glyphMaxY = glyphBounds.y + glyphBounds.height - glyphInset;
    if (kind == ControlKind::Close) {
      canvas.drawLine({glyphMinX, glyphMinY}, {glyphMaxX, glyphMaxY}, stroke);
      canvas.drawLine({glyphMaxX, glyphMinY}, {glyphMinX, glyphMaxY}, stroke);
    } else if (kind == ControlKind::Maximize) {
      Rect const box = Rect::sharp(glyphMinX,
                                   glyphMinY,
                                   std::max(0.f, glyphMaxX - glyphMinX),
                                   std::max(0.f, glyphMaxY - glyphMinY));
      canvas.drawRect(box, CornerRadius{}, FillStyle::none(), stroke, ShadowStyle::none());
    } else {
      float const minimizeY = glyphBounds.y + glyphBounds.height * 0.5f;
      canvas.drawLine({glyphMinX, minimizeY}, {glyphMaxX, minimizeY}, stroke);
    }
  };

  drawButton(rects.minimizeButton, surface.minimizeButtonHovered, surface.minimizeButtonPressed, ControlKind::Minimize);
  drawButton(rects.maximizeButton, surface.maximizeButtonHovered, surface.maximizeButtonPressed, ControlKind::Maximize);
  drawButton(rects.closeButton, surface.closeButtonHovered, surface.closeButtonPressed, ControlKind::Close);
}

void drawDefaultChrome(Canvas& canvas,
                       TextSystem& textSystem,
                       CommittedSurfaceSnapshot const& surface,
                       ChromeConfig const& chrome) {
  float const windowX = static_cast<float>(surface.x);
  float const windowWidth = static_cast<float>(surface.width);
  Rect const titleRect = windowTitleBarRect(surface);
  float const titleBarHeight = titleRect.height;
  if (titleBarHeight <= 0.f) return;

  float const titleTop = titleRect.y;
  CornerRadius const frameRadius = chrome.windowCornerRadius;
  drawGlassMaterialFrame(canvas,
                         windowFrameRect(surface),
                         frameRadius,
                         windowVisibleContentRect(surface, chrome.contentInsetWidth),
                         windowVisibleContentCornerRadius(surface, frameRadius),
                         chrome);

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
  if (windowUsesCutoutChrome(surface)) {
    drawControls(canvas, surface, chrome, static_cast<float>(surface.y), static_cast<float>(chrome.titleBarHeight));
    return;
  }
  drawDefaultChrome(canvas, textSystem, surface, chrome);
}

void drawWindowFrameShadow(Canvas& canvas, CommittedSurfaceSnapshot const& surface, ChromeConfig const& chrome) {
  if (!surface.serverSideDecorated) return;

  Rect const frameRect = windowFrameRect(surface);
  ShadowStyle const shadow = windowShadow(chrome, surface.focused);
  if (shadow.isNone()) return;

  int const steps = std::clamp(static_cast<int>(std::ceil(shadow.radius / 3.f)), 3, 8);
  for (int i = steps; i >= 1; --i) {
    float const t = static_cast<float>(i) / static_cast<float>(steps);
    float const spread = shadow.radius * t;
    float const alpha = shadow.color.a * (1.f - t * 0.72f) / static_cast<float>(steps);
    Color color = shadow.color;
    color.a = alpha;
    if (color.a <= 0.f) continue;

    WindowShadowLayerGeometry const layer = windowShadowLayerGeometry(frameRect, chrome.windowCornerRadius, shadow, spread);
    Path ring;
    ring.rect(layer.rect, layer.cornerRadius);
    ring.rect(frameRect, chrome.windowCornerRadius);
    FillStyle fill = FillStyle::solid(color);
    fill.fillRule = FillRule::EvenOdd;
    if (auto const shadowClip = clippedShadowRect(layer.rect, surface)) {
      bool const clipNeeded = std::abs(shadowClip->y - layer.rect.y) > 0.5f ||
                              std::abs(shadowClip->height - layer.rect.height) > 0.5f;
      if (!clipNeeded) {
        canvas.drawPath(ring, fill, StrokeStyle::none(), ShadowStyle::none());
        continue;
      }
      canvas.save();
      canvas.clipRect(*shadowClip);
      canvas.drawPath(ring, fill, StrokeStyle::none(), ShadowStyle::none());
      canvas.restore();
    }
  }
}

void drawWindowFrameBorder(Canvas& canvas, CommittedSurfaceSnapshot const& surface, ChromeConfig const& chrome) {
  if (!surface.serverSideDecorated) return;

  StrokeStyle const border = visibleStroke(chrome.windowBorderColor, chrome.windowBorderWidth);
  if (border.isNone()) return;

  Rect const frameRect = windowFrameRect(surface);
  canvas.drawRect(frameRect,
                  chrome.windowCornerRadius,
                  FillStyle::none(),
                  border,
                  ShadowStyle::none());
}

void drawSnapPreview(Canvas& canvas, SnapPreviewSnapshot const& preview, ChromeConfig const& chrome) {
  float constexpr strokeWidth = 5.f;
  float constexpr strokeInset = strokeWidth * 0.5f;
  Rect const previewRect = Rect::sharp(static_cast<float>(preview.x) + strokeInset,
                                      static_cast<float>(preview.y) + strokeInset,
                                      std::max(0.f, static_cast<float>(preview.width) - strokeWidth),
                                      std::max(0.f, static_cast<float>(preview.height) - strokeWidth));
  CornerRadius const radius{10.f};
  Color border = chrome.glass.borderColor;
  border.a = std::max(border.a, 0.72f);
  canvas.drawRect(previewRect,
                  radius,
                  FillStyle::none(),
                  StrokeStyle::solid(border, strokeWidth),
                  ShadowStyle::none());
}

} // namespace lambda::compositor
