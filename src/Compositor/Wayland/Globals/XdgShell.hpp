#pragma once

#include <cstdint>
#include <string>

struct wl_client;

namespace flux::compositor {

class WaylandServer;

void bindXdgWmBase(wl_client* client, void* data, std::uint32_t version, std::uint32_t id);
void bindXdgDecorationManager(wl_client* client, void* data, std::uint32_t version, std::uint32_t id);

} // namespace flux::compositor
