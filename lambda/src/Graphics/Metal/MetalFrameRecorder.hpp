#pragma once

#include "Graphics/Metal/MetalCanvasTypes.hpp"
#include "Graphics/PathFlattener.hpp"

#include <cstdint>
#include <vector>

namespace lambdaui {

/// Per-frame CPU-side display list: primitive ops + accumulated path mesh vertices.
struct MetalFrameRecorder {
  std::vector<MetalRectOp> rectOps;
  std::vector<MetalImageOp> imageOps;
  std::vector<MetalPathOp> pathOps;
  std::vector<MetalGlyphOp> glyphOps;
  std::vector<MetalBackdropBlurOp> backdropBlurOps;
  std::vector<MetalOpRef> opOrder;
  std::vector<PathVertex> pathVerts;
  std::vector<MetalGlyphVertex> glyphVerts;
  std::vector<MetalGlyphVertexSource> glyphVertexSources;
  std::uint32_t glyphVertexCount = 0;
  mutable void* preparedRectInstanceBuffer = nullptr;
  mutable std::uint32_t preparedRectInstanceCapacity = 0;
  mutable void* preparedImageInstanceBuffer = nullptr;
  mutable std::uint32_t preparedImageInstanceCapacity = 0;
  mutable void* preparedPathVertexBuffer = nullptr;
  mutable std::uint32_t preparedPathVertexCapacity = 0;
  mutable void* preparedGlyphVertexBuffer = nullptr;
  mutable std::uint32_t preparedGlyphVertexCapacity = 0;
  std::uint64_t glyphAtlasGeneration = 0;

  MetalFrameRecorder() = default;
  ~MetalFrameRecorder();
  MetalFrameRecorder(MetalFrameRecorder const&) = delete;
  MetalFrameRecorder& operator=(MetalFrameRecorder const&) = delete;
  MetalFrameRecorder(MetalFrameRecorder&& other) noexcept;
  MetalFrameRecorder& operator=(MetalFrameRecorder&& other) noexcept;

  void clear();
};

} // namespace lambdaui
