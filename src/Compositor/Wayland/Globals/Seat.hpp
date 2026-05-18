#pragma once

#include <cstdint>

struct wl_client;

namespace flux::compositor {

void bindSeat(wl_client* client, void* data, std::uint32_t version, std::uint32_t id);

} // namespace flux::compositor
