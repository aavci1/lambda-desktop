#pragma once

#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Graphics/TextSystem.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

#if defined(__APPLE__)
#import <Metal/Metal.h>
#endif

namespace lambdaui {

struct GlyphKey {
  std::uint32_t fontId = 0;
  std::uint32_t glyphId = 0;
  std::uint16_t sizeQ8 = 0; // fontSize * 4, quantized to 0.25pt steps

  bool operator==(GlyphKey const& o) const noexcept {
    return fontId == o.fontId && glyphId == o.glyphId && sizeQ8 == o.sizeQ8;
  }
};

struct GlyphKeyHash {
  std::size_t operator()(GlyphKey const& k) const noexcept;
};

struct AtlasEntry {
  std::uint16_t u = 0;
  std::uint16_t v = 0;
  std::uint16_t width = 0;
  std::uint16_t height = 0;
  Point bearing{};
};

class GlyphAtlas {
public:
  GlyphAtlas(id<MTLDevice> device, TextSystem& textSystem, id<MTLCommandQueue> queue = nil);

  AtlasEntry const& getOrUpload(GlyphKey const& key);

  id<MTLTexture> texture() const { return texture_; }

  bool grow();
  void flushUploads(id<MTLCommandBuffer> commandBuffer);
  std::uint64_t generation() const noexcept { return generation_; }

  /// Deferred growth is handled after present so frame begin never blocks on atlas copy work.
  void prepareForFrameBegin();

  /// Tier B: grow after a presented frame when utilization is still high, giving headroom for the next frame.
  void afterPresent();

  /// Return false to defer a grow that would invalidate already-emitted glyph UVs in the current frame.
  void setBeforeGrowCallback(std::function<bool()> cb) { beforeGrow_ = std::move(cb); }

  std::uint32_t atlasPixelWidth() const { return atlasWidth_; }
  std::uint32_t atlasPixelHeight() const { return atlasHeight_; }

private:
  struct PendingUpload {
    id<MTLBuffer> buffer{nil};
    id<MTLTexture> texture{nil};
    std::uint32_t x{0};
    std::uint32_t y{0};
    std::uint32_t width{0};
    std::uint32_t height{0};
  };

  std::optional<AtlasEntry> allocateAndUpload(GlyphKey const& key);

  bool pressureHighForHeadroom() const;
  id<MTLTexture> createTexture(std::uint32_t width, std::uint32_t height) const;
  bool encodeClearTexture(id<MTLBlitCommandEncoder> blit,
                          id<MTLTexture> texture,
                          std::uint32_t width,
                          std::uint32_t height) const;
  bool clearTexture(id<MTLTexture> texture, std::uint32_t width, std::uint32_t height) const;
  void queueUpload(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height,
                   std::vector<std::uint8_t> const& r8);

  id<MTLDevice> device_{nil};
  id<MTLCommandQueue> queue_{nil};
  id<MTLTexture> texture_{nil};
  TextSystem& textSystem_;

  std::uint32_t atlasWidth_ = 1024;
  std::uint32_t atlasHeight_ = 1024;

  std::uint32_t shelfX_ = 1;
  std::uint32_t shelfY_ = 1;
  std::uint32_t shelfH_ = 0;

  std::unordered_map<GlyphKey, AtlasEntry, GlyphKeyHash> entries_;
  std::vector<PendingUpload> pendingUploads_;
  AtlasEntry transientEmptyEntry_{};

  std::uint64_t generation_ = 1;
  bool pendingGrow_ = false;
  std::function<bool()> beforeGrow_{};
};

} // namespace lambdaui
