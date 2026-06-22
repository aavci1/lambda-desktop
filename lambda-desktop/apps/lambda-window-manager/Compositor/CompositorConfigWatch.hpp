#pragma once

#include "Compositor/Config/AppliedCompositorConfig.hpp"
#include "Compositor/Config/CompositorConfig.hpp"
#include "Compositor/Presenter.hpp"
#include "Compositor/WaylandServer.hpp"

#include <Lambda/Graphics/Canvas.hpp>

#include <filesystem>
#include <functional>

namespace lambda::compositor {

class AsyncWallpaperLoader;

struct CompositorConfigWatchContext {
  LoadedCompositorConfig& loadedConfig;
  AppliedCompositorConfig& appliedConfig;
  WaylandServer& wayland;
  Presenter& presenter;
  lambda::Canvas& canvas;
  std::function<CompositorConfig()> effectiveConfig;
  std::function<void(bool forceOutputScale)> applyOutputScale;
  AsyncWallpaperLoader* wallpaperLoader = nullptr;
  std::uint32_t wallpaperMaxLongEdge = 1920;
  std::filesystem::path wallpaperCacheDir;
};

/// Returns true when config was reloaded and applied.
bool maybeReloadCompositorConfig(CompositorConfigWatchContext& ctx);

void applyCompositorRuntimeConfig(CompositorConfigWatchContext& ctx, bool forceOutputScale = false);

} // namespace lambda::compositor
