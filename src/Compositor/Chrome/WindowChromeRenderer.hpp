#pragma once

#include "Compositor/Chrome/ChromeConfig.hpp"
#include "Compositor/WaylandServer.hpp"

#include <Flux/Graphics/Canvas.hpp>
#include <Flux/Graphics/TextSystem.hpp>

namespace flux::compositor {

void drawWindowChrome(Canvas& canvas,
                      TextSystem& textSystem,
                      CommittedSurfaceSnapshot const& surface,
                      ChromeConfig const& chrome);
void drawWindowFrameShadow(Canvas& canvas, CommittedSurfaceSnapshot const& surface, ChromeConfig const& chrome);
void drawWindowFrameBorder(Canvas& canvas, CommittedSurfaceSnapshot const& surface, ChromeConfig const& chrome);
void drawSnapPreview(Canvas& canvas, SnapPreviewSnapshot const& preview, ChromeConfig const& chrome);

} // namespace flux::compositor
