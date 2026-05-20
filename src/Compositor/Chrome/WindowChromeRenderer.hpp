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
void drawSnapPreview(Canvas& canvas, SnapPreviewSnapshot const& preview);
void drawCommandLauncher(Canvas& canvas,
                         TextSystem& textSystem,
                         CommandLauncherSnapshot const& launcher,
                         std::int32_t outputWidth,
                         std::int32_t outputHeight);

} // namespace flux::compositor
