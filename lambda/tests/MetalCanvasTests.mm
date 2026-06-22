#include <doctest/doctest.h>

#include "Graphics/CoreTextSystem.hpp"
#include "Graphics/Metal/GlyphAtlas.hpp"
#include "Graphics/Metal/MetalCanvas.hpp"
#include "Graphics/Metal/MetalFrameRecorder.hpp"

#include <Lambda/Graphics/Image.hpp>
#include <Lambda/Graphics/RenderTarget.hpp>
#include <Lambda/SceneGraph/SceneNode.hpp>
#include <Lambda/SceneGraph/ImageNode.hpp>
#include <Lambda/SceneGraph/PathNode.hpp>
#include <Lambda/SceneGraph/RasterCacheNode.hpp>
#include <Lambda/SceneGraph/RectNode.hpp>
#include <Lambda/SceneGraph/SceneGraph.hpp>
#include <Lambda/SceneGraph/SceneRenderer.hpp>
#include <Lambda/SceneGraph/TextNode.hpp>

#import <Metal/Metal.h>

#include <filesystem>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace lambda;
using namespace lambda::scenegraph;

static std::filesystem::path imageFixturePath() {
  std::filesystem::path path = std::filesystem::path(__FILE__).parent_path();
  path /= "../demos/image-demo/test.png";
  return std::filesystem::weakly_canonical(path);
}

static std::shared_ptr<TextLayout const> makeLabel(CoreTextSystem& textSystem, std::string const& text) {
  Font font{};
  font.family = ".AppleSystemUIFont";
  font.size = 13.f;
  font.weight = 400.f;
  return textSystem.layout(text, font, Colors::white, 120.f, {});
}

static MetalRecorderSlice fullSlice(MetalFrameRecorder const& recorded) {
  return MetalRecorderSlice{
      .orderStart = 0,
      .orderCount = static_cast<std::uint32_t>(recorded.opOrder.size()),
      .rectStart = 0,
      .rectCount = static_cast<std::uint32_t>(recorded.rectOps.size()),
      .imageStart = 0,
      .imageCount = static_cast<std::uint32_t>(recorded.imageOps.size()),
      .pathOpStart = 0,
      .pathOpCount = static_cast<std::uint32_t>(recorded.pathOps.size()),
      .glyphOpStart = 0,
      .glyphOpCount = static_cast<std::uint32_t>(recorded.glyphOps.size()),
      .backdropBlurOpStart = 0,
      .backdropBlurOpCount = static_cast<std::uint32_t>(recorded.backdropBlurOps.size()),
      .pathVertexStart = 0,
      .pathVertexCount = static_cast<std::uint32_t>(recorded.pathVerts.size()),
      .glyphVertexStart = 0,
      .glyphVertexCount = recorded.glyphVertexCount,
  };
}

struct StressScene {
  std::unique_ptr<SceneGraph> graph;
  SceneNode* animatedGroup = nullptr;
};

static StressScene makeStressScene(CoreTextSystem& textSystem, std::shared_ptr<Image> const& image) {
  auto graph = std::make_unique<SceneGraph>();
  auto root = std::make_unique<SceneNode>(lambda::Rect{0.f, 0.f, 640.f, 480.f});
  root->appendChild(std::make_unique<RectNode>(
      lambda::Rect{0.f, 0.f, 640.f, 480.f},
      FillStyle::solid(Color{0.08f, 0.09f, 0.11f, 1.f})
  ));

  auto animatedGroup = std::make_unique<SceneNode>(lambda::Rect{0.f, 0.f, 640.f, 480.f});
  SceneNode* animatedGroupPtr = animatedGroup.get();

  for (int i = 0; i < 256; ++i) {
    animatedGroup->appendChild(std::make_unique<RectNode>(
        lambda::Rect{
            static_cast<float>((i % 32) * 18),
            static_cast<float>((i / 32) * 18),
            14.f,
            14.f,
        },
        FillStyle::solid(Color{
            static_cast<float>((17 * i) % 255) / 255.f,
            static_cast<float>((37 * i) % 255) / 255.f,
            static_cast<float>((53 * i) % 255) / 255.f,
            1.f,
        }),
        StrokeStyle::none(),
        CornerRadius{3.f, 3.f, 3.f, 3.f}
    ));
  }

  for (int i = 0; i < 64; ++i) {
    animatedGroup->appendChild(std::make_unique<TextNode>(
        lambda::Rect{
            static_cast<float>((i % 8) * 72),
            170.f + static_cast<float>(i / 8) * 16.f,
            64.f,
            14.f,
        },
        makeLabel(textSystem, "Row " + std::to_string(i))
    ));
  }

  Path triangle;
  triangle.moveTo({0.f, 0.f});
  triangle.lineTo({100.f, 140.f});
  triangle.lineTo({-80.f, 140.f});
  triangle.close();
  animatedGroup->appendChild(std::make_unique<PathNode>(
      lambda::Rect{320.f, 40.f, 180.f, 140.f},
      triangle,
      FillStyle::solid(Color{0.2f, 0.6f, 0.9f, 1.f}),
      StrokeStyle::none(),
      ShadowStyle::none()
  ));

  if (image) {
    std::shared_ptr<Image const> constImage = image;
    for (int i = 0; i < 9; ++i) {
      animatedGroup->appendChild(std::make_unique<ImageNode>(
          lambda::Rect{
              static_cast<float>((i % 3) * 88),
              320.f + static_cast<float>(i / 3) * 88.f,
              72.f,
              72.f,
          },
          constImage
      ));
    }
  }

  root->appendChild(std::move(animatedGroup));
  graph->setRoot(std::move(root));
  return StressScene{.graph = std::move(graph), .animatedGroup = animatedGroupPtr};
}

static std::uint8_t capturedChannel(std::vector<std::uint8_t> const& pixels, std::uint32_t width,
                                    std::uint32_t x, std::uint32_t y, int channel) {
  std::size_t const idx =
      (static_cast<std::size_t>(y) * width + x) * 4u + static_cast<std::size_t>(channel);
  return pixels[idx];
}

static int colorDelta(std::vector<std::uint8_t> const& pixels, std::uint32_t width,
                      std::uint32_t ax, std::uint32_t ay, std::uint32_t bx, std::uint32_t by) {
  int delta = 0;
  for (int channel = 0; channel < 3; ++channel) {
    delta += std::abs(static_cast<int>(capturedChannel(pixels, width, ax, ay, channel)) -
                      static_cast<int>(capturedChannel(pixels, width, bx, by, channel)));
  }
  return delta;
}

#if defined(__APPLE__)
static std::vector<std::uint8_t> readR8Texture(id<MTLDevice> device,
                                               id<MTLCommandQueue> queue,
                                               id<MTLTexture> texture,
                                               std::uint32_t width,
                                               std::uint32_t height) {
  if (!device || !queue || !texture || width == 0 || height == 0) {
    return {};
  }
  std::size_t const byteCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  id<MTLBuffer> readback =
      [device newBufferWithLength:static_cast<NSUInteger>(byteCount) options:MTLResourceStorageModeShared];
  if (!readback) {
    return {};
  }
  id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
  id<MTLBlitCommandEncoder> blit = commandBuffer ? [commandBuffer blitCommandEncoder] : nil;
  if (!commandBuffer || !blit) {
    return {};
  }
  [blit copyFromTexture:texture
            sourceSlice:0
            sourceLevel:0
           sourceOrigin:MTLOriginMake(0, 0, 0)
             sourceSize:MTLSizeMake(width, height, 1)
               toBuffer:readback
      destinationOffset:0
 destinationBytesPerRow:width
destinationBytesPerImage:width * height];
  [blit endEncoding];
  [commandBuffer commit];
  [commandBuffer waitUntilCompleted];
  if (commandBuffer.status != MTLCommandBufferStatusCompleted) {
    return {};
  }
  auto const* bytes = static_cast<std::uint8_t const*>([readback contents]);
  return std::vector<std::uint8_t>(bytes, bytes + byteCount);
}

struct HeadlessMetalTarget {
  id<MTLDevice> device = nil;
  id<MTLTexture> texture = nil;
  std::unique_ptr<Canvas> targetCanvas;
  int logicalW = 1;
  int logicalH = 1;
  float dpiScale = 1.f;
  std::uint32_t pixelW = 1;
  std::uint32_t pixelH = 1;

  HeadlessMetalTarget(TextSystem& textSystem, int width, int height, float scale = 2.f)
      : device(MTLCreateSystemDefaultDevice()), logicalW(width), logicalH(height), dpiScale(scale),
        pixelW(static_cast<std::uint32_t>(std::ceil(static_cast<float>(width) * scale))),
        pixelH(static_cast<std::uint32_t>(std::ceil(static_cast<float>(height) * scale))) {
    if (!device) {
      return;
    }
    MTLTextureDescriptor* desc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                           width:pixelW
                                                          height:pixelH
                                                       mipmapped:NO];
    desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    desc.storageMode = MTLStorageModePrivate;
    texture = [device newTextureWithDescriptor:desc];
    if (!texture) {
      return;
    }
    targetCanvas = createMetalRenderTargetCanvas(MetalRenderTargetSpec{
        .texture = (__bridge void*)texture,
        .width = pixelW,
        .height = pixelH,
    }, textSystem);
    canvas().resize(logicalW, logicalH);
    canvas().updateDpiScale(dpiScale, dpiScale);
  }

  explicit operator bool() const noexcept { return targetCanvas != nullptr; }

  Canvas& canvas() { return *targetCanvas; }

  void begin(Color clearColor = Colors::transparent) {
    canvas().beginFrame();
    canvas().clear(clearColor);
  }

  void end() { canvas().present(); }

  void render(SceneGraph const& graph, Color clearColor) {
    begin(clearColor);
    SceneRenderer renderer{canvas()};
    renderer.render(graph);
    end();
  }

  std::vector<std::uint8_t> readPixels() {
    id<MTLCommandQueue> queue = [device newCommandQueue];
    if (!queue) {
      return {};
    }
    id<MTLBuffer> readback = [device newBufferWithLength:pixelW * pixelH * 4u
                                                 options:MTLResourceStorageModeShared];
    if (!readback) {
      return {};
    }
    id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [commandBuffer blitCommandEncoder];
    [blit copyFromTexture:texture
              sourceSlice:0
              sourceLevel:0
             sourceOrigin:MTLOriginMake(0, 0, 0)
               sourceSize:MTLSizeMake(pixelW, pixelH, 1)
                 toBuffer:readback
        destinationOffset:0
   destinationBytesPerRow:pixelW * 4u
 destinationBytesPerImage:pixelW * pixelH * 4u];
    [blit endEncoding];
    [commandBuffer commit];
    [commandBuffer waitUntilCompleted];
    if (commandBuffer.status != MTLCommandBufferStatusCompleted) {
      return {};
    }
    auto const* bytes = static_cast<std::uint8_t const*>([readback contents]);
    return std::vector<std::uint8_t>(bytes, bytes + pixelW * pixelH * 4u);
  }
};
#endif

} // namespace

TEST_CASE("Metal GlyphAtlas keeps glyph padding transparent") {
#if !defined(__APPLE__)
  SUCCEED();
#else
  @autoreleasepool {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    REQUIRE(device);
    id<MTLCommandQueue> queue = [device newCommandQueue];
    REQUIRE(queue);

    CoreTextSystem textSystem;
    GlyphAtlas atlas{device, textSystem, queue};

    Font font{};
    font.family = ".AppleSystemUIFont";
    font.size = 96.f;
    font.weight = 500.f;
    auto layout = textSystem.layout("T", font, Colors::black, 200.f, {});
    REQUIRE(!layout->runs.empty());
    TextRun const& run = layout->runs.front().run;
    REQUIRE(!run.glyphIds.empty());

    GlyphKey key{};
    key.fontId = run.fontId;
    key.glyphId = run.glyphIds.front();
    key.sizeQ8 = static_cast<std::uint16_t>(std::lround(run.fontSize * 4.f));
    AtlasEntry const entry = atlas.getOrUpload(key);
    REQUIRE(entry.width > 0);
    REQUIRE(entry.height > 0);
    REQUIRE(entry.u > 0);
    REQUIRE(entry.v > 0);

    id<MTLCommandBuffer> upload = [queue commandBuffer];
    REQUIRE(upload);
    atlas.flushUploads(upload);
    [upload commit];
    [upload waitUntilCompleted];
    REQUIRE(upload.status == MTLCommandBufferStatusCompleted);

    std::uint32_t const oldWidth = atlas.atlasPixelWidth();
    std::uint32_t const oldHeight = atlas.atlasPixelHeight();
    std::vector<std::uint8_t> pixels = readR8Texture(device, queue, atlas.texture(), oldWidth, oldHeight);
    REQUIRE(pixels.size() == static_cast<std::size_t>(oldWidth) * oldHeight);
    auto sample = [&](std::vector<std::uint8_t> const& source, std::uint32_t width,
                      std::uint32_t x, std::uint32_t y) {
      return source[static_cast<std::size_t>(y) * width + x];
    };

    CHECK(sample(pixels, oldWidth, entry.u - 1, entry.v) == 0);
    CHECK(sample(pixels, oldWidth, entry.u, entry.v - 1) == 0);
    CHECK(sample(pixels, oldWidth, entry.u + entry.width, entry.v) == 0);
    CHECK(sample(pixels, oldWidth, entry.u, entry.v + entry.height) == 0);

    REQUIRE(atlas.grow());
    std::uint32_t const newWidth = atlas.atlasPixelWidth();
    std::uint32_t const newHeight = atlas.atlasPixelHeight();
    std::vector<std::uint8_t> grown = readR8Texture(device, queue, atlas.texture(), newWidth, newHeight);
    REQUIRE(grown.size() == static_cast<std::size_t>(newWidth) * newHeight);
    CHECK(sample(grown, newWidth, entry.u - 1, entry.v) == 0);
    CHECK(sample(grown, newWidth, oldWidth + 1, oldHeight + 1) == 0);
  }
#endif
}

TEST_CASE("MetalCanvas can render multiple queued frames without arena aliasing regressions") {
#if !defined(__APPLE__)
  SUCCEED();
#else
  @autoreleasepool {
    CoreTextSystem textSystem;
    HeadlessMetalTarget target{textSystem, 640, 480};
    REQUIRE(target);

    Canvas& canvas = target.canvas();
    std::shared_ptr<Image> image = loadImage(imageFixturePath().string(), canvas.gpuDevice());
    StressScene scene = makeStressScene(textSystem, image);
    REQUIRE(scene.animatedGroup != nullptr);

    SceneRenderer renderer{canvas};
    for (int frame = 0; frame < 18; ++frame) {
      scene.animatedGroup->setPosition(lambda::Point{0.f, static_cast<float>(frame % 3)});
      target.begin(Color{0.08f, 0.09f, 0.11f, 1.f});
      renderer.render(*scene.graph);
      target.end();
    }

    CHECK(true);
  }
#endif
}

TEST_CASE("Metal RenderTarget renders canvas ops into an offscreen texture") {
#if !defined(__APPLE__)
  SUCCEED();
#else
  @autoreleasepool {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    REQUIRE(device);

    constexpr NSUInteger width = 64;
    constexpr NSUInteger height = 64;
    MTLTextureDescriptor* desc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                           width:width
                                                          height:height
                                                       mipmapped:NO];
    desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    desc.storageMode = MTLStorageModePrivate;
    id<MTLTexture> texture = [device newTextureWithDescriptor:desc];
    REQUIRE(texture);
    std::shared_ptr<Image> externalImage =
        Image::fromExternalMetal((__bridge void*)texture, static_cast<std::uint32_t>(width),
                                 static_cast<std::uint32_t>(height));
    REQUIRE(externalImage);
    CHECK(externalImage->size().width == static_cast<float>(width));
    CHECK(externalImage->size().height == static_cast<float>(height));

    lambda::RenderTarget target{lambda::MetalRenderTargetSpec{
        .texture = (__bridge void*)texture,
        .width = static_cast<std::uint32_t>(width),
        .height = static_cast<std::uint32_t>(height),
    }};

    target.beginFrame();
    target.canvas().clear(lambda::Colors::black);
    auto root = std::make_unique<SceneNode>(lambda::Rect{0.f, 0.f, 64.f, 64.f});
    root->appendChild(std::make_unique<RectNode>(
        lambda::Rect{16.f, 16.f, 32.f, 32.f},
        lambda::FillStyle::solid(lambda::Color{1.f, 0.f, 0.f, 1.f})));
    SceneGraph graph{std::move(root)};
    target.renderScene(graph);
    target.endFrame();

    id<MTLCommandQueue> queue = [device newCommandQueue];
    REQUIRE(queue);
    id<MTLBuffer> readback = [device newBufferWithLength:width * height * 4
                                                 options:MTLResourceStorageModeShared];
    REQUIRE(readback);
    id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [commandBuffer blitCommandEncoder];
    [blit copyFromTexture:texture
              sourceSlice:0
              sourceLevel:0
             sourceOrigin:MTLOriginMake(0, 0, 0)
               sourceSize:MTLSizeMake(width, height, 1)
                 toBuffer:readback
        destinationOffset:0
   destinationBytesPerRow:width * 4
 destinationBytesPerImage:width * height * 4];
    [blit endEncoding];
    [commandBuffer commit];
    [commandBuffer waitUntilCompleted];
    REQUIRE(commandBuffer.status == MTLCommandBufferStatusCompleted);

    auto const* bytes = static_cast<std::uint8_t const*>([readback contents]);
    std::size_t const center = (32u * static_cast<std::size_t>(width) + 32u) * 4u;
    CHECK(bytes[center + 2] > 200);
    CHECK(bytes[center + 1] < 32);
    CHECK(bytes[center + 0] < 32);
  }
#endif
}

TEST_CASE("MetalCanvas rejects prepared glyph replay after atlas growth") {
#if !defined(__APPLE__)
  SUCCEED();
#else
  @autoreleasepool {
    CoreTextSystem textSystem;
    HeadlessMetalTarget target{textSystem, 640, 480};
    REQUIRE(target);
    Canvas& canvas = target.canvas();

    Font cachedFont{};
    cachedFont.family = ".AppleSystemUIFont";
    cachedFont.size = 24.f;
    cachedFont.weight = 500.f;
    auto cachedLayout = textSystem.layout("Cached", cachedFont, Colors::white, 160.f, {});

    MetalFrameRecorder recorded;
    target.begin(Colors::black);
    REQUIRE(beginRecordedOpsCaptureForCanvas(&canvas, &recorded));
    canvas.drawTextLayout(*cachedLayout, lambda::Point{12.f, 12.f});
    endRecordedOpsCaptureForCanvas(&canvas);
    target.end();

    REQUIRE(recorded.glyphVertexCount > 0);
    REQUIRE(recorded.glyphAtlasGeneration > 0);

    Font largeFont = cachedFont;
    largeFont.size = 240.f;
    auto largeLayout =
        textSystem.layout("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", largeFont, Colors::white, 12000.f, {});

    target.begin(Colors::black);
    canvas.drawTextLayout(*largeLayout, lambda::Point{0.f, 260.f});
    target.end();

    target.begin(Colors::black);
    CHECK_FALSE(replayRecordedLocalOpsForCanvas(&canvas, recorded, fullSlice(recorded)));
    target.end();
  }
#endif
}

TEST_CASE("MetalCanvas retries glyphs after deferred atlas growth") {
#if !defined(__APPLE__)
  SUCCEED();
#else
  @autoreleasepool {
    CoreTextSystem textSystem;
    HeadlessMetalTarget target{textSystem, 640, 480};
    REQUIRE(target);
    Canvas& canvas = target.canvas();

    Font seedFont{};
    seedFont.family = ".AppleSystemUIFont";
    seedFont.size = 24.f;
    seedFont.weight = 500.f;
    auto seedLayout = textSystem.layout("Seed", seedFont, Colors::white, 160.f, {});

    Font pressureFont = seedFont;
    pressureFont.size = 240.f;
    auto pressureLayout =
        textSystem.layout("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", pressureFont, Colors::white, 12000.f, {});

    target.begin(Colors::black);
    canvas.drawTextLayout(*seedLayout, lambda::Point{0.f, 40.f});
    canvas.drawTextLayout(*pressureLayout, lambda::Point{0.f, 300.f});
    target.end();

    MetalFrameRecorder recorded;
    target.begin(Colors::black);
    REQUIRE(beginRecordedOpsCaptureForCanvas(&canvas, &recorded));
    canvas.drawTextLayout(*pressureLayout, lambda::Point{0.f, 300.f});
    endRecordedOpsCaptureForCanvas(&canvas);
    target.end();

    CHECK(recorded.glyphVertexCount >= 180u);
  }
#endif
}

TEST_CASE("MetalCanvas replays image and path prepared buffers with shader translation") {
#if !defined(__APPLE__)
  SUCCEED();
#else
  @autoreleasepool {
    CoreTextSystem textSystem;
    HeadlessMetalTarget target{textSystem, 64, 64, 1.f};
    REQUIRE(target);
    Canvas& canvas = target.canvas();

    MTLTextureDescriptor* desc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                           width:8
                                                          height:8
                                                       mipmapped:NO];
    desc.usage = MTLTextureUsageShaderRead;
    desc.storageMode = MTLStorageModeShared;
    id<MTLTexture> texture = [target.device newTextureWithDescriptor:desc];
    REQUIRE(texture != nil);
    std::vector<std::uint8_t> bluePixels(8u * 8u * 4u, 0);
    for (std::size_t i = 0; i < bluePixels.size(); i += 4u) {
      bluePixels[i + 0] = 255;
      bluePixels[i + 3] = 255;
    }
    [texture replaceRegion:MTLRegionMake2D(0, 0, 8, 8)
               mipmapLevel:0
                 withBytes:bluePixels.data()
               bytesPerRow:8u * 4u];
    std::shared_ptr<Image> image = Image::fromExternalMetal((__bridge void*)texture, 8, 8);
    REQUIRE(image);

    Path path;
    path.moveTo({4.f, 4.f});
    path.lineTo({20.f, 4.f});
    path.lineTo({20.f, 20.f});
    path.lineTo({4.f, 20.f});
    path.close();

    MetalFrameRecorder recorded;
    target.begin(Colors::black);
    REQUIRE(beginRecordedOpsCaptureForCanvas(&canvas, &recorded));
    canvas.drawPath(path, FillStyle::solid(Colors::red), StrokeStyle::none(), ShadowStyle::none());
    canvas.drawImage(*image, lambda::Rect{0.f, 0.f, 8.f, 8.f}, lambda::Rect{28.f, 4.f, 8.f, 8.f});
    endRecordedOpsCaptureForCanvas(&canvas);
    target.end();

    CHECK(!recorded.pathVerts.empty());
    CHECK(recorded.imageOps.size() == 1);

    target.begin(Colors::black);
    canvas.save();
    canvas.translate(lambda::Point{16.f, 8.f});
    REQUIRE(replayRecordedLocalOpsForCanvas(&canvas, recorded, fullSlice(recorded)));
    canvas.restore();
    CHECK(recorded.preparedPathVertexBuffer != nullptr);
    CHECK(recorded.preparedImageInstanceBuffer != nullptr);
    target.end();

    std::vector<std::uint8_t> pixels = target.readPixels();
    REQUIRE(!pixels.empty());
    std::uint32_t const width = target.pixelW;

    CHECK(capturedChannel(pixels, width, 28, 20, 2) > 200);
    CHECK(capturedChannel(pixels, width, 12, 12, 2) < 32);

    CHECK(capturedChannel(pixels, width, 48, 16, 0) > 200);
    CHECK(capturedChannel(pixels, width, 48, 16, 1) < 32);
    CHECK(capturedChannel(pixels, width, 48, 16, 2) < 32);
  }
#endif
}

TEST_CASE("MetalCanvas applies rounded clip masks to child content") {
#if !defined(__APPLE__)
  SUCCEED();
#else
  @autoreleasepool {
    CoreTextSystem textSystem;
    HeadlessMetalTarget target{textSystem, 640, 480};
    REQUIRE(target);

    auto root = std::make_unique<SceneNode>(lambda::Rect{0.f, 0.f, 640.f, 480.f});
    root->appendChild(std::make_unique<RectNode>(
        lambda::Rect{0.f, 0.f, 640.f, 480.f},
        FillStyle::solid(Colors::white)
    ));

    auto clip = std::make_unique<RectNode>(
        lambda::Rect{20.f, 20.f, 80.f, 20.f},
        FillStyle::none(),
        StrokeStyle::none(),
        CornerRadius::pill(lambda::Rect::sharp(0.f, 0.f, 80.f, 20.f))
    );
    clip->setClipsContents(true);
    clip->appendChild(std::make_unique<RectNode>(
        lambda::Rect{0.f, 0.f, 80.f, 20.f},
        FillStyle::solid(Colors::red)
    ));
    root->appendChild(std::move(clip));

    SceneGraph graph{std::move(root)};
    target.render(graph, Colors::white);

    std::vector<std::uint8_t> pixels = target.readPixels();
    std::uint32_t width = target.pixelW;
    std::uint32_t height = target.pixelH;
    REQUIRE(!pixels.empty());
    REQUIRE(width >= 200);
    REQUIRE(height >= 120);

    std::uint32_t const outsideX = 42;
    std::uint32_t const outsideY = 42;
    CHECK(capturedChannel(pixels, width, outsideX, outsideY, 0) >= 240);
    CHECK(capturedChannel(pixels, width, outsideX, outsideY, 1) >= 240);
    CHECK(capturedChannel(pixels, width, outsideX, outsideY, 2) >= 240);

    std::uint32_t const insideX = 120;
    std::uint32_t const insideY = 60;
    CHECK(capturedChannel(pixels, width, insideX, insideY, 2) >= 180);
    CHECK(capturedChannel(pixels, width, insideX, insideY, 1) <= 80);
    CHECK(capturedChannel(pixels, width, insideX, insideY, 0) <= 80);
  }
#endif
}

TEST_CASE("MetalCanvas shades linear gradient rect fills") {
#if !defined(__APPLE__)
  SUCCEED();
#else
  @autoreleasepool {
    CoreTextSystem textSystem;
    HeadlessMetalTarget target{textSystem, 640, 480};
    REQUIRE(target);

    auto root = std::make_unique<SceneNode>(lambda::Rect{0.f, 0.f, 640.f, 480.f});
    root->appendChild(std::make_unique<RectNode>(
        lambda::Rect{0.f, 0.f, 640.f, 480.f},
        FillStyle::solid(Colors::black)
    ));
    root->appendChild(std::make_unique<RectNode>(
        lambda::Rect{20.f, 20.f, 100.f, 40.f},
        FillStyle::linearGradient(Colors::red, Colors::blue, lambda::Point{0.f, 0.f}, lambda::Point{1.f, 0.f})
    ));

    SceneGraph graph{std::move(root)};
    target.render(graph, Colors::black);

    std::vector<std::uint8_t> pixels = target.readPixels();
    std::uint32_t width = target.pixelW;
    REQUIRE(!pixels.empty());

    std::uint32_t const leftX = 60;
    std::uint32_t const rightX = 220;
    std::uint32_t const y = 60;
    CHECK(capturedChannel(pixels, width, leftX, y, 2) > capturedChannel(pixels, width, leftX, y, 0) + 80);
    CHECK(capturedChannel(pixels, width, rightX, y, 0) > capturedChannel(pixels, width, rightX, y, 2) + 80);
  }
#endif
}

TEST_CASE("MetalCanvas shades radial and conical gradient rect fills") {
#if !defined(__APPLE__)
  SUCCEED();
#else
  @autoreleasepool {
    CoreTextSystem textSystem;
    HeadlessMetalTarget target{textSystem, 640, 480};
    REQUIRE(target);

    auto root = std::make_unique<SceneNode>(lambda::Rect{0.f, 0.f, 640.f, 480.f});
    root->appendChild(std::make_unique<RectNode>(
        lambda::Rect{0.f, 0.f, 640.f, 480.f},
        FillStyle::solid(Colors::black)
    ));
    root->appendChild(std::make_unique<RectNode>(
        lambda::Rect{20.f, 80.f, 100.f, 100.f},
        FillStyle::radialGradient(Colors::white, Colors::black, lambda::Point{0.5f, 0.5f}, 0.5f)
    ));
    root->appendChild(std::make_unique<RectNode>(
        lambda::Rect{150.f, 80.f, 100.f, 100.f},
        FillStyle::conicalGradient({
            GradientStop{0.00f, Colors::red},
            GradientStop{0.33f, Colors::green},
            GradientStop{0.66f, Colors::blue},
            GradientStop{1.00f, Colors::red},
        })
    ));

    SceneGraph graph{std::move(root)};
    target.render(graph, Colors::black);

    std::vector<std::uint8_t> pixels = target.readPixels();
    std::uint32_t width = target.pixelW;
    REQUIRE(!pixels.empty());

    std::uint32_t const radialCenterX = 140;
    std::uint32_t const radialCenterY = 260;
    std::uint32_t const radialEdgeX = 50;
    CHECK(capturedChannel(pixels, width, radialCenterX, radialCenterY, 0) >
          capturedChannel(pixels, width, radialEdgeX, radialCenterY, 0) + 80);
    CHECK(capturedChannel(pixels, width, radialCenterX, radialCenterY, 1) >
          capturedChannel(pixels, width, radialEdgeX, radialCenterY, 1) + 80);
    CHECK(capturedChannel(pixels, width, radialCenterX, radialCenterY, 2) >
          capturedChannel(pixels, width, radialEdgeX, radialCenterY, 2) + 80);

    std::uint32_t const conicRightX = 490;
    std::uint32_t const conicLeftX = 310;
    std::uint32_t const conicY = 260;
    CHECK(colorDelta(pixels, width, conicRightX, conicY, conicLeftX, conicY) > 120);
  }
#endif
}

TEST_CASE("MetalCanvas preserves rounded rect geometry when clipped by the viewport") {
#if !defined(__APPLE__)
  SUCCEED();
#else
  @autoreleasepool {
    CoreTextSystem textSystem;
    HeadlessMetalTarget target{textSystem, 640, 480};
    REQUIRE(target);

    auto root = std::make_unique<SceneNode>(lambda::Rect{0.f, 0.f, 640.f, 480.f});
    root->appendChild(std::make_unique<RectNode>(
        lambda::Rect{0.f, 0.f, 640.f, 480.f},
        FillStyle::solid(Colors::white)
    ));

    auto clip = std::make_unique<RectNode>(
        lambda::Rect{20.f, 30.f, 140.f, 120.f},
        FillStyle::none(),
        StrokeStyle::none(),
        CornerRadius{}
    );
    clip->setClipsContents(true);
    clip->appendChild(std::make_unique<RectNode>(
        lambda::Rect{0.f, -10.f, 100.f, 80.f},
        FillStyle::solid(Colors::red),
        StrokeStyle::none(),
        CornerRadius{28.f, 28.f, 28.f, 28.f}
    ));
    root->appendChild(std::move(clip));

    SceneGraph graph{std::move(root)};
    target.render(graph, Colors::white);

    std::vector<std::uint8_t> pixels = target.readPixels();
    std::uint32_t width = target.pixelW;
    REQUIRE(!pixels.empty());

    std::uint32_t const curvedX = 42;
    std::uint32_t const curvedY = 68;
    CHECK(capturedChannel(pixels, width, curvedX, curvedY, 0) >= 240);
    CHECK(capturedChannel(pixels, width, curvedX, curvedY, 1) >= 240);
    CHECK(capturedChannel(pixels, width, curvedX, curvedY, 2) >= 240);

    std::uint32_t const interiorX = 56;
    std::uint32_t const interiorY = 76;
    CHECK(capturedChannel(pixels, width, interiorX, interiorY, 2) >= 180);
    CHECK(capturedChannel(pixels, width, interiorX, interiorY, 1) <= 80);
    CHECK(capturedChannel(pixels, width, interiorX, interiorY, 0) <= 80);
  }
#endif
}

TEST_CASE("MetalCanvas preserves image sampling when clipped by the viewport") {
#if !defined(__APPLE__)
  SUCCEED();
#else
  @autoreleasepool {
    CoreTextSystem textSystem;
    HeadlessMetalTarget target{textSystem, 640, 480};
    REQUIRE(target);

    std::shared_ptr<Image> image = loadImage(imageFixturePath().string(), target.canvas().gpuDevice());
    REQUIRE(image);

    auto root = std::make_unique<SceneNode>(lambda::Rect{0.f, 0.f, 640.f, 480.f});
    root->appendChild(std::make_unique<RectNode>(
        lambda::Rect{0.f, 0.f, 640.f, 480.f},
        FillStyle::solid(Colors::white)
    ));
    root->appendChild(std::make_unique<ImageNode>(
        lambda::Rect{20.f, 20.f, 120.f, 160.f},
        image,
        ImageFillMode::Stretch
    ));

    auto clip = std::make_unique<RectNode>(
        lambda::Rect{180.f, 40.f, 120.f, 140.f},
        FillStyle::none(),
        StrokeStyle::none(),
        CornerRadius{}
    );
    clip->setClipsContents(true);
    clip->appendChild(std::make_unique<ImageNode>(
        lambda::Rect{0.f, -20.f, 120.f, 160.f},
        image,
        ImageFillMode::Stretch
    ));
    root->appendChild(std::move(clip));

    SceneGraph graph{std::move(root)};
    target.render(graph, Colors::white);

    std::vector<std::uint8_t> pixels = target.readPixels();
    std::uint32_t width = target.pixelW;
    REQUIRE(!pixels.empty());

    std::uint32_t const leftX = 100;
    std::uint32_t const leftY = 120;
    std::uint32_t const rightX = 420;
    std::uint32_t const rightY = 120;
    CHECK(colorDelta(pixels, width, leftX, leftY, rightX, rightY) <= 18);
  }
#endif
}

TEST_CASE("SceneRenderer rasterizes RasterCacheNode into a reusable Metal image") {
#if !defined(__APPLE__)
  SUCCEED();
#else
  @autoreleasepool {
    CoreTextSystem textSystem;
    HeadlessMetalTarget target{textSystem, 640, 480};
    REQUIRE(target);
    Canvas& canvas = target.canvas();

    auto root = std::make_unique<SceneNode>(lambda::Rect{0.f, 0.f, 160.f, 120.f});
    auto raster = std::make_unique<RasterCacheNode>(lambda::Rect{20.f, 24.f, 80.f, 40.f});
    RasterCacheNode* rasterNode = raster.get();
    raster->setSubtree(std::make_unique<RectNode>(
        lambda::Rect{0.f, 0.f, 80.f, 40.f},
        FillStyle::solid(Colors::red)
    ));
    root->appendChild(std::move(raster));

    SceneGraph graph{std::move(root)};
    SceneRenderer renderer{canvas};

    target.begin(Colors::black);
    renderer.render(graph);
    std::shared_ptr<Image> firstCache = rasterNode->cachedImage();
    REQUIRE(firstCache);
    CHECK(firstCache->size() == lambda::Size{160.f, 80.f});
    target.end();

    target.begin(Colors::black);
    renderer.render(graph);
    CHECK(rasterNode->cachedImage() == firstCache);
    target.end();
  }
#endif
}
