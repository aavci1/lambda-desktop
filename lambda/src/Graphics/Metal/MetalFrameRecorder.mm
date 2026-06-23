#import <Metal/Metal.h>

#include "Graphics/Metal/MetalFrameRecorder.hpp"

namespace lambdaui {

MetalFrameRecorder::~MetalFrameRecorder() {
  clear();
}

MetalFrameRecorder::MetalFrameRecorder(MetalFrameRecorder&& other) noexcept
    : rectOps(std::move(other.rectOps)),
      imageOps(std::move(other.imageOps)),
      pathOps(std::move(other.pathOps)),
      glyphOps(std::move(other.glyphOps)),
      backdropBlurOps(std::move(other.backdropBlurOps)),
      opOrder(std::move(other.opOrder)),
      pathVerts(std::move(other.pathVerts)),
      glyphVerts(std::move(other.glyphVerts)),
      glyphVertexSources(std::move(other.glyphVertexSources)),
      glyphVertexCount(other.glyphVertexCount),
      preparedRectInstanceBuffer(other.preparedRectInstanceBuffer),
      preparedRectInstanceCapacity(other.preparedRectInstanceCapacity),
      preparedImageInstanceBuffer(other.preparedImageInstanceBuffer),
      preparedImageInstanceCapacity(other.preparedImageInstanceCapacity),
      preparedPathVertexBuffer(other.preparedPathVertexBuffer),
      preparedPathVertexCapacity(other.preparedPathVertexCapacity),
      preparedGlyphVertexBuffer(other.preparedGlyphVertexBuffer),
      preparedGlyphVertexCapacity(other.preparedGlyphVertexCapacity),
      glyphAtlasGeneration(other.glyphAtlasGeneration) {
  other.rectOps.clear();
  other.imageOps.clear();
  other.pathOps.clear();
  other.glyphOps.clear();
  other.backdropBlurOps.clear();
  other.opOrder.clear();
  other.pathVerts.clear();
  other.glyphVerts.clear();
  other.glyphVertexSources.clear();
  other.glyphVertexCount = 0;
  other.preparedRectInstanceBuffer = nullptr;
  other.preparedRectInstanceCapacity = 0;
  other.preparedImageInstanceBuffer = nullptr;
  other.preparedImageInstanceCapacity = 0;
  other.preparedPathVertexBuffer = nullptr;
  other.preparedPathVertexCapacity = 0;
  other.preparedGlyphVertexBuffer = nullptr;
  other.preparedGlyphVertexCapacity = 0;
  other.glyphAtlasGeneration = 0;
}

MetalFrameRecorder& MetalFrameRecorder::operator=(MetalFrameRecorder&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  clear();
  rectOps = std::move(other.rectOps);
  imageOps = std::move(other.imageOps);
  pathOps = std::move(other.pathOps);
  glyphOps = std::move(other.glyphOps);
  backdropBlurOps = std::move(other.backdropBlurOps);
  opOrder = std::move(other.opOrder);
  pathVerts = std::move(other.pathVerts);
  glyphVerts = std::move(other.glyphVerts);
  glyphVertexSources = std::move(other.glyphVertexSources);
  glyphVertexCount = other.glyphVertexCount;
  preparedRectInstanceBuffer = other.preparedRectInstanceBuffer;
  preparedRectInstanceCapacity = other.preparedRectInstanceCapacity;
  preparedImageInstanceBuffer = other.preparedImageInstanceBuffer;
  preparedImageInstanceCapacity = other.preparedImageInstanceCapacity;
  preparedPathVertexBuffer = other.preparedPathVertexBuffer;
  preparedPathVertexCapacity = other.preparedPathVertexCapacity;
  preparedGlyphVertexBuffer = other.preparedGlyphVertexBuffer;
  preparedGlyphVertexCapacity = other.preparedGlyphVertexCapacity;
  glyphAtlasGeneration = other.glyphAtlasGeneration;
  other.rectOps.clear();
  other.imageOps.clear();
  other.pathOps.clear();
  other.glyphOps.clear();
  other.backdropBlurOps.clear();
  other.opOrder.clear();
  other.pathVerts.clear();
  other.glyphVerts.clear();
  other.glyphVertexSources.clear();
  other.glyphVertexCount = 0;
  other.preparedRectInstanceBuffer = nullptr;
  other.preparedRectInstanceCapacity = 0;
  other.preparedImageInstanceBuffer = nullptr;
  other.preparedImageInstanceCapacity = 0;
  other.preparedPathVertexBuffer = nullptr;
  other.preparedPathVertexCapacity = 0;
  other.preparedGlyphVertexBuffer = nullptr;
  other.preparedGlyphVertexCapacity = 0;
  other.glyphAtlasGeneration = 0;
  return *this;
}

void MetalFrameRecorder::clear() {
  for (auto& op : imageOps) {
    if (op.texture) {
      (void)(__bridge_transfer id<MTLTexture>)op.texture;
    }
    op.texture = nullptr;
  }
  if (preparedGlyphVertexBuffer) {
    (void)(__bridge_transfer id<MTLBuffer>)preparedGlyphVertexBuffer;
    preparedGlyphVertexBuffer = nullptr;
    preparedGlyphVertexCapacity = 0;
  }
  if (preparedPathVertexBuffer) {
    (void)(__bridge_transfer id<MTLBuffer>)preparedPathVertexBuffer;
    preparedPathVertexBuffer = nullptr;
    preparedPathVertexCapacity = 0;
  }
  if (preparedImageInstanceBuffer) {
    (void)(__bridge_transfer id<MTLBuffer>)preparedImageInstanceBuffer;
    preparedImageInstanceBuffer = nullptr;
    preparedImageInstanceCapacity = 0;
  }
  if (preparedRectInstanceBuffer) {
    (void)(__bridge_transfer id<MTLBuffer>)preparedRectInstanceBuffer;
    preparedRectInstanceBuffer = nullptr;
    preparedRectInstanceCapacity = 0;
  }
  rectOps.clear();
  imageOps.clear();
  pathOps.clear();
  glyphOps.clear();
  backdropBlurOps.clear();
  opOrder.clear();
  pathVerts.clear();
  glyphVerts.clear();
  glyphVertexSources.clear();
  glyphVertexCount = 0;
  glyphAtlasGeneration = 0;
}

} // namespace lambdaui
