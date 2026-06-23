#pragma once

#include <algorithm>
#include <cstdint>

struct wl_client;

namespace lambdaui::compositor {

inline constexpr std::uint32_t kBackgroundEffectVersion = 4;

[[nodiscard]] inline std::uint32_t backgroundEffectResourceVersion(std::uint32_t requestedVersion) {
  return std::min(requestedVersion, kBackgroundEffectVersion);
}

void bindBackgroundEffectManager(wl_client* client, void* data, std::uint32_t version, std::uint32_t id);

} // namespace lambdaui::compositor
