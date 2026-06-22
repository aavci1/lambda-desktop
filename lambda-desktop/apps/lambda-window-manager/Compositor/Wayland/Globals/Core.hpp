#pragma once

#include <cstdint>

struct wl_client;

namespace lambda::compositor {

inline constexpr std::uint32_t kCompositorVersion = 5;
inline constexpr std::uint32_t kSubcompositorVersion = 1;

constexpr std::uint32_t compositorResourceVersion(std::uint32_t requestedVersion) noexcept {
  return requestedVersion < kCompositorVersion ? requestedVersion : kCompositorVersion;
}

constexpr std::uint32_t subcompositorResourceVersion(std::uint32_t requestedVersion) noexcept {
  return requestedVersion < kSubcompositorVersion ? requestedVersion : kSubcompositorVersion;
}

void bindCompositor(wl_client* client, void* data, std::uint32_t version, std::uint32_t id);
void bindSubcompositor(wl_client* client, void* data, std::uint32_t version, std::uint32_t id);

} // namespace lambda::compositor
