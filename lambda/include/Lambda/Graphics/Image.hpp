#pragma once

/// \file Lambda/Graphics/Image.hpp
///
/// Part of the Lambda public API.


#include <Lambda/Core/Geometry.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#if LAMBDAUI_VULKAN
#include <vulkan/vulkan.h>
#endif

namespace lambdaui {

/// Abstract image reference; pixel dimensions drive UV normalization in `Canvas::drawImage`.
class Image : public std::enable_shared_from_this<Image> {
public:
  enum class PixelFormat : std::uint8_t {
    Rgba8888,
    Bgra8888,
  };

  virtual ~Image() = default;

  Image(Image const&) = delete;
  Image& operator=(Image const&) = delete;

  virtual Size size() const = 0;

  /// Replace this image with same-size tightly packed 8-bit RGBA pixels.
  /// Returns false when the backend image is immutable or the size does not match.
  virtual bool updateRgbaPixels(std::span<std::uint8_t const> rgbaPixels, void* gpuDevice = nullptr);

  /// Replace this image with same-size tightly packed 8-bit pixels in `format`.
  /// Returns false when the backend image is immutable, the format is incompatible,
  /// or the size does not match.
  virtual bool updatePixels(std::span<std::uint8_t const> pixels,
                            PixelFormat format,
                            void* gpuDevice = nullptr);

  /// Replace a same-format sub-rectangle with 8-bit pixels.
  /// `pixels` must contain either tightly packed rows or enough bytes for
  /// `sourceBytesPerRow * (height - 1) + width * 4`. Returns false
  /// when the backend cannot update regions, the format is incompatible, or
  /// the rectangle is outside the image bounds.
  virtual bool updatePixelsRegion(std::span<std::uint8_t const> pixels,
                                  PixelFormat format,
                                  std::uint32_t x,
                                  std::uint32_t y,
                                  std::uint32_t width,
                                  std::uint32_t height,
                                  void* gpuDevice = nullptr,
                                  std::uint32_t sourceBytesPerRow = 0);

  /// Create an image from tightly packed 8-bit RGBA pixels.
  /// `rgbaPixels` must contain exactly width * height * 4 bytes.
  /// Metal uses `gpuDevice` as an optional id<MTLDevice>; other backends ignore it.
  static std::shared_ptr<Image> fromRgbaPixels(std::uint32_t width, std::uint32_t height,
                                               std::span<std::uint8_t const> rgbaPixels,
                                               void* gpuDevice = nullptr);

  /// Create an image from tightly packed 8-bit pixels in `format`.
  /// `pixels` must contain exactly width * height * 4 bytes.
  static std::shared_ptr<Image> fromPixels(std::uint32_t width,
                                           std::uint32_t height,
                                           std::span<std::uint8_t const> pixels,
                                           PixelFormat format,
                                           void* gpuDevice = nullptr);

#if LAMBDAUI_VULKAN
  struct DmabufPlane {
    int fd = -1;
    std::uint32_t offset = 0;
    std::uint32_t stride = 0;
    std::uint64_t modifier = 0;
  };

  struct DmabufImageSpec {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t drmFormat = 0;
    std::span<DmabufPlane const> planes;
  };

  /// Create an image reference backed by caller-owned Vulkan resources.
  /// The VkImage and VkImageView must outlive all rendering that references the returned Image.
  static std::shared_ptr<Image> fromExternalVulkan(VkImage image, VkImageView view, VkFormat format,
                                                   std::uint32_t width, std::uint32_t height);

  /// Import a single-plane Linux dma-buf as a Vulkan sampled image on Linux (LAMBDAUI_VULKAN).
  /// Not available on macOS/Metal builds: callers must guard with `#if LAMBDAUI_VULKAN` or platform checks.
  /// The supplied plane fd is consumed by this call whether import succeeds or fails.
  static std::shared_ptr<Image> fromDmabuf(DmabufImageSpec const& spec);
#endif

#if LAMBDAUI_METAL
  /// Create an image reference backed by a caller-owned id<MTLTexture>.
  /// The texture must outlive all rendering that references the returned Image.
  static std::shared_ptr<Image> fromExternalMetal(void* texture, std::uint32_t width, std::uint32_t height);
#endif

protected:
  Image() = default;
};

/// CPU-side RGBA decode result. Safe to produce off the render thread.
struct DecodedImageRgba {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<std::uint8_t> pixels;
};

/// Downscales decoded RGBA pixels so the longest edge is at most \p maxLongEdge.
[[nodiscard]] lambdaui::DecodedImageRgba downscaleDecodedImageRgba(lambdaui::DecodedImageRgba image,
                                                               std::uint32_t maxLongEdge);

/// Decodes an image file to tightly packed RGBA pixels without creating a GPU image.
[[nodiscard]] std::optional<DecodedImageRgba> decodeImageRgbaFromFile(std::string_view path);
[[nodiscard]] std::optional<DecodedImageRgba> decodeImageRgbaFromFile(std::string_view path,
                                                                       std::uint32_t maxLongEdge);

/// Creates a GPU image from decoded RGBA pixels. Must run on the render thread.
[[nodiscard]] std::shared_ptr<Image> imageFromDecodedRgba(DecodedImageRgba const& decoded,
                                                          void* gpuDevice = nullptr);

/// Loads an image from disk into a backend image.
/// `gpuDevice` must match the target canvas device when the backend requires it.
std::shared_ptr<Image> loadImage(std::string_view path, void* gpuDevice = nullptr);
std::shared_ptr<Image> loadImage(std::string_view path, void* gpuDevice, std::uint32_t maxLongEdge);

} // namespace lambdaui
