#pragma once

#include <Lambda/Graphics/Styles.hpp>

#include "Graphics/Metal/MetalCanvasTypes.hpp"
#include "Graphics/PathFlattener.hpp"

#include <cstdint>
#include <array>
#include <unordered_map>
#include <vector>

#if defined(__APPLE__)
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#endif

namespace lambdaui {

struct MetalFrameRecorder;

/**
 * Metal device, pipelines, static geometry, and per-frame GPU arenas for instance/path data.
 * Owned by `MetalCanvas`; keeps buffer pools co-located with the PSOs that consume them.
 */
class MetalDeviceResources {
public:
  static constexpr std::size_t kFramesInFlight = 3;

  explicit MetalDeviceResources(CAMetalLayer* layer);
  MetalDeviceResources(id<MTLDevice> device, MTLPixelFormat pixelFormat);
  ~MetalDeviceResources();

  MetalDeviceResources(const MetalDeviceResources&) = delete;
  MetalDeviceResources& operator=(const MetalDeviceResources&) = delete;
  MetalDeviceResources(MetalDeviceResources&&) = delete;
  MetalDeviceResources& operator=(MetalDeviceResources&&) = delete;

  CAMetalLayer* layer() const { return layer_; }
  MTLPixelFormat pixelFormat() const { return pixelFormat_; }
  id<MTLDevice> device() const { return device_; }
  id<MTLCommandQueue> queue() const { return queue_; }
  id<MTLRenderPipelineState> rectPSO(BlendMode mode, std::uint32_t sampleCount = 1);
  id<MTLRenderPipelineState> linePSO(BlendMode mode, std::uint32_t sampleCount = 1);
  id<MTLRenderPipelineState> pathPSO(BlendMode mode, std::uint32_t sampleCount = 1);
  id<MTLRenderPipelineState> glyphPSO(BlendMode mode, std::uint32_t sampleCount = 1);
  id<MTLRenderPipelineState> imagePSO(BlendMode mode, std::uint32_t sampleCount = 1);
  id<MTLRenderPipelineState> backdropPSO(std::uint32_t sampleCount = 1);
  id<MTLRenderPipelineState> backdropBlurPSO();
  id<MTLSamplerState> linearSampler() const { return linearSampler_; }
  id<MTLSamplerState> repeatSampler() const { return repeatSampler_; }
  id<MTLBuffer> quadBuffer() const { return quadBuffer_; }
  void advanceFrame();

  std::uint32_t uploadRectOps(std::vector<MetalRectOp>& ops);
  std::uint32_t uploadImageOps(std::vector<MetalImageOp>& ops);
  /// Copy path vertices into the path arena (no-op if empty).
  void uploadPathVertices(const std::vector<PathVertex>& pathVerts);

  void uploadGlyphVertices(MetalFrameRecorder const& recorder);
  void reserveDrawStateBuffers(std::uint32_t uniformCount, std::uint32_t roundedClipCount);

  id<MTLBuffer> instanceArenaBuffer() const { return instanceArenas_[currentFrameIndex_]; }
  id<MTLBuffer> imageInstanceArenaBuffer() const { return imageInstanceArenas_[currentFrameIndex_]; }
  id<MTLBuffer> pathVertexArenaBuffer() const { return pathVertexArenas_[currentFrameIndex_]; }
  id<MTLBuffer> glyphVertexArenaBuffer() const { return glyphVertexArenas_[currentFrameIndex_]; }
  id<MTLBuffer> drawUniformArenaBuffer() const { return drawUniformArenas_[currentFrameIndex_]; }
  id<MTLBuffer> roundedClipArenaBuffer() const { return roundedClipArenas_[currentFrameIndex_]; }
  std::size_t currentFrameIndex() const noexcept { return currentFrameIndex_; }

private:
  CAMetalLayer* layer_{nil};
  MTLPixelFormat pixelFormat_{MTLPixelFormatBGRA8Unorm};
  id<MTLDevice> device_{nil};
  id<MTLCommandQueue> queue_{nil};
  id<MTLLibrary> lib_{nil};
  std::unordered_map<std::uint32_t, id<MTLRenderPipelineState>> rectPSOCache_;
  std::unordered_map<std::uint32_t, id<MTLRenderPipelineState>> linePSOCache_;
  std::unordered_map<std::uint32_t, id<MTLRenderPipelineState>> pathPSOCache_;
  std::unordered_map<std::uint32_t, id<MTLRenderPipelineState>> glyphPSOCache_;
  std::unordered_map<std::uint32_t, id<MTLRenderPipelineState>> imagePSOCache_;
  std::unordered_map<std::uint32_t, id<MTLRenderPipelineState>> backdropPSOCache_;
  id<MTLRenderPipelineState> backdropBlurPSO_{nil};
  id<MTLBuffer> quadBuffer_{nil};
  id<MTLSamplerState> linearSampler_{nil};
  id<MTLSamplerState> repeatSampler_{nil};

  std::array<id<MTLBuffer>, kFramesInFlight> instanceArenas_{};
  std::array<std::uint32_t, kFramesInFlight> instanceArenaCapacityInstanceCounts_{};
  std::array<id<MTLBuffer>, kFramesInFlight> imageInstanceArenas_{};
  std::array<std::uint32_t, kFramesInFlight> imageInstanceArenaCapacities_{};
  std::array<id<MTLBuffer>, kFramesInFlight> pathVertexArenas_{};
  std::array<std::uint32_t, kFramesInFlight> pathVertexArenaCapacityBytes_{};
  std::array<id<MTLBuffer>, kFramesInFlight> glyphVertexArenas_{};
  std::array<std::uint32_t, kFramesInFlight> glyphVertexArenaCapacityBytes_{};
  std::array<id<MTLBuffer>, kFramesInFlight> drawUniformArenas_{};
  std::array<std::uint32_t, kFramesInFlight> drawUniformArenaCapacities_{};
  std::array<id<MTLBuffer>, kFramesInFlight> roundedClipArenas_{};
  std::array<std::uint32_t, kFramesInFlight> roundedClipArenaCapacities_{};
  std::size_t currentFrameIndex_ = kFramesInFlight - 1;

  void ensureInstanceArenaCapacity(std::uint32_t instanceCount);
  void ensureImageInstanceArenaCapacity(std::uint32_t instanceCount);
  void ensurePathVertexArenaCapacity(std::uint32_t byteCount);
  void ensureGlyphVertexArenaCapacity(std::uint32_t byteCount);
  void ensureDrawUniformArenaCapacity(std::uint32_t uniformCount);
  void ensureRoundedClipArenaCapacity(std::uint32_t roundedClipCount);
};

} // namespace lambdaui
