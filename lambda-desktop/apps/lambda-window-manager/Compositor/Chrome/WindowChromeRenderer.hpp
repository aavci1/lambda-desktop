#pragma once

#include "Compositor/Chrome/ChromeConfig.hpp"
#include "Compositor/WaylandServer.hpp"

#include <Lambda/Graphics/Canvas.hpp>
#include <Lambda/Graphics/TextSystem.hpp>

namespace lambda::compositor {

void drawWindowChrome(Canvas& canvas,
                      TextSystem& textSystem,
                      CommittedSurfaceSnapshot const& surface,
                      ChromeConfig const& chrome);
void drawWindowChromeControls(Canvas& canvas, CommittedSurfaceSnapshot const& surface, ChromeConfig const& chrome);
void drawWindowChromeActiveControls(Canvas& canvas, CommittedSurfaceSnapshot const& surface, ChromeConfig const& chrome);
void drawWindowFrameShadow(Canvas& canvas, CommittedSurfaceSnapshot const& surface, ChromeConfig const& chrome);
void drawWindowFrameBorder(Canvas& canvas, CommittedSurfaceSnapshot const& surface, ChromeConfig const& chrome);
void drawSnapPreview(Canvas& canvas, SnapPreviewSnapshot const& preview, ChromeConfig const& chrome);

} // namespace lambda::compositor
