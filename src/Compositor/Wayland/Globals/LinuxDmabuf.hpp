#pragma once

#include <cstdint>

struct wl_client;

namespace flux::compositor {

void bindLinuxDmabuf(wl_client* client, void* data, std::uint32_t version, std::uint32_t id);
bool isSupportedDmabufFormat(std::uint32_t format);

} // namespace flux::compositor
