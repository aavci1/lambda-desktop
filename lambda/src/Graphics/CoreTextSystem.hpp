#pragma once

#include <Lambda/Graphics/TextCacheStats.hpp>
#include <Lambda/Graphics/TextSystem.hpp>

#include <memory>

namespace lambda {

class CoreTextSystem;

namespace detail {
std::shared_ptr<TextLayout const> paragraphCacheFullAssemblyForTest(
    CoreTextSystem& sys, AttributedString const& text, float maxWidth, TextLayoutOptions const& options);
}

class CoreTextSystem final : public TextSystem {
public:
  CoreTextSystem();
  ~CoreTextSystem() override;

  CoreTextSystem(CoreTextSystem const&) = delete;
  CoreTextSystem& operator=(CoreTextSystem const&) = delete;

  std::shared_ptr<TextLayout const> layout(AttributedString const& text, float maxWidth,
                                           TextLayoutOptions const& options) override;

  std::shared_ptr<TextLayout const> layout(std::string_view utf8, Font const& font, Color const& color,
                                           float maxWidth, TextLayoutOptions const& options) override;

  std::shared_ptr<TextLayout const> layoutBoxedImpl(AttributedString const& text, Rect const& box,
                                                    TextLayoutOptions const& options) override;

  Size measure(AttributedString const& text, float maxWidth, TextLayoutOptions const& options) override;

  Size measure(std::string_view utf8, Font const& font, Color const& color, float maxWidth,
               TextLayoutOptions const& options) override;

  void onFrameBegin(std::uint64_t frameIndex) override;

  void onFrameEnd() override;

  void invalidateAll() override;

  void invalidateForFontChange(std::span<std::uint32_t const> fontIds) override;

  void setParagraphCacheBudget(std::size_t bytes) override;

  [[nodiscard]] TextCacheStats stats() const override;

  std::uint32_t resolveFontId(std::string_view fontFamily, float weight, bool italic) override;

  std::vector<std::uint8_t> rasterizeGlyph(std::uint32_t fontId, std::uint32_t glyphId, float size,
                                           std::uint32_t& outWidth, std::uint32_t& outHeight,
                                           Point& outBearing) override;

  friend std::shared_ptr<TextLayout const> detail::paragraphCacheFullAssemblyForTest(
      CoreTextSystem& sys, AttributedString const& text, float maxWidth, TextLayoutOptions const& options);

private:
  struct Impl;
  std::unique_ptr<Impl> d;

  /// When \p hasPrecomputedHash, \p preHi / \p preLo are the XXH3 128-bit content hash (avoids re-hashing).
  std::shared_ptr<TextLayout const> layoutUnboxed(AttributedString const& text,
                                                  TextLayoutOptions const& options, float maxWidth,
                                                  bool hasPrecomputedHash, std::uint64_t preHi,
                                                  std::uint64_t preLo);
};

} // namespace lambda
