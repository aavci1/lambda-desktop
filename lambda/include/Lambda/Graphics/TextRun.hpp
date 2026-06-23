#pragma once

/// \file Lambda/Graphics/TextRun.hpp
///
/// Part of the Lambda public API.

#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Core/Color.hpp>

#include <cstdint>
#include <optional>
#include <span>

namespace lambdaui {

/// One Core Text `CTRun`-equivalent: resolved style, shaped glyphs, and metrics. Glyph `positions` are
/// relative to this run's baseline-left; `y` is positive downward (canvas space). `glyphIds` and
/// `positions` are non-owning views; backing storage is owned by `TextLayout::ownedStorage` or kept alive via
/// `TextLayout::variantRefs` (paragraph layout variant arenas).
struct TextRun {
    std::uint32_t fontId = 0;
    float fontSize = 0.f;
    Color color = Colors::black;
    std::optional<Color> backgroundColor;
    std::span<std::uint32_t const> glyphIds;
    std::span<Point const> positions;
    float ascent = 0.f;
    float descent = 0.f;
    float width = 0.f;
};

} // namespace lambdaui
