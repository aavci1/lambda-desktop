#pragma once

#include <algorithm>
#include <cstdint>
#include <string>

struct wl_client;

namespace lambdaui::compositor {

class WaylandServer;

inline constexpr std::uint32_t kXdgWmBaseVersion = 6;
inline constexpr std::uint32_t kXdgDecorationManagerVersion = 1;

[[nodiscard]] inline std::uint32_t xdgWmBaseResourceVersion(std::uint32_t requestedVersion) {
  return std::min(requestedVersion, kXdgWmBaseVersion);
}

[[nodiscard]] inline std::uint32_t xdgDecorationResourceVersion(std::uint32_t requestedVersion) {
  return std::min(requestedVersion, kXdgDecorationManagerVersion);
}

void bindXdgWmBase(wl_client* client, void* data, std::uint32_t version, std::uint32_t id);
void bindXdgDecorationManager(wl_client* client, void* data, std::uint32_t version, std::uint32_t id);

} // namespace lambdaui::compositor
