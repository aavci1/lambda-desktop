#pragma once

#include "Compositor/WaylandServer.hpp"

#include <Flux/Graphics/Canvas.hpp>

#include <cstdint>
#include <vector>

namespace flux::compositor {

[[nodiscard]] std::vector<std::uint32_t> makeHardwareArrowCursor(std::uint32_t width, std::uint32_t height);
void drawFallbackCursor(Canvas& canvas, CursorShape shape, float cursorX, float cursorY);

} // namespace flux::compositor
