#include "Compositor/CompositorConfigWatch.hpp"

#include "Compositor/Config/AppliedCompositorConfig.hpp"
#include "Compositor/Config/WallpaperLoader.hpp"
#include "Compositor/CompositorPresentation.hpp"
#include "Graphics/Vulkan/VulkanCanvas.hpp"

namespace lambdaui::compositor {

void applyCompositorRuntimeConfig(CompositorConfigWatchContext& ctx, bool forceOutputScale) {
  auto const configStart = presentation::SteadyClock::now();
  ctx.appliedConfig = applyCompositorConfig(ctx.effectiveConfig(), ctx.canvas);
  lambdaui::setVulkanCanvasBackdropBlurBaseDownsample(&ctx.canvas,
                                                  ctx.appliedConfig.config.rendering.backdropBlurBaseDownsample);
  LAMBDA_WINDOW_MANAGER_TRACE_TIMING("apply-config", configStart);
  ctx.wayland.setShortcutBindings(ctx.appliedConfig.config.shortcutBindings);
  ctx.wayland.setChromeThemeConfig(ctx.appliedConfig.config.chrome, ctx.appliedConfig.config.darkChrome);
  ctx.wayland.setInputConfig({
      .popupGrabs = ctx.appliedConfig.config.popupGrabs,
      .keyboard = ctx.appliedConfig.config.keyboard,
  });
  ctx.applyOutputScale(forceOutputScale);
  if (ctx.wallpaperLoader && ctx.appliedConfig.config.wallpaperPath) {
    if (!tryLoadWallpaperFromCache(ctx)) {
      ctx.wallpaperLoader->requestLoad(*ctx.appliedConfig.config.wallpaperPath,
                                       ctx.wallpaperMaxLongEdge,
                                       ctx.wallpaperCacheDir);
    }
  } else if (ctx.wallpaperLoader) {
    ctx.wallpaperLoader->cancel();
    ctx.appliedConfig.wallpaperImage = nullptr;
    ctx.appliedConfig.wallpaperPreviewImage = nullptr;
    ctx.appliedConfig.wallpaperLoadPending = false;
    ctx.appliedConfig.wallpaperPreviewOpacity = 0.f;
    ctx.appliedConfig.wallpaperPreviewRevealStart.reset();
    ctx.appliedConfig.wallpaperRevealOpacity = 1.f;
    ctx.appliedConfig.wallpaperRevealStart.reset();
  }
}

bool maybeReloadCompositorConfig(CompositorConfigWatchContext& ctx) {
  if (!configChanged(ctx.loadedConfig)) return false;

  auto const previousOutputSelector = ctx.loadedConfig.config.outputSelector;
  ctx.loadedConfig = loadConfigWithMetadata();
  if (ctx.loadedConfig.config.outputSelector != previousOutputSelector) {
    std::fprintf(stderr,
                 "lambda-window-manager: output selector changed; restart required to move compositor outputs\n");
  }
  applyCompositorRuntimeConfig(ctx);
  return true;
}

} // namespace lambdaui::compositor
