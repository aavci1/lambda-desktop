#pragma once

#include "Compositor/Wayland/WaylandTypes.hpp"

#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Graphics/Styles.hpp>

#include <algorithm>
#include <cmath>

namespace lambdaui::compositor {

struct WindowShadowLayerGeometry {
  Rect rect{};
  CornerRadius cornerRadius{};
};

inline bool windowUsesCutoutChrome(CommittedSurfaceSnapshot const& surface) {
  return surface.serverSideDecorated && surface.cutoutsBound && !surface.cutoutsRejected;
}

inline float windowExternalTitleBarHeight(CommittedSurfaceSnapshot const& surface) {
  if (!surface.serverSideDecorated || windowUsesCutoutChrome(surface)) return 0.f;
  return std::max(0.f, static_cast<float>(surface.titleBarHeight));
}

inline Rect windowContentRect(CommittedSurfaceSnapshot const& surface) {
  return Rect::sharp(static_cast<float>(surface.x),
                     static_cast<float>(surface.y),
                     static_cast<float>(surface.width),
                     static_cast<float>(surface.height));
}

inline float windowFrameOutsetWidth(CommittedSurfaceSnapshot const& surface, float configuredOutsetWidth) {
  if (!surface.serverSideDecorated || windowUsesCutoutChrome(surface) || windowExternalTitleBarHeight(surface) <= 0.f) {
    return 0.f;
  }
  return std::max(0.f, configuredOutsetWidth);
}

inline Rect windowFrameRect(CommittedSurfaceSnapshot const& surface, float configuredOutsetWidth = 0.f) {
  float const titleBarHeight = windowExternalTitleBarHeight(surface);
  float const outset = windowFrameOutsetWidth(surface, configuredOutsetWidth);
  return Rect::sharp(static_cast<float>(surface.x) - outset,
                     static_cast<float>(surface.y) - titleBarHeight,
                     static_cast<float>(surface.width) + outset * 2.f,
                     static_cast<float>(surface.height) + titleBarHeight + outset);
}

inline Rect windowTitleBarRect(CommittedSurfaceSnapshot const& surface, float configuredOutsetWidth = 0.f) {
  float const titleBarHeight = windowExternalTitleBarHeight(surface);
  float const outset = windowFrameOutsetWidth(surface, configuredOutsetWidth);
  return Rect::sharp(static_cast<float>(surface.x) - outset,
                     static_cast<float>(surface.y) - titleBarHeight,
                     static_cast<float>(surface.width) + outset * 2.f,
                     titleBarHeight);
}

inline CornerRadius windowTitleBarCornerRadius(CornerRadius const& frameRadius) {
  return CornerRadius{frameRadius.topLeft, frameRadius.topRight, 0.f, 0.f};
}

inline CornerRadius windowContentCornerRadius(CommittedSurfaceSnapshot const& surface,
                                              CornerRadius const& frameRadius) {
  if (windowExternalTitleBarHeight(surface) > 0.f) {
    return CornerRadius{0.f, 0.f, frameRadius.bottomRight, frameRadius.bottomLeft};
  }
  return frameRadius;
}

inline float windowContentChromeInsetWidth(CommittedSurfaceSnapshot const& surface, float configuredInsetWidth) {
  (void)surface;
  (void)configuredInsetWidth;
  return 0.f;
}

inline Rect windowVisibleContentRect(CommittedSurfaceSnapshot const& surface,
                                     float configuredInsetWidth,
                                     float dpiScale = 1.f) {
  Rect const content = windowContentRect(surface);
  float const inset = windowContentChromeInsetWidth(surface, configuredInsetWidth);
  float const clampedInset = std::clamp(inset, 0.f, std::min(content.width * 0.5f, content.height));
  float const left = content.x + clampedInset;
  float const top = content.y + clampedInset;
  float const right = content.x + content.width - clampedInset;
  float const bottom = content.y + content.height - clampedInset;
  float const scale = std::max(1.f, dpiScale);
  if (clampedInset <= 0.f || scale <= 1.f) {
    return Rect::sharp(left, top, std::max(0.f, right - left), std::max(0.f, bottom - top));
  }
  float const overlap = 1.f / scale;
  float const snappedLeft = std::floor(left * scale) / scale;
  float const snappedTop = std::floor(top * scale) / scale;
  float const snappedRight = std::ceil(right * scale) / scale + overlap;
  float const snappedBottom = std::ceil(bottom * scale) / scale + overlap;
  return Rect::sharp(snappedLeft,
                     snappedTop,
                     std::max(0.f, snappedRight - snappedLeft),
                     std::max(0.f, snappedBottom - snappedTop));
}

inline CornerRadius windowVisibleContentCornerRadius(CommittedSurfaceSnapshot const& surface,
                                                    CornerRadius const& frameRadius,
                                                    float configuredInsetWidth) {
  (void)surface;
  (void)frameRadius;
  (void)configuredInsetWidth;
  return CornerRadius{};
}

inline WindowShadowLayerGeometry windowShadowLayerGeometry(Rect const& frameRect,
                                                           CornerRadius const& frameRadius,
                                                           ShadowStyle const& shadow,
                                                           float spread) {
  float const leftSpread = std::max(0.f, spread - shadow.offset.x);
  float const rightSpread = std::max(0.f, spread + shadow.offset.x);
  float const topSpread = std::max(0.f, spread - shadow.offset.y);
  float const bottomSpread = std::max(0.f, spread + shadow.offset.y);
  return WindowShadowLayerGeometry{
      .rect = Rect::sharp(frameRect.x - leftSpread,
                          frameRect.y - topSpread,
                          frameRect.width + leftSpread + rightSpread,
                          frameRect.height + topSpread + bottomSpread),
      .cornerRadius = CornerRadius{
          frameRadius.topLeft + std::max(leftSpread, topSpread),
          frameRadius.topRight + std::max(rightSpread, topSpread),
          frameRadius.bottomRight + std::max(rightSpread, bottomSpread),
          frameRadius.bottomLeft + std::max(leftSpread, bottomSpread),
      },
  };
}

} // namespace lambdaui::compositor
