#include <Lambda/Graphics/Image.hpp>

#if defined(LAMBDA_PLATFORM_LINUX_WAYLAND) || defined(LAMBDA_PLATFORM_LINUX_KMS)
#include <cairo.h>
#include <librsvg/rsvg.h>
#endif

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_STDIO
#include "stb_image.h"
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace lambda {

namespace {

#ifndef NDEBUG
void logLoadImageFailure(std::string_view path) {
  std::error_code ec;
  std::filesystem::path fsPath{std::string(path)};
  char const* reason = "decode failed";
  if (!std::filesystem::exists(fsPath, ec)) {
    reason = ec ? "path check failed" : "file does not exist";
  } else if (!std::filesystem::is_regular_file(fsPath, ec)) {
    reason = ec ? "file type check failed" : "not a regular file";
  } else if (std::filesystem::file_size(fsPath, ec) == 0) {
    reason = ec ? "file size check failed" : "empty file";
  }
  std::fprintf(stderr, "Lambda image loader: failed to load '%.*s': %s\n",
               static_cast<int>(path.size()),
               path.data(),
               reason);
}
#endif

bool isSvgPath(std::filesystem::path const& path) {
  std::string extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return extension == ".svg" || extension == ".svgz";
}

std::optional<DecodedImageRgba> decodeImageRgbaFromBytes(std::span<std::uint8_t const> data) {
  if (data.empty() || data.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return std::nullopt;
  }

  int width = 0;
  int height = 0;
  auto const* bytes = reinterpret_cast<stbi_uc const*>(data.data());
  stbi_uc* decoded = stbi_load_from_memory(bytes, static_cast<int>(data.size()), &width, &height, nullptr,
                                           STBI_rgb_alpha);
  if (!decoded || width <= 0 || height <= 0) {
    if (decoded) {
      stbi_image_free(decoded);
    }
    return std::nullopt;
  }

  std::size_t const pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  if (pixelCount > std::numeric_limits<std::size_t>::max() / 4u) {
    stbi_image_free(decoded);
    return std::nullopt;
  }

  DecodedImageRgba result{
      .width = static_cast<std::uint32_t>(width),
      .height = static_cast<std::uint32_t>(height),
      .pixels = {},
  };
  result.pixels.assign(decoded, decoded + pixelCount * 4u);
  stbi_image_free(decoded);
  return result;
}

#if defined(LAMBDA_PLATFORM_LINUX_WAYLAND) || defined(LAMBDA_PLATFORM_LINUX_KMS)
std::optional<DecodedImageRgba> decodeSvgRgbaFromFile(std::filesystem::path const& path,
                                                      std::uint32_t maxLongEdge) {
  GError* error = nullptr;
  RsvgHandle* handle = rsvg_handle_new_from_file(path.string().c_str(), &error);
  if (!handle) {
    if (error) g_error_free(error);
    return std::nullopt;
  }

  double intrinsicWidth = 0.0;
  double intrinsicHeight = 0.0;
  if (!rsvg_handle_get_intrinsic_size_in_pixels(handle, &intrinsicWidth, &intrinsicHeight) ||
      intrinsicWidth <= 0.0 || intrinsicHeight <= 0.0) {
    g_object_unref(handle);
    return std::nullopt;
  }

  double const scale = maxLongEdge > 0
                           ? static_cast<double>(maxLongEdge) / std::max(intrinsicWidth, intrinsicHeight)
                           : 1.0;
  std::uint32_t const width = std::min<std::uint32_t>(
      4096u, std::max(1u, static_cast<std::uint32_t>(std::ceil(intrinsicWidth * scale))));
  std::uint32_t const height = std::min<std::uint32_t>(
      4096u, std::max(1u, static_cast<std::uint32_t>(std::ceil(intrinsicHeight * scale))));
  if (static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) >
      static_cast<std::uint64_t>(std::numeric_limits<int>::max() / 4)) {
    g_object_unref(handle);
    return std::nullopt;
  }

  cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                        static_cast<int>(width),
                                                        static_cast<int>(height));
  if (!surface || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
    if (surface) cairo_surface_destroy(surface);
    g_object_unref(handle);
    return std::nullopt;
  }
  cairo_t* cr = cairo_create(surface);
  if (!cr || cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
    if (cr) cairo_destroy(cr);
    cairo_surface_destroy(surface);
    g_object_unref(handle);
    return std::nullopt;
  }

  RsvgRectangle viewport{
      .x = 0.0,
      .y = 0.0,
      .width = static_cast<double>(width),
      .height = static_cast<double>(height),
  };
  error = nullptr;
  gboolean const rendered = rsvg_handle_render_document(handle, cr, &viewport, &error);
  cairo_destroy(cr);
  g_object_unref(handle);
  if (!rendered) {
    if (error) g_error_free(error);
    cairo_surface_destroy(surface);
    return std::nullopt;
  }

  cairo_surface_flush(surface);
  unsigned char* data = cairo_image_surface_get_data(surface);
  int const stride = cairo_image_surface_get_stride(surface);
  if (!data || stride <= 0) {
    cairo_surface_destroy(surface);
    return std::nullopt;
  }

  DecodedImageRgba decoded{
      .width = width,
      .height = height,
      .pixels = std::vector<std::uint8_t>(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u),
  };
  for (std::uint32_t y = 0; y < height; ++y) {
    auto const* row = data + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
    for (std::uint32_t x = 0; x < width; ++x) {
      std::size_t const src = static_cast<std::size_t>(x) * 4u;
      std::size_t const dst = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                               static_cast<std::size_t>(x)) * 4u;
      std::uint8_t const b = row[src + 0u];
      std::uint8_t const g = row[src + 1u];
      std::uint8_t const r = row[src + 2u];
      std::uint8_t const a = row[src + 3u];
      decoded.pixels[dst + 3u] = a;
      if (a == 0) {
        decoded.pixels[dst + 0u] = 0;
        decoded.pixels[dst + 1u] = 0;
        decoded.pixels[dst + 2u] = 0;
      } else {
        decoded.pixels[dst + 0u] = static_cast<std::uint8_t>(std::min(255u, (static_cast<unsigned>(r) * 255u) / a));
        decoded.pixels[dst + 1u] = static_cast<std::uint8_t>(std::min(255u, (static_cast<unsigned>(g) * 255u) / a));
        decoded.pixels[dst + 2u] = static_cast<std::uint8_t>(std::min(255u, (static_cast<unsigned>(b) * 255u) / a));
      }
    }
  }
  cairo_surface_destroy(surface);
  return decoded;
}
#endif

DecodedImageRgba halveDecodedImageRgbaBox(DecodedImageRgba const& source) {
  std::uint32_t const srcW = source.width;
  std::uint32_t const srcH = source.height;
  std::uint32_t const dstW = std::max(1u, srcW / 2u);
  std::uint32_t const dstH = std::max(1u, srcH / 2u);
  DecodedImageRgba dst{
      .width = dstW,
      .height = dstH,
      .pixels = std::vector<std::uint8_t>(static_cast<std::size_t>(dstW) * static_cast<std::size_t>(dstH) * 4u),
  };

  for (std::uint32_t y = 0; y < dstH; ++y) {
    for (std::uint32_t x = 0; x < dstW; ++x) {
      std::uint32_t const sx = x * 2u;
      std::uint32_t const sy = y * 2u;
      std::uint32_t r = 0;
      std::uint32_t g = 0;
      std::uint32_t b = 0;
      std::uint32_t a = 0;
      std::uint32_t count = 0;
      for (std::uint32_t oy = 0; oy < 2u; ++oy) {
        for (std::uint32_t ox = 0; ox < 2u; ++ox) {
          std::uint32_t const px = sx + ox;
          std::uint32_t const py = sy + oy;
          if (px >= srcW || py >= srcH) {
            continue;
          }
          std::size_t const index =
              (static_cast<std::size_t>(py) * static_cast<std::size_t>(srcW) + static_cast<std::size_t>(px)) * 4u;
          r += source.pixels[index + 0u];
          g += source.pixels[index + 1u];
          b += source.pixels[index + 2u];
          a += source.pixels[index + 3u];
          ++count;
        }
      }
      if (count == 0) {
        count = 1;
      }
      std::size_t const dstIndex =
          (static_cast<std::size_t>(y) * static_cast<std::size_t>(dstW) + static_cast<std::size_t>(x)) * 4u;
      dst.pixels[dstIndex + 0u] = static_cast<std::uint8_t>(r / count);
      dst.pixels[dstIndex + 1u] = static_cast<std::uint8_t>(g / count);
      dst.pixels[dstIndex + 2u] = static_cast<std::uint8_t>(b / count);
      dst.pixels[dstIndex + 3u] = static_cast<std::uint8_t>(a / count);
    }
  }
  return dst;
}

} // namespace

DecodedImageRgba downscaleDecodedImageRgba(DecodedImageRgba image, std::uint32_t maxLongEdge) {
  if (image.width == 0 || image.height == 0 || maxLongEdge == 0) {
    return image;
  }
  while (std::max(image.width, image.height) > maxLongEdge * 2u) {
    image = halveDecodedImageRgbaBox(image);
  }
  if (std::max(image.width, image.height) <= maxLongEdge) {
    return image;
  }

  float const scale =
      static_cast<float>(maxLongEdge) / static_cast<float>(std::max(image.width, image.height));
  std::uint32_t const dstW = std::max(1u, static_cast<std::uint32_t>(static_cast<float>(image.width) * scale));
  std::uint32_t const dstH = std::max(1u, static_cast<std::uint32_t>(static_cast<float>(image.height) * scale));
  DecodedImageRgba dst{
      .width = dstW,
      .height = dstH,
      .pixels = std::vector<std::uint8_t>(static_cast<std::size_t>(dstW) * static_cast<std::size_t>(dstH) * 4u),
  };

  for (std::uint32_t y = 0; y < dstH; ++y) {
    float const srcY = (static_cast<float>(y) + 0.5f) / static_cast<float>(dstH) * static_cast<float>(image.height) -
                       0.5f;
    std::uint32_t const sy = std::min(image.height - 1u, static_cast<std::uint32_t>(std::max(0.f, srcY)));
    for (std::uint32_t x = 0; x < dstW; ++x) {
      float const srcX =
          (static_cast<float>(x) + 0.5f) / static_cast<float>(dstW) * static_cast<float>(image.width) - 0.5f;
      std::uint32_t const sx = std::min(image.width - 1u, static_cast<std::uint32_t>(std::max(0.f, srcX)));
      std::size_t const srcIndex =
          (static_cast<std::size_t>(sy) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(sx)) * 4u;
      std::size_t const dstIndex =
          (static_cast<std::size_t>(y) * static_cast<std::size_t>(dstW) + static_cast<std::size_t>(x)) * 4u;
      dst.pixels[dstIndex + 0u] = image.pixels[srcIndex + 0u];
      dst.pixels[dstIndex + 1u] = image.pixels[srcIndex + 1u];
      dst.pixels[dstIndex + 2u] = image.pixels[srcIndex + 2u];
      dst.pixels[dstIndex + 3u] = image.pixels[srcIndex + 3u];
    }
  }
  return dst;
}

bool Image::updateRgbaPixels(std::span<std::uint8_t const>, void*) {
  return false;
}

bool Image::updatePixels(std::span<std::uint8_t const> pixels, PixelFormat format, void* gpuDevice) {
  return format == PixelFormat::Rgba8888 && updateRgbaPixels(pixels, gpuDevice);
}

bool Image::updatePixelsRegion(std::span<std::uint8_t const>,
                               PixelFormat,
                               std::uint32_t,
                               std::uint32_t,
                               std::uint32_t,
                               std::uint32_t,
                               void*,
                               std::uint32_t) {
  return false;
}

std::optional<DecodedImageRgba> decodeImageRgbaFromFile(std::string_view path, std::uint32_t maxLongEdge) {
  std::filesystem::path const imagePath{std::string(path)};
#if defined(LAMBDA_PLATFORM_LINUX_WAYLAND) || defined(LAMBDA_PLATFORM_LINUX_KMS)
  if (isSvgPath(imagePath)) {
    return decodeSvgRgbaFromFile(imagePath, maxLongEdge);
  }
#endif
  std::ifstream in(imagePath, std::ios::binary);
  if (!in) {
    return std::nullopt;
  }

  std::error_code ec;
  auto const fileSize = std::filesystem::file_size(imagePath, ec);
  if (ec || fileSize == 0 || fileSize > static_cast<std::uintmax_t>(std::numeric_limits<int>::max())) {
    return std::nullopt;
  }

  std::vector<std::uint8_t> data(static_cast<std::size_t>(fileSize));
  in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
  if (in.gcount() != static_cast<std::streamsize>(data.size())) {
    return std::nullopt;
  }

  auto decoded = decodeImageRgbaFromBytes(data);
  if (decoded && maxLongEdge > 0 && std::max(decoded->width, decoded->height) > maxLongEdge) {
    *decoded = downscaleDecodedImageRgba(std::move(*decoded), maxLongEdge);
  }
  return decoded;
}

std::optional<DecodedImageRgba> decodeImageRgbaFromFile(std::string_view path) {
  return decodeImageRgbaFromFile(path, 0);
}

std::shared_ptr<Image> imageFromDecodedRgba(DecodedImageRgba const& decoded, void* gpuDevice) {
  if (decoded.width == 0 || decoded.height == 0 || decoded.pixels.empty()) {
    return nullptr;
  }
  std::span<std::uint8_t const> pixels(decoded.pixels.data(), decoded.pixels.size());
  return Image::fromRgbaPixels(decoded.width, decoded.height, pixels, gpuDevice);
}

std::shared_ptr<Image> loadImage(std::string_view path, void* gpuDevice) {
  std::optional<DecodedImageRgba> decoded = decodeImageRgbaFromFile(path);
  if (!decoded) {
#ifndef NDEBUG
    logLoadImageFailure(path);
#endif
    return nullptr;
  }
  return imageFromDecodedRgba(*decoded, gpuDevice);
}

std::shared_ptr<Image> loadImage(std::string_view path, void* gpuDevice, std::uint32_t maxLongEdge) {
  std::optional<DecodedImageRgba> decoded = decodeImageRgbaFromFile(path, maxLongEdge);
  if (!decoded) {
#ifndef NDEBUG
    logLoadImageFailure(path);
#endif
    return nullptr;
  }
  return imageFromDecodedRgba(*decoded, gpuDevice);
}

} // namespace lambda
