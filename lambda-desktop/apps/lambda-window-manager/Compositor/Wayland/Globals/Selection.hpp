#pragma once

#include <algorithm>
#include <cstdint>

struct wl_client;

namespace lambdaui::compositor {

class WaylandServer;

inline constexpr std::uint32_t kPrimarySelectionVersion = 1;
inline constexpr std::uint32_t kDataDeviceVersion = 3;

[[nodiscard]] inline std::uint32_t primarySelectionResourceVersion(std::uint32_t requestedVersion) {
  return std::min(requestedVersion, kPrimarySelectionVersion);
}

[[nodiscard]] inline std::uint32_t dataDeviceResourceVersion(std::uint32_t requestedVersion) {
  return std::min(requestedVersion, kDataDeviceVersion);
}

void bindPrimarySelectionManager(wl_client* client, void* data, std::uint32_t version, std::uint32_t id);
void bindDataDeviceManager(wl_client* client, void* data, std::uint32_t version, std::uint32_t id);

} // namespace lambdaui::compositor
