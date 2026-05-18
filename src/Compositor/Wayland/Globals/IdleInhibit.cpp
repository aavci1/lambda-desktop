#include "Compositor/Wayland/Globals/IdleInhibit.hpp"

#include "Compositor/Wayland/ResourceTemplates.hpp"
#include "Compositor/Wayland/WaylandServerImpl.hpp"
#include "idle-inhibit-unstable-v1-server-protocol.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <wayland-server-core.h>

namespace flux::compositor {
namespace {

void idleInhibitManagerDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void idleInhibitorDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

struct zwp_idle_inhibitor_v1_interface const idleInhibitorImpl{
    .destroy = idleInhibitorDestroy,
};

void idleInhibitManagerCreateInhibitor(wl_client* client,
                                       wl_resource* resource,
                                       std::uint32_t id,
                                       wl_resource* surfaceResource) {
  auto* server = serverFrom(resource);
  auto* surface = resourceData<WaylandServer::Impl::Surface>(surfaceResource);
  if (!surface) {
    wl_resource_post_error(resource, 0, "invalid idle inhibitor surface");
    return;
  }

  auto inhibitor = std::make_unique<WaylandServer::Impl::IdleInhibitor>();
  inhibitor->server = server;
  inhibitor->surface = surface;
  wl_resource* inhibitorResource = wl_resource_create(client, &zwp_idle_inhibitor_v1_interface, 1, id);
  if (!inhibitorResource) {
    wl_client_post_no_memory(client);
    return;
  }
  inhibitor->resource = inhibitorResource;
  auto* raw = inhibitor.get();
  server->idleInhibitors_.push_back(std::move(inhibitor));
  wl_resource_set_implementation(inhibitorResource, &idleInhibitorImpl, raw, destroyResourceCallback<WaylandServer::Impl::IdleInhibitor, WaylandServer::Impl, &WaylandServer::Impl::destroyIdleInhibitor>);
  std::fprintf(stderr,
               "flux-compositor: idle inhibitors active=%zu\n",
               server->idleInhibitors_.size());
}

struct zwp_idle_inhibit_manager_v1_interface const idleInhibitManagerImpl{
    .destroy = idleInhibitManagerDestroy,
    .create_inhibitor = idleInhibitManagerCreateInhibitor,
};


void bindIdleInhibitManagerImpl(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  wl_resource* resource = wl_resource_create(client, &zwp_idle_inhibit_manager_v1_interface,
                                             std::min(version, 1u), id);
  wl_resource_set_implementation(resource, &idleInhibitManagerImpl, data, nullptr);
}


} // namespace

void bindIdleInhibitManager(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  bindIdleInhibitManagerImpl(client, data, version, id);
}

} // namespace flux::compositor
