#include "Compositor/Wayland/WaylandServerImpl.hpp"

#include <algorithm>
#include <wayland-server-core.h>

namespace lambda::compositor {
namespace {

std::uint32_t effectiveLayerWidth(WaylandServer::Impl::LayerSurface const* layerSurface) {
  if (!layerSurface) return 0;
  return layerSurface->pending.sizeSet ? layerSurface->pending.width : layerSurface->width;
}

std::uint32_t effectiveLayerHeight(WaylandServer::Impl::LayerSurface const* layerSurface) {
  if (!layerSurface) return 0;
  return layerSurface->pending.sizeSet ? layerSurface->pending.height : layerSurface->height;
}

std::uint32_t effectiveLayerAnchor(WaylandServer::Impl::LayerSurface const* layerSurface) {
  if (!layerSurface) return 0;
  return layerSurface->pending.anchorSet ? layerSurface->pending.anchor : layerSurface->anchor;
}

bool layerSurfacePendingSizeValid(WaylandServer::Impl::LayerSurface const* layerSurface) {
  std::uint32_t const anchor = effectiveLayerAnchor(layerSurface);
  bool const horizontalAnchored = (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT) != 0 &&
                                  (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT) != 0;
  bool const verticalAnchored = (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP) != 0 &&
                                (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM) != 0;
  return (effectiveLayerWidth(layerSurface) != 0 || horizontalAnchored) &&
         (effectiveLayerHeight(layerSurface) != 0 || verticalAnchored);
}

template <typename T>
bool assignIfChanged(T& target, T value) {
  if (target == value) return false;
  target = value;
  return true;
}

} // namespace

bool ackLayerSurfaceConfigure(WaylandServer::Impl::LayerSurface* layerSurface,
                              std::uint32_t serial,
                              wl_resource* errorResource) {
  if (!layerSurface) return false;
  auto configure = std::find_if(layerSurface->pendingConfigures.begin(),
                                layerSurface->pendingConfigures.end(),
                                [serial](LayerSurfaceConfigure const& item) {
                                  return item.serial == serial;
                                });
  if (configure == layerSurface->pendingConfigures.end()) {
    if (serial != 0 && layerSurface->latestConfigureSerial != 0 &&
        serial <= layerSurface->latestConfigureSerial) {
      return true;
    }
    if (errorResource) {
      wl_resource_post_error(errorResource,
                             ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_SURFACE_STATE,
                             "unknown layer-shell configure serial %u",
                             serial);
    }
    return false;
  }

  layerSurface->pending.configureAcked = true;
  layerSurface->pending.configureSerial = configure->serial;
  layerSurface->pending.configureWidth = configure->width;
  layerSurface->pending.configureHeight = configure->height;
  layerSurface->configured = true;
  layerSurface->pendingConfigures.erase(layerSurface->pendingConfigures.begin(), configure + 1);
  return true;
}

LayerSurfaceCommitResult applyLayerSurfacePendingState(WaylandServer::Impl::LayerSurface* layerSurface,
                                                       wl_resource* errorResource) {
  LayerSurfaceCommitResult result;
  if (!layerSurface) return result;
  if (!layerSurfacePendingSizeValid(layerSurface)) {
    result.valid = false;
    if (errorResource) {
      wl_resource_post_error(errorResource,
                             ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_SIZE,
                             "layer-shell size 0 requires opposing anchors");
    }
    return result;
  }

  auto& pending = layerSurface->pending;
  if (pending.sizeSet) {
    result.stateChanged |= assignIfChanged(layerSurface->width, pending.width);
    result.stateChanged |= assignIfChanged(layerSurface->height, pending.height);
    result.configureNeeded = true;
    pending.sizeSet = false;
  }
  if (pending.anchorSet) {
    result.stateChanged |= assignIfChanged(layerSurface->anchor, pending.anchor);
    result.configureNeeded = true;
    pending.anchorSet = false;
  }
  if (pending.exclusiveZoneSet) {
    result.stateChanged |= assignIfChanged(layerSurface->exclusiveZone, pending.exclusiveZone);
    pending.exclusiveZoneSet = false;
  }
  if (pending.keyboardInteractivitySet) {
    result.stateChanged |= assignIfChanged(layerSurface->keyboardInteractivity, pending.keyboardInteractivity);
    pending.keyboardInteractivitySet = false;
  }
  if (pending.marginSet) {
    result.stateChanged |= assignIfChanged(layerSurface->marginTop, pending.marginTop);
    result.stateChanged |= assignIfChanged(layerSurface->marginRight, pending.marginRight);
    result.stateChanged |= assignIfChanged(layerSurface->marginBottom, pending.marginBottom);
    result.stateChanged |= assignIfChanged(layerSurface->marginLeft, pending.marginLeft);
    result.configureNeeded = true;
    pending.marginSet = false;
  }
  if (pending.layerSet) {
    result.stateChanged |= assignIfChanged(layerSurface->layer, pending.layer);
    pending.layerSet = false;
  }
  if (pending.configureAcked) {
    result.stateChanged |= assignIfChanged(layerSurface->configureSerial, pending.configureSerial);
    result.stateChanged |= assignIfChanged(layerSurface->configureWidth, pending.configureWidth);
    result.stateChanged |= assignIfChanged(layerSurface->configureHeight, pending.configureHeight);
    pending.configureAcked = false;
  }
  return result;
}

bool markLayerSurfaceMapped(WaylandServer::Impl::LayerSurface* layerSurface) {
  if (!layerSurface || layerSurface->mapped) return false;
  layerSurface->mapped = true;
  return true;
}

bool resetLayerSurfaceForUnmap(WaylandServer::Impl::LayerSurface* layerSurface) {
  if (!layerSurface || !layerSurface->mapped) return false;
  layerSurface->mapped = false;
  layerSurface->configured = false;
  layerSurface->initialized = false;
  layerSurface->pending.configureAcked = false;
  layerSurface->pending.configureSerial = 0;
  layerSurface->pending.configureWidth = 0;
  layerSurface->pending.configureHeight = 0;
  layerSurface->pendingConfigures.clear();
  layerSurface->configureSerial = 0;
  layerSurface->configureWidth = 0;
  layerSurface->configureHeight = 0;
  return true;
}

} // namespace lambda::compositor
