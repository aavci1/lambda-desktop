#pragma once

#include <cstdint>

struct wl_client;

namespace lambdaui::compositor {

inline constexpr std::uint32_t kCutoutsVersion = 1;

constexpr std::uint32_t cutoutsResourceVersion(std::uint32_t requestedVersion) noexcept {
  return requestedVersion < kCutoutsVersion ? requestedVersion : kCutoutsVersion;
}

void bindCutoutsManager(wl_client* client, void* data, std::uint32_t version, std::uint32_t id);

} // namespace lambdaui::compositor
