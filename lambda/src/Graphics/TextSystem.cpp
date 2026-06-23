#include <Lambda/Graphics/TextSystem.hpp>

#include "Graphics/TextSystemPrivate.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <span>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace lambdaui {

void recomputeTextLayoutMetrics(TextLayout &L) {
    if (L.runs.empty()) {
        if (!L.lines.empty()) {
            float minTop = std::numeric_limits<float>::infinity();
            float maxBot = -std::numeric_limits<float>::infinity();
            float minBaselineY = std::numeric_limits<float>::infinity();
            float maxBaselineY = -std::numeric_limits<float>::infinity();
            for (auto const &lr : L.lines) {
                if (std::isfinite(lr.top) && std::isfinite(lr.bottom) && lr.bottom > lr.top) {
                    minTop = std::min(minTop, lr.top);
                    maxBot = std::max(maxBot, lr.bottom);
                }
                if (std::isfinite(lr.baseline)) {
                    minBaselineY = std::min(minBaselineY, lr.baseline);
                    maxBaselineY = std::max(maxBaselineY, lr.baseline);
                }
            }
            if (std::isfinite(minTop) && std::isfinite(maxBot) && maxBot > minTop) {
                L.measuredSize.width = 0.f;
                L.measuredSize.height = std::max(0.f, maxBot - minTop);
                if (std::isfinite(minBaselineY) && std::isfinite(maxBaselineY)) {
                    L.firstBaseline = minBaselineY - minTop;
                    L.lastBaseline = maxBaselineY - minTop;
                } else {
                    L.firstBaseline = 0.f;
                    L.lastBaseline = 0.f;
                }
                return;
            }
        }
        L.measuredSize = {};
        L.firstBaseline = 0.f;
        L.lastBaseline = 0.f;
        L.lines.clear();
        return;
    }

    float minX = std::numeric_limits<float>::infinity();
    float maxX = -std::numeric_limits<float>::infinity();
    float minTop = std::numeric_limits<float>::infinity();
    float maxBot = -std::numeric_limits<float>::infinity();
    float minBaselineY = std::numeric_limits<float>::infinity();
    float maxBaselineY = -std::numeric_limits<float>::infinity();

    for (auto const &pr : L.runs) {
        TextRun const &r = pr.run;
        minX = std::min(minX, pr.origin.x);
        maxX = std::max(maxX, pr.origin.x + r.width);
        minTop = std::min(minTop, pr.origin.y - r.ascent);
        maxBot = std::max(maxBot, pr.origin.y + r.descent);
        minBaselineY = std::min(minBaselineY, pr.origin.y);
        maxBaselineY = std::max(maxBaselineY, pr.origin.y);
    }

    L.measuredSize.width = std::max(0.f, maxX - minX);
    L.measuredSize.height = std::max(0.f, maxBot - minTop);
    L.firstBaseline = minBaselineY - minTop;
    L.lastBaseline = maxBaselineY - minTop;
}

namespace {

void applyHorizontalPerLine(TextLayout &layout, Rect const &box, HorizontalAlignment horizontalAlignment) {
    if (layout.runs.empty()) {
        return;
    }
    if (horizontalAlignment == HorizontalAlignment::Leading) {
        return;
    }

    // Runs are appended in ctLineIndex order, so group their contiguous ranges once and then operate on
    // each line slice directly instead of rescanning all runs for every visual line.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> lineRunRanges;
    lineRunRanges.reserve(layout.lines.empty() ? layout.runs.size() : layout.lines.size());
    std::uint32_t begin = 0;
    std::uint32_t prevLineId = layout.runs.front().ctLineIndex;
    for (std::uint32_t i = 1; i < layout.runs.size(); ++i) {
        std::uint32_t const lineId = layout.runs[i].ctLineIndex;
        if (lineId != prevLineId) {
            lineRunRanges.emplace_back(begin, i);
            begin = i;
            prevLineId = lineId;
        }
    }
    lineRunRanges.emplace_back(begin, static_cast<std::uint32_t>(layout.runs.size()));

    std::size_t lineCursor = 0;
    for (auto const &[runBegin, runEnd] : lineRunRanges) {
        std::uint32_t const lineId = layout.runs[runBegin].ctLineIndex;
        float minL = layout.runs[runBegin].origin.x;
        float maxR = layout.runs[runBegin].origin.x + layout.runs[runBegin].run.width;
        for (std::uint32_t i = runBegin + 1; i < runEnd; ++i) {
            auto const &pr = layout.runs[i];
            minL = std::min(minL, pr.origin.x);
            maxR = std::max(maxR, pr.origin.x + pr.run.width);
        }

        float const lineWidth = maxR - minL;
        float lineDx = 0.f;
        if (horizontalAlignment == HorizontalAlignment::Center) {
            lineDx = (box.width - lineWidth) * 0.5f - minL;
        } else if (horizontalAlignment == HorizontalAlignment::Trailing) {
            lineDx = box.width - lineWidth - minL;
        }
        for (std::uint32_t i = runBegin; i < runEnd; ++i) {
            layout.runs[i].origin.x += lineDx;
        }

        while (lineCursor < layout.lines.size() && layout.lines[lineCursor].ctLineIndex < lineId) {
            ++lineCursor;
        }
        if (lineCursor < layout.lines.size() && layout.lines[lineCursor].ctLineIndex == lineId) {
            layout.lines[lineCursor].lineMinX += lineDx;
            ++lineCursor;
        }
    }
    recomputeTextLayoutMetrics(layout);
}

} // namespace

namespace detail {

void normalizeOriginsToTopLeft(TextLayout &L) {
    if (L.runs.empty()) {
        return;
    }
    float minX = std::numeric_limits<float>::infinity();
    float minTop = std::numeric_limits<float>::infinity();
    for (auto const &pr : L.runs) {
        TextRun const &r = pr.run;
        minX = std::min(minX, pr.origin.x);
        minTop = std::min(minTop, pr.origin.y - r.ascent);
    }
    for (auto &pr : L.runs) {
        pr.origin.x -= minX;
        pr.origin.y -= minTop;
    }
    for (auto &lr : L.lines) {
        lr.lineMinX -= minX;
        lr.top -= minTop;
        lr.bottom -= minTop;
        lr.baseline -= minTop;
    }
    recomputeTextLayoutMetrics(L);
}

void applyBoxOptions(TextLayout &layout, Rect const &box, TextLayoutOptions const &options) {
    if (layout.runs.empty()) {
        return;
    }

    applyHorizontalPerLine(layout, box, options.horizontalAlignment);

    float const h = layout.measuredSize.height;

    float dy = 0.f;
    switch (options.verticalAlignment) {
    case VerticalAlignment::Top:
        dy = 0.f;
        break;
    case VerticalAlignment::FirstBaseline:
        dy = options.firstBaselineOffset - layout.firstBaseline;
        break;
    case VerticalAlignment::Center:
        dy = (box.height - h) * 0.5f;
        break;
    case VerticalAlignment::Bottom:
        dy = box.height - h;
        break;
    }

    for (auto &pr : layout.runs) {
        pr.origin.y += dy;
    }
    for (auto &lr : layout.lines) {
        lr.top += dy;
        lr.bottom += dy;
        lr.baseline += dy;
    }
    recomputeTextLayoutMetrics(layout);
}

bool hasNotdefGlyph(std::span<std::uint32_t const> gids) noexcept {
    for (std::uint32_t g : gids) {
        if (g == 0) {
            return true;
        }
    }
    return false;
}

void collectDrawableGlyphIndices(std::span<std::uint32_t const> gids, std::vector<std::size_t>& out) {
    out.clear();
    out.reserve(gids.size());
    for (std::size_t i = 0; i < gids.size(); ++i) {
        if (gids[i] != 0) {
            out.push_back(i);
        }
    }
}

} // namespace detail

namespace {

std::size_t countDrawableGlyphs(std::span<std::uint32_t const> gids) noexcept {
    std::size_t total = 0;
    for (std::uint32_t g : gids) {
        if (g != 0) {
            ++total;
        }
    }
    return total;
}

/// Total glyphs written by `cloneTextLayout` (must match the main loop). Used to `reserve` arenas so
/// `glyphArena` never reallocates while earlier `PlacedRun::run.glyphIds` spans still point into it.
std::size_t cloneTextLayoutOutputGlyphCount(TextLayout const &src) noexcept {
    std::size_t total = 0;
    for (auto const &pr : src.runs) {
        std::size_t const gidCount = pr.run.glyphIds.size();
        std::size_t const posCount = pr.run.positions.size();
        std::size_t const n = std::min(gidCount, posCount);
        if (n == 0) {
            continue;
        }
        std::span<std::uint32_t const> const gids {pr.run.glyphIds.data(), n};
        total += detail::hasNotdefGlyph(gids) ? countDrawableGlyphs(gids) : n;
    }
    return total;
}

} // namespace

namespace detail {

bool paragraphCacheLayoutsStructurallyEqual(TextLayout const &a, TextLayout const &b, std::string *dumpOut) {
    auto feq = [](float x, float y) { return std::fabs(x - y) < 1e-4f; };
    // Incremental assembly sometimes reuses memo-derived baselines; full assembly always recomputes metrics.
    auto feqLayout = [](float x, float y) { return std::fabs(x - y) < 0.25f; };
    auto fail = [&](char const *msg) {
        if (dumpOut) {
            *dumpOut += msg;
            *dumpOut += '\n';
        }
        return false;
    };
    if (a.runs.size() != b.runs.size()) {
        return fail("runs.size mismatch");
    }
    if (a.lines.size() != b.lines.size()) {
        return fail("lines.size mismatch");
    }
    bool const variantRefsComparable = !a.variantRefs.empty() && !b.variantRefs.empty();
    if (variantRefsComparable && a.variantRefs.size() != b.variantRefs.size()) {
        return fail("variantRefs.size mismatch");
    }
    if (!feqLayout(a.measuredSize.width, b.measuredSize.width) ||
        !feqLayout(a.measuredSize.height, b.measuredSize.height)) {
        return fail("measuredSize mismatch");
    }
    if (!feqLayout(a.firstBaseline, b.firstBaseline) || !feqLayout(a.lastBaseline, b.lastBaseline)) {
        return fail("first/lastBaseline mismatch");
    }
    for (std::size_t i = 0; i < a.runs.size(); ++i) {
        auto const &x = a.runs[i];
        auto const &y = b.runs[i];
        if (x.run.fontId != y.run.fontId) {
            return fail("run fontId mismatch");
        }
        if (!feq(x.run.fontSize, y.run.fontSize)) {
            return fail("run fontSize mismatch");
        }
        if (!(x.run.color == y.run.color)) {
            return fail("run color mismatch");
        }
        if (x.run.backgroundColor != y.run.backgroundColor) {
            return fail("run backgroundColor mismatch");
        }
        if (!feq(x.run.ascent, y.run.ascent) || !feq(x.run.descent, y.run.descent) || !feq(x.run.width, y.run.width)) {
            return fail("run metrics mismatch");
        }
        if (x.run.glyphIds.size() != y.run.glyphIds.size()) {
            return fail("glyphIds.size mismatch");
        }
        if (x.run.positions.size() != y.run.positions.size()) {
            return fail("positions.size mismatch");
        }
        for (std::size_t g = 0; g < x.run.glyphIds.size(); ++g) {
            if (x.run.glyphIds[g] != y.run.glyphIds[g]) {
                return fail("glyphIds value mismatch");
            }
        }
        for (std::size_t g = 0; g < x.run.positions.size(); ++g) {
            if (!feq(x.run.positions[g].x, y.run.positions[g].x) ||
                !feq(x.run.positions[g].y, y.run.positions[g].y)) {
                return fail("positions value mismatch");
            }
        }
        if (!feq(x.origin.x, y.origin.x) || !feq(x.origin.y, y.origin.y)) {
            return fail("PlacedRun origin mismatch");
        }
        if (x.utf8Begin != y.utf8Begin || x.utf8End != y.utf8End) {
            return fail("PlacedRun utf8 range mismatch");
        }
        if (x.ctLineIndex != y.ctLineIndex) {
            return fail("ctLineIndex mismatch");
        }
    }
    for (std::size_t i = 0; i < a.lines.size(); ++i) {
        auto const &x = a.lines[i];
        auto const &y = b.lines[i];
        if (x.ctLineIndex != y.ctLineIndex) {
            return fail("LineRange ctLineIndex mismatch");
        }
        if (x.byteStart != y.byteStart || x.byteEnd != y.byteEnd) {
            return fail("LineRange byte range mismatch");
        }
        if (!feqLayout(x.lineMinX, y.lineMinX) || !feqLayout(x.top, y.top) || !feqLayout(x.bottom, y.bottom) ||
            !feqLayout(x.baseline, y.baseline)) {
            return fail("LineRange geometry mismatch");
        }
    }
    return true;
}

} // namespace detail

std::shared_ptr<TextLayout> cloneTextLayout(TextLayout const &src) {
    auto out = std::make_shared<TextLayout>();
    out->lines = src.lines;
    out->measuredSize = src.measuredSize;
    out->firstBaseline = src.firstBaseline;
    out->lastBaseline = src.lastBaseline;
    out->variantRefs.clear();

    auto storage = std::make_unique<TextLayoutStorage>();
    std::size_t const totalGlyphs = cloneTextLayoutOutputGlyphCount(src);
    storage->glyphArena.reserve(totalGlyphs);
    storage->positionArena.reserve(totalGlyphs);
    out->runs.reserve(src.runs.size());
    std::vector<std::size_t> kept;
    for (auto const &pr : src.runs) {
        TextLayout::PlacedRun copy = pr;
        std::size_t const gidCount = pr.run.glyphIds.size();
        std::size_t const posCount = pr.run.positions.size();
        std::size_t const n = std::min(gidCount, posCount);
        if (n == 0) {
            copy.run.glyphIds = {};
            copy.run.positions = {};
            out->runs.push_back(std::move(copy));
            continue;
        }

        std::span<std::uint32_t const> const gids {pr.run.glyphIds.data(), n};
        if (!detail::hasNotdefGlyph(gids)) {
            std::size_t const gGlyphStart = storage->glyphArena.size();
            std::size_t const gPosStart = storage->positionArena.size();
            storage->glyphArena.insert(storage->glyphArena.end(), pr.run.glyphIds.begin(),
                                       pr.run.glyphIds.begin() + static_cast<std::ptrdiff_t>(n));
            storage->positionArena.insert(storage->positionArena.end(), pr.run.positions.begin(),
                                          pr.run.positions.begin() + static_cast<std::ptrdiff_t>(n));
            copy.run.glyphIds =
                std::span<std::uint32_t const>(storage->glyphArena.data() + gGlyphStart, n);
            copy.run.positions = std::span<Point const>(storage->positionArena.data() + gPosStart, n);
            out->runs.push_back(std::move(copy));
            continue;
        }

        detail::collectDrawableGlyphIndices(gids, kept);
        if (kept.empty()) {
            copy.run.glyphIds = {};
            copy.run.positions = {};
            out->runs.push_back(std::move(copy));
            continue;
        }

        // `.notdef` (gid 0) filtered — re-anchor relative positions to the first kept glyph (matches CT path).
        std::size_t const fi = kept[0];
        std::size_t const newN = kept.size();
        std::size_t const gGlyphStart = storage->glyphArena.size();
        std::size_t const gPosStart = storage->positionArena.size();
        for (std::size_t j = 0; j < newN; ++j) {
            std::size_t const oi = kept[j];
            storage->glyphArena.push_back(pr.run.glyphIds[oi]);
            float const dx = pr.run.positions[oi].x - pr.run.positions[fi].x;
            float const dy = pr.run.positions[oi].y - pr.run.positions[fi].y;
            storage->positionArena.push_back(Point {dx, dy});
        }
        copy.run.glyphIds =
            std::span<std::uint32_t const>(storage->glyphArena.data() + gGlyphStart, newN);
        copy.run.positions = std::span<Point const>(storage->positionArena.data() + gPosStart, newN);
        out->runs.push_back(std::move(copy));
    }
#ifndef NDEBUG
    assert(storage->glyphArena.size() == totalGlyphs && storage->positionArena.size() == totalGlyphs);
#endif
    out->ownedStorage = std::move(storage);
    return out;
}

void trimTextLayoutToMaxLines(TextLayout &layout, int maxLines, bool normalizeAfter) {
    if (maxLines <= 0 || (layout.runs.empty() && layout.lines.empty())) {
        return;
    }

    std::vector<std::uint32_t> lineOrder;
    if (!layout.lines.empty()) {
        lineOrder.reserve(layout.lines.size());
        for (auto const &lr : layout.lines) {
            lineOrder.push_back(lr.ctLineIndex);
        }
    } else {
        std::unordered_set<std::uint32_t> seenLine;
        lineOrder.reserve(layout.runs.size());
        seenLine.reserve(layout.runs.size());
        for (auto const &pr : layout.runs) {
            if (seenLine.insert(pr.ctLineIndex).second) {
                lineOrder.push_back(pr.ctLineIndex);
            }
        }
    }
    if (static_cast<int>(lineOrder.size()) <= maxLines) {
        return;
    }

    std::unordered_set<std::uint32_t> keptLineIndices;
    keptLineIndices.reserve(static_cast<std::size_t>(maxLines));
    for (int i = 0; i < maxLines; ++i) {
        keptLineIndices.insert(lineOrder[static_cast<std::size_t>(i)]);
    }

    auto newEnd = std::remove_if(layout.runs.begin(), layout.runs.end(), [&](TextLayout::PlacedRun const &pr) {
        return keptLineIndices.count(pr.ctLineIndex) == 0;
    });
    layout.runs.erase(newEnd, layout.runs.end());

    layout.lines.erase(std::remove_if(layout.lines.begin(), layout.lines.end(),
                                      [&](TextLayout::LineRange const &lr) {
                                          return keptLineIndices.count(lr.ctLineIndex) == 0;
                                      }),
                       layout.lines.end());

    if (normalizeAfter) {
        detail::normalizeOriginsToTopLeft(layout);
    } else {
        recomputeTextLayoutMetrics(layout);
    }
}

std::shared_ptr<TextLayout const> TextSystem::layoutBoxedImpl(AttributedString const &text, Rect const &box,
                                                              TextLayoutOptions const &options) {
    float const maxWidth = options.wrapping == TextWrapping::NoWrap ? 0.f : box.width;
    std::shared_ptr<TextLayout const> base = layout(text, maxWidth, options);
    if (!base) {
        return nullptr;
    }
    std::shared_ptr<TextLayout> mut = cloneTextLayout(*base);
    detail::applyBoxOptions(*mut, box, options);
    return mut;
}

} // namespace lambdaui
