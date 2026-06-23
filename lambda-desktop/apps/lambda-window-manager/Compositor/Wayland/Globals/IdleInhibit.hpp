#pragma once

#include <cstdint>

struct wl_client;

namespace lambdaui::compositor {

void bindIdleInhibitManager(wl_client* client, void* data, std::uint32_t version, std::uint32_t id);

} // namespace lambdaui::compositor
