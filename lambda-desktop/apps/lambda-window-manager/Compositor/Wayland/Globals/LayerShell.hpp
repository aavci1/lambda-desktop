#pragma once

#include "Compositor/WaylandServer.hpp"

#include <cstdint>

struct wl_client;

namespace lambda::compositor {

inline constexpr std::uint32_t kLayerShellVersion = 4;

constexpr std::uint32_t layerShellResourceVersion(std::uint32_t requestedVersion) noexcept {
  return requestedVersion < kLayerShellVersion ? requestedVersion : kLayerShellVersion;
}

void bindLayerShell(wl_client* client, void* data, std::uint32_t version, std::uint32_t id);
void refreshShellReservedZones(WaylandServer::Impl* server);
bool reconfigureLayerSurfacesForOutputGeometry(WaylandServer::Impl* server);

} // namespace lambda::compositor
