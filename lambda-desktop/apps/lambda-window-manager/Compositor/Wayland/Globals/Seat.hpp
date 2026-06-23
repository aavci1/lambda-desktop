#pragma once

#include "Compositor/WaylandServer.hpp"

#include <algorithm>
#include <cstdint>
#include <wayland-server-protocol.h>

struct wl_client;

namespace lambdaui::compositor {

inline constexpr std::uint32_t kSeatVersion = 7;
inline constexpr std::uint32_t kSeatCapabilities =
    WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD;

inline std::uint32_t seatResourceVersion(std::uint32_t requestedVersion) {
  return std::min(requestedVersion, kSeatVersion);
}

inline bool seatCapabilitiesAdvertiseTouch(std::uint32_t capabilities) {
  return (capabilities & WL_SEAT_CAPABILITY_TOUCH) != 0;
}

void bindSeat(wl_client* client, void* data, std::uint32_t version, std::uint32_t id);
void sendKeyboardConfiguration(WaylandServer::Impl* server);

} // namespace lambdaui::compositor
