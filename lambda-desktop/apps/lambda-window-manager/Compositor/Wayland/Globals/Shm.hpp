#pragma once

#include <cstdint>

struct wl_client;

namespace lambda::compositor {

inline constexpr std::uint32_t kShmVersion = 1;

constexpr std::uint32_t shmResourceVersion(std::uint32_t requestedVersion) noexcept {
  return requestedVersion < kShmVersion ? requestedVersion : kShmVersion;
}

void bindShm(wl_client* client, void* data, std::uint32_t version, std::uint32_t id);

} // namespace lambda::compositor
