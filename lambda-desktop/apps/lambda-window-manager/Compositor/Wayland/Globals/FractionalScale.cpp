#include "Compositor/Wayland/Globals/FractionalScale.hpp"

#include "Compositor/Wayland/FractionalScaleState.hpp"
#include "Compositor/Wayland/ResourceTemplates.hpp"
#include "Compositor/Wayland/WaylandServerImpl.hpp"
#include "fractional-scale-v1-server-protocol.h"

#include <algorithm>
#include <memory>
#include <wayland-server-core.h>

namespace lambdaui::compositor {
namespace {

void fractionalScaleManagerDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void fractionalScaleDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

struct wp_fractional_scale_v1_interface const fractionalScaleImpl{
    .destroy = fractionalScaleDestroy,
};

void fractionalScaleManagerGetFractionalScale(wl_client* client, wl_resource* resource, std::uint32_t id,
                                              wl_resource* surfaceResource) {
  auto* server = serverFrom(resource);
  auto* surface = resourceData<WaylandServer::Impl::Surface>(surfaceResource);
  if (!surface) {
    wl_resource_post_error(resource,
                           WP_FRACTIONAL_SCALE_MANAGER_V1_ERROR_FRACTIONAL_SCALE_EXISTS,
                           "fractional scale surface was destroyed");
    return;
  }
  if (surface->fractionalScale) {
    wl_resource_post_error(resource,
                           WP_FRACTIONAL_SCALE_MANAGER_V1_ERROR_FRACTIONAL_SCALE_EXISTS,
                           "surface already has a fractional scale object");
    return;
  }

  auto fractionalScale = std::make_unique<WaylandServer::Impl::FractionalScale>();
  fractionalScale->server = server;
  fractionalScale->surface = surface;
  wl_resource* fractionalScaleResource =
      wl_resource_create(client,
                         &wp_fractional_scale_v1_interface,
                         fractionalScaleResourceVersion(static_cast<std::uint32_t>(wl_resource_get_version(resource))),
                         id);
  if (!fractionalScaleResource) {
    wl_client_post_no_memory(client);
    return;
  }
  fractionalScale->resource = fractionalScaleResource;
  auto* raw = fractionalScale.get();
  surface->fractionalScale = raw;
  server->fractionalScales_.push_back(std::move(fractionalScale));
  wl_resource_set_implementation(fractionalScaleResource, &fractionalScaleImpl, raw, destroyResourceCallback<WaylandServer::Impl::FractionalScale, WaylandServer::Impl, &WaylandServer::Impl::destroyFractionalScale>);
  wp_fractional_scale_v1_send_preferred_scale(fractionalScaleResource, fractionalScalePreferredScale120(server->preferredScale_));
}

struct wp_fractional_scale_manager_v1_interface const fractionalScaleManagerImpl{
    .destroy = fractionalScaleManagerDestroy,
    .get_fractional_scale = fractionalScaleManagerGetFractionalScale,
};


void bindFractionalScaleManagerImpl(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  wl_resource* resource =
      wl_resource_create(client, &wp_fractional_scale_manager_v1_interface, fractionalScaleResourceVersion(version), id);
  if (!resource) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(resource, &fractionalScaleManagerImpl, data, nullptr);
}


} // namespace

void bindFractionalScaleManager(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  bindFractionalScaleManagerImpl(client, data, version, id);
}

} // namespace lambdaui::compositor
