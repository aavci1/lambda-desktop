#pragma once

#include "Compositor/WaylandServer.hpp"

#include <cstdint>

struct wl_client;

namespace lambdaui::compositor {

void bindXdgOutputManager(wl_client* client, void* data, std::uint32_t version, std::uint32_t id);
void sendXdgOutputUpdatesForOutputGeometry(WaylandServer::Impl* server, bool includeWlOutputDone = true);

} // namespace lambdaui::compositor
