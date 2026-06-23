#pragma once

#include "Compositor/Wayland/WaylandServerImpl.hpp"

#include <algorithm>
#include <cstdint>

namespace lambdaui::compositor {

inline constexpr std::uint32_t kCursorShapeVersion = 1;

[[nodiscard]] inline std::uint32_t cursorShapeResourceVersion(std::uint32_t boundVersion) {
  return std::min(boundVersion, kCursorShapeVersion);
}

[[nodiscard]] inline bool cursorShapeDeviceUsesPointer(WaylandServer::Impl::CursorShapeDevice const* device,
                                                       wl_resource* pointerResource) {
  return device && device->pointer == pointerResource;
}

[[nodiscard]] inline bool cursorShapeDeviceShouldClearForSurfaceDestroy(
    WaylandServer::Impl::CursorShapeDevice const*,
    WaylandServer::Impl::Surface const*) {
  return false;
}

} // namespace lambdaui::compositor
