#pragma once

#include <Lambda/Graphics/Styles.hpp>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

#include <simd/simd.h>

namespace lambda {

/// GPU instance payload for rect/line SDF shaders (matches `RectInstance` in `CanvasShaders.metal`).
struct MetalRectInstance {
  vector_float4 rect;
  vector_float4 corners;
  vector_float4 fillColor;
  vector_float4 strokeColor;
  vector_float2 strokeWidthOpacity;
  vector_float2 viewport;
  /// .x = rotation radians, .y = gradient stop count, .z = gradient type, .w unused.
  vector_float4 rotationPad;
  /// Premultiplied shadow tint; .w is alpha scale.
  vector_float4 shadowColor;
  /// .xy = offset (device px), .z = blur radius (device px, uniform scale), .w unused.
  vector_float4 shadowGeom;
  /// Extra gradient stops. `fillColor` is stop 0; these are stops 1-3.
  vector_float4 gradientColor1;
  vector_float4 gradientColor2;
  vector_float4 gradientColor3;
  /// Normalized stop positions for stops 0-3.
  vector_float4 gradientStops;
  /// Linear: start.xy/end.zw. Radial/conical: center.xy, radius/startAngle in .z.
  vector_float4 gradientPoints;
};

static_assert(std::is_trivially_copyable_v<MetalRectInstance>);

/// Per-vertex glyph payload (two triangles per glyph). Matches `GlyphVertexIn` in `CanvasShaders.metal`.
struct MetalGlyphVertex {
  vector_float2 pos{};
  vector_float2 uv{};
  vector_float4 color{};
};

struct MetalGlyphVertexSource {
  enum Kind : std::uint8_t {
    Owned,
    Borrowed,
  } kind = Owned;
  std::uint32_t start = 0;
  std::uint32_t count = 0;
  MetalGlyphVertex const* borrowed = nullptr;
};

/// GPU instance for `image_sdf_vert` / `image_sdf_frag` (matches `ImageInstance` in `CanvasShaders.metal`).
struct MetalImageInstance {
  MetalRectInstance sdf;
  vector_float4 uvBounds; // u0, v0, u1, v1 in normalized texture coordinates
  vector_float2 texSizeInv;
  vector_float2 imageModePad; // x: 0 = clamp UV bounds, 1 = tile (repeat sampler)
};

static_assert(std::is_trivially_copyable_v<MetalImageInstance>);

inline constexpr std::size_t kMetalRoundedClipMaskCapacity = 4;

struct MetalRoundedClipStack {
  vector_float4 header{};
  vector_float4 entries[kMetalRoundedClipMaskCapacity * 2]{};
};

static_assert(std::is_trivially_copyable_v<MetalRoundedClipStack>);

struct MetalDrawUniforms {
  vector_float2 viewport{};
  vector_float2 translation{};
};

static_assert(std::is_trivially_copyable_v<MetalDrawUniforms>);

struct MetalRectOp {
  MetalRectInstance inst{};
  /// Non-owning MTLBuffer pointer for prepared static rect instances. Null means use the per-frame instance arena.
  void* externalInstanceBuffer = nullptr;
  std::uint32_t externalInstanceIndex = 0;
  std::uint32_t arenaInstanceIndex = 0;
  BlendMode blendMode = BlendMode::Normal;
  vector_float2 translation{};
  MetalRoundedClipStack roundedClip{};
  bool isLine = false;
  bool scissorValid = false;
  std::uint32_t scissorX = 0;
  std::uint32_t scissorY = 0;
  std::uint32_t scissorW = 0;
  std::uint32_t scissorH = 0;
};

static_assert(std::is_trivially_copyable_v<MetalRectOp>);

struct MetalImageOp {
  MetalImageInstance inst{};
  /// Non-owning MTLBuffer pointer for prepared static image instances. Null means use the per-frame image arena.
  void* externalInstanceBuffer = nullptr;
  std::uint32_t externalInstanceIndex = 0;
  std::uint32_t arenaInstanceIndex = 0;
  BlendMode blendMode = BlendMode::Normal;
  vector_float2 translation{};
  MetalRoundedClipStack roundedClip{};
  void* texture = nullptr;
  bool repeatSampler = false;
  bool scissorValid = false;
  std::uint32_t scissorX = 0;
  std::uint32_t scissorY = 0;
  std::uint32_t scissorW = 0;
  std::uint32_t scissorH = 0;
};

struct MetalPathOp {
  std::uint32_t pathStart = 0;
  std::uint32_t pathCount = 0;
  /// Non-owning MTLBuffer pointer for prepared static path vertices. Null means use the per-frame path arena.
  void* externalVertexBuffer = nullptr;
  BlendMode blendMode = BlendMode::Normal;
  vector_float2 translation{};
  MetalRoundedClipStack roundedClip{};
  bool scissorValid = false;
  std::uint32_t scissorX = 0;
  std::uint32_t scissorY = 0;
  std::uint32_t scissorW = 0;
  std::uint32_t scissorH = 0;
};

struct MetalGlyphOp {
  std::uint32_t glyphStart = 0;
  std::uint32_t glyphVertexCount = 0;
  /// Non-owning MTLBuffer pointer for prepared static glyph vertices. Null means use the per-frame glyph arena.
  void* externalVertexBuffer = nullptr;
  BlendMode blendMode = BlendMode::Normal;
  vector_float2 translation{};
  MetalRoundedClipStack roundedClip{};
  bool scissorValid = false;
  std::uint32_t scissorX = 0;
  std::uint32_t scissorY = 0;
  std::uint32_t scissorW = 0;
  std::uint32_t scissorH = 0;
};

struct MetalBackdropBlurOp {
  vector_float4 rect{};
  vector_float4 tint{};
  vector_float4 corners{};
  float radius = 0.f;
  MetalRoundedClipStack roundedClip{};
  bool scissorValid = false;
  std::uint32_t scissorX = 0;
  std::uint32_t scissorY = 0;
  std::uint32_t scissorW = 0;
  std::uint32_t scissorH = 0;
};

struct MetalOpRef {
  enum Kind : std::uint8_t { Rect, Image, Path, Glyph, BackdropBlur } kind = Rect;
  std::uint32_t index = 0;
};

struct MetalRecorderSlice {
  std::uint32_t orderStart = 0;
  std::uint32_t orderCount = 0;
  std::uint32_t rectStart = 0;
  std::uint32_t rectCount = 0;
  std::uint32_t imageStart = 0;
  std::uint32_t imageCount = 0;
  std::uint32_t pathOpStart = 0;
  std::uint32_t pathOpCount = 0;
  std::uint32_t glyphOpStart = 0;
  std::uint32_t glyphOpCount = 0;
  std::uint32_t backdropBlurOpStart = 0;
  std::uint32_t backdropBlurOpCount = 0;
  std::uint32_t pathVertexStart = 0;
  std::uint32_t pathVertexCount = 0;
  std::uint32_t glyphVertexStart = 0;
  std::uint32_t glyphVertexCount = 0;
};

static_assert(std::is_trivially_copyable_v<MetalImageOp>);
static_assert(std::is_trivially_copyable_v<MetalPathOp>);
static_assert(std::is_trivially_copyable_v<MetalGlyphOp>);
static_assert(std::is_trivially_copyable_v<MetalBackdropBlurOp>);
static_assert(std::is_trivially_copyable_v<MetalOpRef>);
static_assert(std::is_trivially_copyable_v<MetalRecorderSlice>);

struct MetalScissorState {
  bool scissorValid = false;
  std::uint32_t scissorX = 0;
  std::uint32_t scissorY = 0;
  std::uint32_t scissorW = 0;
  std::uint32_t scissorH = 0;
};

} // namespace lambda
