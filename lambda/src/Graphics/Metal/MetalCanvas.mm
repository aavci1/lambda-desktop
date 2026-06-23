#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#import <simd/simd.h>

#include <Lambda/Graphics/Canvas.hpp>
#include <Lambda/Graphics/Image.hpp>
#include <Lambda/Graphics/RenderTarget.hpp>
#include <Lambda/Graphics/TextSystem.hpp>
#include <Lambda/Graphics/TextLayout.hpp>

#include "Graphics/CanvasGeometry.hpp"
#include "Graphics/Metal/GlyphAtlas.hpp"
#include "Graphics/Metal/MetalImage.hpp"
#include "Graphics/Metal/MetalCanvasTypes.hpp"
#include "Graphics/Metal/MetalDeviceResources.hpp"
#include "Graphics/Metal/MetalFrameRecorder.hpp"
#include "Graphics/Metal/MetalPathRasterizer.hpp"
#include "Debug/PerfCounters.hpp"

namespace lambdaui {
class Window;
}

#include "Graphics/Metal/MetalCanvas.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace lambdaui {

namespace {

constexpr NSUInteger kQuadStripCount = 4;
constexpr NSUInteger kFramesInFlight = 3;
constexpr std::uint32_t kPreferredLiveFrameSampleCount = 4;

std::uint32_t chooseLiveFrameSampleCount(id<MTLDevice> device) {
  if (device && [device supportsTextureSampleCount:kPreferredLiveFrameSampleCount]) {
    return kPreferredLiveFrameSampleCount;
  }
  return 1;
}

vector_float4 toSimd4(const Color& c) { return simd_make_float4(c.r, c.g, c.b, c.a); }

vector_float4 cornersToSimd(const CornerRadius& cr) {
  return simd_make_float4(cr.topLeft, cr.topRight, cr.bottomRight, cr.bottomLeft);
}

constexpr float kGradientTypeNone = 0.f;
constexpr float kGradientTypeLinear = 1.f;
constexpr float kGradientTypeRadial = 2.f;
constexpr float kGradientTypeConical = 3.f;

struct EncodedFillStyle {
  bool hasFill = false;
  Color fillColor = Colors::transparent;
  Color gradientColor1 = Colors::transparent;
  Color gradientColor2 = Colors::transparent;
  Color gradientColor3 = Colors::transparent;
  vector_float4 gradientStops = simd_make_float4(0.f, 1.f, 1.f, 1.f);
  vector_float4 gradientPoints = simd_make_float4(0.f, 0.f, 1.f, 1.f);
  float gradientStopCount = 0.f;
  float gradientType = kGradientTypeNone;
};

template <typename Gradient>
void encodeGradientStops(EncodedFillStyle& encoded, Gradient const& gradient) {
  encoded.hasFill = true;
  encoded.fillColor = gradient.stops[0].color;
  encoded.gradientColor1 = gradient.stops[1].color;
  encoded.gradientColor2 = gradient.stopCount > 2 ? gradient.stops[2].color : gradient.stops[1].color;
  encoded.gradientColor3 = gradient.stopCount > 3 ? gradient.stops[3].color : encoded.gradientColor2;
  encoded.gradientStops = simd_make_float4(
      gradient.stops[0].position,
      gradient.stops[1].position,
      gradient.stopCount > 2 ? gradient.stops[2].position : gradient.stops[1].position,
      gradient.stopCount > 3 ? gradient.stops[3].position : gradient.stops[gradient.stopCount - 1].position);
  encoded.gradientStopCount = static_cast<float>(gradient.stopCount);
}

EncodedFillStyle encodeFillStyle(FillStyle const& fs) {
  EncodedFillStyle encoded{};
  Color color{};
  if (fs.solidColor(&color)) {
    encoded.hasFill = true;
    encoded.fillColor = color;
    return encoded;
  }

  LinearGradient gradient{};
  if (fs.linearGradient(&gradient) && gradient.stopCount >= 2) {
    encodeGradientStops(encoded, gradient);
    encoded.gradientPoints =
        simd_make_float4(gradient.start.x, gradient.start.y, gradient.end.x, gradient.end.y);
    encoded.gradientType = kGradientTypeLinear;
    return encoded;
  }
  RadialGradient radial{};
  if (fs.radialGradient(&radial) && radial.stopCount >= 2) {
    encodeGradientStops(encoded, radial);
    encoded.gradientPoints = simd_make_float4(radial.center.x, radial.center.y, radial.radius, 0.f);
    encoded.gradientType = kGradientTypeRadial;
    return encoded;
  }
  ConicalGradient conical{};
  if (fs.conicalGradient(&conical) && conical.stopCount >= 2) {
    encodeGradientStops(encoded, conical);
    encoded.gradientPoints =
        simd_make_float4(conical.center.x, conical.center.y, conical.startAngleRadians, 0.f);
    encoded.gradientType = kGradientTypeConical;
  }
  return encoded;
}

bool representativeFillColor(FillStyle const& fs, Color* out) {
  if (fs.solidColor(out)) {
    return true;
  }
  LinearGradient gradient{};
  if (fs.linearGradient(&gradient) && gradient.stopCount > 0) {
    *out = gradient.stops[0].color;
    return true;
  }
  RadialGradient radial{};
  if (fs.radialGradient(&radial) && radial.stopCount > 0) {
    *out = radial.stops[0].color;
    return true;
  }
  ConicalGradient conical{};
  if (fs.conicalGradient(&conical) && conical.stopCount > 0) {
    *out = conical.stops[0].color;
    return true;
  }
  return false;
}

bool intersects(Rect const& a, Rect const& b) { return a.intersects(b); }

float averageLinearScale(Mat3 const& m) {
  float const sx = std::hypot(m.m[0], m.m[1]);
  float const sy = std::hypot(m.m[3], m.m[4]);
  constexpr float eps = 1e-6f;
  if (sx <= eps && sy <= eps) {
    return 1.f;
  }
  if (sx <= eps) {
    return sy;
  }
  if (sy <= eps) {
    return sx;
  }
  return 0.5f * (sx + sy);
}

bool tryDecomposeSimilarity(Mat3 const& m, float* outAngle, float* outScale) {
  const float a = m.m[0], b = m.m[1], c = m.m[3], d = m.m[4];
  const float det = a * d - b * c;
  const float sx = std::hypot(a, b);
  const float sy = std::hypot(c, d);
  constexpr float eps = 1e-5f;
  if (det <= eps || sx <= eps || sy <= eps) {
    return false;
  }
  float const scale = 0.5f * (sx + sy);
  if (std::abs(sx - sy) > std::max(0.01f, scale * 0.02f)) {
    return false;
  }
  if (std::abs(a * c + b * d) > std::max(0.01f, scale * scale * 0.02f)) {
    return false;
  }
  *outAngle = std::atan2(b, a);
  *outScale = scale;
  return true;
}

/** Stroke-only paths that flatten to polylines (Move/Line only). */
bool pathIsMoveLineOnlyStroke(Path const& path) {
  if (path.commandCount() < 2) {
    return false;
  }
  for (size_t i = 0; i < path.commandCount(); ++i) {
    Path::CommandView cv = path.command(i);
    switch (cv.type) {
      case Path::CommandType::MoveTo:
      case Path::CommandType::LineTo:
      case Path::CommandType::SetWinding:
        break;
      default:
        return false;
    }
  }
  return true;
}

template <typename Vec>
void recordVectorCapacityIncrease(std::size_t previousCapacity, Vec const& vec);

void appendGlyphQuad(std::vector<MetalGlyphVertex>& out, Mat3 const& M, float dpiX, float dpiY, Point tlLogical,
                     float gw, float gh, float u0, float v0, float u1, float v1, vector_float4 premulRgba) {
  Point const c0 = tlLogical;
  Point const c1 = {tlLogical.x + gw, tlLogical.y};
  Point const c2 = {tlLogical.x, tlLogical.y + gh};
  Point const c3 = {tlLogical.x + gw, tlLogical.y + gh};
  Point p0 = M.apply(c0);
  Point p1 = M.apply(c1);
  Point p2 = M.apply(c2);
  Point p3 = M.apply(c3);
  p0.x *= dpiX;
  p0.y *= dpiY;
  p1.x *= dpiX;
  p1.y *= dpiY;
  p2.x *= dpiX;
  p2.y *= dpiY;
  p3.x *= dpiX;
  p3.y *= dpiY;
  vector_float2 const uv0 = simd_make_float2(u0, v0);
  vector_float2 const uv1 = simd_make_float2(u1, v0);
  vector_float2 const uv2 = simd_make_float2(u0, v1);
  vector_float2 const uv3 = simd_make_float2(u1, v1);
  auto push = [&](Point const& p, vector_float2 uv) {
    MetalGlyphVertex gv{};
    gv.pos = simd_make_float2(p.x, p.y);
    gv.uv = uv;
    gv.color = premulRgba;
    out.push_back(gv);
  };
  push(p0, uv0);
  push(p1, uv1);
  push(p2, uv2);
  push(p1, uv1);
  push(p3, uv3);
  push(p2, uv2);
}

void appendOwnedGlyphSource(MetalFrameRecorder& recorder, std::uint32_t start, std::uint32_t count) {
  if (count == 0) {
    return;
  }
  std::size_t const sourceCapacity = recorder.glyphVertexSources.capacity();
  recorder.glyphVertexSources.push_back(MetalGlyphVertexSource{
      .kind = MetalGlyphVertexSource::Owned,
      .start = start,
      .count = count,
      .borrowed = nullptr,
  });
  recordVectorCapacityIncrease(sourceCapacity, recorder.glyphVertexSources);
  recorder.glyphVertexCount += count;
}

void appendBorrowedGlyphSource(MetalFrameRecorder& recorder, MetalGlyphVertex const* vertices,
                               std::uint32_t count) {
  if (count == 0) {
    return;
  }
  std::size_t const sourceCapacity = recorder.glyphVertexSources.capacity();
  recorder.glyphVertexSources.push_back(MetalGlyphVertexSource{
      .kind = MetalGlyphVertexSource::Borrowed,
      .start = 0,
      .count = count,
      .borrowed = vertices,
  });
  recordVectorCapacityIncrease(sourceCapacity, recorder.glyphVertexSources);
  recorder.glyphVertexCount += count;
}

template <typename T>
void tagOpWithClip(T& op, bool clipValid, MTLScissorRect const& clip, MetalRoundedClipStack const& roundedClip) {
  if (clipValid) {
    op.scissorValid = true;
    op.scissorX = static_cast<std::uint32_t>(clip.x);
    op.scissorY = static_cast<std::uint32_t>(clip.y);
    op.scissorW = static_cast<std::uint32_t>(clip.width);
    op.scissorH = static_cast<std::uint32_t>(clip.height);
  } else {
    op.scissorValid = false;
  }
  op.roundedClip = roundedClip;
}

void* retainTexturePointer(void* texture) {
  if (!texture) {
    return nullptr;
  }
  return (__bridge_retained void*)((__bridge id<MTLTexture>)texture);
}

template <typename T>
bool sameScissorForBatch(T const& a, T const& b) {
  if (a.scissorValid != b.scissorValid) {
    return false;
  }
  if (!a.scissorValid) {
    return true;
  }
  return a.scissorX == b.scissorX && a.scissorY == b.scissorY && a.scissorW == b.scissorW &&
         a.scissorH == b.scissorH;
}

template <typename T>
bool sameRoundedClipForBatch(T const& a, T const& b) {
  return std::memcmp(&a.roundedClip, &b.roundedClip, sizeof(MetalRoundedClipStack)) == 0;
}

template <typename T>
bool sameTranslationForBatch(T const& a, T const& b) {
  return a.translation.x == b.translation.x && a.translation.y == b.translation.y;
}

template <typename Vec>
void recordVectorCapacityIncrease(std::size_t previousCapacity, Vec const& vec) {
  if (!debug::perf::enabled() || vec.capacity() <= previousCapacity) {
    return;
  }
  using Value = typename Vec::value_type;
  debug::perf::recordRecorderCapacityGrowth(
      static_cast<std::uint64_t>((vec.capacity() - previousCapacity) * sizeof(Value)));
}

struct RecorderCapacitySnapshot {
  bool enabled = false;
  std::size_t rectOps = 0;
  std::size_t imageOps = 0;
  std::size_t pathOps = 0;
  std::size_t glyphOps = 0;
  std::size_t opOrder = 0;
  std::size_t pathVerts = 0;
  std::size_t glyphVerts = 0;
  std::size_t glyphSources = 0;
};

struct MetalBackdropUniforms {
  vector_float4 rect{};
  vector_float4 tint{};
  vector_float4 corners{};
  vector_float4 params{};
  vector_float4 blurParams{};
};

struct MetalRecorderEncodeState {
  std::uint32_t uniformIndex = 0;
  std::uint32_t clipIndex = 0;
};

RecorderCapacitySnapshot snapshotRecorderCapacity(MetalFrameRecorder const& recorder) {
  if (!debug::perf::enabled()) {
    return {};
  }
  return RecorderCapacitySnapshot{
      .enabled = true,
      .rectOps = recorder.rectOps.capacity(),
      .imageOps = recorder.imageOps.capacity(),
      .pathOps = recorder.pathOps.capacity(),
      .glyphOps = recorder.glyphOps.capacity(),
      .opOrder = recorder.opOrder.capacity(),
      .pathVerts = recorder.pathVerts.capacity(),
      .glyphVerts = recorder.glyphVerts.capacity(),
      .glyphSources = recorder.glyphVertexSources.capacity(),
  };
}

void recordRecorderCapacityIncreases(RecorderCapacitySnapshot const& before, MetalFrameRecorder const& recorder) {
  if (!before.enabled) {
    return;
  }
  recordVectorCapacityIncrease(before.rectOps, recorder.rectOps);
  recordVectorCapacityIncrease(before.imageOps, recorder.imageOps);
  recordVectorCapacityIncrease(before.pathOps, recorder.pathOps);
  recordVectorCapacityIncrease(before.glyphOps, recorder.glyphOps);
  recordVectorCapacityIncrease(before.opOrder, recorder.opOrder);
  recordVectorCapacityIncrease(before.pathVerts, recorder.pathVerts);
  recordVectorCapacityIncrease(before.glyphVerts, recorder.glyphVerts);
  recordVectorCapacityIncrease(before.glyphSources, recorder.glyphVertexSources);
}

MetalDrawUniforms makeDrawUniforms(float viewportW, float viewportH, vector_float2 translation) {
  MetalDrawUniforms uniforms{};
  uniforms.viewport = simd_make_float2(viewportW, viewportH);
  uniforms.translation = translation;
  return uniforms;
}

template <typename T>
void setEncoderScissorForOp(id<MTLRenderCommandEncoder> enc, T const& op, MTLScissorRect fullScissor,
                            MTLScissorRect* last, bool* haveLast) {
  MTLScissorRect sc = fullScissor;
  if (op.scissorValid) {
    sc.x = op.scissorX;
    sc.y = op.scissorY;
    sc.width = op.scissorW;
    sc.height = op.scissorH;
  }
  if (!*haveLast || sc.x != last->x || sc.y != last->y || sc.width != last->width || sc.height != last->height) {
    [enc setScissorRect:sc];
    *last = sc;
    *haveLast = true;
  }
}

inline void setEncoderRoundedClipBuffer(id<MTLRenderCommandEncoder> enc, id<MTLBuffer> clipBuffer,
                                        MetalRoundedClipStack* clipDst, std::uint32_t* clipIndex,
                                        MetalRoundedClipStack const& roundedClip) {
  clipDst[*clipIndex] = roundedClip;
  NSUInteger const offset = static_cast<NSUInteger>(*clipIndex) * sizeof(MetalRoundedClipStack);
  [enc setFragmentBuffer:clipBuffer offset:offset atIndex:0];
  ++(*clipIndex);
}

inline void setEncoderDrawUniformBuffer(id<MTLRenderCommandEncoder> enc, id<MTLBuffer> uniformBuffer,
                                        MetalDrawUniforms* uniformDst, std::uint32_t* uniformIndex,
                                        MetalDrawUniforms const& uniforms, NSUInteger bufferIndex) {
  uniformDst[*uniformIndex] = uniforms;
  NSUInteger const offset = static_cast<NSUInteger>(*uniformIndex) * sizeof(MetalDrawUniforms);
  [enc setVertexBuffer:uniformBuffer offset:offset atIndex:bufferIndex];
  ++(*uniformIndex);
}

class ScopedCanvasPresentTimer {
public:
  ScopedCanvasPresentTimer()
      : enabled_(debug::perf::enabled()),
        startedAt_(enabled_ ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{}) {}

  ~ScopedCanvasPresentTimer() {
    if (!enabled_) {
      return;
    }
    auto elapsed = std::chrono::steady_clock::now() - startedAt_;
    if (elapsed > drawableWait_) {
      elapsed -= drawableWait_;
    } else {
      elapsed = std::chrono::steady_clock::duration::zero();
    }
    debug::perf::recordDuration(debug::perf::TimedMetric::CanvasPresent,
                                std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed));
  }

  bool enabled() const { return enabled_; }

  void recordDrawableWaitSince(std::chrono::steady_clock::time_point startedAt) {
    if (!enabled_) {
      return;
    }
    auto const elapsed = std::chrono::steady_clock::now() - startedAt;
    drawableWait_ += elapsed;
    debug::perf::recordDuration(debug::perf::TimedMetric::CanvasDrawableWait,
                                std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed));
  }

private:
  bool enabled_ = false;
  std::chrono::steady_clock::time_point startedAt_{};
  std::chrono::steady_clock::duration drawableWait_{};
};

} // namespace

class MetalCanvas final : public Canvas {
public:
  MetalCanvas(Window* /*window*/, CAMetalLayer* layer, unsigned int handle, TextSystem& textSystem,
              std::function<void()> requestRedraw)
      : textSystem_(textSystem)
      , metal_(layer)
      , windowHandle_(handle)
      , requestRedraw_(std::move(requestRedraw)) {
    glyphAtlas_ = std::make_unique<GlyphAtlas>(metal_.device(), textSystem_, metal_.queue());
    glyphAtlas_->setBeforeGrowCallback([this]() {
      MetalFrameRecorder const& recorder = activeRecorder();
      return recorder.glyphVerts.empty() && recorder.glyphVertexSources.empty() &&
             recorder.glyphVertexCount == 0;
    });
    frameSem_ = dispatch_semaphore_create(static_cast<int>(kFramesInFlight));
    frameSampleCount_ = chooseLiveFrameSampleCount(metal_.device());
    pushState();
  }

  MetalCanvas(MetalRenderTargetSpec const& spec, TextSystem& textSystem)
      : textSystem_(textSystem)
      , metal_(targetDevice(spec), targetPixelFormat(spec))
      , targetSpec_(spec)
      , targetMode_(true) {
    if (!targetTexture()) {
      throw std::runtime_error("Metal RenderTarget requires a valid MTLTexture");
    }
    glyphAtlas_ = std::make_unique<GlyphAtlas>(metal_.device(), textSystem_, metal_.queue());
    glyphAtlas_->setBeforeGrowCallback([this]() {
      MetalFrameRecorder const& recorder = activeRecorder();
      return recorder.glyphVerts.empty() && recorder.glyphVertexSources.empty() &&
             recorder.glyphVertexCount == 0;
    });
    frameSem_ = dispatch_semaphore_create(static_cast<int>(kFramesInFlight));
    frameSampleCount_ = 1;
    pushState();
  }

  ~MetalCanvas() override { frame_.clear(); }

  Backend backend() const noexcept override { return Backend::Metal; }

  unsigned int windowHandle() const override { return windowHandle_; }

  void resize(int width, int height) override {
    logicalW_ = width;
    logicalH_ = height;
  }

  void updateDpiScale(float scaleX, float scaleY) override {
    dpiScaleX_ = scaleX;
    dpiScaleY_ = scaleY;
    dpiScale_ = std::min(dpiScaleX_, dpiScaleY_);
  }

  void beginFrame() override {
    frame_.clear();
    // Each frame must start from a clean stack and identity transform. If a prior `render`
    // left extra states (e.g. unbalanced save/restore), transforms would accumulate and later
    // nodes (cards, subtitle) would draw off-screen or with wrong opacity.
    while (stateStack_.size() > 1) {
      stateStack_.pop_back();
    }
    if (!stateStack_.empty()) {
      stateStack_.back() = GpuState{};
      updateClipScissor();
    }
    if (!targetMode_) {
      dispatch_semaphore_wait(frameSem_, DISPATCH_TIME_FOREVER);
    }
    metal_.advanceFrame();
    if (targetMode_) {
      refreshTargetFrameMetrics();
    } else {
      refreshFrameDrawableMetrics();
    }
    drawable_ = nil;
    cmdBuf_ = nil;
    inFrame_ = true;
    if (!targetMode_ && frameDrawableW_ >= 1.f && frameDrawableH_ >= 1.f &&
        !acquireDrawableForFrame()) {
      dispatch_semaphore_signal(frameSem_);
      inFrame_ = false;
      syncPresent_ = false;
      if (requestRedraw_) {
        requestRedraw_();
      }
      return;
    }
    const CGSize ds = frameDrawableSize_;
    CGFloat cs = 1.0;
    if (!targetMode_) {
      cs = metal_.layer().contentsScale;
      if (cs < 0.01) {
        cs = 1.0;
      }
      dpiScaleX_ = static_cast<float>(cs);
      dpiScaleY_ = static_cast<float>(cs);
      dpiScale_ = std::min(dpiScaleX_, dpiScaleY_);
    }
    if (!targetMode_ && (logicalW_ <= 0 || logicalH_ <= 0)) {
      logicalW_ = static_cast<int>(std::lround(static_cast<double>(ds.width) / static_cast<double>(cs)));
      logicalH_ = static_cast<int>(std::lround(static_cast<double>(ds.height) / static_cast<double>(cs)));
    }
    glyphAtlas_->prepareForFrameBegin();
  }

  void clear(Color color) override { clearColor_ = color; }

  void setSyncPresent(bool sync) noexcept { syncPresent_ = sync; }

  void present() override {
    if (!inFrame_) {
      syncPresent_ = false;
      return;
    }

    if (targetMode_) {
      presentRenderTarget();
      return;
    }

    struct FrameCounter {
      ~FrameCounter() { debug::perf::recordPresentedFrame(); }
    } frameCounter{};
    ScopedCanvasPresentTimer perfTimer;

    const float vw = frameDrawableW_;
    const float vh = frameDrawableH_;
    if (vw < 1.f || vh < 1.f) {
      syncPresent_ = false;
      frame_.clear();
      cmdBuf_ = nil;
      drawable_ = nil;
      dispatch_semaphore_signal(frameSem_);
      inFrame_ = false;
      return;
    }

    debug::perf::recordFrameOps(frame_.rectOps.size(), frame_.imageOps.size(), frame_.pathOps.size(),
                                frame_.glyphOps.size(), frame_.opOrder.size(), frame_.pathVerts.size(),
                                frame_.glyphVertexCount);
    std::uint32_t const uploadedRectInstances = metal_.uploadRectOps(frame_.rectOps);
    debug::perf::recordUploadBytes(debug::perf::RenderCounterKind::Rect,
                                   static_cast<std::uint64_t>(uploadedRectInstances) *
                                       sizeof(MetalRectInstance));
    std::uint32_t const uploadedImageInstances = metal_.uploadImageOps(frame_.imageOps);
    debug::perf::recordUploadBytes(debug::perf::RenderCounterKind::Image,
                                   static_cast<std::uint64_t>(uploadedImageInstances) *
                                       sizeof(MetalImageInstance));
    debug::perf::recordUploadBytes(debug::perf::RenderCounterKind::Path,
                                   frame_.pathVerts.size() * sizeof(PathVertex));
    debug::perf::recordUploadBytes(debug::perf::RenderCounterKind::Glyph,
                                   static_cast<std::uint64_t>(frame_.glyphVertexCount) *
                                       sizeof(MetalGlyphVertex));

    metal_.uploadPathVertices(frame_.pathVerts);
    metal_.uploadGlyphVertices(frame_);
    metal_.reserveDrawStateBuffers(static_cast<std::uint32_t>(frame_.opOrder.size()),
                                   static_cast<std::uint32_t>(frame_.opOrder.size()));

    cmdBuf_ = [metal_.queue() commandBuffer];
    if (!drawable_ || !cmdBuf_) {
      dispatch_semaphore_signal(frameSem_);
      cmdBuf_ = nil;
      drawable_ = nil;
      inFrame_ = false;
      syncPresent_ = false;
      frame_.clear();
      if (requestRedraw_) {
        requestRedraw_();
      }
      return;
    }
    glyphAtlas_->flushUploads(cmdBuf_);

    bool const backdropFrame = hasBackdropBlurOps(frame_);
    id<MTLTexture> renderTargetTexture = drawable_.texture;
    std::uint32_t renderSampleCount = 1;
    if (frameSampleCount_ > 1 && !backdropFrame) {
      if (id<MTLTexture> msaaTexture = ensureLiveFrameMsaaTexture(drawable_.texture)) {
        renderTargetTexture = msaaTexture;
        renderSampleCount = frameSampleCount_;
      }
    }

    bool const encodedBackdropFrame =
        backdropFrame &&
        encodeFrameWithBackdropBlur(renderTargetTexture, drawable_.texture, renderSampleCount);

    if (!encodedBackdropFrame) {
      MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
      pass.colorAttachments[0].texture = renderTargetTexture;
      pass.colorAttachments[0].loadAction = MTLLoadActionClear;
      pass.colorAttachments[0].clearColor =
          MTLClearColorMake(clearColor_.r, clearColor_.g, clearColor_.b, clearColor_.a);
      if (renderSampleCount > 1) {
        pass.colorAttachments[0].resolveTexture = drawable_.texture;
        pass.colorAttachments[0].storeAction = MTLStoreActionMultisampleResolve;
      } else {
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
      }

      id<MTLRenderCommandEncoder> enc = [cmdBuf_ renderCommandEncoderWithDescriptor:pass];
      MTLViewport vp = {0, 0, frameDrawableSize_.width, frameDrawableSize_.height, 0.0, 1.0};
      [enc setViewport:vp];

    MTLScissorRect const fullScissor = {0, 0, frameDrawablePixelsW_, frameDrawablePixelsH_};
    MTLScissorRect lastScissor = {0, 0, 0, 0};
    bool haveScissor = false;

    id<MTLBuffer> pathBuf = metal_.pathVertexArenaBuffer();
    id<MTLBuffer> uniformBuf = metal_.drawUniformArenaBuffer();
    id<MTLBuffer> clipBuf = metal_.roundedClipArenaBuffer();
    auto* uniformDst = uniformBuf ? static_cast<MetalDrawUniforms*>([uniformBuf contents]) : nullptr;
    auto* clipDst = clipBuf ? static_cast<MetalRoundedClipStack*>([clipBuf contents]) : nullptr;
    std::uint32_t uniformIndex = 0;
    std::uint32_t clipIndex = 0;
    std::size_t const opCount = frame_.opOrder.size();
    std::size_t i = 0;
    while (i < opCount) {
      MetalOpRef const ref = frame_.opOrder[i];
      if (ref.kind == MetalOpRef::BackdropBlur) {
        ++i;
        continue;
      }
      if (ref.kind == MetalOpRef::Rect) {
        MetalRectOp const& op = frame_.rectOps[ref.index];
        std::size_t j = i + 1;
        while (j < opCount) {
          MetalOpRef const nextRef = frame_.opOrder[j];
          if (nextRef.kind != MetalOpRef::Rect) {
            break;
          }
          MetalRectOp const& o2 = frame_.rectOps[nextRef.index];
          MetalOpRef const prevRef = frame_.opOrder[j - 1];
          MetalRectOp const& prev = frame_.rectOps[prevRef.index];
          std::uint32_t const prevInstanceIndex =
              prev.externalInstanceBuffer ? prev.externalInstanceIndex : prev.arenaInstanceIndex;
          std::uint32_t const nextInstanceIndex =
              o2.externalInstanceBuffer ? o2.externalInstanceIndex : o2.arenaInstanceIndex;
          if (o2.externalInstanceBuffer != op.externalInstanceBuffer ||
              o2.isLine != op.isLine || o2.blendMode != op.blendMode || !sameScissorForBatch(op, o2) ||
              !sameRoundedClipForBatch(op, o2) || !sameTranslationForBatch(op, o2) ||
              nextInstanceIndex != prevInstanceIndex + 1) {
            break;
          }
          ++j;
        }
        std::size_t const runLen = j - i;
        setEncoderScissorForOp(enc, op, fullScissor, &lastScissor, &haveScissor);
        MetalDrawUniforms const uniforms = makeDrawUniforms(vw, vh, op.translation);
        setEncoderRoundedClipBuffer(enc, clipBuf, clipDst, &clipIndex, op.roundedClip);
        id<MTLBuffer> instanceBuf =
            op.externalInstanceBuffer ? (__bridge id<MTLBuffer>)op.externalInstanceBuffer
                                      : metal_.instanceArenaBuffer();
        std::uint32_t const instanceIndex =
            op.externalInstanceBuffer ? op.externalInstanceIndex : op.arenaInstanceIndex;
        setEncoderDrawUniformBuffer(enc, uniformBuf, uniformDst, &uniformIndex, uniforms, 2);
        if (!op.isLine) {
          [enc setRenderPipelineState:metal_.rectPSO(op.blendMode, renderSampleCount)];
          [enc setVertexBuffer:metal_.quadBuffer() offset:0 atIndex:0];
          const NSUInteger off = static_cast<NSUInteger>(instanceIndex) * sizeof(MetalRectInstance);
          [enc setVertexBuffer:instanceBuf offset:off atIndex:1];
          [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:kQuadStripCount
                  instanceCount:static_cast<NSUInteger>(runLen)];
          debug::perf::recordDrawCall(debug::perf::RenderCounterKind::Rect);
        } else {
          [enc setRenderPipelineState:metal_.linePSO(op.blendMode, renderSampleCount)];
          [enc setVertexBuffer:metal_.quadBuffer() offset:0 atIndex:0];
          const NSUInteger off = static_cast<NSUInteger>(instanceIndex) * sizeof(MetalRectInstance);
          [enc setVertexBuffer:instanceBuf offset:off atIndex:1];
          [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:kQuadStripCount
                  instanceCount:static_cast<NSUInteger>(runLen)];
          debug::perf::recordDrawCall(debug::perf::RenderCounterKind::Rect);
        }
        i = j;
        continue;
      }
      if (ref.kind == MetalOpRef::Glyph) {
        MetalGlyphOp const& op = frame_.glyphOps[ref.index];
        std::size_t j = i + 1;
        std::uint32_t runStart = op.glyphStart;
        std::uint32_t runVerts = op.glyphVertexCount;
        while (j < opCount) {
          MetalOpRef const nextRef = frame_.opOrder[j];
          if (nextRef.kind != MetalOpRef::Glyph) {
            break;
          }
          MetalGlyphOp const& o2 = frame_.glyphOps[nextRef.index];
          if (o2.externalVertexBuffer != op.externalVertexBuffer ||
              o2.blendMode != op.blendMode || !sameScissorForBatch(op, o2) ||
              !sameRoundedClipForBatch(op, o2) || !sameTranslationForBatch(op, o2) ||
              o2.glyphStart != runStart + runVerts || nextRef.index != frame_.opOrder[j - 1].index + 1) {
            break;
          }
          runVerts += o2.glyphVertexCount;
          ++j;
        }
        setEncoderScissorForOp(enc, op, fullScissor, &lastScissor, &haveScissor);
        setEncoderRoundedClipBuffer(enc, clipBuf, clipDst, &clipIndex, op.roundedClip);
        [enc setRenderPipelineState:metal_.glyphPSO(op.blendMode, renderSampleCount)];
        id<MTLBuffer> gbuf =
            op.externalVertexBuffer ? (__bridge id<MTLBuffer>)op.externalVertexBuffer : metal_.glyphVertexArenaBuffer();
        const NSUInteger goff = static_cast<NSUInteger>(runStart) * sizeof(MetalGlyphVertex);
        [enc setVertexBuffer:gbuf offset:goff atIndex:0];
        MetalDrawUniforms const uniforms = makeDrawUniforms(vw, vh, op.translation);
        setEncoderDrawUniformBuffer(enc, uniformBuf, uniformDst, &uniformIndex, uniforms, 1);
        [enc setFragmentTexture:glyphAtlas_->texture() atIndex:0];
        [enc setFragmentSamplerState:metal_.linearSampler() atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:static_cast<NSUInteger>(runVerts)];
        debug::perf::recordDrawCall(debug::perf::RenderCounterKind::Glyph);
        i = j;
        continue;
      }

      if (ref.kind == MetalOpRef::Image) {
        MetalImageOp const& op = frame_.imageOps[ref.index];
        setEncoderScissorForOp(enc, op, fullScissor, &lastScissor, &haveScissor);
        if (!op.texture) {
          ++i;
          continue;
        }
        setEncoderRoundedClipBuffer(enc, clipBuf, clipDst, &clipIndex, op.roundedClip);
        [enc setRenderPipelineState:metal_.imagePSO(op.blendMode, renderSampleCount)];
        [enc setVertexBuffer:metal_.quadBuffer() offset:0 atIndex:0];
        id<MTLBuffer> imageInstanceBuf =
            op.externalInstanceBuffer ? (__bridge id<MTLBuffer>)op.externalInstanceBuffer
                                      : metal_.imageInstanceArenaBuffer();
        std::uint32_t const imageInstanceIndex =
            op.externalInstanceBuffer ? op.externalInstanceIndex : op.arenaInstanceIndex;
        const NSUInteger off = static_cast<NSUInteger>(imageInstanceIndex) * sizeof(MetalImageInstance);
        [enc setVertexBuffer:imageInstanceBuf offset:off atIndex:1];
        MetalDrawUniforms const uniforms = makeDrawUniforms(vw, vh, op.translation);
        setEncoderDrawUniformBuffer(enc, uniformBuf, uniformDst, &uniformIndex, uniforms, 2);
        [enc setFragmentTexture:(__bridge id<MTLTexture>)op.texture atIndex:0];
        [enc setFragmentSamplerState:op.repeatSampler ? metal_.repeatSampler() : metal_.linearSampler() atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:kQuadStripCount
                instanceCount:1];
        debug::perf::recordDrawCall(debug::perf::RenderCounterKind::Image);
        ++i;
        continue;
      }
      if (ref.kind == MetalOpRef::Path) {
        MetalPathOp const& op = frame_.pathOps[ref.index];
        setEncoderScissorForOp(enc, op, fullScissor, &lastScissor, &haveScissor);
        if (op.pathCount == 0) {
          ++i;
          continue;
        }
        setEncoderRoundedClipBuffer(enc, clipBuf, clipDst, &clipIndex, op.roundedClip);
        [enc setRenderPipelineState:metal_.pathPSO(op.blendMode, renderSampleCount)];
        const NSUInteger off = static_cast<NSUInteger>(op.pathStart) * sizeof(PathVertex);
        id<MTLBuffer> effectivePathBuf =
            op.externalVertexBuffer ? (__bridge id<MTLBuffer>)op.externalVertexBuffer : pathBuf;
        [enc setVertexBuffer:effectivePathBuf offset:off atIndex:0];
        MetalDrawUniforms const uniforms = makeDrawUniforms(vw, vh, op.translation);
        setEncoderDrawUniformBuffer(enc, uniformBuf, uniformDst, &uniformIndex, uniforms, 1);
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:static_cast<NSUInteger>(op.pathCount)];
        debug::perf::recordDrawCall(debug::perf::RenderCounterKind::Path);
        ++i;
        continue;
      }
      assert(false && "unsupported Metal op ref kind");
    }

      [enc endEncoding];
    }

    if (captureNextFrame_) {
      captureWidth_ = static_cast<std::uint32_t>(frameDrawablePixelsW_);
      captureHeight_ = static_cast<std::uint32_t>(frameDrawablePixelsH_);
      captureBytesPerRow_ = static_cast<NSUInteger>(captureWidth_) * 4U;
      captureBuffer_ = [metal_.device() newBufferWithLength:captureBytesPerRow_ * captureHeight_
                                                    options:MTLResourceStorageModeShared];
      if (captureBuffer_) {
        id<MTLBlitCommandEncoder> blit = [cmdBuf_ blitCommandEncoder];
        [blit copyFromTexture:drawable_.texture
                  sourceSlice:0
                  sourceLevel:0
                 sourceOrigin:MTLOriginMake(0, 0, 0)
                   sourceSize:MTLSizeMake(captureWidth_, captureHeight_, 1)
                     toBuffer:captureBuffer_
            destinationOffset:0
       destinationBytesPerRow:captureBytesPerRow_
     destinationBytesPerImage:captureBytesPerRow_ * captureHeight_];
        [blit endEncoding];
      }
      captureNextFrame_ = false;
    }

    if (syncPresent_) {
      lastSubmittedCmdBuf_ = cmdBuf_;
      [cmdBuf_ commit];
      [cmdBuf_ waitUntilScheduled];
      [drawable_ present];
      dispatch_semaphore_signal(frameSem_);
      syncPresent_ = false;
    } else {
      dispatch_semaphore_t sem = frameSem_;
      [cmdBuf_ addCompletedHandler:^(id<MTLCommandBuffer> /*cb*/) {
        dispatch_semaphore_signal(sem);
      }];
      [cmdBuf_ presentDrawable:drawable_];
      lastSubmittedCmdBuf_ = cmdBuf_;
      [cmdBuf_ commit];
    }

    frame_.clear();
    glyphAtlas_->afterPresent();

    cmdBuf_ = nil;
    drawable_ = nil;
    inFrame_ = false;
  }

  void save() override { pushState(); }

  void restore() override {
    if (stateStack_.size() <= 1) {
      return;
    }
    popState();
  }

  void setTransform(Mat3 const& m) override {
    currentState().transform = m;
    updateClipScissor();
  }

  void transform(Mat3 const& m) override {
    auto& st = currentState();
    auto const adjustRoundedClips = [&](float cornerScale) {
      for (std::uint32_t i = 0; i < st.roundedClipCount; ++i) {
        RoundedClipState& mask = st.roundedClips[i];
        mask.rect = boundsOfTransformedRect(mask.rect, m.inverse());
        mask.corners.topLeft *= cornerScale;
        mask.corners.topRight *= cornerScale;
        mask.corners.bottomRight *= cornerScale;
        mask.corners.bottomLeft *= cornerScale;
        clampRoundRectCornerRadii(mask.rect.width, mask.rect.height, mask.corners);
      }
    };
    if (st.clip.has_value()) {
      st.clip = boundsOfTransformedRect(*st.clip, m.inverse());
    }
    if (st.roundedClipCount > 0) {
      if (m.isTranslationOnly()) {
        adjustRoundedClips(1.f);
      } else {
        float const sx = std::hypot(m.m[0], m.m[1]);
        float const sy = std::hypot(m.m[3], m.m[4]);
        constexpr float eps = 1e-4f;
        if (sx > eps && sy > eps && std::abs(sx - sy) <= eps && std::abs(m.m[1]) <= eps && std::abs(m.m[3]) <= eps) {
          adjustRoundedClips(1.f / sx);
        } else {
          adjustRoundedClips(0.f);
        }
      }
    }
    st.transform = st.transform * m;
    updateClipScissor();
  }

  void translate(Point offset) override { transform(Mat3::translate(offset)); }

  void translate(float x, float y) override { translate(Point{x, y}); }

  void scale(float sx, float sy) override { transform(Mat3::scale(sx, sy)); }

  void scale(float s) override { scale(s, s); }

  void rotate(float radians) override { transform(Mat3::rotate(radians)); }

  void rotate(float radians, Point pivot) override { transform(Mat3::rotate(radians, pivot)); }

  Mat3 currentTransform() const override { return currentState().transform; }

  void clipRect(Rect rect, CornerRadius const& cornerRadius, bool /*antiAlias*/) override {
    auto& st = currentState();
    Rect effective = rect;
    if (!st.clip.has_value()) {
      st.clip = rect;
    } else {
      effective = intersectRects(*st.clip, rect);
      st.clip = effective;
    }

    if (effective.width <= 0.f || effective.height <= 0.f || cornerRadius.isZero()) {
      updateClipScissor();
      return;
    }

    CornerRadius adjusted = cornerRadius;
    constexpr float eps = 1e-3f;
    bool const clipped =
        std::abs(effective.x - rect.x) > eps || std::abs(effective.y - rect.y) > eps ||
        std::abs(effective.width - rect.width) > eps || std::abs(effective.height - rect.height) > eps;
    if (clipped) {
      adjusted = cornerRadiiAfterAxisAlignedClip(rect, effective, cornerRadius);
    }
    clampRoundRectCornerRadii(effective.width, effective.height, adjusted);
    if (!adjusted.isZero() && st.roundedClipCount < kMetalRoundedClipMaskCapacity) {
      st.roundedClips[st.roundedClipCount++] = RoundedClipState{
          .rect = effective,
          .corners = adjusted,
      };
    }
    updateClipScissor();
  }

  Rect clipBounds() const override {
    if (stateStack_.empty()) {
      return viewportLogicalRect();
    }
    const auto& st = stateStack_.back();
    if (st.clip.has_value()) {
      return *st.clip;
    }
    return viewportLogicalRect();
  }

  bool quickReject(Rect rect) const override {
    if (!currentState().clip.has_value()) {
      return false;
    }
    // `rect` and `st.clip` are both in the current local coordinate system (same as drawRect).
    // Do not compare transformed bounds to an axis-aligned clip in local space.
    return !intersects(rect, *currentState().clip);
  }

  void setOpacity(float o) override { currentState().opacity = std::clamp(o, 0.f, 1.f); }

  float opacity() const override { return currentState().opacity; }

  void setBlendMode(BlendMode mode) override { currentState().blendMode = mode; }

  BlendMode blendMode() const override { return currentState().blendMode; }

  void drawRect(Rect const& rect, CornerRadius const& cornerRadius, FillStyle const& fs,
                StrokeStyle const& ss, ShadowStyle const& shadow) override {
    if (!inFrame_) {
      return;
    }
    EncodedFillStyle fill = encodeFillStyle(fs);
    Color strokeC{};
    bool hasStroke = false;
    if (!ss.isNone() && ss.solidColor(&strokeC)) {
      hasStroke = true;
    }
    if (!fill.hasFill && !hasStroke && shadow.isNone()) {
      return;
    }
    if (quickReject(rect)) {
      return;
    }
    const float op = effectiveOpacity();
    Mat3 const& M = currentState().transform;
    CornerRadius crEffective = cornerRadius;
    clampRoundRectCornerRadii(rect.width, rect.height, crEffective);

    float rotationRad = 0.f;
    float transformScale = 1.f;
    Rect mapped{};
    if (M.isTranslationOnly()) {
      mapped = Rect::sharp(rect.x + M.m[6], rect.y + M.m[7], rect.width, rect.height);
      rotationRad = 0.f;
    } else {
      bool const decomposed = tryDecomposeSimilarity(M, &rotationRad, &transformScale);
      if (decomposed) {
        Point const center = M.apply(rect.center());
        float const mappedWidth = rect.width * transformScale;
        float const mappedHeight = rect.height * transformScale;
        mapped = Rect::sharp(center.x - mappedWidth * 0.5f, center.y - mappedHeight * 0.5f,
                             mappedWidth, mappedHeight);
      } else {
        mapped = boundsOfTransformedRect(rect, M);
        transformScale = averageLinearScale(M);
        rotationRad = 0.f;
      }
    }
    const float s = dpiScale_ * transformScale;
    CornerRadius cr{};
    cr.topLeft = crEffective.topLeft * s;
    cr.topRight = crEffective.topRight * s;
    cr.bottomRight = crEffective.bottomRight * s;
    cr.bottomLeft = crEffective.bottomLeft * s;
    Rect device = Rect::sharp(mapped.x * dpiScaleX_, mapped.y * dpiScaleY_, mapped.width * dpiScaleX_,
                              mapped.height * dpiScaleY_);
    Color shadowC{};
    float shadowOx = 0.f;
    float shadowOy = 0.f;
    float shadowR = 0.f;
    if (!shadow.isNone()) {
      shadowC = shadow.color;
      shadowC.a *= op;
      shadowOx = shadow.offset.x * dpiScaleX_ * transformScale;
      shadowOy = shadow.offset.y * dpiScaleY_ * transformScale;
      shadowR = shadow.radius * s;
    }
    emitRect(device, cr, fill, hasStroke ? strokeC : Color{0, 0, 0, 0},
             hasStroke ? ss.width * s : 0.f, op, rotationRad, shadowC, shadowOx, shadowOy, shadowR);
  }

  void drawLine(Point a, Point b, StrokeStyle const& ss) override {
    if (!inFrame_) {
      return;
    }
    Color stroke{};
    if (!ss.solidColor(&stroke)) {
      return;
    }
    const float paintOpacity = effectiveOpacity();
    const float pad = std::max(ss.width * 2.f, 4.f);
    if (currentState().clip.has_value()) {
      const float minX = std::min(a.x, b.x) - pad;
      const float maxX = std::max(a.x, b.x) + pad;
      const float minY = std::min(a.y, b.y) - pad;
      const float maxY = std::max(a.y, b.y) + pad;
      Rect const lineBoundsLocal = Rect::sharp(minX, minY, maxX - minX, maxY - minY);
      if (!intersects(lineBoundsLocal, *currentState().clip)) {
        return;
      }
    }
    Point ta{};
    Point tb{};
    if (currentState().transform.isTranslationOnly()) {
      ta = Point{a.x + currentState().transform.m[6], a.y + currentState().transform.m[7]};
      tb = Point{b.x + currentState().transform.m[6], b.y + currentState().transform.m[7]};
    } else {
      ta = currentState().transform.apply(a);
      tb = currentState().transform.apply(b);
    }
    const float dx = tb.x - ta.x;
    const float dy = tb.y - ta.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    // Reject only true degenerates. A threshold like 1e-4f dropped vertical carets when
    // `line.bottom - line.top` was tiny-but-positive (Core Text / float noise) or subpixel after transform.
    if (!std::isfinite(len) || len <= 0.f) {
      return;
    }
    const float w = len + pad * 2.f;
    const float h = ss.width + pad * 2.f;
    const float cx = (ta.x + tb.x) * 0.5f;
    const float cy = (ta.y + tb.y) * 0.5f;
    Rect const lineBounds = Rect::sharp(cx - w * 0.5f, cy - h * 0.5f, w, h);

    MetalRectOp op{};
    op.isLine = true;
    op.inst.rect = simd_make_float4(lineBounds.x * dpiScaleX_, lineBounds.y * dpiScaleY_,
                                    lineBounds.width * dpiScaleX_, lineBounds.height * dpiScaleY_);
    const float inv = 1.f / len;
    const float lenDevice = std::hypot(dx * dpiScaleX_, dy * dpiScaleY_);
    op.inst.corners = simd_make_float4(dx * inv, dy * inv, lenDevice * 0.5f, 0.f);
    op.inst.fillColor = simd_make_float4(0.f, 0.f, 0.f, 0.f);
    op.inst.strokeColor = toSimd4(stroke);
    op.inst.strokeWidthOpacity =
        simd_make_float2(ss.width * dpiScale_, paintOpacity);
    op.inst.viewport = simd_make_float2(frameDrawableW_, frameDrawableH_);
    op.inst.rotationPad = simd_make_float4(0.f, 0.f, 0.f, 0.f);
    op.inst.shadowColor = simd_make_float4(0.f, 0.f, 0.f, 0.f);
    op.inst.shadowGeom = simd_make_float4(0.f, 0.f, 0.f, 0.f);
    op.blendMode = currentState().blendMode;
    pushRectOp(std::move(op));
  }

  void drawPath(Path const& path, FillStyle const& fs, StrokeStyle const& ss,
                ShadowStyle const& shadow) override {
    if (!inFrame_ || path.isEmpty()) {
      return;
    }
    MetalFrameRecorder& recorder = activeRecorder();

    if (path.commandCount() == 1) {
      Path::CommandView cv = path.command(0);
      if (cv.type == Path::CommandType::Rect && cv.dataCount >= 8) {
        const float* d = cv.data;
        Rect r{d[0], d[1], d[2], d[3]};
        CornerRadius cr{d[4], d[5], d[6], d[7]};
        drawRect(r, cr, fs, ss, shadow);
        return;
      }
      const bool circlePrim = cv.type == Path::CommandType::Circle && cv.dataCount >= 3;
      const bool ellipsePrim = cv.type == Path::CommandType::Ellipse && cv.dataCount >= 4;
      if ((circlePrim || ellipsePrim) && !fs.isNone()) {
        if (circlePrim) {
          float const rad = cv.data[2];
          Rect r{cv.data[0] - rad, cv.data[1] - rad, rad * 2.f, rad * 2.f};
          drawRect(r, CornerRadius::pill(r), fs, ss, shadow);
        } else {
          float const rx = cv.data[2];
          float const ry = cv.data[3];
          Rect r{cv.data[0] - rx, cv.data[1] - ry, rx * 2.f, ry * 2.f};
          drawRect(r, CornerRadius::pill(r), fs, ss, shadow);
        }
        return;
      }
    }

    // Round stroke on open polylines: `drawLine` uses the capsule SDF (same rounded look as clock
    // hands). CPU path-mesh stroke expansion is a separate pipeline and does not match that shader.
    if (fs.isNone() && ss.cap == StrokeCap::Round && ss.join == StrokeJoin::Round && pathIsMoveLineOnlyStroke(path)) {
      Color sc{};
      if (ss.solidColor(&sc)) {
        auto subpaths = PathFlattener::flattenSubpaths(path);
        std::size_t const nOpsBefore = recorder.opOrder.size();
        for (auto const& sp : subpaths) {
          if (sp.size() < 2) {
            continue;
          }
          for (size_t i = 0; i + 1 < sp.size(); ++i) {
            drawLine(sp[i], sp[i + 1], ss);
          }
        }
        if (recorder.opOrder.size() > nOpsBefore) {
          return;
        }
      }
    }

    // Mesh path: draw translated fill as drop shadow (no SDF blur; matches rect shadow intent via offset).
    if (!shadow.isNone()) {
      Color fillProbe{};
      if (!fs.isNone() && representativeFillColor(fs, &fillProbe)) {
        (void)fillProbe;
        save();
        translate(shadow.offset.x, shadow.offset.y);
        std::size_t const nShadow = recorder.pathOps.size();
        metalPathRasterizeToMesh(path, FillStyle::solid(shadow.color), StrokeStyle::none(), currentState().transform,
                                 dpiScaleX_, dpiScaleY_, effectiveOpacity(), frameDrawableW_, frameDrawableH_,
                                 recorder.pathVerts, recorder.pathOps, recorder.opOrder,
                                 currentState().blendMode);
        if (recorder.pathOps.size() > nShadow) {
          tagOpWithClip(recorder.pathOps.back(), clipScissorValid_, clipScissor_, clipRoundedStack_);
        }
        restore();
      }
    }

    std::size_t const nOpsBefore = recorder.pathOps.size();
    metalPathRasterizeToMesh(path, fs, ss, currentState().transform, dpiScaleX_, dpiScaleY_, effectiveOpacity(),
                             frameDrawableW_, frameDrawableH_, recorder.pathVerts, recorder.pathOps,
                             recorder.opOrder,
                             currentState().blendMode);
    if (recorder.pathOps.size() > nOpsBefore) {
      tagOpWithClip(recorder.pathOps.back(), clipScissorValid_, clipScissor_, clipRoundedStack_);
    }
  }

  void drawCircle(Point center, float radius, FillStyle const& fs, StrokeStyle const& ss) override {
    Rect r{center.x - radius, center.y - radius, radius * 2.f, radius * 2.f};
    drawRect(r, CornerRadius::pill(r), fs, ss, ShadowStyle::none());
  }

  void drawImage(Image const& image, Rect const& src, Rect const& dst, CornerRadius const& corners,
                 float opacity) override {
    if (!inFrame_) {
      return;
    }
    MetalImage const* mh = tryMetalImage(image);
    if (!mh || !mh->texture()) {
      return;
    }
    Size const sz = image.size();
    if (sz.width <= 0.f || sz.height <= 0.f) {
      return;
    }
    if (src.width <= 0.f || src.height <= 0.f || dst.width <= 0.f || dst.height <= 0.f) {
      return;
    }
    if (quickReject(dst)) {
      return;
    }
    Mat3 const& M = currentState().transform;
    if (currentState().clip.has_value()) {
      Rect const inter = intersectRects(dst, *currentState().clip);
      if (inter.width <= 0.f || inter.height <= 0.f) {
        return;
      }
    }

    float rotationRad = 0.f;
    float transformScale = 1.f;
    Rect mapped{};
    if (M.isTranslationOnly()) {
      mapped = Rect::sharp(dst.x + M.m[6], dst.y + M.m[7], dst.width, dst.height);
    } else if (tryDecomposeSimilarity(M, &rotationRad, &transformScale)) {
      Point const center = M.apply(dst.center());
      float const mappedWidth = dst.width * transformScale;
      float const mappedHeight = dst.height * transformScale;
      mapped = Rect::sharp(center.x - mappedWidth * 0.5f, center.y - mappedHeight * 0.5f,
                           mappedWidth, mappedHeight);
    } else {
      mapped = boundsOfTransformedRect(dst, M);
      transformScale = averageLinearScale(M);
    }

    const float s = dpiScale_ * transformScale;
    CornerRadius cr{};
    cr.topLeft = corners.topLeft * s;
    cr.topRight = corners.topRight * s;
    cr.bottomRight = corners.bottomRight * s;
    cr.bottomLeft = corners.bottomLeft * s;
    Rect device = Rect::sharp(mapped.x * dpiScaleX_, mapped.y * dpiScaleY_, mapped.width * dpiScaleX_,
                              mapped.height * dpiScaleY_);

    const float op = effectiveOpacity() * opacity;
    float const iw = sz.width;
    float const ih = sz.height;
    float const u0 = src.x / iw;
    float const v0 = src.y / ih;
    float const u1 = (src.x + src.width) / iw;
    float const v1 = (src.y + src.height) / ih;

    emitImage(mh->texture(), device, cr, simd_make_float4(u0, v0, u1, v1), simd_make_float2(0.f, 0.f), 0.f, op,
              rotationRad, false);
  }

  void drawImageTiled(Image const& image, Rect const& dst, CornerRadius const& corners, float opacity) override {
    if (!inFrame_) {
      return;
    }
    MetalImage const* mh = tryMetalImage(image);
    if (!mh || !mh->texture()) {
      return;
    }
    Size const sz = image.size();
    if (sz.width <= 0.f || sz.height <= 0.f) {
      return;
    }
    if (dst.width <= 0.f || dst.height <= 0.f) {
      return;
    }
    if (quickReject(dst)) {
      return;
    }
    Mat3 const& M = currentState().transform;
    if (currentState().clip.has_value()) {
      Rect const inter = intersectRects(dst, *currentState().clip);
      if (inter.width <= 0.f || inter.height <= 0.f) {
        return;
      }
    }

    float rotationRad = 0.f;
    float transformScale = 1.f;
    Rect mapped{};
    if (M.isTranslationOnly()) {
      mapped = Rect::sharp(dst.x + M.m[6], dst.y + M.m[7], dst.width, dst.height);
    } else if (tryDecomposeSimilarity(M, &rotationRad, &transformScale)) {
      Point const center = M.apply(dst.center());
      float const mappedWidth = dst.width * transformScale;
      float const mappedHeight = dst.height * transformScale;
      mapped = Rect::sharp(center.x - mappedWidth * 0.5f, center.y - mappedHeight * 0.5f,
                           mappedWidth, mappedHeight);
    } else {
      mapped = boundsOfTransformedRect(dst, M);
      transformScale = averageLinearScale(M);
    }

    const float s = dpiScale_ * transformScale;
    CornerRadius cr{};
    cr.topLeft = corners.topLeft * s;
    cr.topRight = corners.topRight * s;
    cr.bottomRight = corners.bottomRight * s;
    cr.bottomLeft = corners.bottomLeft * s;
    Rect device = Rect::sharp(mapped.x * dpiScaleX_, mapped.y * dpiScaleY_, mapped.width * dpiScaleX_,
                              mapped.height * dpiScaleY_);

    const float op = effectiveOpacity() * opacity;
    vector_float2 const texInv = simd_make_float2(1.f / sz.width, 1.f / sz.height);
    emitImage(mh->texture(), device, cr, simd_make_float4(0.f, 0.f, 0.f, 0.f), texInv, 1.f, op, rotationRad, true);
  }

  void drawBackdropBlur(Rect const& rect, float radius, Color tint, CornerRadius const& corners) override {
    if (!inFrame_ || radius <= 0.f || rect.width <= 0.f || rect.height <= 0.f || quickReject(rect)) {
      return;
    }
    Mat3 const& M = currentState().transform;
    Rect mapped = M.isTranslationOnly()
                      ? Rect::sharp(rect.x + M.m[6], rect.y + M.m[7], rect.width, rect.height)
                      : boundsOfTransformedRect(rect, M);
    Rect device = Rect::sharp(mapped.x * dpiScaleX_, mapped.y * dpiScaleY_, mapped.width * dpiScaleX_,
                              mapped.height * dpiScaleY_);
    if (device.width <= 0.f || device.height <= 0.f) {
      return;
    }

    MetalBackdropBlurOp op{};
    op.rect = simd_make_float4(device.x, device.y, device.width, device.height);
    op.tint = toSimd4(tint);
    op.corners = cornersToSimd(deviceCornerRadii(corners, M, device));
    op.radius = radius * dpiScale_;
    tagOpWithClip(op, clipScissorValid_, clipScissor_, clipRoundedStack_);

    MetalFrameRecorder& recorder = activeRecorder();
    RecorderCapacitySnapshot const capacityBefore = snapshotRecorderCapacity(recorder);
    recorder.opOrder.push_back(MetalOpRef{
        .kind = MetalOpRef::BackdropBlur,
        .index = static_cast<std::uint32_t>(recorder.backdropBlurOps.size()),
    });
    recorder.backdropBlurOps.push_back(op);
    recordRecorderCapacityIncreases(capacityBefore, recorder);
  }

  void preflightTextGlyphs(TextLayout const& layout) {
    for (auto const& placed : layout.runs) {
      TextRun const& text = placed.run;
      if (text.glyphIds.empty()) {
        continue;
      }
      float const physicalFontSize = text.fontSize * dpiScaleX_;
      std::size_t const glyphCount = std::min(text.glyphIds.size(), text.positions.size());
      for (std::size_t i = 0; i < glyphCount; ++i) {
        GlyphKey key{};
        key.fontId = text.fontId;
        key.glyphId = text.glyphIds[i];
        unsigned const q = static_cast<unsigned>(physicalFontSize * 4.f);
        key.sizeQ8 = static_cast<std::uint16_t>(std::min(65535u, q));
        (void)glyphAtlas_->getOrUpload(key);
      }
    }
  }

  void drawTextLayout(TextLayout const& layout, Point origin) override {
    if (!inFrame_) {
      return;
    }
    preflightTextGlyphs(layout);
    MetalFrameRecorder& recorder = activeRecorder();

    Mat3 const& M = currentState().transform;
    BlendMode const blend = currentState().blendMode;
    float const op = effectiveOpacity();

    float const aw = static_cast<float>(glyphAtlas_->atlasPixelWidth());
    float const ah = static_cast<float>(glyphAtlas_->atlasPixelHeight());
    if (aw < 1.f || ah < 1.f) {
      return;
    }
    float const invAw = 1.f / aw;
    float const invAh = 1.f / ah;
    float const invDpiX = 1.f / dpiScaleX_;
    float const invDpiY = 1.f / dpiScaleY_;
    std::uint32_t const glyphStart = recorder.glyphVertexCount;
    std::uint32_t const ownedGlyphStart = static_cast<std::uint32_t>(recorder.glyphVerts.size());
    std::size_t const glyphVertsCapacity = recorder.glyphVerts.capacity();
    for (auto const& placed : layout.runs) {
      TextRun const& text = placed.run;
      float const baselineY = origin.y + placed.origin.y;
      float const x = origin.x + placed.origin.x;

      if (text.backgroundColor.has_value() && text.width > 0.f) {
        drawRect(Rect{x, baselineY - text.ascent, text.width, text.ascent + text.descent}, CornerRadius{}, FillStyle::solid(*text.backgroundColor), StrokeStyle::none(), ShadowStyle::none());
      }

      if (text.glyphIds.empty()) {
        continue;
      }

      float const effectiveAlpha = text.color.a * op;
      vector_float4 const premul = simd_make_float4(text.color.r * effectiveAlpha,
                                                    text.color.g * effectiveAlpha,
                                                    text.color.b * effectiveAlpha,
                                                    effectiveAlpha);

      float const physicalFontSize = text.fontSize * dpiScaleX_;

      std::size_t const glyphCount = std::min(text.glyphIds.size(), text.positions.size());
      for (std::size_t i = 0; i < glyphCount; ++i) {
        GlyphKey key{};
        key.fontId = text.fontId;
        key.glyphId = text.glyphIds[i];
        unsigned const q = static_cast<unsigned>(physicalFontSize * 4.f);
        key.sizeQ8 = static_cast<std::uint16_t>(std::min(65535u, q));

        AtlasEntry const& entry = glyphAtlas_->getOrUpload(key);
        if (entry.width == 0 || entry.height == 0) {
          continue;
        }

        float const u0 = static_cast<float>(entry.u) * invAw;
        float const u1 = static_cast<float>(entry.u + entry.width) * invAw;
        float const vLo = static_cast<float>(entry.v) * invAh;
        float const vHi = static_cast<float>(entry.v + entry.height) * invAh;

        Point const ink = {x + text.positions[i].x, baselineY + text.positions[i].y};
        Point const tl = {ink.x - entry.bearing.x * invDpiX, ink.y - entry.bearing.y * invDpiY};
        float const gw = static_cast<float>(entry.width) * invDpiX;
        float const gh = static_cast<float>(entry.height) * invDpiY;

        appendGlyphQuad(recorder.glyphVerts, M, dpiScaleX_, dpiScaleY_, tl, gw, gh, u0, vLo, u1, vHi, premul);
      }
    }
    recordVectorCapacityIncrease(glyphVertsCapacity, recorder.glyphVerts);

    std::uint32_t const vertCount = static_cast<std::uint32_t>(recorder.glyphVerts.size()) - ownedGlyphStart;
    if (vertCount > 0) {
      recorder.glyphAtlasGeneration = glyphAtlas_->generation();
      appendOwnedGlyphSource(recorder, ownedGlyphStart, vertCount);
      MetalGlyphOp op{};
      op.glyphStart = glyphStart;
      op.glyphVertexCount = vertCount;
      op.blendMode = blend;
      pushGlyphOp(std::move(op));
    }
  }

  void* gpuDevice() const override { return (__bridge void*)metal_.device(); }

private:
  struct RoundedClipState {
    Rect rect{};
    CornerRadius corners{};
  };

  struct GpuState {
    Mat3 transform = Mat3::identity();
    float opacity = 1.f;
    BlendMode blendMode = BlendMode::Normal;
    std::optional<Rect> clip;
    std::array<RoundedClipState, kMetalRoundedClipMaskCapacity> roundedClips{};
    std::uint32_t roundedClipCount = 0;
  };

  TextSystem& textSystem_;
  std::unique_ptr<GlyphAtlas> glyphAtlas_;
  MetalDeviceResources metal_;
  unsigned int windowHandle_{0};
  std::function<void()> requestRedraw_;
  MetalRenderTargetSpec targetSpec_{};
  bool targetMode_{false};

  dispatch_semaphore_t frameSem_{nullptr};
  id<MTLCommandBuffer> cmdBuf_{nil};
  id<MTLCommandBuffer> lastSubmittedCmdBuf_{nil};
  id<CAMetalDrawable> drawable_{nil};
  id<MTLTexture> liveFrameMsaaTexture_{nil};
  id<MTLTexture> backdropSceneTexture_{nil};
  NSUInteger backdropSceneTextureW_{0};
  NSUInteger backdropSceneTextureH_{0};
  id<MTLTexture> backdropScratchTexture_{nil};
  NSUInteger backdropScratchTextureW_{0};
  NSUInteger backdropScratchTextureH_{0};
  id<MTLTexture> backdropBlurTexture_{nil};
  NSUInteger backdropBlurTextureW_{0};
  NSUInteger backdropBlurTextureH_{0};
  std::array<id<MTLBuffer>, kFramesInFlight> backdropUniformBuffers_{};
  std::array<NSUInteger, kFramesInFlight> backdropUniformBufferCapacities_{};
  std::array<id<MTLBuffer>, kFramesInFlight> backdropClipBuffers_{};
  std::array<NSUInteger, kFramesInFlight> backdropClipBufferCapacities_{};
  id<MTLBuffer> captureBuffer_{nil};
  NSUInteger captureBytesPerRow_{0};
  std::uint32_t captureWidth_{0};
  std::uint32_t captureHeight_{0};
  bool captureNextFrame_{false};
  bool inFrame_{false};
  bool syncPresent_{false};
  std::uint32_t frameSampleCount_{1};
  id<MTLSharedEvent> renderTargetCompletionEvent_{nil};
  std::uint64_t renderTargetCompletionValue_{0};

  Color clearColor_{0.f, 0.f, 0.f, 1.f};
  int logicalW_{0};
  int logicalH_{0};
  float dpiScaleX_{1.f};
  float dpiScaleY_{1.f};
  float dpiScale_{1.f};
  CGSize frameDrawableSize_{};
  float frameDrawableW_{0.f};
  float frameDrawableH_{0.f};
  NSUInteger frameDrawablePixelsW_{0};
  NSUInteger frameDrawablePixelsH_{0};

  MetalFrameRecorder frame_;
  MetalFrameRecorder* captureRecorder_{nullptr};
  std::vector<GpuState> stateStack_;

  MTLScissorRect clipScissor_{};
  bool clipScissorValid_{false};
  MetalRoundedClipStack clipRoundedStack_{};

  GpuState& currentState() { return stateStack_.back(); }
  GpuState const& currentState() const { return stateStack_.back(); }
  MetalFrameRecorder& activeRecorder() { return captureRecorder_ ? *captureRecorder_ : frame_; }
  MetalFrameRecorder const& activeRecorder() const { return captureRecorder_ ? *captureRecorder_ : frame_; }

  static id<MTLTexture> textureFromSpec(MetalRenderTargetSpec const& spec) {
    return (__bridge id<MTLTexture>)spec.texture;
  }

  static id<MTLDevice> targetDevice(MetalRenderTargetSpec const& spec) {
    id<MTLTexture> texture = textureFromSpec(spec);
    return texture && texture.device ? texture.device : MTLCreateSystemDefaultDevice();
  }

  static MTLPixelFormat targetPixelFormat(MetalRenderTargetSpec const& spec) {
    if (spec.format != 0) {
      return static_cast<MTLPixelFormat>(spec.format);
    }
    id<MTLTexture> texture = textureFromSpec(spec);
    return texture ? texture.pixelFormat : MTLPixelFormatBGRA8Unorm;
  }

  id<MTLTexture> targetTexture() const {
    return (__bridge id<MTLTexture>)targetSpec_.texture;
  }

  id<MTLCommandBuffer> targetCommandBuffer() const {
    return (__bridge id<MTLCommandBuffer>)targetSpec_.commandBuffer;
  }

  id<MTLSharedEvent> targetSharedEvent() const {
    return (__bridge id<MTLSharedEvent>)targetSpec_.sharedEvent;
  }

  bool acquireDrawableForFrame() {
    auto const startedAt =
        debug::perf::enabled() ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    drawable_ = [metal_.layer() nextDrawable];
    if (debug::perf::enabled()) {
      auto const elapsed = std::chrono::steady_clock::now() - startedAt;
      debug::perf::recordDuration(debug::perf::TimedMetric::CanvasDrawableWait,
                                  std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed));
    }
    return drawable_ != nil;
  }

  id<MTLSharedEvent> internalRenderTargetCompletionEvent() {
    if (@available(macOS 10.14, *)) {
      if (!renderTargetCompletionEvent_) {
        renderTargetCompletionEvent_ = [metal_.device() newSharedEvent];
      }
      return renderTargetCompletionEvent_;
    }
    return nil;
  }

  bool waitForSharedEvent(id<MTLSharedEvent> event, std::uint64_t value) {
    if (!event) {
      return false;
    }
    if (@available(macOS 10.14, *)) {
      return [event waitUntilSignaledValue:value timeoutMS:UINT64_MAX] == YES;
    }
    return false;
  }

  id<MTLTexture> ensureLiveFrameMsaaTexture(id<MTLTexture> resolveTexture) {
    if (frameSampleCount_ <= 1 || !resolveTexture) {
      return nil;
    }
    if (liveFrameMsaaTexture_ && liveFrameMsaaTexture_.width == resolveTexture.width &&
        liveFrameMsaaTexture_.height == resolveTexture.height &&
        liveFrameMsaaTexture_.pixelFormat == resolveTexture.pixelFormat &&
        liveFrameMsaaTexture_.sampleCount == frameSampleCount_) {
      return liveFrameMsaaTexture_;
    }

    MTLTextureDescriptor* desc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:resolveTexture.pixelFormat
                                                           width:resolveTexture.width
                                                          height:resolveTexture.height
                                                       mipmapped:NO];
    desc.textureType = MTLTextureType2DMultisample;
    desc.sampleCount = frameSampleCount_;
    desc.storageMode = MTLStorageModePrivate;
    desc.usage = MTLTextureUsageRenderTarget;
    liveFrameMsaaTexture_ = [metal_.device() newTextureWithDescriptor:desc];
    return liveFrameMsaaTexture_;
  }

  bool recordedGlyphAtlasCurrent(MetalFrameRecorder const& recorded,
                                 MetalRecorderSlice const& slice) const {
    if (slice.glyphVertexCount == 0 && slice.glyphOpCount == 0) {
      return true;
    }
    return recorded.glyphAtlasGeneration == glyphAtlas_->generation();
  }

  void pushState() { stateStack_.push_back(stateStack_.empty() ? GpuState{} : stateStack_.back()); }

  void popState() {
    stateStack_.pop_back();
    updateClipScissor();
  }

  float effectiveOpacity() const { return currentState().opacity; }

  void refreshFrameDrawableMetrics() {
    CGSize const ds = metal_.layer().drawableSize;
    frameDrawableSize_ = ds;
    frameDrawableW_ = static_cast<float>(ds.width);
    frameDrawableH_ = static_cast<float>(ds.height);
    frameDrawablePixelsW_ = static_cast<NSUInteger>(ds.width);
    frameDrawablePixelsH_ = static_cast<NSUInteger>(ds.height);
  }

  void refreshTargetFrameMetrics() {
    id<MTLTexture> texture = targetTexture();
    NSUInteger const pixelW = targetSpec_.width > 0 ? static_cast<NSUInteger>(targetSpec_.width)
                                                    : (texture ? texture.width : 0);
    NSUInteger const pixelH = targetSpec_.height > 0 ? static_cast<NSUInteger>(targetSpec_.height)
                                                     : (texture ? texture.height : 0);
    frameDrawableSize_ = CGSizeMake(static_cast<CGFloat>(pixelW), static_cast<CGFloat>(pixelH));
    frameDrawableW_ = static_cast<float>(pixelW);
    frameDrawableH_ = static_cast<float>(pixelH);
    frameDrawablePixelsW_ = pixelW;
    frameDrawablePixelsH_ = pixelH;
    if (logicalW_ <= 0) {
      logicalW_ = static_cast<int>(std::ceil(static_cast<float>(pixelW) / std::max(dpiScaleX_, 0.01f)));
    }
    if (logicalH_ <= 0) {
      logicalH_ = static_cast<int>(std::ceil(static_cast<float>(pixelH) / std::max(dpiScaleY_, 0.01f)));
    }
  }

  Rect viewportLogicalRect() const {
    if (logicalW_ > 0 && logicalH_ > 0) {
      return Rect::sharp(0, 0, static_cast<float>(logicalW_), static_cast<float>(logicalH_));
    }
    CGSize ds = inFrame_ ? frameDrawableSize_ : metal_.layer().drawableSize;
    return Rect::sharp(0, 0, static_cast<float>(ds.width) / dpiScaleX_, static_cast<float>(ds.height) / dpiScaleY_);
  }

  CornerRadius deviceCornerRadii(CornerRadius corners, Mat3 const& transform, Rect const& deviceRect) const {
    float const sx = std::hypot(transform.m[0], transform.m[1]);
    float const sy = std::hypot(transform.m[3], transform.m[4]);
    float scale = 1.f;
    constexpr float eps = 1e-4f;
    if (sx > eps && sy > eps && std::abs(sx - sy) <= 0.01f) {
      scale = 0.5f * (sx + sy);
    }
    corners.topLeft *= dpiScale_ * scale;
    corners.topRight *= dpiScale_ * scale;
    corners.bottomRight *= dpiScale_ * scale;
    corners.bottomLeft *= dpiScale_ * scale;
    clampRoundRectCornerRadii(deviceRect.width, deviceRect.height, corners);
    return corners;
  }

  void updateRoundedClipStack() {
    clipRoundedStack_ = {};
    GpuState const& st = currentState();
    if (st.roundedClipCount == 0) {
      return;
    }
    Mat3 const& M = st.transform;
    for (std::uint32_t i = 0; i < st.roundedClipCount && i < kMetalRoundedClipMaskCapacity; ++i) {
      RoundedClipState const& mask = st.roundedClips[i];
      Rect const world = boundsOfTransformedRect(mask.rect, M);
      Rect const device = Rect::sharp(world.x * dpiScaleX_, world.y * dpiScaleY_, world.width * dpiScaleX_,
                                      world.height * dpiScaleY_);
      if (device.width <= 0.f || device.height <= 0.f) {
        continue;
      }
      CornerRadius const deviceCorners = deviceCornerRadii(mask.corners, M, device);
      std::uint32_t const slot = static_cast<std::uint32_t>(clipRoundedStack_.header.x);
      clipRoundedStack_.entries[slot * 2] = simd_make_float4(device.x, device.y, device.width, device.height);
      clipRoundedStack_.entries[slot * 2 + 1] = cornersToSimd(deviceCorners);
      clipRoundedStack_.header.x = static_cast<float>(slot + 1);
    }
  }

  void updateClipScissor() {
    updateRoundedClipStack();
    if (!currentState().clip.has_value()) {
      clipScissorValid_ = false;
      return;
    }
    Mat3 const& M = currentState().transform;
    Rect const world = boundsOfTransformedRect(*currentState().clip, M);
    float const minX = world.x * dpiScaleX_;
    float const minY = world.y * dpiScaleY_;
    float const maxX = (world.x + world.width) * dpiScaleX_;
    float const maxY = (world.y + world.height) * dpiScaleY_;
    NSUInteger const dw = inFrame_ ? frameDrawablePixelsW_ : static_cast<NSUInteger>(metal_.layer().drawableSize.width);
    NSUInteger const dh = inFrame_ ? frameDrawablePixelsH_ : static_cast<NSUInteger>(metal_.layer().drawableSize.height);
    float const clampedMinX = std::clamp(minX, 0.f, static_cast<float>(dw));
    float const clampedMinY = std::clamp(minY, 0.f, static_cast<float>(dh));
    float const clampedMaxX = std::clamp(maxX, 0.f, static_cast<float>(dw));
    float const clampedMaxY = std::clamp(maxY, 0.f, static_cast<float>(dh));

    // Expand to the covered pixel envelope instead of truncating. This avoids
    // thin or partially clipped primitives blinking out when their visible
    // extent is subpixel but still non-zero in logical space.
    NSUInteger const x0 = static_cast<NSUInteger>(std::floor(clampedMinX));
    NSUInteger const y0 = static_cast<NSUInteger>(std::floor(clampedMinY));
    NSUInteger const x1 = static_cast<NSUInteger>(std::ceil(clampedMaxX));
    NSUInteger const y1 = static_cast<NSUInteger>(std::ceil(clampedMaxY));
    clipScissor_.x = std::min(x0, dw);
    clipScissor_.y = std::min(y0, dh);
    clipScissor_.width = x1 > clipScissor_.x ? std::min(x1 - clipScissor_.x, dw - clipScissor_.x) : 0;
    clipScissor_.height = y1 > clipScissor_.y ? std::min(y1 - clipScissor_.y, dh - clipScissor_.y) : 0;
    clipScissorValid_ = clipScissor_.width > 0 && clipScissor_.height > 0;
  }

  void pushRectOp(MetalRectOp&& op) {
    MetalFrameRecorder& recorder = activeRecorder();
    RecorderCapacitySnapshot const capacityBefore = snapshotRecorderCapacity(recorder);
    tagOpWithClip(op, clipScissorValid_, clipScissor_, clipRoundedStack_);
    recorder.opOrder.push_back(MetalOpRef{
        .kind = MetalOpRef::Rect,
        .index = static_cast<std::uint32_t>(recorder.rectOps.size()),
    });
    recorder.rectOps.push_back(std::move(op));
    recordRecorderCapacityIncreases(capacityBefore, recorder);
  }

  void pushImageOp(MetalImageOp&& op) {
    MetalFrameRecorder& recorder = activeRecorder();
    RecorderCapacitySnapshot const capacityBefore = snapshotRecorderCapacity(recorder);
    tagOpWithClip(op, clipScissorValid_, clipScissor_, clipRoundedStack_);
    recorder.opOrder.push_back(MetalOpRef{
        .kind = MetalOpRef::Image,
        .index = static_cast<std::uint32_t>(recorder.imageOps.size()),
    });
    recorder.imageOps.push_back(std::move(op));
    recordRecorderCapacityIncreases(capacityBefore, recorder);
  }

  void pushGlyphOp(MetalGlyphOp&& op) {
    MetalFrameRecorder& recorder = activeRecorder();
    RecorderCapacitySnapshot const capacityBefore = snapshotRecorderCapacity(recorder);
    tagOpWithClip(op, clipScissorValid_, clipScissor_, clipRoundedStack_);
    recorder.opOrder.push_back(MetalOpRef{
        .kind = MetalOpRef::Glyph,
        .index = static_cast<std::uint32_t>(recorder.glyphOps.size()),
    });
    recorder.glyphOps.push_back(std::move(op));
    recordRecorderCapacityIncreases(capacityBefore, recorder);
  }

  void emitRect(Rect const& deviceRect, CornerRadius const& corners, EncodedFillStyle const& fill,
                Color const& strokeColor, float strokeWidth, float opacity, float rotationRad,
                Color const& shadowColor, float shadowOffsetX, float shadowOffsetY, float shadowRadius) {
    MetalRectOp op{};
    op.inst.rect = simd_make_float4(deviceRect.x, deviceRect.y, deviceRect.width, deviceRect.height);
    op.inst.corners = cornersToSimd(corners);
    op.inst.fillColor = toSimd4(fill.fillColor);
    op.inst.strokeColor = toSimd4(strokeColor);
    op.inst.strokeWidthOpacity = simd_make_float2(strokeWidth, opacity);
    op.inst.viewport = simd_make_float2(frameDrawableW_, frameDrawableH_);
    op.inst.rotationPad = simd_make_float4(rotationRad, fill.gradientStopCount, fill.gradientType, 0.f);
    op.inst.shadowColor = toSimd4(shadowColor);
    op.inst.shadowGeom = simd_make_float4(shadowOffsetX, shadowOffsetY, shadowRadius, 0.f);
    op.inst.gradientColor1 = toSimd4(fill.gradientColor1);
    op.inst.gradientColor2 = toSimd4(fill.gradientColor2);
    op.inst.gradientColor3 = toSimd4(fill.gradientColor3);
    op.inst.gradientStops = fill.gradientStops;
    op.inst.gradientPoints = fill.gradientPoints;
    op.blendMode = currentState().blendMode;
    pushRectOp(std::move(op));
  }

  void emitImage(id<MTLTexture> tex, Rect const& deviceRect, CornerRadius const& corners, vector_float4 const& uvBounds,
                 vector_float2 const& texSizeInv, float imageMode, float opacity, float rotationRad, bool repeat) {
    MetalImageOp op{};
    op.inst.sdf.rect = simd_make_float4(deviceRect.x, deviceRect.y, deviceRect.width, deviceRect.height);
    op.inst.sdf.corners = cornersToSimd(corners);
    op.inst.sdf.fillColor = simd_make_float4(1.f, 1.f, 1.f, 1.f);
    op.inst.sdf.strokeColor = simd_make_float4(0.f, 0.f, 0.f, 0.f);
    op.inst.sdf.strokeWidthOpacity = simd_make_float2(0.f, opacity);
    op.inst.sdf.viewport = simd_make_float2(frameDrawableW_, frameDrawableH_);
    op.inst.sdf.rotationPad = simd_make_float4(rotationRad, 0.f, 0.f, 0.f);
    op.inst.sdf.shadowColor = simd_make_float4(0.f, 0.f, 0.f, 0.f);
    op.inst.sdf.shadowGeom = simd_make_float4(0.f, 0.f, 0.f, 0.f);
    op.inst.uvBounds = uvBounds;
    op.inst.texSizeInv = texSizeInv;
    op.inst.imageModePad = simd_make_float2(imageMode, 0.f);
    op.blendMode = currentState().blendMode;
    op.texture = (__bridge_retained void*)tex;
    op.repeatSampler = repeat;
    pushImageOp(std::move(op));
  }

  static bool hasBackdropBlurOps(MetalFrameRecorder const& recorder) {
    return std::any_of(recorder.opOrder.begin(), recorder.opOrder.end(), [](MetalOpRef const& ref) {
      return ref.kind == MetalOpRef::BackdropBlur;
    });
  }

  static std::size_t nextBackdropBlurOp(MetalFrameRecorder const& recorder, std::size_t start = 0) {
    std::size_t const begin = std::min(start, recorder.opOrder.size());
    auto const it = std::find_if(recorder.opOrder.begin() + static_cast<std::ptrdiff_t>(begin),
                                 recorder.opOrder.end(),
                                 [](MetalOpRef const& ref) {
      return ref.kind == MetalOpRef::BackdropBlur;
    });
    return it == recorder.opOrder.end()
               ? recorder.opOrder.size()
               : static_cast<std::size_t>(std::distance(recorder.opOrder.begin(), it));
  }

  static std::size_t backdropBlurRunEnd(MetalFrameRecorder const& recorder, std::size_t start) {
    std::size_t end = start;
    while (end < recorder.opOrder.size() && recorder.opOrder[end].kind == MetalOpRef::BackdropBlur) {
      ++end;
    }
    return end;
  }

  static float maxBackdropBlurRadius(MetalFrameRecorder const& recorder,
                                     std::size_t start,
                                     std::size_t end) {
    float radius = 0.f;
    std::size_t const opEnd = std::min(end, recorder.opOrder.size());
    for (std::size_t index = std::min(start, opEnd); index < opEnd; ++index) {
      MetalOpRef const ref = recorder.opOrder[index];
      if (ref.kind != MetalOpRef::BackdropBlur || ref.index >= recorder.backdropBlurOps.size()) continue;
      radius = std::max(radius, recorder.backdropBlurOps[ref.index].radius);
    }
    return radius;
  }

  id<MTLTexture> createBackdropTexture(NSUInteger width, NSUInteger height) {
    if (width == 0 || height == 0) {
      return nil;
    }
    MTLTextureDescriptor* desc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:metal_.pixelFormat()
                                                           width:width
                                                          height:height
                                                       mipmapped:NO];
    desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    desc.storageMode = MTLStorageModePrivate;
    return [metal_.device() newTextureWithDescriptor:desc];
  }

  id<MTLTexture> ensureBackdropSceneTexture(NSUInteger width, NSUInteger height) {
    if (backdropSceneTexture_ && backdropSceneTextureW_ == width && backdropSceneTextureH_ == height) {
      return backdropSceneTexture_;
    }
    backdropSceneTexture_ = createBackdropTexture(width, height);
    backdropSceneTextureW_ = backdropSceneTexture_ ? width : 0;
    backdropSceneTextureH_ = backdropSceneTexture_ ? height : 0;
    return backdropSceneTexture_;
  }

  id<MTLTexture> ensureBackdropScratchTexture(NSUInteger width, NSUInteger height) {
    if (backdropScratchTexture_ && backdropScratchTextureW_ == width && backdropScratchTextureH_ == height) {
      return backdropScratchTexture_;
    }
    backdropScratchTexture_ = createBackdropTexture(width, height);
    backdropScratchTextureW_ = backdropScratchTexture_ ? width : 0;
    backdropScratchTextureH_ = backdropScratchTexture_ ? height : 0;
    return backdropScratchTexture_;
  }

  id<MTLTexture> ensureBackdropBlurTexture(NSUInteger width, NSUInteger height) {
    if (backdropBlurTexture_ && backdropBlurTextureW_ == width && backdropBlurTextureH_ == height) {
      return backdropBlurTexture_;
    }
    backdropBlurTexture_ = createBackdropTexture(width, height);
    backdropBlurTextureW_ = backdropBlurTexture_ ? width : 0;
    backdropBlurTextureH_ = backdropBlurTexture_ ? height : 0;
    return backdropBlurTexture_;
  }

  bool ensureBackdropDrawStateBuffers(std::size_t stateCount,
                                      id<MTLBuffer>* uniformBuffer,
                                      id<MTLBuffer>* clipBuffer) {
    NSUInteger const needed = static_cast<NSUInteger>(std::max<std::size_t>(1, stateCount));
    std::size_t const frameIndex = metal_.currentFrameIndex();
    NSUInteger& frameUniformCapacity = backdropUniformBufferCapacities_[frameIndex];
    NSUInteger& frameClipCapacity = backdropClipBufferCapacities_[frameIndex];
    if (needed > frameUniformCapacity) {
      NSUInteger const newCapacity =
          frameUniformCapacity == 0 ? needed : std::max(needed, frameUniformCapacity * 2);
      backdropUniformBuffers_[frameIndex] =
          [metal_.device() newBufferWithLength:newCapacity * sizeof(MetalDrawUniforms)
                                       options:MTLResourceStorageModeShared];
      frameUniformCapacity = backdropUniformBuffers_[frameIndex] ? newCapacity : 0;
    }
    if (needed > frameClipCapacity) {
      NSUInteger const newCapacity =
          frameClipCapacity == 0 ? needed : std::max(needed, frameClipCapacity * 2);
      backdropClipBuffers_[frameIndex] =
          [metal_.device() newBufferWithLength:newCapacity * sizeof(MetalRoundedClipStack)
                                       options:MTLResourceStorageModeShared];
      frameClipCapacity = backdropClipBuffers_[frameIndex] ? newCapacity : 0;
    }
    if (uniformBuffer) {
      *uniformBuffer = backdropUniformBuffers_[frameIndex];
    }
    if (clipBuffer) {
      *clipBuffer = backdropClipBuffers_[frameIndex];
    }
    return backdropUniformBuffers_[frameIndex] != nil && backdropClipBuffers_[frameIndex] != nil;
  }

  static NSUInteger backdropBlurDownsample(NSUInteger width, NSUInteger height, float radius) {
    if (width < 2 || height < 2 || radius < 2.f) {
      return 1;
    }
    return 2;
  }

  void encodeBackdropQuad(id<MTLRenderCommandEncoder> enc, MetalBackdropBlurOp const& op,
                          id<MTLTexture> sceneTexture, float viewportW, float viewportH,
                          std::uint32_t renderSampleCount) {
    if (!sceneTexture || op.rect.z <= 0.f || op.rect.w <= 0.f) {
      return;
    }
    MetalBackdropUniforms uniforms{};
    uniforms.rect = op.rect;
    uniforms.tint = op.tint;
    uniforms.corners = op.corners;
    uniforms.params = simd_make_float4(viewportW, viewportH,
                                       1.f / std::max<float>(1.f, viewportW),
                                       1.f / std::max<float>(1.f, viewportH));
    uniforms.blurParams = simd_make_float4(std::max(0.f, op.radius), 0.f, 0.f, 0.f);

    [enc setRenderPipelineState:metal_.backdropPSO(renderSampleCount)];
    [enc setVertexBuffer:metal_.quadBuffer() offset:0 atIndex:0];
    [enc setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];
    [enc setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:0];
    [enc setFragmentTexture:sceneTexture atIndex:0];
    [enc setFragmentSamplerState:metal_.linearSampler() atIndex:0];
    [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:kQuadStripCount];
  }

  bool encodeBackdropBlurPass(id<MTLTexture> source, id<MTLTexture> target, float radius,
                              vector_float2 axis) {
    if (!source || !target || [target width] == 0 || [target height] == 0) {
      return false;
    }
    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = target;
    pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;

    id<MTLRenderCommandEncoder> enc = [cmdBuf_ renderCommandEncoderWithDescriptor:pass];
    if (!enc) {
      return false;
    }
    float const viewportW = static_cast<float>([target width]);
    float const viewportH = static_cast<float>([target height]);
    MTLViewport vp = {0, 0, viewportW, viewportH, 0.0, 1.0};
    [enc setViewport:vp];

    MetalBackdropUniforms uniforms{};
    uniforms.rect = simd_make_float4(0.f, 0.f, viewportW, viewportH);
    uniforms.params = simd_make_float4(viewportW, viewportH,
                                       1.f / std::max<float>(1.f, static_cast<float>([source width])),
                                       1.f / std::max<float>(1.f, static_cast<float>([source height])));
    uniforms.blurParams = simd_make_float4(std::max(0.f, radius), axis.x, axis.y, 0.f);

    [enc setRenderPipelineState:metal_.backdropBlurPSO()];
    [enc setVertexBuffer:metal_.quadBuffer() offset:0 atIndex:0];
    [enc setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];
    [enc setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:0];
    [enc setFragmentTexture:source atIndex:0];
    [enc setFragmentSamplerState:metal_.linearSampler() atIndex:0];
    [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:kQuadStripCount];
    [enc endEncoding];
    debug::perf::recordDrawCall(debug::perf::RenderCounterKind::Image);
    return true;
  }

  bool encodeBackdropBlur(id<MTLTexture> sceneTexture, id<MTLTexture> scratchTexture,
                          id<MTLTexture> blurredTexture, float radius) {
    constexpr int kIterations = 3;
    float const passRadius = radius / std::sqrt(static_cast<float>(kIterations));
    id<MTLTexture> source = sceneTexture;
    for (int i = 0; i < kIterations; ++i) {
      if (!encodeBackdropBlurPass(source, scratchTexture, passRadius, simd_make_float2(1.f, 0.f)) ||
          !encodeBackdropBlurPass(scratchTexture, blurredTexture, passRadius, simd_make_float2(0.f, 1.f))) {
        return false;
      }
      source = blurredTexture;
    }
    return true;
  }

  bool encodeBackdropDownsample(id<MTLTexture> source, id<MTLTexture> target) {
    if (!source || !target || [target width] == 0 || [target height] == 0) {
      return false;
    }
    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = target;
    pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;

    id<MTLRenderCommandEncoder> enc = [cmdBuf_ renderCommandEncoderWithDescriptor:pass];
    if (!enc) {
      return false;
    }
    float const viewportW = static_cast<float>([target width]);
    float const viewportH = static_cast<float>([target height]);
    MTLViewport vp = {0, 0, viewportW, viewportH, 0.0, 1.0};
    [enc setViewport:vp];

    MetalBackdropUniforms uniforms{};
    uniforms.rect = simd_make_float4(0.f, 0.f, viewportW, viewportH);
    uniforms.params = simd_make_float4(viewportW, viewportH,
                                       1.f / std::max<float>(1.f, viewportW),
                                       1.f / std::max<float>(1.f, viewportH));
    uniforms.blurParams = simd_make_float4(0.f, 1.f, 0.f, 0.f);

    [enc setRenderPipelineState:metal_.backdropBlurPSO()];
    [enc setVertexBuffer:metal_.quadBuffer() offset:0 atIndex:0];
    [enc setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];
    [enc setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:0];
    [enc setFragmentTexture:source atIndex:0];
    [enc setFragmentSamplerState:metal_.linearSampler() atIndex:0];
    [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:kQuadStripCount];
    [enc endEncoding];
    debug::perf::recordDrawCall(debug::perf::RenderCounterKind::Image);
    return true;
  }

  bool encodeFrameWithBackdropBlur(id<MTLTexture> renderTargetTexture, id<MTLTexture> resolveTexture,
                                   std::uint32_t renderSampleCount) {
    (void)resolveTexture;
    if (renderSampleCount != 1) {
      return false;
    }
    float const maxBlurRadius = maxBackdropBlurRadius(frame_, 0, frame_.opOrder.size());
    NSUInteger const downsample =
        backdropBlurDownsample(frameDrawablePixelsW_, frameDrawablePixelsH_, maxBlurRadius);
    NSUInteger const blurWidth = std::max<NSUInteger>(1, (frameDrawablePixelsW_ + downsample - 1) / downsample);
    NSUInteger const blurHeight = std::max<NSUInteger>(1, (frameDrawablePixelsH_ + downsample - 1) / downsample);
    id<MTLTexture> sceneTexture = ensureBackdropSceneTexture(blurWidth, blurHeight);
    id<MTLTexture> scratchTexture = ensureBackdropScratchTexture(blurWidth, blurHeight);
    id<MTLTexture> blurredTexture = ensureBackdropBlurTexture(blurWidth, blurHeight);
    if (!sceneTexture || !scratchTexture || !blurredTexture || !renderTargetTexture) {
      return false;
    }

    id<MTLBuffer> finalUniformBuffer = nil;
    id<MTLBuffer> finalClipBuffer = nil;
    if (!ensureBackdropDrawStateBuffers(frame_.opOrder.size(), &finalUniformBuffer, &finalClipBuffer)) {
      return false;
    }

    auto makeFinalPass = [&](MTLLoadAction loadAction) {
      MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
      pass.colorAttachments[0].texture = renderTargetTexture;
      pass.colorAttachments[0].loadAction = loadAction;
      pass.colorAttachments[0].clearColor =
          MTLClearColorMake(clearColor_.r, clearColor_.g, clearColor_.b, clearColor_.a);
      pass.colorAttachments[0].storeAction = MTLStoreActionStore;
      return pass;
    };

    id<MTLRenderCommandEncoder> finalEnc = [cmdBuf_ renderCommandEncoderWithDescriptor:makeFinalPass(MTLLoadActionClear)];
    if (!finalEnc) return false;
    MTLViewport finalVp = {0, 0, frameDrawableSize_.width, frameDrawableSize_.height, 0.0, 1.0};
    [finalEnc setViewport:finalVp];

    MetalRecorderEncodeState finalEncodeState{};
    std::size_t cursor = 0;
    while (cursor < frame_.opOrder.size()) {
      std::size_t const blurStart = nextBackdropBlurOp(frame_, cursor);
      if (blurStart > cursor) {
        encodeRecorderOps(frame_, finalEnc, frameDrawableW_, frameDrawableH_, frameDrawablePixelsW_,
                          frameDrawablePixelsH_, finalUniformBuffer, finalClipBuffer, 1,
                          nil, cursor, blurStart, &finalEncodeState);
      }
      if (blurStart >= frame_.opOrder.size()) {
        cursor = frame_.opOrder.size();
        break;
      }

      std::size_t const blurEnd = backdropBlurRunEnd(frame_, blurStart);
      [finalEnc endEncoding];
      finalEnc = nil;

      if (!encodeBackdropDownsample(renderTargetTexture, sceneTexture)) return false;
      float const blurRadius = maxBackdropBlurRadius(frame_, blurStart, blurEnd);
      if (!encodeBackdropBlur(sceneTexture,
                              scratchTexture,
                              blurredTexture,
                              blurRadius / static_cast<float>(downsample))) {
        return false;
      }

      finalEnc = [cmdBuf_ renderCommandEncoderWithDescriptor:makeFinalPass(MTLLoadActionLoad)];
      if (!finalEnc) return false;
      [finalEnc setViewport:finalVp];
      encodeRecorderOps(frame_, finalEnc, frameDrawableW_, frameDrawableH_, frameDrawablePixelsW_,
                        frameDrawablePixelsH_, finalUniformBuffer, finalClipBuffer, 1,
                        blurredTexture, blurStart, blurEnd, &finalEncodeState);
      cursor = blurEnd;
    }

    if (finalEnc) [finalEnc endEncoding];
    return true;
  }

  void uploadRecorder(MetalFrameRecorder& recorder) {
    metal_.uploadRectOps(recorder.rectOps);
    metal_.uploadImageOps(recorder.imageOps);
    metal_.uploadPathVertices(recorder.pathVerts);
    metal_.uploadGlyphVertices(recorder);
    metal_.reserveDrawStateBuffers(static_cast<std::uint32_t>(recorder.opOrder.size()),
                                   static_cast<std::uint32_t>(recorder.opOrder.size()));
  }

  void encodeRecorderOps(MetalFrameRecorder& recorder, id<MTLRenderCommandEncoder> enc,
                         float viewportW, float viewportH,
                         NSUInteger viewportPixelsW, NSUInteger viewportPixelsH,
                         id<MTLBuffer> uniformBufferOverride = nil,
                         id<MTLBuffer> clipBufferOverride = nil,
                         std::uint32_t renderSampleCount = 1,
                         id<MTLTexture> backdropSource = nil,
                         std::size_t orderStart = 0,
                         std::size_t orderEnd = std::numeric_limits<std::size_t>::max(),
                         MetalRecorderEncodeState* encodeState = nullptr) {
    MTLScissorRect const fullScissor = {0, 0, viewportPixelsW, viewportPixelsH};
    MTLScissorRect lastScissor = {0, 0, 0, 0};
    bool haveScissor = false;

    id<MTLBuffer> pathBuf = metal_.pathVertexArenaBuffer();
    id<MTLBuffer> uniformBuf = uniformBufferOverride ? uniformBufferOverride : metal_.drawUniformArenaBuffer();
    id<MTLBuffer> clipBuf = clipBufferOverride ? clipBufferOverride : metal_.roundedClipArenaBuffer();
    auto* uniformDst = uniformBuf ? static_cast<MetalDrawUniforms*>([uniformBuf contents]) : nullptr;
    auto* clipDst = clipBuf ? static_cast<MetalRoundedClipStack*>([clipBuf contents]) : nullptr;
    std::uint32_t uniformIndex = encodeState ? encodeState->uniformIndex : 0;
    std::uint32_t clipIndex = encodeState ? encodeState->clipIndex : 0;
    std::size_t const opCount = std::min(orderEnd, recorder.opOrder.size());
    std::size_t i = std::min(orderStart, opCount);
    while (i < opCount) {
      MetalOpRef const ref = recorder.opOrder[i];
      if (ref.kind == MetalOpRef::BackdropBlur) {
        MetalBackdropBlurOp const& op = recorder.backdropBlurOps[ref.index];
        setEncoderScissorForOp(enc, op, fullScissor, &lastScissor, &haveScissor);
        encodeBackdropQuad(enc, op, backdropSource, viewportW, viewportH, renderSampleCount);
        debug::perf::recordDrawCall(debug::perf::RenderCounterKind::Image);
        ++i;
        continue;
      }
      if (ref.kind == MetalOpRef::Rect) {
        MetalRectOp const& op = recorder.rectOps[ref.index];
        std::size_t j = i + 1;
        while (j < opCount) {
          MetalOpRef const nextRef = recorder.opOrder[j];
          if (nextRef.kind != MetalOpRef::Rect) {
            break;
          }
          MetalRectOp const& o2 = recorder.rectOps[nextRef.index];
          MetalOpRef const prevRef = recorder.opOrder[j - 1];
          MetalRectOp const& prev = recorder.rectOps[prevRef.index];
          std::uint32_t const prevInstanceIndex =
              prev.externalInstanceBuffer ? prev.externalInstanceIndex : prev.arenaInstanceIndex;
          std::uint32_t const nextInstanceIndex =
              o2.externalInstanceBuffer ? o2.externalInstanceIndex : o2.arenaInstanceIndex;
          if (o2.externalInstanceBuffer != op.externalInstanceBuffer ||
              o2.isLine != op.isLine || o2.blendMode != op.blendMode || !sameScissorForBatch(op, o2) ||
              !sameRoundedClipForBatch(op, o2) || !sameTranslationForBatch(op, o2) ||
              nextInstanceIndex != prevInstanceIndex + 1) {
            break;
          }
          ++j;
        }
        std::size_t const runLen = j - i;
        setEncoderScissorForOp(enc, op, fullScissor, &lastScissor, &haveScissor);
        MetalDrawUniforms const uniforms = makeDrawUniforms(viewportW, viewportH, op.translation);
        setEncoderRoundedClipBuffer(enc, clipBuf, clipDst, &clipIndex, op.roundedClip);
        id<MTLBuffer> instanceBuf =
            op.externalInstanceBuffer ? (__bridge id<MTLBuffer>)op.externalInstanceBuffer
                                      : metal_.instanceArenaBuffer();
        std::uint32_t const instanceIndex =
            op.externalInstanceBuffer ? op.externalInstanceIndex : op.arenaInstanceIndex;
        setEncoderDrawUniformBuffer(enc, uniformBuf, uniformDst, &uniformIndex, uniforms, 2);
        if (!op.isLine) {
          [enc setRenderPipelineState:metal_.rectPSO(op.blendMode, renderSampleCount)];
          [enc setVertexBuffer:metal_.quadBuffer() offset:0 atIndex:0];
          const NSUInteger off = static_cast<NSUInteger>(instanceIndex) * sizeof(MetalRectInstance);
          [enc setVertexBuffer:instanceBuf offset:off atIndex:1];
          [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:kQuadStripCount
                  instanceCount:static_cast<NSUInteger>(runLen)];
          debug::perf::recordDrawCall(debug::perf::RenderCounterKind::Rect);
        } else {
          [enc setRenderPipelineState:metal_.linePSO(op.blendMode, renderSampleCount)];
          [enc setVertexBuffer:metal_.quadBuffer() offset:0 atIndex:0];
          const NSUInteger off = static_cast<NSUInteger>(instanceIndex) * sizeof(MetalRectInstance);
          [enc setVertexBuffer:instanceBuf offset:off atIndex:1];
          [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:kQuadStripCount
                  instanceCount:static_cast<NSUInteger>(runLen)];
          debug::perf::recordDrawCall(debug::perf::RenderCounterKind::Rect);
        }
        i = j;
        continue;
      }
      if (ref.kind == MetalOpRef::Glyph) {
        MetalGlyphOp const& op = recorder.glyphOps[ref.index];
        std::size_t j = i + 1;
        std::uint32_t runStart = op.glyphStart;
        std::uint32_t runVerts = op.glyphVertexCount;
        while (j < opCount) {
          MetalOpRef const nextRef = recorder.opOrder[j];
          if (nextRef.kind != MetalOpRef::Glyph) {
            break;
          }
          MetalGlyphOp const& o2 = recorder.glyphOps[nextRef.index];
          if (o2.externalVertexBuffer != op.externalVertexBuffer ||
              o2.blendMode != op.blendMode || !sameScissorForBatch(op, o2) ||
              !sameRoundedClipForBatch(op, o2) || !sameTranslationForBatch(op, o2) ||
              o2.glyphStart != runStart + runVerts || nextRef.index != recorder.opOrder[j - 1].index + 1) {
            break;
          }
          runVerts += o2.glyphVertexCount;
          ++j;
        }
        setEncoderScissorForOp(enc, op, fullScissor, &lastScissor, &haveScissor);
        setEncoderRoundedClipBuffer(enc, clipBuf, clipDst, &clipIndex, op.roundedClip);
        [enc setRenderPipelineState:metal_.glyphPSO(op.blendMode, renderSampleCount)];
        id<MTLBuffer> gbuf =
            op.externalVertexBuffer ? (__bridge id<MTLBuffer>)op.externalVertexBuffer : metal_.glyphVertexArenaBuffer();
        const NSUInteger goff = static_cast<NSUInteger>(runStart) * sizeof(MetalGlyphVertex);
        [enc setVertexBuffer:gbuf offset:goff atIndex:0];
        MetalDrawUniforms const uniforms = makeDrawUniforms(viewportW, viewportH, op.translation);
        setEncoderDrawUniformBuffer(enc, uniformBuf, uniformDst, &uniformIndex, uniforms, 1);
        [enc setFragmentTexture:glyphAtlas_->texture() atIndex:0];
        [enc setFragmentSamplerState:metal_.linearSampler() atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:static_cast<NSUInteger>(runVerts)];
        debug::perf::recordDrawCall(debug::perf::RenderCounterKind::Glyph);
        i = j;
        continue;
      }

      if (ref.kind == MetalOpRef::Image) {
        MetalImageOp const& op = recorder.imageOps[ref.index];
        setEncoderScissorForOp(enc, op, fullScissor, &lastScissor, &haveScissor);
        if (!op.texture) {
          ++i;
          continue;
        }
        setEncoderRoundedClipBuffer(enc, clipBuf, clipDst, &clipIndex, op.roundedClip);
        [enc setRenderPipelineState:metal_.imagePSO(op.blendMode, renderSampleCount)];
        [enc setVertexBuffer:metal_.quadBuffer() offset:0 atIndex:0];
        id<MTLBuffer> imageInstanceBuf =
            op.externalInstanceBuffer ? (__bridge id<MTLBuffer>)op.externalInstanceBuffer
                                      : metal_.imageInstanceArenaBuffer();
        std::uint32_t const imageInstanceIndex =
            op.externalInstanceBuffer ? op.externalInstanceIndex : op.arenaInstanceIndex;
        const NSUInteger off = static_cast<NSUInteger>(imageInstanceIndex) * sizeof(MetalImageInstance);
        [enc setVertexBuffer:imageInstanceBuf offset:off atIndex:1];
        MetalDrawUniforms const uniforms = makeDrawUniforms(viewportW, viewportH, op.translation);
        setEncoderDrawUniformBuffer(enc, uniformBuf, uniformDst, &uniformIndex, uniforms, 2);
        [enc setFragmentTexture:(__bridge id<MTLTexture>)op.texture atIndex:0];
        [enc setFragmentSamplerState:op.repeatSampler ? metal_.repeatSampler() : metal_.linearSampler() atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:kQuadStripCount
                instanceCount:1];
        debug::perf::recordDrawCall(debug::perf::RenderCounterKind::Image);
        ++i;
        continue;
      }
      if (ref.kind == MetalOpRef::Path) {
        MetalPathOp const& op = recorder.pathOps[ref.index];
        setEncoderScissorForOp(enc, op, fullScissor, &lastScissor, &haveScissor);
        if (op.pathCount == 0) {
          ++i;
          continue;
        }
        setEncoderRoundedClipBuffer(enc, clipBuf, clipDst, &clipIndex, op.roundedClip);
        [enc setRenderPipelineState:metal_.pathPSO(op.blendMode, renderSampleCount)];
        const NSUInteger off = static_cast<NSUInteger>(op.pathStart) * sizeof(PathVertex);
        id<MTLBuffer> effectivePathBuf =
            op.externalVertexBuffer ? (__bridge id<MTLBuffer>)op.externalVertexBuffer : pathBuf;
        [enc setVertexBuffer:effectivePathBuf offset:off atIndex:0];
        MetalDrawUniforms const uniforms = makeDrawUniforms(viewportW, viewportH, op.translation);
        setEncoderDrawUniformBuffer(enc, uniformBuf, uniformDst, &uniformIndex, uniforms, 1);
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:static_cast<NSUInteger>(op.pathCount)];
        debug::perf::recordDrawCall(debug::perf::RenderCounterKind::Path);
        ++i;
        continue;
      }
      assert(false && "unsupported Metal op ref kind");
    }
    if (encodeState) {
      encodeState->uniformIndex = uniformIndex;
      encodeState->clipIndex = clipIndex;
    }
  }

public:
  float dpiScale() const noexcept override { return dpiScale_; }

  void beginRecordedOpsCapture(MetalFrameRecorder* target) {
    if (!target) {
      return;
    }
    target->clear();
    captureRecorder_ = target;
    stateStack_.push_back(GpuState{});
    updateClipScissor();
  }

  void endRecordedOpsCapture() {
    captureRecorder_ = nullptr;
    if (!stateStack_.empty()) {
      stateStack_.pop_back();
    }
    updateClipScissor();
  }

  std::shared_ptr<Image> rasterizeWithRenderTarget(Size logicalSize, RasterizeDrawCallback const& draw,
                                                   float dpiScale) {
    if (logicalSize.width <= 0.f || logicalSize.height <= 0.f || dpiScale <= 0.f) {
      return nullptr;
    }
    NSUInteger const pixelW = static_cast<NSUInteger>(std::ceil(logicalSize.width * dpiScale));
    NSUInteger const pixelH = static_cast<NSUInteger>(std::ceil(logicalSize.height * dpiScale));
    if (pixelW == 0 || pixelH == 0 || !metal_.device() || !metal_.queue()) {
      return nullptr;
    }

    MTLTextureDescriptor* desc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                           width:pixelW
                                                          height:pixelH
                                                       mipmapped:NO];
    desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    desc.storageMode = MTLStorageModePrivate;
    id<MTLTexture> texture = [metal_.device() newTextureWithDescriptor:desc];
    if (!texture) {
      return nullptr;
    }
    MetalRenderTargetSpec spec{
        .texture = (__bridge void*)texture,
        .width = static_cast<std::uint32_t>(pixelW),
        .height = static_cast<std::uint32_t>(pixelH),
    };
    auto targetCanvas = std::make_unique<MetalCanvas>(spec, textSystem_);
    targetCanvas->resize(static_cast<int>(std::ceil(logicalSize.width)),
                         static_cast<int>(std::ceil(logicalSize.height)));
    targetCanvas->updateDpiScale(dpiScale, dpiScale);
    targetCanvas->beginFrame();
    targetCanvas->clear(Colors::transparent);
    draw(*targetCanvas, Rect::sharp(0.f, 0.f, logicalSize.width, logicalSize.height));
    targetCanvas->present();
    return std::make_shared<MetalImage>(texture, static_cast<std::uint32_t>(pixelW),
                                        static_cast<std::uint32_t>(pixelH));
  }

  void* preparedRectInstanceBuffer(MetalFrameRecorder const& recorded) {
    std::uint32_t const instanceCount = static_cast<std::uint32_t>(recorded.rectOps.size());
    if (instanceCount == 0) {
      return nullptr;
    }
    if (recorded.preparedRectInstanceBuffer && recorded.preparedRectInstanceCapacity >= instanceCount) {
      return recorded.preparedRectInstanceBuffer;
    }
    if (recorded.preparedRectInstanceBuffer) {
      (void)(__bridge_transfer id<MTLBuffer>)recorded.preparedRectInstanceBuffer;
      recorded.preparedRectInstanceBuffer = nullptr;
      recorded.preparedRectInstanceCapacity = 0;
    }
    NSUInteger const bytes = static_cast<NSUInteger>(instanceCount * sizeof(MetalRectInstance));
    id<MTLBuffer> buffer = [metal_.device() newBufferWithLength:bytes options:MTLResourceStorageModeShared];
    if (!buffer) {
      return nullptr;
    }
    auto* dst = static_cast<MetalRectInstance*>([buffer contents]);
    for (std::size_t i = 0; i < recorded.rectOps.size(); ++i) {
      dst[i] = recorded.rectOps[i].inst;
    }
    recorded.preparedRectInstanceBuffer = (__bridge_retained void*)buffer;
    recorded.preparedRectInstanceCapacity = instanceCount;
    return recorded.preparedRectInstanceBuffer;
  }

  void* preparedImageInstanceBuffer(MetalFrameRecorder const& recorded) {
    std::uint32_t const instanceCount = static_cast<std::uint32_t>(recorded.imageOps.size());
    if (instanceCount == 0) {
      return nullptr;
    }
    if (recorded.preparedImageInstanceBuffer && recorded.preparedImageInstanceCapacity >= instanceCount) {
      return recorded.preparedImageInstanceBuffer;
    }
    if (recorded.preparedImageInstanceBuffer) {
      (void)(__bridge_transfer id<MTLBuffer>)recorded.preparedImageInstanceBuffer;
      recorded.preparedImageInstanceBuffer = nullptr;
      recorded.preparedImageInstanceCapacity = 0;
    }
    NSUInteger const bytes = static_cast<NSUInteger>(instanceCount * sizeof(MetalImageInstance));
    id<MTLBuffer> buffer = [metal_.device() newBufferWithLength:bytes options:MTLResourceStorageModeShared];
    if (!buffer) {
      return nullptr;
    }
    auto* dst = static_cast<MetalImageInstance*>([buffer contents]);
    for (std::size_t i = 0; i < recorded.imageOps.size(); ++i) {
      dst[i] = recorded.imageOps[i].inst;
    }
    recorded.preparedImageInstanceBuffer = (__bridge_retained void*)buffer;
    recorded.preparedImageInstanceCapacity = instanceCount;
    return recorded.preparedImageInstanceBuffer;
  }

  void* preparedPathVertexBuffer(MetalFrameRecorder const& recorded) {
    std::uint32_t const vertexCount = static_cast<std::uint32_t>(recorded.pathVerts.size());
    if (vertexCount == 0) {
      return nullptr;
    }
    if (recorded.preparedPathVertexBuffer && recorded.preparedPathVertexCapacity >= vertexCount) {
      return recorded.preparedPathVertexBuffer;
    }
    if (recorded.preparedPathVertexBuffer) {
      (void)(__bridge_transfer id<MTLBuffer>)recorded.preparedPathVertexBuffer;
      recorded.preparedPathVertexBuffer = nullptr;
      recorded.preparedPathVertexCapacity = 0;
    }
    NSUInteger const bytes = static_cast<NSUInteger>(vertexCount * sizeof(PathVertex));
    id<MTLBuffer> buffer = [metal_.device() newBufferWithBytes:recorded.pathVerts.data()
                                                        length:bytes
                                                       options:MTLResourceStorageModeShared];
    if (!buffer) {
      return nullptr;
    }
    recorded.preparedPathVertexBuffer = (__bridge_retained void*)buffer;
    recorded.preparedPathVertexCapacity = vertexCount;
    return recorded.preparedPathVertexBuffer;
  }

  void* preparedGlyphVertexBuffer(MetalFrameRecorder const& recorded) {
    std::uint32_t const vertexCount = static_cast<std::uint32_t>(recorded.glyphVerts.size());
    if (vertexCount == 0) {
      return nullptr;
    }
    if (recorded.preparedGlyphVertexBuffer && recorded.preparedGlyphVertexCapacity >= vertexCount) {
      return recorded.preparedGlyphVertexBuffer;
    }
    if (recorded.preparedGlyphVertexBuffer) {
      (void)(__bridge_transfer id<MTLBuffer>)recorded.preparedGlyphVertexBuffer;
      recorded.preparedGlyphVertexBuffer = nullptr;
      recorded.preparedGlyphVertexCapacity = 0;
    }
    NSUInteger const bytes = static_cast<NSUInteger>(vertexCount * sizeof(MetalGlyphVertex));
    id<MTLBuffer> buffer = [metal_.device() newBufferWithBytes:recorded.glyphVerts.data()
                                                        length:bytes
                                                       options:MTLResourceStorageModeShared];
    if (!buffer) {
      return nullptr;
    }
    recorded.preparedGlyphVertexBuffer = (__bridge_retained void*)buffer;
    recorded.preparedGlyphVertexCapacity = vertexCount;
    return recorded.preparedGlyphVertexBuffer;
  }

  void replayRecordedOps(MetalFrameRecorder const& recorded, MetalRecorderSlice const& slice) {
    if (!recordedGlyphAtlasCurrent(recorded, slice)) {
      return;
    }
    MetalFrameRecorder& frame = frame_;
    RecorderCapacitySnapshot const capacityBefore = snapshotRecorderCapacity(frame);
    void* const externalRectBuffer = slice.rectCount > 0 ? preparedRectInstanceBuffer(recorded) : nullptr;
    void* const externalImageBuffer = slice.imageCount > 0 ? preparedImageInstanceBuffer(recorded) : nullptr;
    void* const externalPathBuffer = slice.pathVertexCount > 0 ? preparedPathVertexBuffer(recorded) : nullptr;
    void* const externalGlyphBuffer = slice.glyphVertexCount > 0 ? preparedGlyphVertexBuffer(recorded) : nullptr;
    std::uint32_t const framePathVertexBase = static_cast<std::uint32_t>(frame.pathVerts.size());
    std::uint32_t const frameGlyphVertexBase = frame.glyphVertexCount;
    std::uint32_t const frameRectBase = static_cast<std::uint32_t>(frame.rectOps.size());
    std::uint32_t const frameImageBase = static_cast<std::uint32_t>(frame.imageOps.size());
    std::uint32_t const framePathOpBase = static_cast<std::uint32_t>(frame.pathOps.size());
    std::uint32_t const frameGlyphOpBase = static_cast<std::uint32_t>(frame.glyphOps.size());
    std::uint32_t const frameBackdropBlurOpBase = static_cast<std::uint32_t>(frame.backdropBlurOps.size());

    if (slice.pathVertexCount > 0 && !externalPathBuffer) {
      frame.pathVerts.insert(frame.pathVerts.end(),
                              recorded.pathVerts.begin() + static_cast<std::ptrdiff_t>(slice.pathVertexStart),
                              recorded.pathVerts.begin() +
                                  static_cast<std::ptrdiff_t>(slice.pathVertexStart + slice.pathVertexCount));
    }
    if (slice.glyphVertexCount > 0) {
      if (!externalGlyphBuffer) {
        appendBorrowedGlyphSource(frame,
                                  recorded.glyphVerts.data() + static_cast<std::size_t>(slice.glyphVertexStart),
                                  slice.glyphVertexCount);
      }
    }
    if (slice.rectCount > 0) {
      frame.rectOps.insert(frame.rectOps.end(),
                            recorded.rectOps.begin() + static_cast<std::ptrdiff_t>(slice.rectStart),
                            recorded.rectOps.begin() + static_cast<std::ptrdiff_t>(slice.rectStart + slice.rectCount));
      if (externalRectBuffer) {
        for (std::uint32_t i = 0; i < slice.rectCount; ++i) {
          MetalRectOp& op = frame.rectOps[frameRectBase + static_cast<std::size_t>(i)];
          op.externalInstanceBuffer = externalRectBuffer;
          op.externalInstanceIndex = slice.rectStart + i;
        }
      }
    }
    if (slice.imageCount > 0) {
      frame.imageOps.insert(frame.imageOps.end(),
                             recorded.imageOps.begin() + static_cast<std::ptrdiff_t>(slice.imageStart),
                             recorded.imageOps.begin() +
                                 static_cast<std::ptrdiff_t>(slice.imageStart + slice.imageCount));
      for (std::uint32_t i = 0; i < slice.imageCount; ++i) {
        MetalImageOp& op = frame.imageOps[frameImageBase + static_cast<std::size_t>(i)];
        op.externalInstanceBuffer = externalImageBuffer;
        if (externalImageBuffer) {
          op.externalInstanceIndex = slice.imageStart + i;
        }
        if (op.texture) {
          op.texture = retainTexturePointer(op.texture);
        }
      }
    }
    if (slice.pathOpCount > 0) {
      frame.pathOps.insert(frame.pathOps.end(),
                            recorded.pathOps.begin() + static_cast<std::ptrdiff_t>(slice.pathOpStart),
                            recorded.pathOps.begin() +
                                static_cast<std::ptrdiff_t>(slice.pathOpStart + slice.pathOpCount));
      for (std::uint32_t i = 0; i < slice.pathOpCount; ++i) {
        MetalPathOp& op = frame.pathOps[framePathOpBase + static_cast<std::size_t>(i)];
        op.externalVertexBuffer = externalPathBuffer;
        if (!externalPathBuffer) {
          op.pathStart = framePathVertexBase + (op.pathStart - slice.pathVertexStart);
        }
      }
    }
    if (slice.glyphOpCount > 0) {
      frame.glyphOps.insert(frame.glyphOps.end(),
                             recorded.glyphOps.begin() + static_cast<std::ptrdiff_t>(slice.glyphOpStart),
                             recorded.glyphOps.begin() +
                                 static_cast<std::ptrdiff_t>(slice.glyphOpStart + slice.glyphOpCount));
      for (std::uint32_t i = 0; i < slice.glyphOpCount; ++i) {
        MetalGlyphOp& op = frame.glyphOps[frameGlyphOpBase + static_cast<std::size_t>(i)];
        op.externalVertexBuffer = externalGlyphBuffer;
        if (!op.externalVertexBuffer) {
          op.glyphStart = frameGlyphVertexBase + (op.glyphStart - slice.glyphVertexStart);
        }
      }
    }
    if (slice.backdropBlurOpCount > 0) {
      frame.backdropBlurOps.insert(
          frame.backdropBlurOps.end(),
          recorded.backdropBlurOps.begin() + static_cast<std::ptrdiff_t>(slice.backdropBlurOpStart),
          recorded.backdropBlurOps.begin() +
              static_cast<std::ptrdiff_t>(slice.backdropBlurOpStart + slice.backdropBlurOpCount));
    }
    if (slice.orderCount > 0) {
      frame.opOrder.insert(frame.opOrder.end(),
                            recorded.opOrder.begin() + static_cast<std::ptrdiff_t>(slice.orderStart),
                            recorded.opOrder.begin() + static_cast<std::ptrdiff_t>(slice.orderStart + slice.orderCount));
      for (std::uint32_t i = 0; i < slice.orderCount; ++i) {
        MetalOpRef& ref = frame.opOrder[frame.opOrder.size() - slice.orderCount + i];
        switch (ref.kind) {
        case MetalOpRef::Rect:
          ref.index = frameRectBase + (ref.index - slice.rectStart);
          break;
        case MetalOpRef::Image:
          ref.index = frameImageBase + (ref.index - slice.imageStart);
          break;
        case MetalOpRef::Path:
          ref.index = framePathOpBase + (ref.index - slice.pathOpStart);
          break;
        case MetalOpRef::Glyph:
          ref.index = frameGlyphOpBase + (ref.index - slice.glyphOpStart);
          break;
        case MetalOpRef::BackdropBlur:
          ref.index = frameBackdropBlurOpBase + (ref.index - slice.backdropBlurOpStart);
          break;
        }
      }
    }
    recordRecorderCapacityIncreases(capacityBefore, frame);
  }

  bool replayRecordedLocalOps(MetalFrameRecorder const& recorded, MetalRecorderSlice const& slice) {
    if (!inFrame_) {
      return false;
    }
    if (!recordedGlyphAtlasCurrent(recorded, slice)) {
      return false;
    }
    if (!currentState().transform.isTranslationOnly()) {
      return false;
    }

    float const dx = currentState().transform.m[6] * dpiScaleX_;
    float const dy = currentState().transform.m[7] * dpiScaleY_;
    vector_float2 const translation = simd_make_float2(dx, dy);
    float const opacityScale = currentState().opacity;
    std::optional<Rect> const clipRect = currentState().clip;
    MetalFrameRecorder& frame = frame_;
    RecorderCapacitySnapshot const capacityBefore = snapshotRecorderCapacity(frame);
    void* const externalGlyphBuffer =
        (slice.glyphVertexCount > 0 && opacityScale >= 0.9999f) ? preparedGlyphVertexBuffer(recorded) : nullptr;
    void* const externalRectBuffer =
        (slice.rectCount > 0 && opacityScale >= 0.9999f) ? preparedRectInstanceBuffer(recorded) : nullptr;
    void* const externalImageBuffer =
        (slice.imageCount > 0 && opacityScale >= 0.9999f) ? preparedImageInstanceBuffer(recorded) : nullptr;
    void* const externalPathBuffer =
        (slice.pathVertexCount > 0 && opacityScale >= 0.9999f) ? preparedPathVertexBuffer(recorded) : nullptr;
    std::uint32_t const framePathVertexBase = static_cast<std::uint32_t>(frame.pathVerts.size());
    std::uint32_t const frameGlyphVertexBase = frame.glyphVertexCount;
    std::uint32_t const frameRectBase = static_cast<std::uint32_t>(frame.rectOps.size());
    std::uint32_t const frameImageBase = static_cast<std::uint32_t>(frame.imageOps.size());
    std::uint32_t const framePathOpBase = static_cast<std::uint32_t>(frame.pathOps.size());
    std::uint32_t const frameGlyphOpBase = static_cast<std::uint32_t>(frame.glyphOps.size());
    std::uint32_t const frameBackdropBlurOpBase = static_cast<std::uint32_t>(frame.backdropBlurOps.size());

    if (slice.pathVertexCount > 0 && !externalPathBuffer) {
      frame.pathVerts.reserve(frame.pathVerts.size() + slice.pathVertexCount);
    }
    if (slice.glyphVertexCount > 0 && opacityScale < 0.9999f) {
      frame.glyphVerts.reserve(frame.glyphVerts.size() + slice.glyphVertexCount);
    }
    if (slice.rectCount > 0) {
      frame.rectOps.reserve(frame.rectOps.size() + slice.rectCount);
    }
    if (slice.imageCount > 0) {
      frame.imageOps.reserve(frame.imageOps.size() + slice.imageCount);
    }
    if (slice.pathOpCount > 0) {
      frame.pathOps.reserve(frame.pathOps.size() + slice.pathOpCount);
    }
    if (slice.glyphOpCount > 0) {
      frame.glyphOps.reserve(frame.glyphOps.size() + slice.glyphOpCount);
    }
    if (slice.backdropBlurOpCount > 0) {
      frame.backdropBlurOps.reserve(frame.backdropBlurOps.size() + slice.backdropBlurOpCount);
    }
    if (slice.orderCount > 0) {
      frame.opOrder.reserve(frame.opOrder.size() + slice.orderCount);
    }

    if (slice.pathVertexCount > 0 && !externalPathBuffer) {
      std::size_t const start = static_cast<std::size_t>(slice.pathVertexStart);
      std::size_t const count = static_cast<std::size_t>(slice.pathVertexCount);
      frame.pathVerts.insert(frame.pathVerts.end(), recorded.pathVerts.begin() + static_cast<std::ptrdiff_t>(start),
                             recorded.pathVerts.begin() + static_cast<std::ptrdiff_t>(start + count));
      if (opacityScale < 0.9999f) {
        for (std::size_t i = 0; i < count; ++i) {
          PathVertex& vertex = frame.pathVerts[static_cast<std::size_t>(framePathVertexBase) + i];
          vertex.color[3] *= opacityScale;
        }
      }
    }

    if (slice.glyphVertexCount > 0) {
      std::size_t const start = static_cast<std::size_t>(slice.glyphVertexStart);
      std::size_t const count = static_cast<std::size_t>(slice.glyphVertexCount);
      if (opacityScale < 0.9999f) {
        std::uint32_t const ownedGlyphStart = static_cast<std::uint32_t>(frame.glyphVerts.size());
        frame.glyphVerts.insert(frame.glyphVerts.end(),
                                recorded.glyphVerts.begin() + static_cast<std::ptrdiff_t>(start),
                                recorded.glyphVerts.begin() + static_cast<std::ptrdiff_t>(start + count));
        appendOwnedGlyphSource(frame, ownedGlyphStart, slice.glyphVertexCount);
        for (std::size_t i = 0; i < count; ++i) {
          MetalGlyphVertex& vertex = frame.glyphVerts[static_cast<std::size_t>(ownedGlyphStart) + i];
          vertex.color *= opacityScale;
        }
      } else if (!externalGlyphBuffer) {
        appendBorrowedGlyphSource(frame, recorded.glyphVerts.data() + start, slice.glyphVertexCount);
      }
    }

    if (slice.rectCount > 0) {
      std::size_t const start = static_cast<std::size_t>(slice.rectStart);
      std::size_t const count = static_cast<std::size_t>(slice.rectCount);
      frame.rectOps.insert(frame.rectOps.end(), recorded.rectOps.begin() + static_cast<std::ptrdiff_t>(start),
                           recorded.rectOps.begin() + static_cast<std::ptrdiff_t>(start + count));
      for (std::size_t i = 0; i < count; ++i) {
        MetalRectOp& op = frame.rectOps[static_cast<std::size_t>(frameRectBase) + i];
        (void)clipRect;
        op.translation = translation;
        op.externalInstanceBuffer = externalRectBuffer;
        if (externalRectBuffer) {
          op.externalInstanceIndex = static_cast<std::uint32_t>(start + i);
        } else {
          op.inst.strokeWidthOpacity.y *= opacityScale;
        }
        tagOpWithClip(op, clipScissorValid_, clipScissor_, clipRoundedStack_);
      }
    }

    if (slice.imageCount > 0) {
      std::size_t const start = static_cast<std::size_t>(slice.imageStart);
      std::size_t const count = static_cast<std::size_t>(slice.imageCount);
      frame.imageOps.insert(frame.imageOps.end(), recorded.imageOps.begin() + static_cast<std::ptrdiff_t>(start),
                            recorded.imageOps.begin() + static_cast<std::ptrdiff_t>(start + count));
      for (std::size_t i = 0; i < count; ++i) {
        MetalImageOp& op = frame.imageOps[static_cast<std::size_t>(frameImageBase) + i];
        op.translation = translation;
        op.externalInstanceBuffer = externalImageBuffer;
        if (externalImageBuffer) {
          op.externalInstanceIndex = static_cast<std::uint32_t>(start + i);
        } else {
          op.inst.sdf.strokeWidthOpacity.y *= opacityScale;
        }
        if (op.texture) {
          op.texture = retainTexturePointer(op.texture);
        }
        tagOpWithClip(op, clipScissorValid_, clipScissor_, clipRoundedStack_);
      }
    }

    if (slice.pathOpCount > 0) {
      std::size_t const start = static_cast<std::size_t>(slice.pathOpStart);
      std::size_t const count = static_cast<std::size_t>(slice.pathOpCount);
      frame.pathOps.insert(frame.pathOps.end(), recorded.pathOps.begin() + static_cast<std::ptrdiff_t>(start),
                           recorded.pathOps.begin() + static_cast<std::ptrdiff_t>(start + count));
      for (std::size_t i = 0; i < count; ++i) {
        MetalPathOp& op = frame.pathOps[static_cast<std::size_t>(framePathOpBase) + i];
        op.externalVertexBuffer = externalPathBuffer;
        if (!externalPathBuffer) {
          op.pathStart = framePathVertexBase + (op.pathStart - slice.pathVertexStart);
        }
        op.translation = translation;
        tagOpWithClip(op, clipScissorValid_, clipScissor_, clipRoundedStack_);
      }
    }

    if (slice.glyphOpCount > 0) {
      std::size_t const start = static_cast<std::size_t>(slice.glyphOpStart);
      std::size_t const count = static_cast<std::size_t>(slice.glyphOpCount);
      frame.glyphOps.insert(frame.glyphOps.end(), recorded.glyphOps.begin() + static_cast<std::ptrdiff_t>(start),
                            recorded.glyphOps.begin() + static_cast<std::ptrdiff_t>(start + count));
      for (std::size_t i = 0; i < count; ++i) {
        MetalGlyphOp& op = frame.glyphOps[static_cast<std::size_t>(frameGlyphOpBase) + i];
        op.externalVertexBuffer = externalGlyphBuffer;
        if (!externalGlyphBuffer) {
          op.glyphStart = frameGlyphVertexBase + (op.glyphStart - slice.glyphVertexStart);
        }
        op.translation = translation;
        tagOpWithClip(op, clipScissorValid_, clipScissor_, clipRoundedStack_);
      }
    }
    if (slice.backdropBlurOpCount > 0) {
      std::size_t const start = static_cast<std::size_t>(slice.backdropBlurOpStart);
      std::size_t const count = static_cast<std::size_t>(slice.backdropBlurOpCount);
      frame.backdropBlurOps.insert(
          frame.backdropBlurOps.end(),
          recorded.backdropBlurOps.begin() + static_cast<std::ptrdiff_t>(start),
          recorded.backdropBlurOps.begin() + static_cast<std::ptrdiff_t>(start + count));
      for (std::size_t i = 0; i < count; ++i) {
        MetalBackdropBlurOp& op = frame.backdropBlurOps[static_cast<std::size_t>(frameBackdropBlurOpBase) + i];
        op.rect.x += translation.x;
        op.rect.y += translation.y;
        op.tint.w *= opacityScale;
        tagOpWithClip(op, clipScissorValid_, clipScissor_, clipRoundedStack_);
      }
    }

    if (slice.orderCount > 0) {
      std::size_t const start = static_cast<std::size_t>(slice.orderStart);
      std::size_t const count = static_cast<std::size_t>(slice.orderCount);
      frame.opOrder.insert(frame.opOrder.end(), recorded.opOrder.begin() + static_cast<std::ptrdiff_t>(start),
                           recorded.opOrder.begin() + static_cast<std::ptrdiff_t>(start + count));
      for (std::size_t i = 0; i < count; ++i) {
        MetalOpRef& ref = frame.opOrder[frame.opOrder.size() - count + i];
        switch (ref.kind) {
        case MetalOpRef::Rect:
          ref.index = frameRectBase + (ref.index - slice.rectStart);
          break;
        case MetalOpRef::Image:
          ref.index = frameImageBase + (ref.index - slice.imageStart);
          break;
        case MetalOpRef::Path:
          ref.index = framePathOpBase + (ref.index - slice.pathOpStart);
          break;
        case MetalOpRef::Glyph:
          ref.index = frameGlyphOpBase + (ref.index - slice.glyphOpStart);
          break;
        case MetalOpRef::BackdropBlur:
          ref.index = frameBackdropBlurOpBase + (ref.index - slice.backdropBlurOpStart);
          break;
        }
      }
    }

    recordRecorderCapacityIncreases(capacityBefore, frame);
    return true;
  }

  void presentRenderTarget() {
    id<MTLTexture> texture = targetTexture();
    if (!texture || frameDrawablePixelsW_ == 0 || frameDrawablePixelsH_ == 0) {
      frame_.clear();
      cmdBuf_ = nil;
      inFrame_ = false;
      return;
    }

    debug::perf::recordFrameOps(frame_.rectOps.size(), frame_.imageOps.size(), frame_.pathOps.size(),
                                frame_.glyphOps.size(), frame_.opOrder.size(), frame_.pathVerts.size(),
                                frame_.glyphVertexCount);
    std::uint32_t const uploadedRectInstances = metal_.uploadRectOps(frame_.rectOps);
    debug::perf::recordUploadBytes(debug::perf::RenderCounterKind::Rect,
                                   static_cast<std::uint64_t>(uploadedRectInstances) *
                                       sizeof(MetalRectInstance));
    std::uint32_t const uploadedImageInstances = metal_.uploadImageOps(frame_.imageOps);
    debug::perf::recordUploadBytes(debug::perf::RenderCounterKind::Image,
                                   static_cast<std::uint64_t>(uploadedImageInstances) *
                                       sizeof(MetalImageInstance));
    debug::perf::recordUploadBytes(debug::perf::RenderCounterKind::Path,
                                   frame_.pathVerts.size() * sizeof(PathVertex));
    debug::perf::recordUploadBytes(debug::perf::RenderCounterKind::Glyph,
                                   static_cast<std::uint64_t>(frame_.glyphVertexCount) *
                                       sizeof(MetalGlyphVertex));

    metal_.uploadPathVertices(frame_.pathVerts);
    metal_.uploadGlyphVertices(frame_);
    metal_.reserveDrawStateBuffers(static_cast<std::uint32_t>(frame_.opOrder.size()),
                                   static_cast<std::uint32_t>(frame_.opOrder.size()));

    id<MTLCommandBuffer> externalCommandBuffer = targetCommandBuffer();
    cmdBuf_ = externalCommandBuffer ? externalCommandBuffer : [metal_.queue() commandBuffer];
    if (!cmdBuf_) {
      frame_.clear();
      inFrame_ = false;
      return;
    }
    glyphAtlas_->flushUploads(cmdBuf_);

    bool const encodedBackdropFrame = hasBackdropBlurOps(frame_) && encodeFrameWithBackdropBlur(texture, texture, 1);
    if (!encodedBackdropFrame) {
      MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
      pass.colorAttachments[0].texture = texture;
      pass.colorAttachments[0].loadAction = MTLLoadActionClear;
      pass.colorAttachments[0].clearColor =
          MTLClearColorMake(clearColor_.r, clearColor_.g, clearColor_.b, clearColor_.a);
      pass.colorAttachments[0].storeAction = MTLStoreActionStore;

      id<MTLRenderCommandEncoder> enc = [cmdBuf_ renderCommandEncoderWithDescriptor:pass];
      if (enc) {
        MTLViewport vp = {0, 0, frameDrawableSize_.width, frameDrawableSize_.height, 0.0, 1.0};
        [enc setViewport:vp];
        encodeRecorderOps(frame_, enc, frameDrawableW_, frameDrawableH_, frameDrawablePixelsW_,
                          frameDrawablePixelsH_);
        [enc endEncoding];
      }
    }

    id<MTLSharedEvent> externalEvent = targetSharedEvent();
    id<MTLSharedEvent> signalEvent = externalEvent;
    std::uint64_t signalValue = targetSpec_.signalValue;
    bool waitForInternalEvent = false;
    if (!signalEvent && !externalCommandBuffer) {
      signalEvent = internalRenderTargetCompletionEvent();
      if (signalEvent) {
        signalValue = ++renderTargetCompletionValue_;
        waitForInternalEvent = true;
      }
    }
    if (signalEvent) {
      [cmdBuf_ encodeSignalEvent:signalEvent value:signalValue];
    }

    lastSubmittedCmdBuf_ = cmdBuf_;
    if (!externalCommandBuffer) {
      [cmdBuf_ commit];
      if (waitForInternalEvent) {
        (void)waitForSharedEvent(signalEvent, signalValue);
      } else if (!externalEvent) {
        [cmdBuf_ waitUntilCompleted];
      }
      debug::perf::recordPresentedFrame();
    }

    frame_.clear();
    glyphAtlas_->afterPresent();
    cmdBuf_ = nil;
    inFrame_ = false;
  }

  void waitForLastPresentComplete() {
    if (!lastSubmittedCmdBuf_) {
      return;
    }
    [lastSubmittedCmdBuf_ waitUntilCompleted];
  }

  void requestNextFrameCapture() { captureNextFrame_ = true; }

  bool takeCapturedFrame(std::vector<std::uint8_t>& out, std::uint32_t& width, std::uint32_t& height) {
    if (!captureBuffer_ || captureWidth_ == 0 || captureHeight_ == 0) {
      return false;
    }
    std::size_t const size = static_cast<std::size_t>(captureBytesPerRow_) * captureHeight_;
    out.resize(size);
    std::memcpy(out.data(), [captureBuffer_ contents], size);
    width = captureWidth_;
    height = captureHeight_;
    captureBuffer_ = nil;
    captureBytesPerRow_ = 0;
    captureWidth_ = 0;
    captureHeight_ = 0;
    return true;
  }
};

std::unique_ptr<Canvas> createMetalCanvas(Window* window, void* caMetalLayer, unsigned int handle,
                                          TextSystem& textSystem,
                                          std::function<void()> requestRedraw) {
  return std::make_unique<MetalCanvas>(window, (__bridge CAMetalLayer*)caMetalLayer, handle,
                                       textSystem, std::move(requestRedraw));
}

std::unique_ptr<Canvas> createMetalRenderTargetCanvas(MetalRenderTargetSpec const& spec,
                                                      TextSystem& textSystem) {
  return std::make_unique<MetalCanvas>(spec, textSystem);
}

void setSyncPresentForCanvas(Canvas* canvas, bool sync) {
  if (!canvas) {
    return;
  }
  if (auto* mc = dynamic_cast<MetalCanvas*>(canvas)) {
    mc->setSyncPresent(sync);
  }
}

void waitForCanvasLastPresentComplete(Canvas* canvas) {
  if (!canvas) {
    return;
  }
  if (auto* mc = dynamic_cast<MetalCanvas*>(canvas)) {
    mc->waitForLastPresentComplete();
  }
}

std::shared_ptr<Image> rasterizeToImage(Canvas& canvas, Size logicalSize,
                                        RasterizeDrawCallback draw, float dpiScale) {
  if (!draw || logicalSize.width <= 0.f || logicalSize.height <= 0.f) {
    return nullptr;
  }
  auto* mc = dynamic_cast<MetalCanvas*>(&canvas);
  if (!mc) {
    return nullptr;
  }
  float const resolvedDpiScale = dpiScale > 0.f ? dpiScale : mc->dpiScale();
  return mc->rasterizeWithRenderTarget(logicalSize, draw, resolvedDpiScale);
}

bool beginRecordedOpsCaptureForCanvas(Canvas* canvas, MetalFrameRecorder* target) {
  if (!canvas || !target) {
    return false;
  }
  if (auto* mc = dynamic_cast<MetalCanvas*>(canvas)) {
    mc->beginRecordedOpsCapture(target);
    return true;
  }
  return false;
}

void endRecordedOpsCaptureForCanvas(Canvas* canvas) {
  if (!canvas) {
    return;
  }
  if (auto* mc = dynamic_cast<MetalCanvas*>(canvas)) {
    mc->endRecordedOpsCapture();
  }
}

void replayRecordedOpsForCanvas(Canvas* canvas, MetalFrameRecorder const& recorded, MetalRecorderSlice const& slice) {
  if (!canvas) {
    return;
  }
  if (auto* mc = dynamic_cast<MetalCanvas*>(canvas)) {
    mc->replayRecordedOps(recorded, slice);
  }
}

bool replayRecordedLocalOpsForCanvas(Canvas* canvas, MetalFrameRecorder const& recorded, MetalRecorderSlice const& slice) {
  if (!canvas) {
    return false;
  }
  if (auto* mc = dynamic_cast<MetalCanvas*>(canvas)) {
    return mc->replayRecordedLocalOps(recorded, slice);
  }
  return false;
}

float dpiScaleForCanvas(Canvas* canvas) {
  if (!canvas) {
    return 1.f;
  }
  if (auto* mc = dynamic_cast<MetalCanvas*>(canvas)) {
    return mc->dpiScale();
  }
  return 1.f;
}

bool requestNextFrameCaptureForCanvas(Canvas* canvas) {
  if (!canvas) {
    return false;
  }
  if (auto* mc = dynamic_cast<MetalCanvas*>(canvas)) {
    mc->requestNextFrameCapture();
    return true;
  }
  return false;
}

bool takeCapturedFrameForCanvas(Canvas* canvas, std::vector<std::uint8_t>& out, std::uint32_t& width,
                                std::uint32_t& height) {
  if (!canvas) {
    return false;
  }
  if (auto* mc = dynamic_cast<MetalCanvas*>(canvas)) {
    return mc->takeCapturedFrame(out, width, height);
  }
  return false;
}

} // namespace lambdaui
