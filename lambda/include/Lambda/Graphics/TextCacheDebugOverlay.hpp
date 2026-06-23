#pragma once

/// \file Lambda/Graphics/TextCacheDebugOverlay.hpp
///
/// Text-cache stats panel (enable via \ref Runtime::setTextCacheOverlayEnabled). Rolling window
/// state is stored per \ref Window (see \ref TextCacheRingBuffer).

#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Graphics/TextCacheStats.hpp>

#include <array>
#include <cstddef>

namespace lambdaui {

class Canvas;
class TextSystem;

/// Per-window rolling samples for the overlay (last 60 frames).
struct TextCacheRingBuffer {
  std::array<TextCacheStats, 60> samples{};
  std::size_t writeIdx = 0;
  std::size_t count = 0;
};

/// Draws the text-cache stats panel in the top-left of the window (call after scene + overlay render).
void renderTextCacheDebugOverlay(Canvas& canvas, Rect viewport, TextCacheRingBuffer& ring,
                                 TextSystem& textSystem);

} // namespace lambdaui
