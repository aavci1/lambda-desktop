#pragma once

/// \file Lambda/Graphics/TextCacheStats.hpp
///
/// Diagnostics for the Core Text caching layers (see TextSystem::stats()).

#include <cstdint>

namespace lambdaui {

struct TextCacheStats {
  struct LayerStats {
    std::uint64_t hits = 0;
    std::uint64_t misses = 0;
    std::uint64_t evictions = 0;
    std::uint64_t currentBytes = 0;
    std::uint64_t peakBytes = 0;
  };

  LayerStats l0_sizedFont{};
  LayerStats l1_color{};
  LayerStats l1_runAttr{};
  LayerStats l1_paraStyle{};
  LayerStats l2_framesetter{};
  LayerStats l3_layout{};
  LayerStats l4_boxLayout{};
  LayerStats l2_5_paragraph{};
  LayerStats l2_5_variant{};
  LayerStats l2_5_memo{};

  std::uint64_t contentHashCollisions = 0;
};

} // namespace lambdaui
