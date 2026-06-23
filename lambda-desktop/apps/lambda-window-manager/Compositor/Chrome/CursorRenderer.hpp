#pragma once

#include "Compositor/Surface/SurfaceRenderer.hpp"
#include "Compositor/WaylandServer.hpp"

#include <Lambda/Graphics/Canvas.hpp>
#include <Lambda/Platform/Linux/KmsOutput.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lambdaui::compositor {

struct CursorRenderState {
  CachedClientImage clientImage;
  CachedClientImage themeImage;
  CursorShape themeShape = CursorShape::Arrow;
  std::string themeName;
  float themeScale = 0.f;
  std::int32_t themeBaseSize = 0;
  std::int32_t themeHotspotX = 0;
  std::int32_t themeHotspotY = 0;
  std::uint64_t themeSerial = 0;
  std::uint64_t hardwareSerial = 0;
  std::uint64_t hardwareClientId = 0;
  CursorShape hardwareShape = CursorShape::Arrow;
  bool hardwareVisible = false;
  bool hardwareClient = false;
};

void drawCompositorCursor(WaylandServer& wayland,
                          Canvas& canvas,
                          platform::KmsOutput const& output,
                          CursorRenderState& cursorState,
                          std::optional<std::string> const& cursorTheme,
                          std::int32_t cursorSize,
                          bool hardwareCursorEnabled);

[[nodiscard]] bool moveCurrentHardwareCursor(WaylandServer& wayland,
                                             platform::KmsOutput const& output,
                                             CursorRenderState const& cursorState,
                                             bool hardwareCursorEnabled);

} // namespace lambdaui::compositor
