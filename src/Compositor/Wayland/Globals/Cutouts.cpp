#include "Compositor/Wayland/Globals/Cutouts.hpp"

#include "Compositor/Wayland/DecorationState.hpp"
#include "Compositor/Wayland/ResourceTemplates.hpp"
#include "Compositor/Wayland/WaylandServerImpl.hpp"
#include "xx-cutouts-v1-server-protocol.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <wayland-server-core.h>

namespace flux::compositor {
namespace {

void cutoutsManagerDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void cutoutsDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void cutoutsSetUnhandled(wl_client*, wl_resource* resource, wl_array* unhandled) {
  auto* cutouts = resourceData<WaylandServer::Impl::XxCutouts>(resource);
  if (!cutouts || !unhandled) return;
  if (unhandled->size == 0) {
    cutouts->pendingControlsUnhandled = false;
    return;
  }
  if (unhandled->size % sizeof(std::uint32_t) != 0u) {
    wl_resource_post_error(resource,
                           XX_CUTOUTS_V1_ERROR_INVALID_ELEMENT_ID,
                           "invalid cutout element id array size");
    return;
  }

  bool controlsUnhandled = false;
  auto* begin = static_cast<std::uint32_t*>(unhandled->data);
  auto* end = reinterpret_cast<std::uint32_t*>(static_cast<char*>(unhandled->data) + unhandled->size);
  for (auto* id = begin; id < end; ++id) {
    if (*id == kCompositorControlsCutoutId) {
      controlsUnhandled = true;
      continue;
    }
    wl_resource_post_error(resource,
                           XX_CUTOUTS_V1_ERROR_INVALID_ELEMENT_ID,
                           "invalid cutout element id %u",
                           *id);
    return;
  }
  cutouts->pendingControlsUnhandled = controlsUnhandled;
}

struct xx_cutouts_v1_interface const cutoutsImpl{
    .destroy = cutoutsDestroy,
    .set_unhandled = cutoutsSetUnhandled,
};

void cutoutsManagerGetCutouts(wl_client* client,
                              wl_resource* resource,
                              std::uint32_t id,
                              wl_resource* surfaceResource) {
  auto* server = serverFrom(resource);
  auto* surface = resourceData<WaylandServer::Impl::Surface>(surfaceResource);
  if (!surfaceIsXdgToplevel(surface)) {
    wl_resource_post_error(resource,
                           XX_CUTOUTS_MANAGER_V1_ERROR_INVALID_ROLE,
                           "xx_cutouts_v1 requires an xdg_toplevel surface");
    return;
  }
  auto* toplevel = toplevelForSurface(server, surface);
  if (!toplevel) {
    wl_resource_post_error(resource,
                           XX_CUTOUTS_MANAGER_V1_ERROR_INVALID_ROLE,
                           "xx_cutouts_v1 requires an xdg_toplevel surface");
    return;
  }
  if (toplevel->cutouts) {
    wl_resource_post_error(resource,
                           XX_CUTOUTS_MANAGER_V1_ERROR_DEFUNCT_CUTOUTS_OBJECT,
                           "xdg_toplevel already has a cutouts object");
    return;
  }

  auto cutouts = std::make_unique<WaylandServer::Impl::XxCutouts>();
  cutouts->server = server;
  cutouts->toplevel = toplevel;
  wl_resource* cutoutsResource = wl_resource_create(client, &xx_cutouts_v1_interface, 1, id);
  if (!cutoutsResource) {
    wl_client_post_no_memory(client);
    return;
  }
  cutouts->resource = cutoutsResource;
  auto* raw = cutouts.get();
  toplevel->cutouts = raw;
  toplevel->cutoutsRejected = false;
  server->cutouts_.push_back(std::move(cutouts));
  wl_resource_set_implementation(cutoutsResource,
                                 &cutoutsImpl,
                                 raw,
                                 destroyResourceCallback<WaylandServer::Impl::XxCutouts,
                                                         WaylandServer::Impl,
                                                         &WaylandServer::Impl::destroyXxCutouts>);

  if (surface->frameWidth > 0 && surface->frameHeight > 0) sendToplevelStateConfigure(server, toplevel);
}

struct xx_cutouts_manager_v1_interface const cutoutsManagerImpl{
    .destroy = cutoutsManagerDestroy,
    .get_cutouts = cutoutsManagerGetCutouts,
};

} // namespace

void bindCutoutsManager(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  wl_resource* resource =
      wl_resource_create(client, &xx_cutouts_manager_v1_interface, std::min(version, 1u), id);
  wl_resource_set_implementation(resource, &cutoutsManagerImpl, data, nullptr);
}

} // namespace flux::compositor
