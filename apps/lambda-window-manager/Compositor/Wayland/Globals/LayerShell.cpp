#include "Compositor/Wayland/Globals/LayerShell.hpp"

#include "Compositor/Wayland/LayerShellState.hpp"
#include "Compositor/Wayland/LayerShellZones.hpp"
#include "Compositor/Wayland/ResourceTemplates.hpp"
#include "Compositor/Wayland/WaylandServerImpl.hpp"
#include "Compositor/Wayland/XdgPopupState.hpp"
#include "wlr-layer-shell-unstable-v1-server-protocol.h"

#include <memory>
#include <vector>
#include <wayland-server-core.h>

namespace lambda::compositor {
namespace {

constexpr std::uint32_t kValidLayerAnchors = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                                             ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                                             ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                             ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
constexpr std::size_t kMaxPendingLayerConfigures = 16;

void layerShellDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void layerSurfaceSetSize(wl_client*, wl_resource* resource, std::uint32_t width, std::uint32_t height) {
  auto* layerSurface = resourceData<WaylandServer::Impl::LayerSurface>(resource);
  if (!layerSurface) return;
  layerSurface->pending.width = width;
  layerSurface->pending.height = height;
  layerSurface->pending.sizeSet = true;
}

void layerSurfaceSetAnchor(wl_client*, wl_resource* resource, std::uint32_t anchor) {
  auto* layerSurface = resourceData<WaylandServer::Impl::LayerSurface>(resource);
  if (!layerSurface) return;
  if ((anchor & ~kValidLayerAnchors) != 0) {
    wl_resource_post_error(resource, ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_ANCHOR, "invalid layer-shell anchor");
    return;
  }
  layerSurface->pending.anchor = anchor;
  layerSurface->pending.anchorSet = true;
}

void layerSurfaceSetExclusiveZone(wl_client*, wl_resource* resource, std::int32_t zone) {
  auto* layerSurface = resourceData<WaylandServer::Impl::LayerSurface>(resource);
  if (!layerSurface) return;
  layerSurface->pending.exclusiveZone = zone;
  layerSurface->pending.exclusiveZoneSet = true;
}

void layerSurfaceSetMargin(wl_client*, wl_resource* resource, std::int32_t top, std::int32_t right,
                           std::int32_t bottom, std::int32_t left) {
  auto* layerSurface = resourceData<WaylandServer::Impl::LayerSurface>(resource);
  if (!layerSurface) return;
  layerSurface->pending.marginTop = top;
  layerSurface->pending.marginRight = right;
  layerSurface->pending.marginBottom = bottom;
  layerSurface->pending.marginLeft = left;
  layerSurface->pending.marginSet = true;
}

void layerSurfaceSetKeyboardInteractivity(wl_client*, wl_resource* resource, std::uint32_t interactivity) {
  auto* layerSurface = resourceData<WaylandServer::Impl::LayerSurface>(resource);
  if (!layerSurface) return;
  std::uint32_t const value =
      normalizeLayerShellKeyboardInteractivity(static_cast<std::uint32_t>(wl_resource_get_version(resource)),
                                               interactivity);
  if (!validLayerShellKeyboardInteractivity(value)) {
    wl_resource_post_error(resource,
                           ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_KEYBOARD_INTERACTIVITY,
                           "invalid layer-shell keyboard interactivity");
    return;
  }
  layerSurface->pending.keyboardInteractivity = value;
  layerSurface->pending.keyboardInteractivitySet = true;
}

void layerSurfaceGetPopup(wl_client*, wl_resource* resource, wl_resource* popupResource) {
  auto* layerSurface = resourceData<WaylandServer::Impl::LayerSurface>(resource);
  auto* popup = resourceData<WaylandServer::Impl::XdgPopup>(popupResource);
  if (!layerSurface || !layerSurface->surface || !popup || !popup->xdgSurface ||
      !popup->xdgSurface->surface) {
    return;
  }

  WaylandServer::Impl::Surface* popupSurface = popup->xdgSurface->surface;
  WaylandServer::Impl::Surface* oldParent = popup->parentSurface;
  WaylandServer::Impl::Surface* newParent = layerSurface->surface;
  if (oldParent == newParent) {
    return;
  }

  XdgPopupReparentGeometry const geometry = xdgPopupReparentGeometry({
      .oldParentX = oldParent ? oldParent->windowX : 0,
      .oldParentY = oldParent ? oldParent->windowY : 0,
      .newParentX = newParent->windowX,
      .newParentY = newParent->windowY,
      .popupWindowX = popupSurface->windowX,
      .popupWindowY = popupSurface->windowY,
  });
  popupSurface->windowX = geometry.popupWindowX;
  popupSurface->windowY = geometry.popupWindowY;
  popup->parentSurface = newParent;
  popup->configuredX = geometry.configuredX;
  popup->configuredY = geometry.configuredY;
}

void layerSurfaceAckConfigure(wl_client*, wl_resource* resource, std::uint32_t serial) {
  auto* layerSurface = resourceData<WaylandServer::Impl::LayerSurface>(resource);
  (void)ackLayerSurfaceConfigure(layerSurface, serial, resource);
}

void layerSurfaceDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void layerSurfaceSetLayer(wl_client*, wl_resource* resource, std::uint32_t layer) {
  auto* layerSurface = resourceData<WaylandServer::Impl::LayerSurface>(resource);
  if (!layerSurface) return;
  if (layer > ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY) {
    wl_resource_post_error(resource, ZWLR_LAYER_SHELL_V1_ERROR_INVALID_LAYER, "invalid layer-shell layer");
    return;
  }
  layerSurface->pending.layer = layer;
  layerSurface->pending.layerSet = true;
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
    .set_layer = layerSurfaceSetLayer,
};

void layerShellGetLayerSurface(wl_client* client, wl_resource* resource, std::uint32_t id,
                               wl_resource* surfaceResource, wl_resource*, std::uint32_t layer,
                               char const* nameSpace) {
  auto* server = serverFrom(resource);
  auto* surface = resourceData<WaylandServer::Impl::Surface>(surfaceResource);
  if (!surfaceHasNoRole(surface)) {
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
  layerSurface->pending.layer = layer;
  layerSurface->nameSpace = nameSpace ? nameSpace : "";
  wl_resource* layerResource = wl_resource_create(client,
                                                  &zwlr_layer_surface_v1_interface,
                                                  layerShellResourceVersion(wl_resource_get_version(resource)),
                                                  id);
  if (!layerResource) {
    wl_client_post_no_memory(client);
    return;
  }
  layerSurface->resource = layerResource;
  auto* raw = layerSurface.get();
  surface->layerSurface = raw;
  surface->role = SurfaceRole::LayerSurface;
  if (server->cursorSurface_ == surface) server->cursorSurface_ = nullptr;
  server->layerSurfaces_.push_back(std::move(layerSurface));
  wl_resource_set_implementation(layerResource, &layerSurfaceImpl, raw, destroyResourceCallback<WaylandServer::Impl::LayerSurface, WaylandServer::Impl, &WaylandServer::Impl::destroyLayerSurface>);
}

struct zwlr_layer_shell_v1_interface const layerShellImpl{
    .get_layer_surface = layerShellGetLayerSurface,
    .destroy = layerShellDestroy,
};

void bindLayerShellImpl(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  wl_resource* resource =
      wl_resource_create(client, &zwlr_layer_shell_v1_interface, layerShellResourceVersion(version), id);
  if (!resource) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(resource, &layerShellImpl, data, nullptr);
}

} // namespace

void refreshShellReservedZones(WaylandServer::Impl* server) {
  if (!server) return;
  std::vector<LayerShellReservedZoneInput> inputs;
  inputs.reserve(server->layerSurfaces_.size());
  for (auto const& layerSurface : server->layerSurfaces_) {
    if (!layerSurface || !layerSurface->mapped) continue;
    std::int32_t const extent = layerSurface->surface && displayHeight(layerSurface->surface) > 0
                                    ? displayHeight(layerSurface->surface)
                                    : static_cast<std::int32_t>(layerSurface->height);
    inputs.push_back({
        .nameSpace = layerSurface->nameSpace.c_str(),
        .exclusiveZone = layerSurface->exclusiveZone,
        .anchor = layerSurface->anchor,
        .marginBottom = layerSurface->marginBottom,
        .extent = extent,
    });
  }
  LayerShellReservedZones const zones = aggregateLayerShellReservedZones(inputs);
  if (zones.dock == server->dockReservedZone_) return;
  server->dockReservedZone_ = zones.dock;
  ++server->contentSerial_;
  server->notifyShellStateChanged();
}

bool applyLayerGeometry(WaylandServer::Impl::LayerSurface* layerSurface) {
  if (!layerSurface || !layerSurface->surface) return false;
  auto* surface = layerSurface->surface;
  std::int32_t const width = displayWidth(surface);
  std::int32_t const height = displayHeight(surface);
  if (width <= 0 || height <= 0) return false;

  std::int32_t oldX = surface->windowX;
  std::int32_t oldY = surface->windowY;

  LayerShellPlacement const placement = resolveLayerShellPlacement({
      .anchor = layerSurface->anchor,
      .marginTop = layerSurface->marginTop,
      .marginRight = layerSurface->marginRight,
      .marginBottom = layerSurface->marginBottom,
      .marginLeft = layerSurface->marginLeft,
      .surfaceWidth = width,
      .surfaceHeight = height,
      .outputWidth = layerSurface->server->logicalOutputWidth(),
      .outputHeight = layerSurface->server->logicalOutputHeight(),
  });
  surface->windowX = placement.x;
  surface->windowY = placement.y;
  return surface->windowX != oldX || surface->windowY != oldY;
}

void sendLayerConfigure(WaylandServer::Impl::LayerSurface* layerSurface) {
  if (!layerSurface || !layerSurface->resource) return;
  LayerShellConfigureSize const size = resolveLayerShellConfigureSize({
      .requestedWidth = layerSurface->width,
      .requestedHeight = layerSurface->height,
      .anchor = layerSurface->anchor,
      .marginTop = layerSurface->marginTop,
      .marginRight = layerSurface->marginRight,
      .marginBottom = layerSurface->marginBottom,
      .marginLeft = layerSurface->marginLeft,
      .outputWidth = layerSurface->server->logicalOutputWidth(),
      .outputHeight = layerSurface->server->logicalOutputHeight(),
  });
  std::uint32_t const serial = layerSurface->server->nextConfigureSerial_++;
  layerSurface->latestConfigureSerial = serial;
  layerSurface->pendingConfigures.push_back({
      .serial = serial,
      .width = size.width,
      .height = size.height,
  });
  while (layerSurface->pendingConfigures.size() > kMaxPendingLayerConfigures) {
    layerSurface->pendingConfigures.erase(layerSurface->pendingConfigures.begin());
  }
  zwlr_layer_surface_v1_send_configure(layerSurface->resource,
                                       serial,
                                       size.width,
                                       size.height);
}

bool reconfigureLayerSurfacesForOutputGeometry(WaylandServer::Impl* server) {
  if (!server) return false;

  bool touched = false;
  for (auto const& layerSurface : server->layerSurfaces_) {
    if (!layerSurface || !layerSurface->surface || !layerSurface->resource) continue;
    touched = true;
    applyLayerGeometry(layerSurface.get());
    sendLayerConfigure(layerSurface.get());
  }

  return touched;
}

void bindLayerShell(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  bindLayerShellImpl(client, data, version, id);
}

} // namespace lambda::compositor
