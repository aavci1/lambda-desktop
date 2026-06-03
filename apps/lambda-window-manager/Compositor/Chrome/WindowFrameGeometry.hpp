#pragma once

#include "Compositor/Wayland/WaylandTypes.hpp"

#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Graphics/Styles.hpp>

#include <algorithm>

namespace lambda::compositor {

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

inline Rect windowFrameRect(CommittedSurfaceSnapshot const& surface) {
  float const titleBarHeight = windowExternalTitleBarHeight(surface);
  return Rect::sharp(static_cast<float>(surface.x),
                     static_cast<float>(surface.y) - titleBarHeight,
                     static_cast<float>(surface.width),
                     static_cast<float>(surface.height) + titleBarHeight);
}

inline Rect windowTitleBarRect(CommittedSurfaceSnapshot const& surface) {
  float const titleBarHeight = windowExternalTitleBarHeight(surface);
  return Rect::sharp(static_cast<float>(surface.x),
                     static_cast<float>(surface.y) - titleBarHeight,
                     static_cast<float>(surface.width),
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
  if (!surface.serverSideDecorated || windowUsesCutoutChrome(surface) || windowExternalTitleBarHeight(surface) <= 0.f) {
    return 0.f;
  }
  return std::max(0.f, configuredInsetWidth);
}

inline Rect windowVisibleContentRect(CommittedSurfaceSnapshot const& surface, float configuredInsetWidth) {
  Rect const content = windowContentRect(surface);
  float const inset = windowContentChromeInsetWidth(surface, configuredInsetWidth);
  float const clampedInset = std::clamp(inset, 0.f, std::min(content.width * 0.5f, content.height));
  return Rect::sharp(content.x + clampedInset,
                     content.y + clampedInset,
                     std::max(0.f, content.width - clampedInset * 2.f),
                     std::max(0.f, content.height - clampedInset * 2.f));
}

inline CornerRadius windowVisibleContentCornerRadius(CommittedSurfaceSnapshot const& surface,
                                                    CornerRadius const& frameRadius) {
  return windowContentCornerRadius(surface, frameRadius);
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

} // namespace lambda::compositor
