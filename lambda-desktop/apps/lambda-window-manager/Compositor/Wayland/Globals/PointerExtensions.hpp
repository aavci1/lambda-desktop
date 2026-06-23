#pragma once

#include "Compositor/Wayland/WaylandServerImpl.hpp"

#include <cstdint>

struct wl_client;

namespace lambdaui::compositor {

void bindRelativePointerManager(wl_client* client, void* data, std::uint32_t version, std::uint32_t id);
void bindPointerConstraints(wl_client* client, void* data, std::uint32_t version, std::uint32_t id);
void destroyPointerExtensionResourcesForPointer(WaylandServer::Impl* server, wl_resource* pointerResource);
bool applyPointerConstraintsPendingState(WaylandServer::Impl* server,
                                         WaylandServer::Impl::Surface* surface,
                                         bool includeLivePendingState);

} // namespace lambdaui::compositor
