#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "Graphics/Metal/MetalImage.hpp"

namespace lambda {

MetalImage::MetalImage(id<MTLTexture> texture) : texture_(texture) {}

MetalImage::MetalImage(id<MTLTexture> texture, std::uint32_t width, std::uint32_t height)
    : texture_(texture), widthOverride_(width), heightOverride_(height) {}

Size MetalImage::size() const {
  if (widthOverride_ > 0 && heightOverride_ > 0) {
    return Size{static_cast<float>(widthOverride_), static_cast<float>(heightOverride_)};
  }
  if (!texture_) {
    return {};
  }
  return Size{static_cast<float>(texture_.width), static_cast<float>(texture_.height)};
}

bool MetalImage::updateRgbaPixels(std::span<std::uint8_t const> rgbaPixels, void*) {
  return updatePixels(rgbaPixels, PixelFormat::Rgba8888, nullptr);
}

bool MetalImage::updatePixels(std::span<std::uint8_t const> pixels, PixelFormat format, void*) {
  if (!texture_) {
    return false;
  }
  MTLPixelFormat const expectedFormat =
      format == PixelFormat::Bgra8888 ? MTLPixelFormatBGRA8Unorm : MTLPixelFormatRGBA8Unorm;
  if (texture_.pixelFormat != expectedFormat) {
    return false;
  }
  Size const imageSize = size();
  auto const width = static_cast<std::uint32_t>(imageSize.width);
  auto const height = static_cast<std::uint32_t>(imageSize.height);
  std::size_t const expectedSize = static_cast<std::size_t>(width) * height * 4u;
  if (width == 0 || height == 0 || pixels.size() != expectedSize) {
    return false;
  }
  MTLRegion region = MTLRegionMake2D(0, 0, width, height);
  [texture_ replaceRegion:region mipmapLevel:0 withBytes:pixels.data() bytesPerRow:width * 4u];
  return true;
}

bool MetalImage::updatePixelsRegion(std::span<std::uint8_t const> pixels,
                                    PixelFormat format,
                                    std::uint32_t x,
                                    std::uint32_t y,
                                    std::uint32_t width,
                                    std::uint32_t height,
                                    void*,
                                    std::uint32_t sourceBytesPerRow) {
  if (!texture_ || width == 0 || height == 0) {
    return false;
  }
  MTLPixelFormat const expectedFormat =
      format == PixelFormat::Bgra8888 ? MTLPixelFormatBGRA8Unorm : MTLPixelFormatRGBA8Unorm;
  if (texture_.pixelFormat != expectedFormat) {
    return false;
  }
  Size const imageSize = size();
  auto const imageWidth = static_cast<std::uint32_t>(imageSize.width);
  auto const imageHeight = static_cast<std::uint32_t>(imageSize.height);
  if (x > imageWidth || y > imageHeight || width > imageWidth - x || height > imageHeight - y) {
    return false;
  }
  std::size_t const rowBytes = static_cast<std::size_t>(width) * 4u;
  std::size_t const stride = sourceBytesPerRow == 0 ? rowBytes : sourceBytesPerRow;
  if (stride < rowBytes) {
    return false;
  }
  std::size_t const expectedSize = stride * static_cast<std::size_t>(height - 1u) + rowBytes;
  if (pixels.size() < expectedSize) {
    return false;
  }
  MTLRegion region = MTLRegionMake2D(x, y, width, height);
  [texture_ replaceRegion:region mipmapLevel:0 withBytes:pixels.data() bytesPerRow:stride];
  return true;
}

MetalImage const* tryMetalImage(Image const& image) {
  return dynamic_cast<MetalImage const*>(&image);
}

std::shared_ptr<Image> Image::fromExternalMetal(void* texture, std::uint32_t width, std::uint32_t height) {
  id<MTLTexture> metalTexture = (__bridge id<MTLTexture>)texture;
  if (!metalTexture) {
    return nullptr;
  }
  return std::make_shared<MetalImage>(metalTexture, width, height);
}

std::shared_ptr<Image> Image::fromRgbaPixels(std::uint32_t width, std::uint32_t height,
                                             std::span<std::uint8_t const> rgbaPixels, void* gpuDevice) {
  return fromPixels(width, height, rgbaPixels, PixelFormat::Rgba8888, gpuDevice);
}

std::shared_ptr<Image> Image::fromPixels(std::uint32_t width,
                                         std::uint32_t height,
                                         std::span<std::uint8_t const> pixels,
                                         PixelFormat format,
                                         void* gpuDevice) {
  std::size_t const expectedSize = static_cast<std::size_t>(width) * height * 4u;
  if (width == 0 || height == 0 || pixels.size() != expectedSize) {
    return nullptr;
  }

  id<MTLDevice> device =
      gpuDevice ? (__bridge id<MTLDevice>)gpuDevice : MTLCreateSystemDefaultDevice();
  if (!device) {
    return nullptr;
  }

  MTLTextureDescriptor* desc =
      [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:(format == PixelFormat::Bgra8888
                                                                   ? MTLPixelFormatBGRA8Unorm
                                                                   : MTLPixelFormatRGBA8Unorm)
                                                         width:width
                                                        height:height
                                                     mipmapped:NO];
  desc.usage = MTLTextureUsageShaderRead;
  id<MTLTexture> tex = [device newTextureWithDescriptor:desc];
  if (!tex) {
    return nullptr;
  }

  MTLRegion region = MTLRegionMake2D(0, 0, width, height);
  [tex replaceRegion:region mipmapLevel:0 withBytes:pixels.data() bytesPerRow:width * 4u];
  return std::make_shared<MetalImage>(tex);
}

} // namespace lambda
