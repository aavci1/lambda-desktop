#include "Compositor/Surface/CommittedSurfacePainter.hpp"

#include "Graphics/CoreTextSystem.hpp"

#include <Flux/Graphics/Image.hpp>
#include <Flux/Graphics/RenderTarget.hpp>

#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <Metal/Metal.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct U8Color {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
  std::uint8_t a = 255;
};

void putPixel(std::vector<std::uint8_t>& pixels,
              int width,
              int height,
              int x,
              int y,
              U8Color color) {
  if (x < 0 || y < 0 || x >= width || y >= height) return;
  std::size_t const offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                              static_cast<std::size_t>(x)) * 4u;
  pixels[offset + 0u] = color.r;
  pixels[offset + 1u] = color.g;
  pixels[offset + 2u] = color.b;
  pixels[offset + 3u] = color.a;
}

void fillRect(std::vector<std::uint8_t>& pixels,
              int width,
              int height,
              int left,
              int top,
              int rectWidth,
              int rectHeight,
              U8Color color) {
  int const right = std::min(width, left + rectWidth);
  int const bottom = std::min(height, top + rectHeight);
  for (int y = std::max(0, top); y < bottom; ++y) {
    for (int x = std::max(0, left); x < right; ++x) {
      putPixel(pixels, width, height, x, y, color);
    }
  }
}

std::vector<std::uint8_t> makePixels(int width, int height, U8Color top, U8Color bottom) {
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
  for (int y = 0; y < height; ++y) {
    float const t = height <= 1 ? 0.f : static_cast<float>(y) / static_cast<float>(height - 1);
    U8Color row{
        .r = static_cast<std::uint8_t>(std::lround(static_cast<float>(top.r) * (1.f - t) + static_cast<float>(bottom.r) * t)),
        .g = static_cast<std::uint8_t>(std::lround(static_cast<float>(top.g) * (1.f - t) + static_cast<float>(bottom.g) * t)),
        .b = static_cast<std::uint8_t>(std::lround(static_cast<float>(top.b) * (1.f - t) + static_cast<float>(bottom.b) * t)),
        .a = 255,
    };
    for (int x = 0; x < width; ++x) {
      putPixel(pixels, width, height, x, y, row);
    }
  }
  return pixels;
}

std::vector<std::uint8_t> makeForeignCsdPixels(int width, int height) {
  auto pixels = makePixels(width, height, U8Color{35, 41, 58, 255}, U8Color{22, 25, 36, 255});
  fillRect(pixels, width, height, 0, 0, width, 38, U8Color{17, 22, 34, 255});
  fillRect(pixels, width, height, 16, 12, 14, 14, U8Color{226, 85, 85, 255});
  fillRect(pixels, width, height, 38, 12, 14, 14, U8Color{239, 188, 72, 255});
  fillRect(pixels, width, height, 60, 12, 14, 14, U8Color{71, 190, 118, 255});
  fillRect(pixels, width, height, 18, 58, width - 36, 42, U8Color{51, 63, 86, 255});
  fillRect(pixels, width, height, 18, 116, width - 120, 14, U8Color{80, 95, 124, 255});
  fillRect(pixels, width, height, 18, 140, width - 80, 14, U8Color{66, 78, 103, 255});
  return pixels;
}

std::vector<std::uint8_t> makeTier2Pixels(int width, int height) {
  auto pixels = makePixels(width, height, U8Color{244, 247, 252, 255}, U8Color{219, 227, 238, 255});
  fillRect(pixels, width, height, 22, 24, width - 44, 52, U8Color{255, 255, 255, 255});
  fillRect(pixels, width, height, 22, 92, width / 2 - 30, 104, U8Color{232, 238, 248, 255});
  fillRect(pixels, width, height, width / 2 + 10, 92, width / 2 - 32, 104, U8Color{210, 222, 238, 255});
  fillRect(pixels, width, height, 46, 112, width / 2 - 78, 12, U8Color{116, 130, 156, 255});
  fillRect(pixels, width, height, width / 2 + 34, 112, width / 2 - 80, 12, U8Color{116, 130, 156, 255});
  return pixels;
}

std::vector<std::uint8_t> makeTier3Pixels(int width, int height) {
  auto pixels = makePixels(width, height, U8Color{237, 243, 249, 255}, U8Color{205, 218, 234, 255});
  fillRect(pixels, width, height, 0, 0, width, 42, U8Color{247, 250, 253, 242});
  fillRect(pixels, width, height, 16, 9, 52, 24, U8Color{224, 232, 244, 255});
  fillRect(pixels, width, height, 82, 9, 150, 24, U8Color{255, 255, 255, 255});
  fillRect(pixels, width, height, 248, 9, std::max(0, width - 360), 24, U8Color{232, 239, 248, 255});
  fillRect(pixels, width, height, width - 90, 0, 90, 42, U8Color{247, 250, 253, 242});
  fillRect(pixels, width, height, 22, 66, width - 44, 54, U8Color{255, 255, 255, 255});
  fillRect(pixels, width, height, 22, 140, 120, height - 166, U8Color{226, 235, 246, 255});
  fillRect(pixels, width, height, 164, 140, width - 186, height - 166, U8Color{241, 245, 250, 255});
  return pixels;
}

flux::compositor::CommittedSurfaceSnapshot snapshot(std::uint64_t id,
                                                    int x,
                                                    int y,
                                                    int width,
                                                    int height) {
  return flux::compositor::CommittedSurfaceSnapshot{
      .id = id,
      .x = x,
      .y = y,
      .width = width,
      .height = height,
      .bufferWidth = width,
      .bufferHeight = height,
      .sourceX = 0.f,
      .sourceY = 0.f,
      .sourceWidth = static_cast<float>(width),
      .sourceHeight = static_cast<float>(height),
      .destinationWidth = width,
      .destinationHeight = height,
      .serial = id,
  };
}

std::vector<std::uint8_t> readBgraTexture(id<MTLDevice> device,
                                          id<MTLTexture> texture,
                                          std::uint32_t width,
                                          std::uint32_t height) {
  id<MTLCommandQueue> queue = [device newCommandQueue];
  if (!queue) throw std::runtime_error("failed to create Metal command queue for readback");
  id<MTLBuffer> readback = [device newBufferWithLength:static_cast<NSUInteger>(width) * height * 4u
                                               options:MTLResourceStorageModeShared];
  if (!readback) throw std::runtime_error("failed to create Metal readback buffer");
  id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
  id<MTLBlitCommandEncoder> blit = [commandBuffer blitCommandEncoder];
  [blit copyFromTexture:texture
            sourceSlice:0
            sourceLevel:0
           sourceOrigin:MTLOriginMake(0, 0, 0)
             sourceSize:MTLSizeMake(width, height, 1)
               toBuffer:readback
      destinationOffset:0
 destinationBytesPerRow:width * 4u
destinationBytesPerImage:width * height * 4u];
  [blit endEncoding];
  [commandBuffer commit];
  [commandBuffer waitUntilCompleted];
  if (commandBuffer.status != MTLCommandBufferStatusCompleted) {
    throw std::runtime_error("Metal readback command failed");
  }
  auto const* bytes = static_cast<std::uint8_t const*>([readback contents]);
  return std::vector<std::uint8_t>(bytes, bytes + static_cast<std::size_t>(width) * height * 4u);
}

void writePng(std::filesystem::path const& path,
              std::vector<std::uint8_t> const& bgra,
              std::uint32_t width,
              std::uint32_t height) {
  if (width == 0 || height == 0 || bgra.size() < static_cast<std::size_t>(width) * height * 4u) {
    throw std::runtime_error("invalid PNG input pixels");
  }
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path());
  }

  CFDataRef data = CFDataCreate(kCFAllocatorDefault,
                                bgra.data(),
                                static_cast<CFIndex>(bgra.size()));
  if (!data) throw std::runtime_error("failed to create PNG pixel data");
  CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
  CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
  CGBitmapInfo const bitmapInfo =
      kCGBitmapByteOrder32Little | static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedFirst);
  CGImageRef image = CGImageCreate(width,
                                   height,
                                   8,
                                   32,
                                   static_cast<std::size_t>(width) * 4u,
                                   colorSpace,
                                   bitmapInfo,
                                   provider,
                                   nullptr,
                                   false,
                                   kCGRenderingIntentDefault);
  if (!image) {
    if (colorSpace) CGColorSpaceRelease(colorSpace);
    if (provider) CGDataProviderRelease(provider);
    CFRelease(data);
    throw std::runtime_error("failed to create CGImage");
  }

  std::string const output = path.string();
  CFURLRef url = CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault,
                                                         reinterpret_cast<UInt8 const*>(output.data()),
                                                         static_cast<CFIndex>(output.size()),
                                                         false);
  CGImageDestinationRef destination = url
      ? CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, nullptr)
      : nullptr;
  if (!destination) {
    CGImageRelease(image);
    if (url) CFRelease(url);
    if (colorSpace) CGColorSpaceRelease(colorSpace);
    if (provider) CGDataProviderRelease(provider);
    CFRelease(data);
    throw std::runtime_error("failed to create PNG destination");
  }
  CGImageDestinationAddImage(destination, image, nullptr);
  bool const ok = CGImageDestinationFinalize(destination);
  CFRelease(destination);
  CFRelease(url);
  CGImageRelease(image);
  if (colorSpace) CGColorSpaceRelease(colorSpace);
  if (provider) CGDataProviderRelease(provider);
  CFRelease(data);
  if (!ok) throw std::runtime_error("failed to write PNG");
}

} // namespace

int main(int argc, char** argv) {
  @autoreleasepool {
    try {
      std::filesystem::path output =
          argc > 1 ? std::filesystem::path(argv[1])
                   : std::filesystem::path("build/compositor-window-chrome-fixture.png");
      constexpr int logicalWidth = 960;
      constexpr int logicalHeight = 640;
      constexpr float scale = 2.f;
      std::uint32_t const pixelWidth = static_cast<std::uint32_t>(std::ceil(logicalWidth * scale));
      std::uint32_t const pixelHeight = static_cast<std::uint32_t>(std::ceil(logicalHeight * scale));

      id<MTLDevice> device = MTLCreateSystemDefaultDevice();
      if (!device) throw std::runtime_error("no Metal device available");
      MTLTextureDescriptor* desc =
          [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                             width:pixelWidth
                                                            height:pixelHeight
                                                         mipmapped:NO];
      desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
      desc.storageMode = MTLStorageModePrivate;
      id<MTLTexture> texture = [device newTextureWithDescriptor:desc];
      if (!texture) throw std::runtime_error("failed to create output texture");

      flux::CoreTextSystem textSystem;
      flux::RenderTarget target{flux::MetalRenderTargetSpec{
          .texture = (__bridge void*)texture,
          .width = pixelWidth,
          .height = pixelHeight,
      }};
      flux::Canvas& canvas = target.canvas();
      canvas.resize(logicalWidth, logicalHeight);
      canvas.updateDpiScale(scale, scale);

      auto foreignImage = flux::Image::fromRgbaPixels(300, 190, makeForeignCsdPixels(300, 190), canvas.gpuDevice());
      auto tier2Image = flux::Image::fromRgbaPixels(360, 230, makeTier2Pixels(360, 230), canvas.gpuDevice());
      auto tier3Image = flux::Image::fromRgbaPixels(460, 280, makeTier3Pixels(460, 280), canvas.gpuDevice());
      if (!foreignImage || !tier2Image || !tier3Image) throw std::runtime_error("failed to create fixture images");

      flux::compositor::ChromeConfig chrome{};
      auto foreign = snapshot(1, 42, 76, 300, 190);
      foreign.focused = false;

      auto tier2 = snapshot(2, 390, 116, 360, 230);
      tier2.titleBarHeight = chrome.titleBarHeight;
      tier2.title = "Tier 2 SSD";
      tier2.serverSideDecorated = true;
      tier2.focused = false;

      auto tier3 = snapshot(3, 250, 300, 460, 280);
      tier3.serverSideDecorated = true;
      tier3.cutoutsBound = true;
      tier3.closeButtonHovered = true;
      tier3.focused = true;

      flux::compositor::SurfaceVisualState foreignVisual{};
      flux::compositor::SurfaceVisualState tier2Visual{};
      flux::compositor::SurfaceVisualState tier3Visual{};
      auto const frameTime = std::chrono::steady_clock::now();

      target.beginFrame();
      canvas.clear(flux::Color{0.10f, 0.12f, 0.17f, 1.f});
      canvas.drawRect(flux::Rect::sharp(0.f, 0.f, logicalWidth, logicalHeight),
                      flux::CornerRadius{},
                      flux::FillStyle::linearGradient(flux::Color{0.13f, 0.16f, 0.22f, 1.f},
                                                      flux::Color{0.34f, 0.39f, 0.48f, 1.f},
                                                      flux::Point{0.f, 0.f},
                                                      flux::Point{1.f, 1.f}),
                      flux::StrokeStyle::none());
      canvas.drawRect(flux::Rect::sharp(18.f, 18.f, logicalWidth - 36.f, logicalHeight - 36.f),
                      flux::CornerRadius{18.f},
                      flux::FillStyle::solid(flux::Color{1.f, 1.f, 1.f, 0.04f}),
                      flux::StrokeStyle::solid(flux::Color{1.f, 1.f, 1.f, 0.12f}, 1.f));

      flux::compositor::drawCommittedSurfaceSnapshot(canvas, textSystem, foreign, foreignVisual, *foreignImage, frameTime, chrome, false);
      flux::compositor::drawCommittedSurfaceSnapshot(canvas, textSystem, tier2, tier2Visual, *tier2Image, frameTime, chrome, false);
      flux::compositor::drawCommittedSurfaceSnapshot(canvas, textSystem, tier3, tier3Visual, *tier3Image, frameTime, chrome, false);
      target.endFrame();

      writePng(output, readBgraTexture(device, texture, pixelWidth, pixelHeight), pixelWidth, pixelHeight);
      std::cout << output << '\n';
      return 0;
    } catch (std::exception const& error) {
      std::cerr << "flux-compositor-render-fixture: " << error.what() << '\n';
      return 1;
    }
  }
}
