#pragma once

#include <Flux/Core/Geometry.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace flux::compositor {

enum class ScreenshotMode : std::uint8_t {
  FullOutput,
  ActiveWindow,
  Region,
};

struct ScreenshotRegion {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t width = 0;
  std::int32_t height = 0;

  constexpr bool operator==(ScreenshotRegion const&) const = default;
};

struct ScreenshotRequest {
  ScreenshotMode mode = ScreenshotMode::FullOutput;
  std::optional<ScreenshotRegion> region;
  bool includeCursor = true;

  constexpr bool operator==(ScreenshotRequest const&) const = default;
};

struct ScreenshotSelectionOverlay {
  bool dragging = false;
  float startX = 0.f;
  float startY = 0.f;
  float currentX = 0.f;
  float currentY = 0.f;
  std::optional<ScreenshotRegion> region;
};

struct ScreenshotImage {
  std::vector<std::uint8_t> pixels;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

struct ScreenshotSaveResult {
  std::filesystem::path path;
  std::string error;
};

[[nodiscard]] std::optional<ScreenshotRegion> normalizeScreenshotRegion(ScreenshotRegion region,
                                                                         std::int32_t boundsWidth,
                                                                         std::int32_t boundsHeight);
[[nodiscard]] std::optional<ScreenshotRegion> screenshotSelectionRegion(float startX,
                                                                        float startY,
                                                                        float currentX,
                                                                        float currentY,
                                                                        std::int32_t boundsWidth,
                                                                        std::int32_t boundsHeight,
                                                                        std::int32_t minimumSize = 2);
[[nodiscard]] std::optional<ScreenshotRegion> logicalRegionToFramebuffer(ScreenshotRegion region,
                                                                         std::int32_t logicalWidth,
                                                                         std::int32_t logicalHeight,
                                                                         std::uint32_t framebufferWidth,
                                                                         std::uint32_t framebufferHeight);
[[nodiscard]] ScreenshotRequest makeScreenshotRequest(ScreenshotMode mode,
                                                      std::optional<ScreenshotRegion> region = std::nullopt);
[[nodiscard]] std::optional<ScreenshotImage> cropBgra(std::vector<std::uint8_t> const& bgra,
                                                      std::uint32_t width,
                                                      std::uint32_t height,
                                                      ScreenshotRegion region);
void maskBgraToRoundedRect(std::vector<std::uint8_t>& bgra,
                           std::uint32_t width,
                           std::uint32_t height,
                           CornerRadius corners);
[[nodiscard]] std::filesystem::path defaultScreenshotPath();
[[nodiscard]] ScreenshotSaveResult saveScreenshotPng(std::filesystem::path path,
                                                     std::vector<std::uint8_t> const& bgra,
                                                     std::uint32_t width,
                                                     std::uint32_t height);
[[nodiscard]] ScreenshotSaveResult saveScreenshotPng(std::vector<std::uint8_t> const& bgra,
                                                     std::uint32_t width,
                                                     std::uint32_t height);

} // namespace flux::compositor
