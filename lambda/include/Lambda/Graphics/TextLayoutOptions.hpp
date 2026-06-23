#pragma once

/// \file Lambda/Graphics/TextLayoutOptions.hpp
///
/// Part of the Lambda public API.


#include <cstdint>

namespace lambdaui {

enum class HorizontalAlignment : std::uint8_t { Leading, Center, Trailing };

/// `FirstBaseline` uses `firstBaselineOffset`: distance from the box top to the desired first baseline.
enum class VerticalAlignment : std::uint8_t { Top, Center, Bottom, FirstBaseline };

enum class TextWrapping : std::uint8_t {
  NoWrap,       ///< Single line; ignores box width for wrapping.
  Wrap,         ///< Break at word boundaries (default).
  WrapAnywhere, ///< Break at any character when no word boundary fits.
};

struct TextLayoutOptions {
  HorizontalAlignment horizontalAlignment = HorizontalAlignment::Leading;
  VerticalAlignment verticalAlignment = VerticalAlignment::Top;
  TextWrapping wrapping = TextWrapping::Wrap;
  /// Absolute minimum and maximum line height in points (Core Text). 0 = do not set min/max; prefer
  /// \c lineHeightMultiple when both come from a typographic multiplier.
  float lineHeight = 0.f;
  /// Core Text line-height multiple (0 = natural). Mutually preferred over \c lineHeight for theme-style
  /// multipliers so lines never pack tighter than glyph bounds.
  float lineHeightMultiple = 0.f;
  int maxLines = 0; ///< 0 = unlimited.

  /// Distance from the box top to the desired first baseline (only for `VerticalAlignment::FirstBaseline`).
  /// With offset 0, the first baseline is placed on the box top edge (ascenders may draw above the box).
  float firstBaselineOffset = 0.f;

  /// When true, Core Text cache layers do not update hit/miss counters (debug overlays / uncached paths).
  /// A suppressed call that populates L2 still skips L2 miss accounting; a later normal query for the same
  /// content may count as an L2 hit, so cumulative hits + misses can diverge from total queries (hit rate is
  /// still meaningful).
  bool suppressCacheStats = false;
};

} // namespace lambdaui
