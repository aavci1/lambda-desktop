#pragma once

#include <cstdint>

struct wl_client;

namespace flux::compositor {

void bindCompositor(wl_client* client, void* data, std::uint32_t version, std::uint32_t id);
void bindSubcompositor(wl_client* client, void* data, std::uint32_t version, std::uint32_t id);

} // namespace flux::compositor
