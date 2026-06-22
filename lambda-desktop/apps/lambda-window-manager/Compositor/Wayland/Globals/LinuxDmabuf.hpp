#pragma once

#include <algorithm>
#include <cstdint>

struct wl_client;

namespace lambda::compositor {

inline constexpr std::uint32_t kLinuxDmabufVersion = 5;

[[nodiscard]] inline std::uint32_t linuxDmabufResourceVersion(std::uint32_t requestedVersion) {
  return std::min(requestedVersion, kLinuxDmabufVersion);
}

void bindLinuxDmabuf(wl_client* client, void* data, std::uint32_t version, std::uint32_t id);
bool isSupportedDmabufFormat(std::uint32_t format);

} // namespace lambda::compositor
