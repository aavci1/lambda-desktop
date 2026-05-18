#pragma once

#include "Compositor/WaylandServer.hpp"

#include <Flux/Graphics/Canvas.hpp>
#include <Flux/Platform/Linux/KmsOutput.hpp>

#include <cstdint>
#include <vector>

namespace flux::compositor {

struct CachedClientImage;

[[nodiscard]] std::vector<std::uint32_t> makeHardwareArrowCursor(std::uint32_t width, std::uint32_t height);
void drawFallbackCursor(Canvas& canvas, CursorShape shape, float cursorX, float cursorY);
void drawCompositorCursor(WaylandServer& wayland,
                          Canvas& canvas,
                          platform::KmsOutput const& output,
                          CachedClientImage& cursorImage,
                          bool hardwareArrowCursor,
                          std::vector<std::uint32_t> const& hardwareCursorPixels,
                          std::uint32_t hardwareCursorWidth,
                          std::uint32_t hardwareCursorHeight);

} // namespace flux::compositor
