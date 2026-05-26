#pragma once

#include "Compositor/Wayland/WaylandTypes.hpp"

#include <Flux/Core/Geometry.hpp>
#include <Flux/Graphics/Styles.hpp>

#include <algorithm>

namespace flux::compositor {

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

} // namespace flux::compositor
