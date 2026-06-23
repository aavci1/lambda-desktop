#pragma once

/// \file Lambda/Graphics/TextSystem.hpp
///
/// Part of the Lambda public API.

#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Core/Color.hpp>
#include <Lambda/Graphics/AttributedString.hpp>
#include <Lambda/Graphics/Font.hpp>
#include <Lambda/Graphics/TextCacheStats.hpp>
#include <Lambda/Graphics/TextLayout.hpp>
#include <Lambda/Graphics/TextLayoutOptions.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lambdaui {

class TextSystem {
  public:
    virtual ~TextSystem() = default;

    // -----------------------------------------------------------------
    // Layout — returns immutable TextLayout (cache entries are shared).
    // -----------------------------------------------------------------

    /// Unconstrained: no box, no alignment. `maxWidth == 0` means no wrapping.
    virtual std::shared_ptr<TextLayout const> layout(AttributedString const &text, float maxWidth = 0.f,
                                                     TextLayoutOptions const &options = {}) = 0;

    virtual std::shared_ptr<TextLayout const> layout(std::string_view utf8, Font const &font, Color const &color,
                                                     float maxWidth = 0.f,
                                                     TextLayoutOptions const &options = {}) = 0;

    /// Box-constrained: PlacedRun origins are pre-offset for alignment within the box.
    /// Drawing: `canvas.drawTextLayout(*result, {box.x, box.y})` — no further arithmetic.
    /// With `TextWrapping::NoWrap`, width is not limited to `box.width`; text may extend past the box unless the
    /// caller clips (e.g. canvas scissor).
    std::shared_ptr<TextLayout const> layout(AttributedString const &text, Rect const &box,
                                             TextLayoutOptions const &options = {}) {
        return layoutBoxedImpl(text, box, options);
    }

    std::shared_ptr<TextLayout const> layout(std::string_view utf8, Font const &font, Color const &color,
                                             Rect const &box, TextLayoutOptions const &options = {}) {
        AttributedString as;
        as.utf8 = std::string(utf8);
        as.runs.push_back({.start = 0,
                           .end = static_cast<std::uint32_t>(utf8.size()),
                           .font = font,
                           .color = color,
                           .backgroundColor = std::nullopt});
        return layoutBoxedImpl(as, box, options);
    }

    // -----------------------------------------------------------------
    // Measure — CPU only, safe in layout pass, no canvas required
    // -----------------------------------------------------------------

    virtual Size measure(AttributedString const &text, float maxWidth = 0.f,
                         TextLayoutOptions const &options = {}) = 0;

    virtual Size measure(std::string_view utf8, Font const &font, Color const &color, float maxWidth = 0.f,
                         TextLayoutOptions const &options = {}) = 0;

    // -----------------------------------------------------------------
    // Frame lifecycle — call from build orchestrator once per frame
    // -----------------------------------------------------------------

    virtual void onFrameBegin(std::uint64_t frameIndex) { (void)frameIndex; }

    virtual void onFrameEnd() {}

    // -----------------------------------------------------------------
    // Invalidation
    // -----------------------------------------------------------------

    virtual void invalidateAll() {}

    /// Drop cache entries that reference any of the given font ids (resolved font registry indices).
    virtual void invalidateForFontChange(std::span<std::uint32_t const> fontIds) { (void)fontIds; }

    /// Byte budget for the paragraph shape cache (Core Text backend only). Default is implementation-defined.
    virtual void setParagraphCacheBudget(std::size_t bytes) { (void)bytes; }

    // -----------------------------------------------------------------
    // Diagnostics
    // -----------------------------------------------------------------

    [[nodiscard]] virtual TextCacheStats stats() const { return {}; }

    // -----------------------------------------------------------------
    // Backend interface (called by GlyphAtlas — not for app code)
    // -----------------------------------------------------------------

    virtual std::uint32_t resolveFontId(std::string_view fontFamily, float weight, bool italic) = 0;

    virtual std::vector<std::uint8_t> rasterizeGlyph(std::uint32_t fontId, std::uint32_t glyphId, float size,
                                                     std::uint32_t &outWidth, std::uint32_t &outHeight,
                                                     Point &outBearing) = 0;

  protected:
    virtual std::shared_ptr<TextLayout const> layoutBoxedImpl(AttributedString const &text, Rect const &box,
                                                              TextLayoutOptions const &options = {});
};

} // namespace lambdaui
