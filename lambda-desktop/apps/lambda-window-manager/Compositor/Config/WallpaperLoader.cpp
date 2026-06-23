#include "Compositor/Config/WallpaperLoader.hpp"

#include "Compositor/CompositorConfigWatch.hpp"
#include "Compositor/Config/AppliedCompositorConfig.hpp"
#include "Compositor/Config/WallpaperCache.hpp"
#include "Compositor/CompositorPresentation.hpp"

#include <cstdio>
#include <utility>

namespace lambdaui::compositor {

AsyncWallpaperLoader::~AsyncWallpaperLoader() {
  cancel();
}

void AsyncWallpaperLoader::cancel() {
  cancel_.store(true, std::memory_order_release);
  if (worker_.joinable()) {
    worker_.join();
  }
  loading_.store(false, std::memory_order_release);
  std::lock_guard const lock(mutex_);
  previewReady_.reset();
  ready_.reset();
}

void AsyncWallpaperLoader::requestLoad(std::string path,
                                       std::uint32_t maxLongEdge,
                                       std::filesystem::path cacheRoot) {
  cancel();
  cancel_.store(false, std::memory_order_release);
  loading_.store(true, std::memory_order_release);
  worker_ = std::thread([this, path = std::move(path), maxLongEdge, cacheRoot = std::move(cacheRoot)]() mutable {
    workerMain(std::move(path), maxLongEdge, std::move(cacheRoot));
  });
}

void AsyncWallpaperLoader::workerMain(std::string path,
                                      std::uint32_t maxLongEdge,
                                      std::filesystem::path cacheRoot) {
  auto const start = presentation::SteadyClock::now();
  std::filesystem::path const sourcePath{path};

  std::optional<lambdaui::DecodedImageRgba> decoded =
      readWallpaperCache(sourcePath, maxLongEdge, cacheRoot);
  bool fromCache = decoded.has_value();

  if (!decoded) {
    decoded = lambdaui::decodeImageRgbaFromFile(path);
    if (decoded) {
      auto const preview = lambdaui::downscaleDecodedImageRgba(*decoded, wallpaperPreviewMaxEdge());
      if (!cancel_.load(std::memory_order_acquire)) {
        std::lock_guard const lock(mutex_);
        previewReady_ = preview;
      }

      auto const beforeDownscale = presentation::SteadyClock::now();
      *decoded = lambdaui::downscaleDecodedImageRgba(std::move(*decoded), maxLongEdge);
      if (presentation::timingTraceEnabled()) {
        LAMBDA_WINDOW_MANAGER_TRACE_TIMING("wallpaper-downscale", beforeDownscale);
      }
      if (!writeWallpaperCache(sourcePath, maxLongEdge, cacheRoot, *decoded)) {
        std::fprintf(stderr, "lambda-window-manager: failed to write wallpaper cache for %s\n", path.c_str());
      }
    }
  }

  double const decodeMs = presentation::elapsedMilliseconds(start);
  if (cancel_.load(std::memory_order_acquire)) {
    loading_.store(false, std::memory_order_release);
    return;
  }

  std::lock_guard const lock(mutex_);
  if (decoded) {
    ready_ = std::move(*decoded);
    lastDecodeMs_ = decodeMs;
    std::fprintf(stderr,
                 "lambda-window-manager: wallpaper %s %ux%u in %.1fms (%s)\n",
                 path.c_str(),
                 ready_->width,
                 ready_->height,
                 decodeMs,
                 fromCache ? "cache" : "decoded");
  } else {
    std::fprintf(stderr, "lambda-window-manager: failed to decode wallpaper %s\n", path.c_str());
    lastDecodeMs_ = decodeMs;
  }
  loading_.store(false, std::memory_order_release);
}

std::optional<lambdaui::DecodedImageRgba> AsyncWallpaperLoader::takePreview() {
  std::lock_guard const lock(mutex_);
  return std::exchange(previewReady_, std::nullopt);
}

std::optional<lambdaui::DecodedImageRgba> AsyncWallpaperLoader::takeReady(double* decodeMilliseconds) {
  std::lock_guard const lock(mutex_);
  if (!ready_) {
    return std::nullopt;
  }
  if (decodeMilliseconds) {
    *decodeMilliseconds = lastDecodeMs_;
  }
  return std::exchange(ready_, std::nullopt);
}

bool tryLoadWallpaperFromCache(CompositorConfigWatchContext& ctx) {
  if (!ctx.appliedConfig.config.wallpaperPath || ctx.wallpaperCacheDir.empty()) {
    return false;
  }

  auto const cacheStart = presentation::SteadyClock::now();
  std::optional<lambdaui::DecodedImageRgba> decoded = readWallpaperCache(
      *ctx.appliedConfig.config.wallpaperPath, ctx.wallpaperMaxLongEdge, ctx.wallpaperCacheDir);
  if (!decoded) {
    return false;
  }

  ctx.appliedConfig.wallpaperImage =
      lambdaui::imageFromDecodedRgba(*decoded, ctx.canvas.gpuDevice());
  if (!ctx.appliedConfig.wallpaperImage) {
    return false;
  }

  ctx.appliedConfig.wallpaperLoadPending = false;
  ctx.appliedConfig.wallpaperPreviewImage.reset();
  ctx.appliedConfig.wallpaperPreviewOpacity = 0.f;
  ctx.appliedConfig.wallpaperPreviewRevealStart.reset();
  ctx.appliedConfig.wallpaperRevealOpacity = 1.f;
  ctx.appliedConfig.wallpaperRevealStart.reset();
  LAMBDA_WINDOW_MANAGER_TRACE_TIMING("wallpaper-cache", cacheStart);
  std::fprintf(stderr,
               "lambda-window-manager: wallpaper loaded from cache %ux%u\n",
               decoded->width,
               decoded->height);
  return true;
}

bool pollWallpaperPreview(CompositorConfigWatchContext& ctx) {
  if (!ctx.wallpaperLoader) {
    return false;
  }

  std::optional<lambdaui::DecodedImageRgba> decoded = ctx.wallpaperLoader->takePreview();
  if (!decoded) {
    return false;
  }

  auto const uploadStart = presentation::SteadyClock::now();
  ctx.appliedConfig.wallpaperPreviewImage = lambdaui::imageFromDecodedRgba(*decoded, ctx.canvas.gpuDevice());
  LAMBDA_WINDOW_MANAGER_TRACE_TIMING("wallpaper-preview-upload", uploadStart);
  if (!ctx.appliedConfig.wallpaperPreviewImage) {
    return false;
  }

  startWallpaperPreviewReveal(ctx.appliedConfig);
  return true;
}

bool pollWallpaperLoad(CompositorConfigWatchContext& ctx) {
  if (!ctx.wallpaperLoader) {
    return false;
  }

  double decodeMs = 0.0;
  std::optional<lambdaui::DecodedImageRgba> decoded = ctx.wallpaperLoader->takeReady(&decodeMs);
  if (!decoded) {
    return false;
  }

  auto const uploadStart = presentation::SteadyClock::now();
  ctx.appliedConfig.wallpaperImage = lambdaui::imageFromDecodedRgba(*decoded, ctx.canvas.gpuDevice());
  LAMBDA_WINDOW_MANAGER_TRACE_TIMING("wallpaper-upload", uploadStart);
  if (presentation::timingTraceEnabled()) {
    std::fprintf(stderr,
                 "lambda-window-manager: timing wallpaper-decode %.3fms (async)\n",
                 decodeMs);
  }
  if (!ctx.appliedConfig.wallpaperImage) {
    return false;
  }

  startWallpaperReveal(ctx.appliedConfig);
  return true;
}

} // namespace lambdaui::compositor
