#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#import <simd/simd.h>

#include "Graphics/Metal/MetalDeviceResources.hpp"
#include "Graphics/Metal/MetalFrameRecorder.hpp"
#include "Graphics/Metal/MetalShaderLibrary.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <stdexcept>

namespace lambda {

namespace {

constexpr NSUInteger kQuadStripCount = 4;

std::uint32_t blendModeKey(BlendMode m) { return static_cast<std::uint32_t>(static_cast<int>(m)); }

std::uint32_t normalizedSampleCount(std::uint32_t sampleCount) { return std::max(sampleCount, 1u); }

std::uint32_t pipelineKey(BlendMode mode, std::uint32_t sampleCount, std::uint32_t salt = 0) {
  return blendModeKey(mode) ^ (normalizedSampleCount(sampleCount) << 24) ^ salt;
}

void setPipelineSampleCount(MTLRenderPipelineDescriptor* descriptor, std::uint32_t sampleCount) {
  NSUInteger const count = static_cast<NSUInteger>(normalizedSampleCount(sampleCount));
#if defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 130000
  if (@available(macOS 13.0, *)) {
    descriptor.rasterSampleCount = count;
    return;
  }
#endif
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  descriptor.sampleCount = count;
#pragma clang diagnostic pop
}

void setSrcOverBlend(MTLRenderPipelineColorAttachmentDescriptor* att) {
  att.blendingEnabled = YES;
  att.rgbBlendOperation = MTLBlendOperationAdd;
  att.alphaBlendOperation = MTLBlendOperationAdd;
  // Fragment output is premultiplied; matches glyph pipeline and Porter–Duff factors below.
  att.sourceRGBBlendFactor = MTLBlendFactorOne;
  att.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
  att.sourceAlphaBlendFactor = MTLBlendFactorOne;
  att.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
}

void applyBlendModeToAttachment(MTLRenderPipelineColorAttachmentDescriptor* att, BlendMode mode) {
  switch (mode) {
  case BlendMode::Normal:
  case BlendMode::SrcOver:
    setSrcOverBlend(att);
    return;
  case BlendMode::Multiply:
    att.blendingEnabled = YES;
    att.rgbBlendOperation = MTLBlendOperationAdd;
    att.alphaBlendOperation = MTLBlendOperationAdd;
    att.sourceRGBBlendFactor = MTLBlendFactorDestinationColor;
    att.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    att.sourceAlphaBlendFactor = MTLBlendFactorDestinationAlpha;
    att.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    return;
  case BlendMode::Screen:
    att.blendingEnabled = YES;
    att.rgbBlendOperation = MTLBlendOperationAdd;
    att.alphaBlendOperation = MTLBlendOperationAdd;
    att.sourceRGBBlendFactor = MTLBlendFactorOneMinusDestinationColor;
    att.destinationRGBBlendFactor = MTLBlendFactorOne;
    att.sourceAlphaBlendFactor = MTLBlendFactorOne;
    att.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    return;
  case BlendMode::Darken:
    att.blendingEnabled = YES;
    att.rgbBlendOperation = MTLBlendOperationMin;
    att.alphaBlendOperation = MTLBlendOperationAdd;
    att.sourceRGBBlendFactor = MTLBlendFactorOne;
    att.destinationRGBBlendFactor = MTLBlendFactorOne;
    att.sourceAlphaBlendFactor = MTLBlendFactorOne;
    att.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    return;
  case BlendMode::Lighten:
    att.blendingEnabled = YES;
    att.rgbBlendOperation = MTLBlendOperationMax;
    att.alphaBlendOperation = MTLBlendOperationAdd;
    att.sourceRGBBlendFactor = MTLBlendFactorOne;
    att.destinationRGBBlendFactor = MTLBlendFactorOne;
    att.sourceAlphaBlendFactor = MTLBlendFactorOne;
    att.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    return;
  case BlendMode::Clear:
    att.blendingEnabled = YES;
    att.rgbBlendOperation = MTLBlendOperationAdd;
    att.alphaBlendOperation = MTLBlendOperationAdd;
    att.sourceRGBBlendFactor = MTLBlendFactorZero;
    att.destinationRGBBlendFactor = MTLBlendFactorZero;
    att.sourceAlphaBlendFactor = MTLBlendFactorZero;
    att.destinationAlphaBlendFactor = MTLBlendFactorZero;
    return;
  case BlendMode::Src:
    att.blendingEnabled = YES;
    att.rgbBlendOperation = MTLBlendOperationAdd;
    att.alphaBlendOperation = MTLBlendOperationAdd;
    att.sourceRGBBlendFactor = MTLBlendFactorOne;
    att.destinationRGBBlendFactor = MTLBlendFactorZero;
    att.sourceAlphaBlendFactor = MTLBlendFactorOne;
    att.destinationAlphaBlendFactor = MTLBlendFactorZero;
    return;
  case BlendMode::Dst:
    att.blendingEnabled = YES;
    att.rgbBlendOperation = MTLBlendOperationAdd;
    att.alphaBlendOperation = MTLBlendOperationAdd;
    att.sourceRGBBlendFactor = MTLBlendFactorZero;
    att.destinationRGBBlendFactor = MTLBlendFactorOne;
    att.sourceAlphaBlendFactor = MTLBlendFactorZero;
    att.destinationAlphaBlendFactor = MTLBlendFactorOne;
    return;
  case BlendMode::DstOver:
    att.blendingEnabled = YES;
    att.rgbBlendOperation = MTLBlendOperationAdd;
    att.alphaBlendOperation = MTLBlendOperationAdd;
    att.sourceRGBBlendFactor = MTLBlendFactorOneMinusDestinationAlpha;
    att.destinationRGBBlendFactor = MTLBlendFactorOne;
    att.sourceAlphaBlendFactor = MTLBlendFactorOneMinusDestinationAlpha;
    att.destinationAlphaBlendFactor = MTLBlendFactorOne;
    return;
  case BlendMode::SrcIn:
    att.blendingEnabled = YES;
    att.rgbBlendOperation = MTLBlendOperationAdd;
    att.alphaBlendOperation = MTLBlendOperationAdd;
    att.sourceRGBBlendFactor = MTLBlendFactorDestinationAlpha;
    att.destinationRGBBlendFactor = MTLBlendFactorZero;
    att.sourceAlphaBlendFactor = MTLBlendFactorDestinationAlpha;
    att.destinationAlphaBlendFactor = MTLBlendFactorZero;
    return;
  case BlendMode::DstIn:
    att.blendingEnabled = YES;
    att.rgbBlendOperation = MTLBlendOperationAdd;
    att.alphaBlendOperation = MTLBlendOperationAdd;
    att.sourceRGBBlendFactor = MTLBlendFactorZero;
    att.destinationRGBBlendFactor = MTLBlendFactorSourceAlpha;
    att.sourceAlphaBlendFactor = MTLBlendFactorZero;
    att.destinationAlphaBlendFactor = MTLBlendFactorSourceAlpha;
    return;
  case BlendMode::SrcOut:
    att.blendingEnabled = YES;
    att.rgbBlendOperation = MTLBlendOperationAdd;
    att.alphaBlendOperation = MTLBlendOperationAdd;
    att.sourceRGBBlendFactor = MTLBlendFactorOneMinusDestinationAlpha;
    att.destinationRGBBlendFactor = MTLBlendFactorZero;
    att.sourceAlphaBlendFactor = MTLBlendFactorOneMinusDestinationAlpha;
    att.destinationAlphaBlendFactor = MTLBlendFactorZero;
    return;
  case BlendMode::DstOut:
    att.blendingEnabled = YES;
    att.rgbBlendOperation = MTLBlendOperationAdd;
    att.alphaBlendOperation = MTLBlendOperationAdd;
    att.sourceRGBBlendFactor = MTLBlendFactorZero;
    att.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    att.sourceAlphaBlendFactor = MTLBlendFactorZero;
    att.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    return;
  default:
    // Overlay / soft-light / non-separable / Xor / SrcAtop / DstAtop: approximate as Normal until shader blend.
    setSrcOverBlend(att);
    return;
  }
}

id<MTLRenderPipelineState> makePipeline(id<MTLDevice> device, id<MTLLibrary> lib, NSString* vert, NSString* frag,
                                        MTLPixelFormat colorFormat, BlendMode blendMode,
                                        std::uint32_t sampleCount) {
  id<MTLFunction> vf = [lib newFunctionWithName:vert];
  id<MTLFunction> ff = [lib newFunctionWithName:frag];
  if (!vf || !ff) {
    return nil;
  }
  MTLRenderPipelineDescriptor* d = [[MTLRenderPipelineDescriptor alloc] init];
  d.vertexFunction = vf;
  d.fragmentFunction = ff;
  setPipelineSampleCount(d, sampleCount);
  d.colorAttachments[0].pixelFormat = colorFormat;
  applyBlendModeToAttachment(d.colorAttachments[0], blendMode);
  NSError* err = nil;
  id<MTLRenderPipelineState> pso = [device newRenderPipelineStateWithDescriptor:d error:&err];
  if (!pso && err) {
    NSLog(@"Lambda MetalDeviceResources: pipeline error: %@", err);
  }
  return pso;
}

id<MTLRenderPipelineState> makePathPipeline(id<MTLDevice> device, id<MTLLibrary> lib, MTLPixelFormat colorFormat,
                                            BlendMode blendMode, std::uint32_t sampleCount) {
  id<MTLFunction> vf = [lib newFunctionWithName:@"path_vert"];
  id<MTLFunction> ff = [lib newFunctionWithName:@"path_frag"];
  if (!vf || !ff) {
    return nil;
  }
  MTLVertexDescriptor* vd = [[MTLVertexDescriptor alloc] init];
  vd.attributes[0].format = MTLVertexFormatFloat2;
  vd.attributes[0].offset = 0;
  vd.attributes[0].bufferIndex = 0;
  vd.attributes[1].format = MTLVertexFormatFloat4;
  vd.attributes[1].offset = 8;
  vd.attributes[1].bufferIndex = 0;
  vd.attributes[2].format = MTLVertexFormatFloat2;
  vd.attributes[2].offset = 24;
  vd.attributes[2].bufferIndex = 0;
  vd.layouts[0].stride = 32;
  vd.layouts[0].stepRate = 1;
  vd.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

  MTLRenderPipelineDescriptor* d = [[MTLRenderPipelineDescriptor alloc] init];
  d.vertexFunction = vf;
  d.fragmentFunction = ff;
  d.vertexDescriptor = vd;
  setPipelineSampleCount(d, sampleCount);
  d.colorAttachments[0].pixelFormat = colorFormat;
  applyBlendModeToAttachment(d.colorAttachments[0], blendMode);
  NSError* err = nil;
  id<MTLRenderPipelineState> pso = [device newRenderPipelineStateWithDescriptor:d error:&err];
  if (!pso && err) {
    NSLog(@"Lambda MetalDeviceResources: path pipeline error: %@", err);
  }
  return pso;
}

id<MTLRenderPipelineState> makeGlyphPipeline(id<MTLDevice> device, id<MTLLibrary> lib, MTLPixelFormat colorFormat, BlendMode blendMode,
                                             std::uint32_t sampleCount) {
  (void)blendMode;

  id<MTLFunction> vf = [lib newFunctionWithName:@"glyph_vert"];
  id<MTLFunction> ff = [lib newFunctionWithName:@"glyph_frag"];
  if (!vf || !ff) {
    return nil;
  }
  MTLVertexDescriptor* vd = [[MTLVertexDescriptor alloc] init];
  vd.attributes[0].format = MTLVertexFormatFloat2;
  vd.attributes[0].offset = 0;
  vd.attributes[0].bufferIndex = 0;
  vd.attributes[1].format = MTLVertexFormatFloat2;
  vd.attributes[1].offset = 8;
  vd.attributes[1].bufferIndex = 0;
  vd.attributes[2].format = MTLVertexFormatFloat4;
  vd.attributes[2].offset = 16;
  vd.attributes[2].bufferIndex = 0;
  // Must match `sizeof(MetalGlyphVertex)` (pos + uv + premul color).
  vd.layouts[0].stride = sizeof(MetalGlyphVertex);
  vd.layouts[0].stepRate = 1;
  vd.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

  MTLRenderPipelineDescriptor* d = [[MTLRenderPipelineDescriptor alloc] init];
  d.vertexFunction = vf;
  d.fragmentFunction = ff;
  d.vertexDescriptor = vd;
  setPipelineSampleCount(d, sampleCount);
  d.colorAttachments[0].pixelFormat = colorFormat;
  // Glyphs are composited as premultiplied alpha: source rgb is already multiplied by alpha,
  // so use (One, OneMinusSrcAlpha) rather than the straight-alpha (SourceAlpha, OneMinusSrcAlpha).
  auto* att = d.colorAttachments[0];
  att.blendingEnabled = YES;
  att.rgbBlendOperation = MTLBlendOperationAdd;
  att.alphaBlendOperation = MTLBlendOperationAdd;
  att.sourceRGBBlendFactor = MTLBlendFactorOne;
  att.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
  att.sourceAlphaBlendFactor = MTLBlendFactorOne;
  att.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
  NSError* err = nil;
  id<MTLRenderPipelineState> pso = [device newRenderPipelineStateWithDescriptor:d error:&err];
  if (!pso && err) {
    NSLog(@"Lambda MetalDeviceResources: glyph pipeline error: %@", err);
  }
  return pso;
}

} // namespace

id<MTLRenderPipelineState> MetalDeviceResources::rectPSO(BlendMode mode, std::uint32_t sampleCount) {
  sampleCount = normalizedSampleCount(sampleCount);
  const std::uint32_t k = pipelineKey(mode, sampleCount);
  if (auto it = rectPSOCache_.find(k); it != rectPSOCache_.end()) {
    return it->second;
  }
  id<MTLRenderPipelineState> pso =
      makePipeline(device_, lib_, @"rect_sdf_vert", @"rect_sdf_frag", pixelFormat_, mode, sampleCount);
  if (!pso) {
    throw std::runtime_error("MetalDeviceResources: rect pipeline creation failed");
  }
  rectPSOCache_[k] = pso;
  return pso;
}

id<MTLRenderPipelineState> MetalDeviceResources::linePSO(BlendMode mode, std::uint32_t sampleCount) {
  sampleCount = normalizedSampleCount(sampleCount);
  const std::uint32_t k = pipelineKey(mode, sampleCount);
  if (auto it = linePSOCache_.find(k); it != linePSOCache_.end()) {
    return it->second;
  }
  id<MTLRenderPipelineState> pso =
      makePipeline(device_, lib_, @"line_sdf_vert", @"line_sdf_frag", pixelFormat_, mode, sampleCount);
  if (!pso) {
    throw std::runtime_error("MetalDeviceResources: line pipeline creation failed");
  }
  linePSOCache_[k] = pso;
  return pso;
}

id<MTLRenderPipelineState> MetalDeviceResources::pathPSO(BlendMode mode, std::uint32_t sampleCount) {
  sampleCount = normalizedSampleCount(sampleCount);
  const std::uint32_t k = pipelineKey(mode, sampleCount);
  if (auto it = pathPSOCache_.find(k); it != pathPSOCache_.end()) {
    return it->second;
  }
  id<MTLRenderPipelineState> pso = makePathPipeline(device_, lib_, pixelFormat_, mode, sampleCount);
  if (!pso) {
    throw std::runtime_error("MetalDeviceResources: path pipeline creation failed");
  }
  pathPSOCache_[k] = pso;
  return pso;
}

id<MTLRenderPipelineState> MetalDeviceResources::glyphPSO(BlendMode mode, std::uint32_t sampleCount) {
  sampleCount = normalizedSampleCount(sampleCount);
  // Bump when glyph vertex layout / stride changes so cached PSOs are not reused incorrectly.
  const std::uint32_t k = pipelineKey(mode, sampleCount, 0x47595049u); // 'GYPI' — v2: premul blend
  if (auto it = glyphPSOCache_.find(k); it != glyphPSOCache_.end()) {
    return it->second;
  }
  id<MTLRenderPipelineState> pso = makeGlyphPipeline(device_, lib_, pixelFormat_, mode, sampleCount);
  if (!pso) {
    throw std::runtime_error("MetalDeviceResources: glyph pipeline creation failed");
  }
  glyphPSOCache_[k] = pso;
  return pso;
}

id<MTLRenderPipelineState> MetalDeviceResources::imagePSO(BlendMode mode, std::uint32_t sampleCount) {
  sampleCount = normalizedSampleCount(sampleCount);
  const std::uint32_t k = pipelineKey(mode, sampleCount, 0x494d4749u); // 'IMGI'
  if (auto it = imagePSOCache_.find(k); it != imagePSOCache_.end()) {
    return it->second;
  }
  id<MTLRenderPipelineState> pso =
      makePipeline(device_, lib_, @"image_sdf_vert", @"image_sdf_frag", pixelFormat_, mode, sampleCount);
  if (!pso) {
    throw std::runtime_error("MetalDeviceResources: image pipeline creation failed");
  }
  imagePSOCache_[k] = pso;
  return pso;
}

id<MTLRenderPipelineState> MetalDeviceResources::backdropPSO(std::uint32_t sampleCount) {
  sampleCount = normalizedSampleCount(sampleCount);
  const std::uint32_t k = pipelineKey(BlendMode::SrcOver, sampleCount, 0x424c5552u); // 'BLUR'
  if (auto it = backdropPSOCache_.find(k); it != backdropPSOCache_.end()) {
    return it->second;
  }
  id<MTLRenderPipelineState> pso =
      makePipeline(device_, lib_, @"backdrop_vert", @"backdrop_frag", pixelFormat_, BlendMode::SrcOver,
                   sampleCount);
  if (!pso) {
    throw std::runtime_error("MetalDeviceResources: backdrop pipeline creation failed");
  }
  backdropPSOCache_[k] = pso;
  return pso;
}

id<MTLRenderPipelineState> MetalDeviceResources::backdropBlurPSO() {
  if (backdropBlurPSO_) {
    return backdropBlurPSO_;
  }
  backdropBlurPSO_ =
      makePipeline(device_, lib_, @"backdrop_vert", @"backdrop_gaussian_frag", pixelFormat_,
                   BlendMode::SrcOver, 1);
  if (!backdropBlurPSO_) {
    throw std::runtime_error("MetalDeviceResources: backdrop blur pipeline creation failed");
  }
  return backdropBlurPSO_;
}

MetalDeviceResources::MetalDeviceResources(CAMetalLayer* layer) : layer_(layer) {
  device_ = layer_.device ? layer_.device : MTLCreateSystemDefaultDevice();
  layer_.device = device_;
  layer_.pixelFormat = MTLPixelFormatBGRA8Unorm;
  layer_.framebufferOnly = NO;
  pixelFormat_ = layer_.pixelFormat;
  queue_ = [device_ newCommandQueue];

  lib_ = lambda::detail::lambdaLoadShaderLibrary(device_);

  static const vector_float2 kQuadStrip[kQuadStripCount] = {
      {-1.f, -1.f},
      {1.f, -1.f},
      {-1.f, 1.f},
      {1.f, 1.f},
  };
  quadBuffer_ = [device_ newBufferWithBytes:kQuadStrip length:sizeof(kQuadStrip) options:MTLResourceStorageModeShared];

  MTLSamplerDescriptor* sd = [MTLSamplerDescriptor new];
  sd.minFilter = MTLSamplerMinMagFilterLinear;
  sd.magFilter = MTLSamplerMinMagFilterLinear;
  sd.sAddressMode = MTLSamplerAddressModeClampToEdge;
  sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
  linearSampler_ = [device_ newSamplerStateWithDescriptor:sd];

  MTLSamplerDescriptor* rd = [MTLSamplerDescriptor new];
  rd.minFilter = MTLSamplerMinMagFilterLinear;
  rd.magFilter = MTLSamplerMinMagFilterLinear;
  rd.sAddressMode = MTLSamplerAddressModeRepeat;
  rd.tAddressMode = MTLSamplerAddressModeRepeat;
  repeatSampler_ = [device_ newSamplerStateWithDescriptor:rd];
}

MetalDeviceResources::MetalDeviceResources(id<MTLDevice> device, MTLPixelFormat pixelFormat)
    : pixelFormat_(pixelFormat == MTLPixelFormatInvalid ? MTLPixelFormatBGRA8Unorm : pixelFormat),
      device_(device ? device : MTLCreateSystemDefaultDevice()) {
  queue_ = [device_ newCommandQueue];

  lib_ = lambda::detail::lambdaLoadShaderLibrary(device_);

  static const vector_float2 kQuadStrip[kQuadStripCount] = {
      {-1.f, -1.f},
      {1.f, -1.f},
      {-1.f, 1.f},
      {1.f, 1.f},
  };
  quadBuffer_ = [device_ newBufferWithBytes:kQuadStrip length:sizeof(kQuadStrip) options:MTLResourceStorageModeShared];

  MTLSamplerDescriptor* sd = [MTLSamplerDescriptor new];
  sd.minFilter = MTLSamplerMinMagFilterLinear;
  sd.magFilter = MTLSamplerMinMagFilterLinear;
  sd.sAddressMode = MTLSamplerAddressModeClampToEdge;
  sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
  linearSampler_ = [device_ newSamplerStateWithDescriptor:sd];

  MTLSamplerDescriptor* rd = [MTLSamplerDescriptor new];
  rd.minFilter = MTLSamplerMinMagFilterLinear;
  rd.magFilter = MTLSamplerMinMagFilterLinear;
  rd.sAddressMode = MTLSamplerAddressModeRepeat;
  rd.tAddressMode = MTLSamplerAddressModeRepeat;
  repeatSampler_ = [device_ newSamplerStateWithDescriptor:rd];
}

MetalDeviceResources::~MetalDeviceResources() = default;

void MetalDeviceResources::advanceFrame() {
  currentFrameIndex_ = (currentFrameIndex_ + 1) % kFramesInFlight;
}

void MetalDeviceResources::ensureInstanceArenaCapacity(std::uint32_t instanceCount) {
  if (instanceCount == 0) {
    return;
  }
  if (instanceCount <= instanceArenaCapacityInstanceCounts_[currentFrameIndex_]) {
    return;
  }
  const std::uint32_t newCap = instanceArenaCapacityInstanceCounts_[currentFrameIndex_] == 0
                                   ? instanceCount
                                   : std::max(instanceCount, instanceArenaCapacityInstanceCounts_[currentFrameIndex_] * 2);
  instanceArenas_[currentFrameIndex_] =
      [device_ newBufferWithLength:newCap * sizeof(MetalRectInstance) options:MTLResourceStorageModeShared];
  instanceArenaCapacityInstanceCounts_[currentFrameIndex_] = newCap;
}

void MetalDeviceResources::ensureImageInstanceArenaCapacity(std::uint32_t instanceCount) {
  if (instanceCount == 0) {
    return;
  }
  if (instanceCount <= imageInstanceArenaCapacities_[currentFrameIndex_]) {
    return;
  }
  const std::uint32_t newCap =
      imageInstanceArenaCapacities_[currentFrameIndex_] == 0
          ? instanceCount
          : std::max(instanceCount, imageInstanceArenaCapacities_[currentFrameIndex_] * 2);
  imageInstanceArenas_[currentFrameIndex_] =
      [device_ newBufferWithLength:newCap * sizeof(MetalImageInstance) options:MTLResourceStorageModeShared];
  imageInstanceArenaCapacities_[currentFrameIndex_] = newCap;
}

void MetalDeviceResources::ensurePathVertexArenaCapacity(std::uint32_t byteCount) {
  if (byteCount == 0) {
    return;
  }
  if (byteCount <= pathVertexArenaCapacityBytes_[currentFrameIndex_]) {
    return;
  }
  const std::uint32_t newCap =
      pathVertexArenaCapacityBytes_[currentFrameIndex_] == 0
          ? byteCount
          : std::max(byteCount, pathVertexArenaCapacityBytes_[currentFrameIndex_] * 2);
  pathVertexArenas_[currentFrameIndex_] = [device_ newBufferWithLength:newCap options:MTLResourceStorageModeShared];
  pathVertexArenaCapacityBytes_[currentFrameIndex_] = newCap;
}

void MetalDeviceResources::ensureGlyphVertexArenaCapacity(std::uint32_t byteCount) {
  if (byteCount == 0) {
    return;
  }
  if (byteCount <= glyphVertexArenaCapacityBytes_[currentFrameIndex_]) {
    return;
  }
  const std::uint32_t newCap =
      glyphVertexArenaCapacityBytes_[currentFrameIndex_] == 0
          ? byteCount
          : std::max(byteCount, glyphVertexArenaCapacityBytes_[currentFrameIndex_] * 2);
  glyphVertexArenas_[currentFrameIndex_] = [device_ newBufferWithLength:newCap options:MTLResourceStorageModeShared];
  glyphVertexArenaCapacityBytes_[currentFrameIndex_] = newCap;
}

void MetalDeviceResources::ensureDrawUniformArenaCapacity(std::uint32_t uniformCount) {
  if (uniformCount == 0) {
    return;
  }
  if (uniformCount <= drawUniformArenaCapacities_[currentFrameIndex_]) {
    return;
  }
  const std::uint32_t newCap =
      drawUniformArenaCapacities_[currentFrameIndex_] == 0
          ? uniformCount
          : std::max(uniformCount, drawUniformArenaCapacities_[currentFrameIndex_] * 2);
  drawUniformArenas_[currentFrameIndex_] =
      [device_ newBufferWithLength:newCap * sizeof(MetalDrawUniforms) options:MTLResourceStorageModeShared];
  drawUniformArenaCapacities_[currentFrameIndex_] = newCap;
}

void MetalDeviceResources::ensureRoundedClipArenaCapacity(std::uint32_t roundedClipCount) {
  if (roundedClipCount == 0) {
    return;
  }
  if (roundedClipCount <= roundedClipArenaCapacities_[currentFrameIndex_]) {
    return;
  }
  const std::uint32_t newCap =
      roundedClipArenaCapacities_[currentFrameIndex_] == 0
          ? roundedClipCount
          : std::max(roundedClipCount, roundedClipArenaCapacities_[currentFrameIndex_] * 2);
  roundedClipArenas_[currentFrameIndex_] =
      [device_ newBufferWithLength:newCap * sizeof(MetalRoundedClipStack) options:MTLResourceStorageModeShared];
  roundedClipArenaCapacities_[currentFrameIndex_] = newCap;
}

std::uint32_t MetalDeviceResources::uploadRectOps(std::vector<MetalRectOp>& ops) {
  std::uint32_t uploadCount = 0;
  for (MetalRectOp const& op : ops) {
    if (!op.externalInstanceBuffer) {
      ++uploadCount;
    }
  }
  ensureInstanceArenaCapacity(uploadCount);
  if (uploadCount == 0) {
    return 0;
  }
  auto* dst = static_cast<MetalRectInstance*>([instanceArenas_[currentFrameIndex_] contents]);
  std::uint32_t cursor = 0;
  for (std::size_t i = 0; i < ops.size(); ++i) {
    if (ops[i].externalInstanceBuffer) {
      continue;
    }
    ops[i].arenaInstanceIndex = cursor;
    dst[cursor] = ops[i].inst;
    ++cursor;
  }
  return uploadCount;
}

void MetalDeviceResources::uploadPathVertices(const std::vector<PathVertex>& pathVerts) {
  const NSUInteger pathVertBytes = static_cast<NSUInteger>(pathVerts.size() * sizeof(PathVertex));
  ensurePathVertexArenaCapacity(static_cast<std::uint32_t>(pathVertBytes));
  if (pathVertBytes == 0) {
    return;
  }
  std::memcpy([pathVertexArenas_[currentFrameIndex_] contents], pathVerts.data(), pathVertBytes);
}

void MetalDeviceResources::uploadGlyphVertices(MetalFrameRecorder const& recorder) {
  const NSUInteger bytes = static_cast<NSUInteger>(recorder.glyphVertexCount * sizeof(MetalGlyphVertex));
  ensureGlyphVertexArenaCapacity(static_cast<std::uint32_t>(bytes));
  if (bytes == 0) {
    return;
  }
  auto* dst = static_cast<MetalGlyphVertex*>([glyphVertexArenas_[currentFrameIndex_] contents]);
  std::uint32_t cursor = 0;
  for (MetalGlyphVertexSource const& source : recorder.glyphVertexSources) {
    if (source.count == 0) {
      continue;
    }
    MetalGlyphVertex const* src = source.borrowed;
    if (source.kind == MetalGlyphVertexSource::Owned) {
      src = recorder.glyphVerts.data() + source.start;
    }
    assert(src != nullptr);
    std::memcpy(dst + cursor, src, static_cast<std::size_t>(source.count) * sizeof(MetalGlyphVertex));
    cursor += source.count;
  }
  assert(cursor == recorder.glyphVertexCount);
}

std::uint32_t MetalDeviceResources::uploadImageOps(std::vector<MetalImageOp>& ops) {
  std::uint32_t uploadCount = 0;
  for (MetalImageOp const& op : ops) {
    if (!op.externalInstanceBuffer) {
      ++uploadCount;
    }
  }
  ensureImageInstanceArenaCapacity(uploadCount);
  if (uploadCount == 0) {
    return 0;
  }
  auto* dst = static_cast<MetalImageInstance*>([imageInstanceArenas_[currentFrameIndex_] contents]);
  std::uint32_t cursor = 0;
  for (std::size_t i = 0; i < ops.size(); ++i) {
    if (ops[i].externalInstanceBuffer) {
      continue;
    }
    ops[i].arenaInstanceIndex = cursor;
    dst[cursor] = ops[i].inst;
    ++cursor;
  }
  return uploadCount;
}

void MetalDeviceResources::reserveDrawStateBuffers(std::uint32_t uniformCount, std::uint32_t roundedClipCount) {
  ensureDrawUniformArenaCapacity(uniformCount);
  ensureRoundedClipArenaCapacity(roundedClipCount);
}

} // namespace lambda
