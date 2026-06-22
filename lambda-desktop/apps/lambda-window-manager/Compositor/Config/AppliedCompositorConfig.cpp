#include "Compositor/Config/AppliedCompositorConfig.hpp"

#include <algorithm>
#include <cmath>

namespace lambda::compositor {
namespace {

constexpr std::uint32_t kWallpaperPreviewMaxEdge = 384u;

FillStyle makeWallpaperLoadingFill(CompositorConfig const& config) {
  if (config.backgroundGradientEnd) {
    return FillStyle::linearGradient(config.backgroundColor,
                                     *config.backgroundGradientEnd,
                                     {0.f, 0.f},
                                     {1.f, 1.f});
  }

  if (config.backgroundConfigured) {
    return FillStyle::solid(config.backgroundColor);
  }

  // Neutral slate gradient used while the wallpaper decodes. Avoids flashing the
  // default bright compositor blue when `background` is unset in config.
  Color const top{0.07f, 0.08f, 0.11f, 1.f};
  Color const bottom{0.13f, 0.15f, 0.19f, 1.f};
  return FillStyle::linearGradient(top, bottom, {0.f, 0.f}, {1.f, 1.f});
}

Color wallpaperLoadingClearColor(CompositorConfig const& config) {
  if (config.backgroundConfigured) {
    return config.backgroundColor;
  }
  return Color{0.07f, 0.08f, 0.11f, 1.f};
}

float easeOutCubic(float value) {
  float const t = std::clamp(value, 0.f, 1.f);
  float const inverse = 1.f - t;
  return 1.f - inverse * inverse * inverse;
}

void advanceOpacityReveal(float& opacity,
                          std::optional<std::chrono::steady_clock::time_point>& start,
                          std::chrono::steady_clock::time_point now,
                          std::chrono::milliseconds duration) {
  if (!start) {
    return;
  }
  auto const elapsed = now - *start;
  if (elapsed >= duration) {
    opacity = 1.f;
    start.reset();
    return;
  }
  float const t = static_cast<float>(elapsed.count()) / static_cast<float>(duration.count());
  opacity = easeOutCubic(t);
}

} // namespace

AppliedCompositorConfig applyCompositorConfig(CompositorConfig const& config, Canvas& canvas) {
  (void)canvas;
  AppliedCompositorConfig applied{
      .config = config,
      .backgroundFill = config.backgroundGradientEnd
                              ? FillStyle::linearGradient(config.backgroundColor,
                                                          *config.backgroundGradientEnd,
                                                          {0.f, 0.f},
                                                          {1.f, 1.f})
                              : FillStyle::solid(config.backgroundColor),
      .wallpaperPlaceholderFill = makeWallpaperLoadingFill(config),
      .wallpaperPreviewImage = nullptr,
      .wallpaperImage = nullptr,
      .wallpaperLoadPending = config.wallpaperPath.has_value(),
      .wallpaperPreviewOpacity = 0.f,
      .wallpaperRevealOpacity = config.wallpaperPath ? 0.f : 1.f,
      .wallpaperPreviewRevealStart = std::nullopt,
      .wallpaperRevealStart = std::nullopt,
  };
  return applied;
}

void startWallpaperPreviewReveal(AppliedCompositorConfig& config) {
  config.wallpaperPreviewRevealStart = std::chrono::steady_clock::now();
  config.wallpaperPreviewOpacity = 0.f;
}

void startWallpaperReveal(AppliedCompositorConfig& config) {
  config.wallpaperLoadPending = false;
  config.wallpaperRevealStart = std::chrono::steady_clock::now();
  config.wallpaperRevealOpacity = 0.f;
}

void advanceWallpaperReveal(AppliedCompositorConfig& config, std::chrono::steady_clock::time_point now) {
  advanceOpacityReveal(config.wallpaperPreviewOpacity,
                       config.wallpaperPreviewRevealStart,
                       now,
                       std::chrono::milliseconds(420));
  advanceOpacityReveal(config.wallpaperRevealOpacity,
                       config.wallpaperRevealStart,
                       now,
                       std::chrono::milliseconds(650));

  if (config.wallpaperRevealOpacity >= 1.f && config.wallpaperImage) {
    config.wallpaperPreviewImage.reset();
    config.wallpaperPreviewOpacity = 0.f;
    config.wallpaperPreviewRevealStart.reset();
  }
}

void drawCompositorBackground(Canvas& canvas,
                              AppliedCompositorConfig& config,
                              std::uint32_t outputWidth,
                              std::uint32_t outputHeight,
                              bool clearTarget,
                              bool fillTarget) {
  auto const now = std::chrono::steady_clock::now();
  advanceWallpaperReveal(config, now);

  bool const wallpaperLoading =
      config.config.wallpaperPath && (config.wallpaperLoadPending || config.wallpaperRevealOpacity < 1.f);
  if (clearTarget) {
    canvas.clear(wallpaperLoading ? wallpaperLoadingClearColor(config.config) : config.config.backgroundColor);
  }

  auto const bounds = Rect::sharp(0.f, 0.f, static_cast<float>(outputWidth), static_cast<float>(outputHeight));
  bool const paintTarget = clearTarget || fillTarget;

  if (!paintTarget) {
    return;
  }

  if (wallpaperLoading && !config.wallpaperPreviewImage && config.wallpaperRevealOpacity <= 0.f) {
    canvas.drawRect(bounds,
                    CornerRadius{0.f},
                    config.wallpaperPlaceholderFill,
                    StrokeStyle::none(),
                    ShadowStyle::none());
  } else if (!config.config.wallpaperPath) {
    canvas.drawRect(bounds,
                    CornerRadius{0.f},
                    config.backgroundFill,
                    StrokeStyle::none(),
                    ShadowStyle::none());
  } else if (!clearTarget) {
    canvas.drawRect(bounds,
                    CornerRadius{0.f},
                    FillStyle::solid(wallpaperLoading ? wallpaperLoadingClearColor(config.config)
                                                       : config.config.backgroundColor),
                    StrokeStyle::none(),
                    ShadowStyle::none());
  }

  if (config.wallpaperPreviewImage && config.wallpaperPreviewOpacity > 0.f &&
      config.wallpaperRevealOpacity < 1.f) {
    float const previewAlpha = config.wallpaperPreviewOpacity * (1.f - config.wallpaperRevealOpacity);
    if (previewAlpha > 0.f) {
      canvas.drawImage(*config.wallpaperPreviewImage,
                       bounds,
                       config.config.wallpaperMode,
                       CornerRadius{},
                       previewAlpha);
    }
  }

  if (config.wallpaperImage && config.wallpaperRevealOpacity > 0.f) {
    canvas.drawImage(*config.wallpaperImage,
                     bounds,
                     config.config.wallpaperMode,
                     CornerRadius{},
                     config.wallpaperRevealOpacity);
  }
}

std::uint32_t wallpaperPreviewMaxEdge() {
  return kWallpaperPreviewMaxEdge;
}

} // namespace lambda::compositor
