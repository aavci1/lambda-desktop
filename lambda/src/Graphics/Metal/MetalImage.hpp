#pragma once

#include <Lambda/Graphics/Image.hpp>

#import <Metal/Metal.h>

namespace lambdaui {

/// Metal-backed `Image` (BGRA/RGBA texture from disk loader).
class MetalImage final : public Image {
public:
  explicit MetalImage(id<MTLTexture> texture);
  MetalImage(id<MTLTexture> texture, std::uint32_t width, std::uint32_t height);

  Size size() const override;
  bool updateRgbaPixels(std::span<std::uint8_t const> rgbaPixels, void* gpuDevice = nullptr) override;
  bool updatePixels(std::span<std::uint8_t const> pixels,
                    PixelFormat format,
                    void* gpuDevice = nullptr) override;
  bool updatePixelsRegion(std::span<std::uint8_t const> pixels,
                          PixelFormat format,
                          std::uint32_t x,
                          std::uint32_t y,
                          std::uint32_t width,
                          std::uint32_t height,
                          void* gpuDevice = nullptr,
                          std::uint32_t sourceBytesPerRow = 0) override;

  id<MTLTexture> texture() const { return texture_; }

private:
  id<MTLTexture> texture_{nil};
  std::uint32_t widthOverride_ = 0;
  std::uint32_t heightOverride_ = 0;
};

/// Returns null if `image` is not a `MetalImage`.
MetalImage const* tryMetalImage(Image const& image);

} // namespace lambdaui
