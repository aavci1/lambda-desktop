#pragma once

#include <Lambda/Graphics/Image.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace lambdaui::compositor {

struct CompositorConfigWatchContext;

/// Decodes wallpaper files on a worker thread so compositor startup is not blocked.
class AsyncWallpaperLoader {
public:
  AsyncWallpaperLoader() = default;
  ~AsyncWallpaperLoader();

  AsyncWallpaperLoader(AsyncWallpaperLoader const&) = delete;
  AsyncWallpaperLoader& operator=(AsyncWallpaperLoader const&) = delete;

  void requestLoad(std::string path, std::uint32_t maxLongEdge, std::filesystem::path cacheRoot);
  void cancel();

  /// Returns a low-res preview after decode. Call from the render thread only.
  [[nodiscard]] std::optional<lambdaui::DecodedImageRgba> takePreview();

  /// Returns decoded pixels when a background load finishes. Call from the render thread only.
  [[nodiscard]] std::optional<lambdaui::DecodedImageRgba> takeReady(double* decodeMilliseconds = nullptr);

  [[nodiscard]] bool isLoading() const { return loading_.load(std::memory_order_acquire); }

private:
  void workerMain(std::string path, std::uint32_t maxLongEdge, std::filesystem::path cacheRoot);

  mutable std::mutex mutex_;
  std::optional<lambdaui::DecodedImageRgba> previewReady_;
  std::optional<lambdaui::DecodedImageRgba> ready_;
  double lastDecodeMs_ = 0.0;
  std::atomic<bool> loading_{false};
  std::atomic<bool> cancel_{false};
  std::thread worker_;
};

[[nodiscard]] bool tryLoadWallpaperFromCache(CompositorConfigWatchContext& ctx);

[[nodiscard]] bool pollWallpaperPreview(CompositorConfigWatchContext& ctx);

[[nodiscard]] bool pollWallpaperLoad(CompositorConfigWatchContext& ctx);

} // namespace lambdaui::compositor
