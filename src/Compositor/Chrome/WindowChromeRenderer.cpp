#include "Compositor/Chrome/WindowChromeRenderer.hpp"

#include "Compositor/Chrome/ChromeMetrics.hpp"

#include <Flux/Graphics/Font.hpp>
#include <Flux/Graphics/Path.hpp>
#include <Flux/Graphics/TextLayoutOptions.hpp>

#include <algorithm>
#include <cmath>
#include <optional>

namespace flux::compositor {
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
      .radius = focused ? 30.f : 18.f,
      .offset = {0.f, focused ? 14.f : 9.f},
      .color = focused ? chrome.focusedShadowColor : chrome.unfocusedShadowColor,
  };
}

bool usesCutoutChrome(CommittedSurfaceSnapshot const& surface) {
  return surface.serverSideDecorated && surface.cutoutsBound && !surface.cutoutsRejected;
}

bool backgroundEffectCoversContent(CommittedSurfaceSnapshot const& surface) {
  return std::ranges::any_of(surface.backgroundBlurRects, [&](CommittedSurfaceSnapshot::RegionRect const& rect) {
    return rect.x == 0 && rect.y == 0 && rect.width >= surface.width && rect.height >= surface.height;
  });
}

bool backgroundEffectCoversCommittedContent(CommittedSurfaceSnapshot const& surface) {
  int const width = surface.committedWidth > 0 ? surface.committedWidth : surface.width;
  int const height = surface.committedHeight > 0 ? surface.committedHeight : surface.height;
  return std::ranges::any_of(surface.backgroundBlurRects, [&](CommittedSurfaceSnapshot::RegionRect const& rect) {
    return rect.x == 0 && rect.y == 0 && rect.width >= width && rect.height >= height;
  });
}

bool materialCoversTitlebar(CommittedSurfaceSnapshot const& surface, ChromeConfig const& chrome) {
  return backgroundEffectCoversContent(surface) ||
         (surface.geometryAnimationGrowing && backgroundEffectCoversCommittedContent(surface)) ||
         (chrome.windowGlassEnabled && surface.defaultGlassEligible);
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
  float const windowY = static_cast<float>(surface.y);
  float const windowWidth = static_cast<float>(surface.width);
  float const titleBarHeight = static_cast<float>(surface.titleBarHeight);
  if (titleBarHeight <= 0.f) return;

  float const titleTop = windowY - titleBarHeight;
  Rect const titleRect = Rect::sharp(windowX, titleTop, windowWidth, titleBarHeight);
  CornerRadius const frameRadius = chrome.windowCornerRadius;
  CornerRadius const titleRadius{frameRadius.topLeft, frameRadius.topRight, 0.f, 0.f};
  bool const titlebarCoveredBySurfaceMaterial = materialCoversTitlebar(surface, chrome);
  if (!titlebarCoveredBySurfaceMaterial) {
    bool const customEffect = !surface.backgroundBlurRects.empty() && !surface.backgroundEffect.usesDefaultMaterial;
    float const blurRadius = customEffect ? surface.backgroundEffect.blurRadius : chrome.glass.blurRadius;
    Color const baseColor =
        customEffect ? surface.backgroundEffect.baseColor : withOpacity(chrome.glass.baseColor, chrome.glass.opacity);
    Color const titleTint =
        customEffect && surface.backgroundEffect.tint.a > 0.f
            ? surface.backgroundEffect.tint
            : (chrome.windowGlassEnabled
                   ? withOpacity(chrome.glass.tintColor, chrome.glass.opacity)
                   : Color{chrome.glass.tintColor.r, chrome.glass.tintColor.g, chrome.glass.tintColor.b, 1.f});
    if (chrome.windowGlassEnabled && blurRadius > 0.f) {
      canvas.drawBackdropBlur(titleRect, blurRadius, Colors::transparent, titleRadius);
    }
    if (baseColor.a > 0.f) {
      canvas.drawRect(titleRect, titleRadius, FillStyle::solid(baseColor), StrokeStyle::none(), ShadowStyle::none());
    }
    canvas.drawRect(titleRect, titleRadius, FillStyle::solid(titleTint), StrokeStyle::none(), ShadowStyle::none());
  }

  float const separatorY = windowY - 0.5f;
  canvas.drawLine({windowX, separatorY}, {windowX + windowWidth, separatorY},
                  StrokeStyle::solid(chrome.borderLineColor, 0.5f));
  if (!titlebarCoveredBySurfaceMaterial) {
    float const topInsetLeft = windowX + std::min(frameRadius.topLeft, windowWidth * 0.5f);
    float const topInsetRight = windowX + windowWidth - std::min(frameRadius.topRight, windowWidth * 0.5f);
    if (chrome.insetHighlightColor.a > 0.f && topInsetRight > topInsetLeft) {
      canvas.drawLine({topInsetLeft, titleTop + 0.5f},
                      {topInsetRight, titleTop + 0.5f},
                      StrokeStyle::solid(chrome.insetHighlightColor, 1.f));
    }
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

void drawWindowFrameShadow(Canvas& canvas, CommittedSurfaceSnapshot const& surface, ChromeConfig const& chrome) {
  if (!surface.serverSideDecorated) return;

  bool const cutoutChrome = usesCutoutChrome(surface);
  float const windowX = static_cast<float>(surface.x);
  float const windowY = static_cast<float>(surface.y);
  float const windowWidth = static_cast<float>(surface.width);
  float const windowHeight = static_cast<float>(surface.height);
  float const titleBarHeight = cutoutChrome ? 0.f : static_cast<float>(surface.titleBarHeight);
  Rect const frameRect = Rect::sharp(windowX, windowY - titleBarHeight, windowWidth, windowHeight + titleBarHeight);
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

    float const leftSpread = std::max(0.f, spread - shadow.offset.x);
    float const rightSpread = std::max(0.f, spread + shadow.offset.x);
    float const topSpread = std::max(0.f, spread - shadow.offset.y);
    float const bottomSpread = std::max(0.f, spread + shadow.offset.y);
    Rect const layerRect = Rect::sharp(frameRect.x - leftSpread,
                                      frameRect.y - topSpread,
                                      frameRect.width + leftSpread + rightSpread,
                                      frameRect.height + topSpread + bottomSpread);
    CornerRadius const layerRadius{
        chrome.windowCornerRadius.topLeft + std::max(leftSpread, topSpread),
        chrome.windowCornerRadius.topRight + std::max(rightSpread, topSpread),
        chrome.windowCornerRadius.bottomRight + std::max(rightSpread, bottomSpread),
        chrome.windowCornerRadius.bottomLeft + std::max(leftSpread, bottomSpread),
    };
    Path ring;
    ring.rect(layerRect, layerRadius);
    ring.rect(frameRect, chrome.windowCornerRadius);
    FillStyle fill = FillStyle::solid(color);
    fill.fillRule = FillRule::EvenOdd;
    if (auto const shadowClip = clippedShadowRect(layerRect, surface)) {
      bool const clipNeeded = std::abs(shadowClip->y - layerRect.y) > 0.5f ||
                              std::abs(shadowClip->height - layerRect.height) > 0.5f;
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
  if (chrome.windowGlassEnabled && chrome.glass.blurRadius > 0.f) {
    Rect const cacheRect = preview.cacheWidth > 0 && preview.cacheHeight > 0
                               ? Rect::sharp(static_cast<float>(preview.cacheX),
                                             static_cast<float>(preview.cacheY),
                                             static_cast<float>(preview.cacheWidth),
                                             static_cast<float>(preview.cacheHeight))
                               : previewRect;
    canvas.drawBackdropBlurCached(previewRect, cacheRect, chrome.glass.blurRadius, Colors::transparent, radius);
  }
  canvas.drawRect(previewRect,
                  radius,
                  FillStyle::solid(withOpacity(chrome.glass.baseColor, chrome.glass.opacity)),
                  StrokeStyle::none(),
                  ShadowStyle::none());
  canvas.drawRect(previewRect,
                  radius,
                  FillStyle::solid(withOpacity(chrome.glass.tintColor, chrome.glass.opacity)),
                  StrokeStyle::solid(chrome.glass.borderColor, 1.f),
                  ShadowStyle::none());
}

} // namespace flux::compositor
