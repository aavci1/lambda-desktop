#pragma once

/// \file TextSystemPrivate.hpp
///
/// Internal helpers shared by \c TextSystem.cpp and \c CoreTextSystem.mm (not public API).

#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Graphics/TextLayout.hpp>
#include <Lambda/Graphics/TextLayoutOptions.hpp>

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace lambda::detail {

void normalizeOriginsToTopLeft(TextLayout& layout);

void applyBoxOptions(TextLayout& layout, Rect const& box, TextLayoutOptions const& options);

/// Deep structural compare for paragraph-cache testing / parallel assert. Optional \p dumpOut on mismatch.
bool paragraphCacheLayoutsStructurallyEqual(TextLayout const& a, TextLayout const& b,
                                          std::string* dumpOut = nullptr);

/// OpenType glyph id 0 is `.notdef`. \c CTRunGetGlyphs may still return it while \c CTLineDraw skips it
/// visually; probe for that cheaply, then let callers collect indices only when they need to re-anchor.
bool hasNotdefGlyph(std::span<std::uint32_t const> gids) noexcept;
void collectDrawableGlyphIndices(std::span<std::uint32_t const> gids, std::vector<std::size_t>& out);

} // namespace lambda::detail
