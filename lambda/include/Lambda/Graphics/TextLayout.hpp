#pragma once

/// \file Lambda/Graphics/TextLayout.hpp
///
/// Part of the Lambda public API.


#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Detail/SmallVector.hpp>
#include <Lambda/Graphics/TextRun.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace lambdaui {

/// Owning glyph arenas for layouts produced on the slow path or by `cloneTextLayout`. Spans in `runs`
/// point into these vectors.
struct TextLayoutStorage {
  std::vector<std::uint32_t> glyphArena;
  std::vector<Point> positionArena;
};

/// Laid-out text: runs plus placement in layout space. `origin` passed to `Canvas::drawTextLayout` is the layout
/// top-left; each `PlacedRun::origin` is the baseline-left of that run relative to that point (`y` down).
struct TextLayout {
  /// One visual line from Core Text (`CTLine`): UTF-8 byte span and geometry in layout space (after the same
  /// transforms as `runs`: normalize, horizontal alignment, vertical box offset). Populated by CoreTextSystem.
  struct LineRange {
    std::uint32_t ctLineIndex = 0;
    int byteStart = 0;
    int byteEnd = 0; ///< Half-open [byteStart, byteEnd) in UTF-8 source bytes.
    float lineMinX = 0.f;
    float top = 0.f;
    float bottom = 0.f;
    float baseline = 0.f;
  };

  /// Alternate name for `LineRange` (per-line UTF-8 span from Core Text); useful in documentation.
  using VisualLine = LineRange;

  struct PlacedRun {
    TextRun run{};
    Point origin{}; ///< Baseline-left relative to layout origin (top-left).
    /// Half-open UTF-8 byte range in the source string this run maps to (from Core Text).
    std::uint32_t utf8Begin = 0;
    std::uint32_t utf8End = 0;
    /// Index of the `CTLine` this run came from (stable grouping for hit-testing vs. baseline epsilon).
    std::uint32_t ctLineIndex = 0;
  };

  std::vector<PlacedRun> runs;
  /// Empty for legacy layouts; when non-empty, byte ranges match `CTLineGetStringRange` (UTF-8 mapped).
  std::vector<LineRange> lines;
  Size measuredSize{};
  float firstBaseline = 0.f; ///< Distance from layout top to first line baseline.
  float lastBaseline = 0.f;  ///< Distance from layout top to last line baseline.

  /// Keeps backing storage alive for non-owning spans in `runs` (paragraph layout variant glyph arenas).
  /// Holds `std::shared_ptr` to internal variant storage as type-erased `void`; do not dereference from API code.
  lambdaui::detail::SmallVector<std::shared_ptr<void>, 8> variantRefs;

  /// Slow-path / cloned owning storage when spans point here instead of into `variantRefs`.
  std::unique_ptr<TextLayoutStorage> ownedStorage;
};

/// Recomputes `measuredSize`, `firstBaseline`, and `lastBaseline` from current run geometry.
void recomputeTextLayoutMetrics(TextLayout& layout);

/// Drops runs on lines after the first `maxLines` distinct baselines, then optionally normalizes origins to the
/// bounding box top-left (recommended when returning from a shaper).
void trimTextLayoutToMaxLines(TextLayout& layout, int maxLines, bool normalizeAfter = true);

/// Deep copy of a layout (mutable). Copies glyph data into `ownedStorage`; clears `variantRefs`.
std::shared_ptr<TextLayout> cloneTextLayout(TextLayout const& src);

} // namespace lambdaui
