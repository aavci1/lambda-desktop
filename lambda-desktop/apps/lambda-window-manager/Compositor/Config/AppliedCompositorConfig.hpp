#pragma once

#include "Compositor/Config/CompositorConfig.hpp"

#include <Lambda/Graphics/Canvas.hpp>
#include <Lambda/Graphics/Image.hpp>
#include <Lambda/Graphics/Styles.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

namespace lambda::compositor {

struct AppliedCompositorConfig {
  CompositorConfig config;
  FillStyle backgroundFill = FillStyle::solid(Color{0.20f, 0.50f, 0.95f, 1.0f});
  FillStyle wallpaperPlaceholderFill = FillStyle::solid(Color{0.08f, 0.09f, 0.12f, 1.0f});
  std::shared_ptr<Image> wallpaperPreviewImage;
  std::shared_ptr<Image> wallpaperImage;
  bool wallpaperLoadPending = false;
  float wallpaperPreviewOpacity = 0.f;
  float wallpaperRevealOpacity = 1.f;
  std::optional<std::chrono::steady_clock::time_point> wallpaperPreviewRevealStart;
  std::optional<std::chrono::steady_clock::time_point> wallpaperRevealStart;
};

[[nodiscard]] AppliedCompositorConfig applyCompositorConfig(CompositorConfig const& config, Canvas& canvas);

void startWallpaperPreviewReveal(AppliedCompositorConfig& config);
void startWallpaperReveal(AppliedCompositorConfig& config);
void advanceWallpaperReveal(AppliedCompositorConfig& config, std::chrono::steady_clock::time_point now);

void drawCompositorBackground(Canvas& canvas,
                              AppliedCompositorConfig& config,
                              std::uint32_t outputWidth,
                              std::uint32_t outputHeight,
                              bool clearTarget = true,
                              bool fillTarget = true);

[[nodiscard]] std::uint32_t wallpaperPreviewMaxEdge();

} // namespace lambda::compositor
