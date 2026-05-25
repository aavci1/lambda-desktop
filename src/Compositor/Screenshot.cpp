#include "Compositor/Screenshot.hpp"

#include <zlib.h>

#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <utility>

namespace flux::compositor {
namespace {

void writeBigEndian(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 24u) & 0xffu));
  out.push_back(static_cast<std::uint8_t>((value >> 16u) & 0xffu));
  out.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
  out.push_back(static_cast<std::uint8_t>(value & 0xffu));
}

void appendChunk(std::vector<std::uint8_t>& out, char const* type, std::vector<std::uint8_t> const& data) {
  writeBigEndian(out, static_cast<std::uint32_t>(data.size()));
  std::size_t const typeOffset = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), data.begin(), data.end());
  std::uint32_t crc = crc32(0u, nullptr, 0u);
  crc = crc32(crc, out.data() + typeOffset, static_cast<uInt>(4u + data.size()));
  writeBigEndian(out, crc);
}

std::filesystem::path homeDirectory() {
  if (char const* home = std::getenv("HOME"); home && *home) {
    return home;
  }
  return ".";
}

std::string timestampName() {
  auto const now = std::chrono::system_clock::now();
  std::time_t const time = std::chrono::system_clock::to_time_t(now);
  std::tm local{};
  localtime_r(&time, &local);
  char buffer[64]{};
  if (std::strftime(buffer, sizeof(buffer), "Screenshot %Y-%m-%d at %H.%M.%S.png", &local) == 0) {
    return "Screenshot.png";
  }
  return buffer;
}

std::filesystem::path uniquePath(std::filesystem::path path) {
  if (!std::filesystem::exists(path)) {
    return path;
  }
  std::filesystem::path const parent = path.parent_path();
  std::string const stem = path.stem().string();
  std::string const extension = path.extension().string();
  for (int index = 2; index < 1000; ++index) {
    std::filesystem::path candidate = parent / (stem + " " + std::to_string(index) + extension);
    if (!std::filesystem::exists(candidate)) {
      return candidate;
    }
  }
  return path;
}

void writePng(std::filesystem::path const& path,
              std::vector<std::uint8_t> const& bgra,
              std::uint32_t width,
              std::uint32_t height) {
  if (width == 0 || height == 0 || bgra.size() < static_cast<std::size_t>(width) * height * 4u) {
    throw std::runtime_error("invalid screenshot pixels");
  }
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path());
  }

  std::vector<std::uint8_t> scanlines;
  scanlines.reserve(static_cast<std::size_t>(height) * (1u + static_cast<std::size_t>(width) * 4u));
  for (std::uint32_t y = 0; y < height; ++y) {
    scanlines.push_back(0);
    for (std::uint32_t x = 0; x < width; ++x) {
      std::size_t const offset = (static_cast<std::size_t>(y) * width + x) * 4u;
      scanlines.push_back(bgra[offset + 2u]);
      scanlines.push_back(bgra[offset + 1u]);
      scanlines.push_back(bgra[offset + 0u]);
      scanlines.push_back(bgra[offset + 3u]);
    }
  }

  uLongf compressedSize = compressBound(static_cast<uLong>(scanlines.size()));
  std::vector<std::uint8_t> compressed(compressedSize);
  int const result = compress2(compressed.data(),
                               &compressedSize,
                               scanlines.data(),
                               static_cast<uLong>(scanlines.size()),
                               Z_BEST_SPEED);
  if (result != Z_OK) {
    throw std::runtime_error("failed to compress screenshot PNG");
  }
  compressed.resize(compressedSize);

  std::vector<std::uint8_t> png{137, 80, 78, 71, 13, 10, 26, 10};
  std::vector<std::uint8_t> ihdr;
  writeBigEndian(ihdr, width);
  writeBigEndian(ihdr, height);
  ihdr.push_back(8);
  ihdr.push_back(6);
  ihdr.push_back(0);
  ihdr.push_back(0);
  ihdr.push_back(0);
  appendChunk(png, "IHDR", ihdr);
  appendChunk(png, "IDAT", compressed);
  appendChunk(png, "IEND", {});

  std::ofstream output(path, std::ios::binary);
  if (!output) {
    throw std::runtime_error("failed to open screenshot output");
  }
  output.write(reinterpret_cast<char const*>(png.data()), static_cast<std::streamsize>(png.size()));
  if (!output) {
    throw std::runtime_error("failed to write screenshot PNG");
  }
}

float coverageForCorner(float x, float y, float centerX, float centerY, float radius) {
  if (radius <= 0.f) return 1.f;
  float const dx = x - centerX;
  float const dy = y - centerY;
  float const signedDistance = std::sqrt(dx * dx + dy * dy) - radius;
  return std::clamp(0.5f - signedDistance, 0.f, 1.f);
}

float roundedRectCoverage(float x, float y, float width, float height, CornerRadius corners) {
  corners.topLeft = std::clamp(corners.topLeft, 0.f, std::min(width, height) * 0.5f);
  corners.topRight = std::clamp(corners.topRight, 0.f, std::min(width, height) * 0.5f);
  corners.bottomRight = std::clamp(corners.bottomRight, 0.f, std::min(width, height) * 0.5f);
  corners.bottomLeft = std::clamp(corners.bottomLeft, 0.f, std::min(width, height) * 0.5f);

  if (corners.topLeft > 0.f && x < corners.topLeft && y < corners.topLeft) {
    return coverageForCorner(x, y, corners.topLeft, corners.topLeft, corners.topLeft);
  }
  if (corners.topRight > 0.f && x >= width - corners.topRight && y < corners.topRight) {
    return coverageForCorner(x, y, width - corners.topRight, corners.topRight, corners.topRight);
  }
  if (corners.bottomRight > 0.f && x >= width - corners.bottomRight && y >= height - corners.bottomRight) {
    return coverageForCorner(x, y, width - corners.bottomRight, height - corners.bottomRight, corners.bottomRight);
  }
  if (corners.bottomLeft > 0.f && x < corners.bottomLeft && y >= height - corners.bottomLeft) {
    return coverageForCorner(x, y, corners.bottomLeft, height - corners.bottomLeft, corners.bottomLeft);
  }
  return 1.f;
}

} // namespace

std::optional<ScreenshotRegion> normalizeScreenshotRegion(ScreenshotRegion region,
                                                          std::int32_t boundsWidth,
                                                          std::int32_t boundsHeight) {
  if (boundsWidth <= 0 || boundsHeight <= 0) return std::nullopt;

  std::int64_t const left = std::min<std::int64_t>(region.x, static_cast<std::int64_t>(region.x) + region.width);
  std::int64_t const right = std::max<std::int64_t>(region.x, static_cast<std::int64_t>(region.x) + region.width);
  std::int64_t const top = std::min<std::int64_t>(region.y, static_cast<std::int64_t>(region.y) + region.height);
  std::int64_t const bottom = std::max<std::int64_t>(region.y, static_cast<std::int64_t>(region.y) + region.height);

  std::int32_t const x0 =
      static_cast<std::int32_t>(std::clamp(left, std::int64_t{0}, static_cast<std::int64_t>(boundsWidth)));
  std::int32_t const x1 =
      static_cast<std::int32_t>(std::clamp(right, std::int64_t{0}, static_cast<std::int64_t>(boundsWidth)));
  std::int32_t const y0 =
      static_cast<std::int32_t>(std::clamp(top, std::int64_t{0}, static_cast<std::int64_t>(boundsHeight)));
  std::int32_t const y1 =
      static_cast<std::int32_t>(std::clamp(bottom, std::int64_t{0}, static_cast<std::int64_t>(boundsHeight)));
  if (x1 <= x0 || y1 <= y0) return std::nullopt;

  return ScreenshotRegion{
      .x = x0,
      .y = y0,
      .width = x1 - x0,
      .height = y1 - y0,
  };
}

std::optional<ScreenshotRegion> logicalRegionToFramebuffer(ScreenshotRegion region,
                                                           std::int32_t logicalWidth,
                                                           std::int32_t logicalHeight,
                                                           std::uint32_t framebufferWidth,
                                                           std::uint32_t framebufferHeight) {
  if (logicalWidth <= 0 || logicalHeight <= 0 || framebufferWidth == 0 || framebufferHeight == 0) {
    return std::nullopt;
  }
  auto logical = normalizeScreenshotRegion(region, logicalWidth, logicalHeight);
  if (!logical) return std::nullopt;

  double const scaleX = static_cast<double>(framebufferWidth) / static_cast<double>(logicalWidth);
  double const scaleY = static_cast<double>(framebufferHeight) / static_cast<double>(logicalHeight);
  std::int32_t const x0 = std::clamp(static_cast<std::int32_t>(std::floor(logical->x * scaleX)),
                                     0,
                                     static_cast<std::int32_t>(framebufferWidth));
  std::int32_t const y0 = std::clamp(static_cast<std::int32_t>(std::floor(logical->y * scaleY)),
                                     0,
                                     static_cast<std::int32_t>(framebufferHeight));
  std::int32_t const x1 =
      std::clamp(static_cast<std::int32_t>(std::ceil((logical->x + logical->width) * scaleX)),
                 0,
                 static_cast<std::int32_t>(framebufferWidth));
  std::int32_t const y1 =
      std::clamp(static_cast<std::int32_t>(std::ceil((logical->y + logical->height) * scaleY)),
                 0,
                 static_cast<std::int32_t>(framebufferHeight));
  if (x1 <= x0 || y1 <= y0) return std::nullopt;
  return ScreenshotRegion{.x = x0, .y = y0, .width = x1 - x0, .height = y1 - y0};
}

std::optional<ScreenshotImage> cropBgra(std::vector<std::uint8_t> const& bgra,
                                        std::uint32_t width,
                                        std::uint32_t height,
                                        ScreenshotRegion region) {
  if (width == 0 || height == 0 || bgra.size() < static_cast<std::size_t>(width) * height * 4u) {
    return std::nullopt;
  }
  auto normalized = normalizeScreenshotRegion(region,
                                             static_cast<std::int32_t>(width),
                                             static_cast<std::int32_t>(height));
  if (!normalized) return std::nullopt;

  ScreenshotImage image{
      .pixels = {},
      .width = static_cast<std::uint32_t>(normalized->width),
      .height = static_cast<std::uint32_t>(normalized->height),
  };
  image.pixels.resize(static_cast<std::size_t>(image.width) * image.height * 4u);
  for (std::uint32_t y = 0; y < image.height; ++y) {
    std::size_t const sourceOffset =
        (static_cast<std::size_t>(normalized->y + static_cast<std::int32_t>(y)) * width +
         static_cast<std::size_t>(normalized->x)) *
        4u;
    std::size_t const destinationOffset = static_cast<std::size_t>(y) * image.width * 4u;
    std::copy_n(bgra.data() + sourceOffset, static_cast<std::size_t>(image.width) * 4u,
                image.pixels.data() + destinationOffset);
  }
  return image;
}

void maskBgraToRoundedRect(std::vector<std::uint8_t>& bgra,
                           std::uint32_t width,
                           std::uint32_t height,
                           CornerRadius corners) {
  if (width == 0 || height == 0 || bgra.size() < static_cast<std::size_t>(width) * height * 4u ||
      corners.isZero()) {
    return;
  }

  float const rectWidth = static_cast<float>(width);
  float const rectHeight = static_cast<float>(height);
  for (std::uint32_t y = 0; y < height; ++y) {
    for (std::uint32_t x = 0; x < width; ++x) {
      float const coverage = roundedRectCoverage(static_cast<float>(x) + 0.5f,
                                                static_cast<float>(y) + 0.5f,
                                                rectWidth,
                                                rectHeight,
                                                corners);
      if (coverage >= 1.f) continue;
      std::size_t const offset = (static_cast<std::size_t>(y) * width + x) * 4u;
      if (coverage <= 0.f) {
        bgra[offset + 0u] = 0u;
        bgra[offset + 1u] = 0u;
        bgra[offset + 2u] = 0u;
        bgra[offset + 3u] = 0u;
      } else {
        bgra[offset + 3u] = static_cast<std::uint8_t>(
            std::clamp(std::lround(static_cast<float>(bgra[offset + 3u]) * coverage), 0l, 255l));
      }
    }
  }
}

std::filesystem::path defaultScreenshotPath() {
  return uniquePath(homeDirectory() / "Pictures" / "Screenshots" / timestampName());
}

ScreenshotSaveResult saveScreenshotPng(std::vector<std::uint8_t> const& bgra,
                                       std::uint32_t width,
                                       std::uint32_t height) {
  return saveScreenshotPng(defaultScreenshotPath(), bgra, width, height);
}

ScreenshotSaveResult saveScreenshotPng(std::filesystem::path path,
                                       std::vector<std::uint8_t> const& bgra,
                                       std::uint32_t width,
                                       std::uint32_t height) {
  ScreenshotSaveResult result{.path = std::move(path), .error = {}};
  try {
    writePng(result.path, bgra, width, height);
    return result;
  } catch (std::exception const& e) {
    result.error = e.what();
    return result;
  } catch (...) {
    result.error = "unknown screenshot error";
    return result;
  }
}

} // namespace flux::compositor
