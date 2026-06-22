#pragma once

#include <algorithm>
#include <cstdint>

struct wl_client;

namespace lambda::compositor {

inline constexpr std::uint32_t kActivationVersion = 1;

[[nodiscard]] inline std::uint32_t activationResourceVersion(std::uint32_t requestedVersion) {
  return std::min(requestedVersion, kActivationVersion);
}

void bindActivation(wl_client* client, void* data, std::uint32_t version, std::uint32_t id);

} // namespace lambda::compositor
