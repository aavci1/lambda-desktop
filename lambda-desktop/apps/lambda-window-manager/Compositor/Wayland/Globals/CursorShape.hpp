#pragma once

#include "Compositor/WaylandServer.hpp"

#include <cstdint>

struct wl_client;
struct wl_resource;

namespace lambdaui::compositor {

void bindCursorShapeManager(wl_client* client, void* data, std::uint32_t version, std::uint32_t id);
void destroyCursorShapeDevicesForPointer(WaylandServer::Impl* server, wl_resource* pointerResource);

} // namespace lambdaui::compositor
