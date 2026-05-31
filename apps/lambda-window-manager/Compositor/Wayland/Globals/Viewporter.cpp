#include "Compositor/Wayland/Globals/Viewporter.hpp"

#include "Compositor/Wayland/ResourceTemplates.hpp"
#include "Compositor/Wayland/ViewporterState.hpp"
#include "Compositor/Wayland/WaylandServerImpl.hpp"
#include "viewporter-server-protocol.h"

#include <memory>
#include <wayland-server-core.h>

namespace lambda::compositor {
namespace {

void viewporterDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void viewportDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void viewportSetSource(wl_client*, wl_resource* resource, wl_fixed_t x, wl_fixed_t y,
                       wl_fixed_t width, wl_fixed_t height) {
  auto* viewport = resourceData<WaylandServer::Impl::Viewport>(resource);
  if (!viewport || !viewport->surface) {
    wl_resource_post_error(resource, WP_VIEWPORT_ERROR_NO_SURFACE, "viewport surface was destroyed");
    return;
  }

  bool const unset = x == wl_fixed_from_int(-1) && y == wl_fixed_from_int(-1) &&
                     width == wl_fixed_from_int(-1) && height == wl_fixed_from_int(-1);
  if (unset) {
    viewport->surface->pendingViewportState.sourceSet = false;
    viewport->surface->pendingViewportState.sourceX = 0.f;
    viewport->surface->pendingViewportState.sourceY = 0.f;
    viewport->surface->pendingViewportState.sourceWidth = 0.f;
    viewport->surface->pendingViewportState.sourceHeight = 0.f;
    return;
  }

  double const sourceX = wl_fixed_to_double(x);
  double const sourceY = wl_fixed_to_double(y);
  double const sourceWidth = wl_fixed_to_double(width);
  double const sourceHeight = wl_fixed_to_double(height);
  if (sourceX < 0.0 || sourceY < 0.0 || sourceWidth <= 0.0 || sourceHeight <= 0.0) {
    wl_resource_post_error(resource, WP_VIEWPORT_ERROR_BAD_VALUE, "invalid viewport source rectangle");
    return;
  }

  viewport->surface->pendingViewportState.sourceSet = true;
  viewport->surface->pendingViewportState.sourceX = static_cast<float>(sourceX);
  viewport->surface->pendingViewportState.sourceY = static_cast<float>(sourceY);
  viewport->surface->pendingViewportState.sourceWidth = static_cast<float>(sourceWidth);
  viewport->surface->pendingViewportState.sourceHeight = static_cast<float>(sourceHeight);
}

void viewportSetDestination(wl_client*, wl_resource* resource, std::int32_t width, std::int32_t height) {
  auto* viewport = resourceData<WaylandServer::Impl::Viewport>(resource);
  if (!viewport || !viewport->surface) {
    wl_resource_post_error(resource, WP_VIEWPORT_ERROR_NO_SURFACE, "viewport surface was destroyed");
    return;
  }

  if (width == -1 && height == -1) {
    viewport->surface->pendingViewportState.destinationSet = false;
    viewport->surface->pendingViewportState.destinationWidth = 0;
    viewport->surface->pendingViewportState.destinationHeight = 0;
    return;
  }
  if (width <= 0 || height <= 0) {
    wl_resource_post_error(resource, WP_VIEWPORT_ERROR_BAD_VALUE, "invalid viewport destination size");
    return;
  }

  viewport->surface->pendingViewportState.destinationSet = true;
  viewport->surface->pendingViewportState.destinationWidth = width;
  viewport->surface->pendingViewportState.destinationHeight = height;
}

struct wp_viewport_interface const viewportImpl{
    .destroy = viewportDestroy,
    .set_source = viewportSetSource,
    .set_destination = viewportSetDestination,
};

void viewporterGetViewport(wl_client* client, wl_resource* resource, std::uint32_t id,
                           wl_resource* surfaceResource) {
  auto* server = serverFrom(resource);
  auto* surface = resourceData<WaylandServer::Impl::Surface>(surfaceResource);
  if (!surface) {
    wl_resource_post_error(resource, WP_VIEWPORT_ERROR_NO_SURFACE, "viewport surface was destroyed");
    return;
  }
  if (surface->viewport) {
    wl_resource_post_error(resource, WP_VIEWPORTER_ERROR_VIEWPORT_EXISTS, "surface already has a viewport");
    return;
  }

  auto viewport = std::make_unique<WaylandServer::Impl::Viewport>();
  viewport->server = server;
  viewport->surface = surface;
  wl_resource* viewportResource = wl_resource_create(client,
                                                     &wp_viewport_interface,
                                                     viewporterResourceVersion(wl_resource_get_version(resource)),
                                                     id);
  if (!viewportResource) {
    wl_client_post_no_memory(client);
    return;
  }
  viewport->resource = viewportResource;
  auto* raw = viewport.get();
  surface->viewport = raw;
  server->viewports_.push_back(std::move(viewport));
  wl_resource_set_implementation(viewportResource, &viewportImpl, raw, destroyResourceCallback<WaylandServer::Impl::Viewport, WaylandServer::Impl, &WaylandServer::Impl::destroyViewport>);
}

struct wp_viewporter_interface const viewporterImpl{
    .destroy = viewporterDestroy,
    .get_viewport = viewporterGetViewport,
};


void bindViewporterImpl(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  wl_resource* resource = wl_resource_create(client, &wp_viewporter_interface, viewporterResourceVersion(version), id);
  if (!resource) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(resource, &viewporterImpl, data, nullptr);
}


} // namespace

void bindViewporter(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  bindViewporterImpl(client, data, version, id);
}

} // namespace lambda::compositor
