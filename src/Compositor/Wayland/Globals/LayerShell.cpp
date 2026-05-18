#include "Compositor/Wayland/Globals/LayerShell.hpp"

#include "Compositor/Wayland/ResourceTemplates.hpp"
#include "Compositor/Wayland/WaylandServerImpl.hpp"
#include "wlr-layer-shell-unstable-v1-server-protocol.h"

#include <algorithm>
#include <memory>
#include <wayland-server-core.h>

namespace flux::compositor {
namespace {

void layerShellDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void layerSurfaceSetSize(wl_client*, wl_resource* resource, std::uint32_t width, std::uint32_t height) {
  auto* layerSurface = resourceData<WaylandServer::Impl::LayerSurface>(resource);
  if (!layerSurface) return;
  layerSurface->width = width;
  layerSurface->height = height;
}

void layerSurfaceSetAnchor(wl_client*, wl_resource* resource, std::uint32_t anchor) {
  auto* layerSurface = resourceData<WaylandServer::Impl::LayerSurface>(resource);
  if (!layerSurface) return;
  std::uint32_t const valid = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                             ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
  if ((anchor & ~valid) != 0) {
    wl_resource_post_error(resource, ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_ANCHOR, "invalid layer-shell anchor");
    return;
  }
  layerSurface->anchor = anchor;
}

void layerSurfaceSetExclusiveZone(wl_client*, wl_resource*, std::int32_t) {}

void layerSurfaceSetMargin(wl_client*, wl_resource* resource, std::int32_t top, std::int32_t right,
                           std::int32_t bottom, std::int32_t left) {
  auto* layerSurface = resourceData<WaylandServer::Impl::LayerSurface>(resource);
  if (!layerSurface) return;
  layerSurface->marginTop = top;
  layerSurface->marginRight = right;
  layerSurface->marginBottom = bottom;
  layerSurface->marginLeft = left;
}

void layerSurfaceSetKeyboardInteractivity(wl_client*, wl_resource*, std::uint32_t) {}

void layerSurfaceGetPopup(wl_client*, wl_resource*, wl_resource*) {}

void layerSurfaceAckConfigure(wl_client*, wl_resource*, std::uint32_t) {}

void layerSurfaceDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

struct zwlr_layer_surface_v1_interface const layerSurfaceImpl{
    .set_size = layerSurfaceSetSize,
    .set_anchor = layerSurfaceSetAnchor,
    .set_exclusive_zone = layerSurfaceSetExclusiveZone,
    .set_margin = layerSurfaceSetMargin,
    .set_keyboard_interactivity = layerSurfaceSetKeyboardInteractivity,
    .get_popup = layerSurfaceGetPopup,
    .ack_configure = layerSurfaceAckConfigure,
    .destroy = layerSurfaceDestroy,
};

void layerShellGetLayerSurface(wl_client* client, wl_resource* resource, std::uint32_t id,
                               wl_resource* surfaceResource, wl_resource*, std::uint32_t layer,
                               char const* nameSpace) {
  auto* server = serverFrom(resource);
  auto* surface = resourceData<WaylandServer::Impl::Surface>(surfaceResource);
  if (!surface || surface->toplevel || surface->layerSurface || surface->cursor || surface->subsurface) {
    wl_resource_post_error(resource, ZWLR_LAYER_SHELL_V1_ERROR_ROLE, "wl_surface already has a role");
    return;
  }
  if (layer > ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY) {
    wl_resource_post_error(resource, ZWLR_LAYER_SHELL_V1_ERROR_INVALID_LAYER, "invalid layer-shell layer");
    return;
  }

  auto layerSurface = std::make_unique<WaylandServer::Impl::LayerSurface>();
  layerSurface->server = server;
  layerSurface->surface = surface;
  layerSurface->layer = layer;
  layerSurface->nameSpace = nameSpace ? nameSpace : "";
  wl_resource* layerResource = wl_resource_create(client, &zwlr_layer_surface_v1_interface, 1, id);
  if (!layerResource) {
    wl_client_post_no_memory(client);
    return;
  }
  layerSurface->resource = layerResource;
  auto* raw = layerSurface.get();
  surface->layerSurface = raw;
  surface->toplevel = true;
  surface->cursor = false;
  if (server->cursorSurface_ == surface) server->cursorSurface_ = nullptr;
  server->layerSurfaces_.push_back(std::move(layerSurface));
  wl_resource_set_implementation(layerResource, &layerSurfaceImpl, raw, destroyResourceCallback<WaylandServer::Impl::LayerSurface, WaylandServer::Impl, &WaylandServer::Impl::destroyLayerSurface>);
}

struct zwlr_layer_shell_v1_interface const layerShellImpl{
    .get_layer_surface = layerShellGetLayerSurface,
    .destroy = layerShellDestroy,
};

void bindLayerShellImpl(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  wl_resource* resource = wl_resource_create(client, &zwlr_layer_shell_v1_interface, std::min(version, 1u), id);
  wl_resource_set_implementation(resource, &layerShellImpl, data, nullptr);
}

} // namespace

void applyLayerGeometry(WaylandServer::Impl::LayerSurface* layerSurface) {
  if (!layerSurface || !layerSurface->surface) return;
  auto hasAnchor = [](std::uint32_t anchor, std::uint32_t edge) {
    return (anchor & edge) != 0;
  };
  auto* surface = layerSurface->surface;
  std::int32_t const width = displayWidth(surface);
  std::int32_t const height = displayHeight(surface);
  if (width <= 0 || height <= 0) return;

  if (hasAnchor(layerSurface->anchor, ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT)) {
    surface->windowX = layerSurface->marginLeft;
  } else if (hasAnchor(layerSurface->anchor, ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT)) {
    surface->windowX = layerSurface->server->logicalOutputWidth() - width - layerSurface->marginRight;
  } else {
    surface->windowX = (layerSurface->server->logicalOutputWidth() - width) / 2;
  }

  if (hasAnchor(layerSurface->anchor, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP)) {
    surface->windowY = layerSurface->marginTop;
  } else if (hasAnchor(layerSurface->anchor, ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM)) {
    surface->windowY = layerSurface->server->logicalOutputHeight() - height - layerSurface->marginBottom;
  } else {
    surface->windowY = (layerSurface->server->logicalOutputHeight() - height) / 2;
  }
}

void sendLayerConfigure(WaylandServer::Impl::LayerSurface* layerSurface) {
  if (!layerSurface || !layerSurface->resource) return;
  auto hasAnchor = [](std::uint32_t anchor, std::uint32_t edge) {
    return (anchor & edge) != 0;
  };
  std::uint32_t width = layerSurface->width;
  std::uint32_t height = layerSurface->height;
  if (width == 0 &&
      hasAnchor(layerSurface->anchor, ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT) &&
      hasAnchor(layerSurface->anchor, ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT)) {
    width = static_cast<std::uint32_t>(
        std::max(1, layerSurface->server->logicalOutputWidth() - layerSurface->marginLeft - layerSurface->marginRight));
  }
  if (height == 0 &&
      hasAnchor(layerSurface->anchor, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP) &&
      hasAnchor(layerSurface->anchor, ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM)) {
    height = static_cast<std::uint32_t>(
        std::max(1, layerSurface->server->logicalOutputHeight() - layerSurface->marginTop - layerSurface->marginBottom));
  }
  zwlr_layer_surface_v1_send_configure(layerSurface->resource,
                                       layerSurface->server->nextConfigureSerial_++,
                                       width,
                                       height);
}

void bindLayerShell(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  bindLayerShellImpl(client, data, version, id);
}

} // namespace flux::compositor
