#include "Compositor/Wayland/WaylandServerImpl.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace lambda::compositor {
namespace {

using Subsurface = WaylandServer::Impl::Subsurface;
using Surface = WaylandServer::Impl::Surface;
using SurfacePendingCommitState = WaylandServer::Impl::SurfacePendingCommitState;

std::vector<Subsurface*> subsurfacePointers(WaylandServer::Impl* server) {
  std::vector<Subsurface*> result;
  if (!server) return result;
  result.reserve(server->subsurfaces_.size());
  for (auto& subsurface : server->subsurfaces_) {
    if (subsurface) result.push_back(subsurface.get());
  }
  return result;
}

std::vector<Subsurface const*> constSubsurfacePointers(WaylandServer::Impl const* server) {
  std::vector<Subsurface const*> result;
  if (!server) return result;
  result.reserve(server->subsurfaces_.size());
  for (auto const& subsurface : server->subsurfaces_) {
    if (subsurface) result.push_back(subsurface.get());
  }
  return result;
}

std::vector<Subsurface*> pendingSubsurfacesForParent(std::vector<Subsurface*> const& subsurfaces,
                                                     Surface const* parent,
                                                     SubsurfaceStackLayer layer) {
  std::vector<Subsurface*> result;
  if (!parent) return result;
  for (auto* subsurface : subsurfaces) {
    if (!subsurface || subsurface->parent != parent || subsurface->pendingStackLayer != layer) continue;
    result.push_back(subsurface);
  }
  std::stable_sort(result.begin(), result.end(), [](Subsurface const* a, Subsurface const* b) {
    return a->pendingOrder < b->pendingOrder;
  });
  return result;
}

void renumberPendingOrder(std::vector<Subsurface*> const& siblings) {
  std::uint64_t order = 1;
  for (Subsurface* sibling : siblings) {
    if (!sibling) continue;
    sibling->pendingOrder = order++;
  }
}

Subsurface* findCurrentSibling(std::vector<Subsurface*> const& subsurfaces,
                               Subsurface const* subsurface,
                               Surface const* surface) {
  if (!subsurface || !surface) return nullptr;
  for (auto* sibling : subsurfaces) {
    if (!sibling || sibling == subsurface) continue;
    if (sibling->parent == subsurface->parent && sibling->surface == surface) return sibling;
  }
  return nullptr;
}

bool postBadSibling(wl_resource* errorResource,
                    char const* requestName) {
  if (errorResource) {
    wl_resource_post_error(errorResource,
                           WL_SUBSURFACE_ERROR_BAD_SURFACE,
                           "%s sibling must be the parent or another subsurface of the same parent",
                           requestName);
  }
  return false;
}

bool setPendingPlacement(std::vector<Subsurface*> const& subsurfaces,
                         WaylandServer::Impl::Subsurface* subsurface,
                         WaylandServer::Impl::Surface* siblingSurface,
                         bool above,
                         wl_resource* errorResource) {
  if (!subsurface || !subsurface->parent || !siblingSurface) return false;

  if (siblingSurface == subsurface->parent) {
    SubsurfaceStackLayer const layer = above ? SubsurfaceStackLayer::Above : SubsurfaceStackLayer::Below;
    subsurface->pendingStackLayer = layer;
    auto siblings = pendingSubsurfacesForParent(subsurfaces, subsurface->parent, layer);
    siblings.erase(std::remove(siblings.begin(), siblings.end(), subsurface), siblings.end());
    siblings.push_back(subsurface);
    renumberPendingOrder(siblings);
    return true;
  }

  Subsurface* sibling = findCurrentSibling(subsurfaces, subsurface, siblingSurface);
  if (!sibling) return postBadSibling(errorResource, above ? "place_above" : "place_below");

  SubsurfaceStackLayer const layer = sibling->pendingStackLayer;
  subsurface->pendingStackLayer = layer;
  auto siblings = pendingSubsurfacesForParent(subsurfaces, subsurface->parent, layer);
  siblings.erase(std::remove(siblings.begin(), siblings.end(), subsurface), siblings.end());
  auto siblingIt = std::find(siblings.begin(), siblings.end(), sibling);
  if (siblingIt == siblings.end()) return postBadSibling(errorResource, above ? "place_above" : "place_below");
  siblings.insert(above ? siblingIt + 1 : siblingIt, subsurface);
  renumberPendingOrder(siblings);
  return true;
}

bool viewportPendingChanged(Surface const* surface) {
  if (!surface) return false;
  return surface->pendingViewportState.sourceSet != surface->viewportState.sourceSet ||
         surface->pendingViewportState.sourceX != surface->viewportState.sourceX ||
         surface->pendingViewportState.sourceY != surface->viewportState.sourceY ||
         surface->pendingViewportState.sourceWidth != surface->viewportState.sourceWidth ||
         surface->pendingViewportState.sourceHeight != surface->viewportState.sourceHeight ||
         surface->pendingViewportState.destinationSet != surface->viewportState.destinationSet ||
         surface->pendingViewportState.destinationWidth != surface->viewportState.destinationWidth ||
         surface->pendingViewportState.destinationHeight != surface->viewportState.destinationHeight;
}

void clearSurfacePendingCommit(Surface* surface) {
  if (!surface) return;
  surface->pendingBufferState = {};
  surface->pendingViewportState = surface->viewportState;
  surface->pendingRegionState = {};
  surface->pendingDamageState = {};
  surface->pendingBackgroundBlurRects.clear();
  surface->pendingBackgroundEffectState = {};
  surface->backgroundBlurPending = false;
  surface->backgroundEffectStatePending = false;
  surface->pendingPresentationFeedbacks.clear();
  surface->pendingFrameCallbacks.clear();
}

void releaseSupersededCachedBuffer(SurfacePendingCommitState const& cached,
                                   SurfacePendingCommitState const& incoming) {
  if (!cached.bufferState.bufferAttached ||
      !incoming.bufferState.bufferAttached ||
      !cached.bufferState.buffer ||
      cached.bufferState.buffer == incoming.bufferState.buffer) {
    return;
  }
  wl_buffer_send_release(cached.bufferState.buffer);
}

void mergePendingCommitState(SurfacePendingCommitState& cached,
                             SurfacePendingCommitState&& incoming) {
  releaseSupersededCachedBuffer(cached, incoming);
  if (incoming.bufferState.bufferAttached) {
    cached.bufferState.buffer = incoming.bufferState.buffer;
    cached.bufferState.bufferAttached = true;
  }
  if (incoming.bufferState.scaleSet) {
    cached.bufferState.scale = incoming.bufferState.scale;
    cached.bufferState.scaleSet = true;
  }
  if (incoming.bufferState.transformSet) {
    cached.bufferState.transform = incoming.bufferState.transform;
    cached.bufferState.transformSet = true;
  }
  if (incoming.bufferState.offsetSet) {
    cached.bufferState.offsetX = incoming.bufferState.offsetX;
    cached.bufferState.offsetY = incoming.bufferState.offsetY;
    cached.bufferState.offsetSet = true;
  }
  if (incoming.viewportChanged) {
    cached.viewportState = incoming.viewportState;
    cached.viewportChanged = true;
  }
  if (incoming.regionState.opaqueRegionSet) {
    cached.regionState.opaqueRegionRects = std::move(incoming.regionState.opaqueRegionRects);
    cached.regionState.opaqueRegionSet = true;
  }
  if (incoming.regionState.inputRegionSet) {
    cached.regionState.inputRegionInfinite = incoming.regionState.inputRegionInfinite;
    cached.regionState.inputRegionRects = std::move(incoming.regionState.inputRegionRects);
    cached.regionState.inputRegionSet = true;
  }
  cached.damageState.surfaceRects.insert(cached.damageState.surfaceRects.end(),
                                         incoming.damageState.surfaceRects.begin(),
                                         incoming.damageState.surfaceRects.end());
  cached.damageState.bufferRects.insert(cached.damageState.bufferRects.end(),
                                        incoming.damageState.bufferRects.begin(),
                                        incoming.damageState.bufferRects.end());
  if (incoming.backgroundBlurPending) {
    cached.pendingBackgroundBlurRects = std::move(incoming.pendingBackgroundBlurRects);
    cached.backgroundBlurPending = true;
  }
  if (incoming.backgroundEffectStatePending) {
    cached.pendingBackgroundEffectState = incoming.pendingBackgroundEffectState;
    cached.backgroundEffectStatePending = true;
  }
  cached.pendingPresentationFeedbacks.insert(cached.pendingPresentationFeedbacks.end(),
                                             incoming.pendingPresentationFeedbacks.begin(),
                                             incoming.pendingPresentationFeedbacks.end());
  cached.pendingFrameCallbacks.insert(cached.pendingFrameCallbacks.end(),
                                      incoming.pendingFrameCallbacks.begin(),
                                      incoming.pendingFrameCallbacks.end());
}

} // namespace

bool applySubsurfacePendingPosition(WaylandServer::Impl::Subsurface* subsurface) {
  if (!subsurface) return false;
  if (subsurface->x == subsurface->pendingX && subsurface->y == subsurface->pendingY) return false;
  subsurface->x = subsurface->pendingX;
  subsurface->y = subsurface->pendingY;
  return true;
}

bool applySubsurfacePendingOrder(std::vector<WaylandServer::Impl::Subsurface*> subsurfaces,
                                 WaylandServer::Impl::Surface* parent) {
  if (!parent) return false;
  bool changed = false;
  for (auto* subsurface : subsurfaces) {
    if (!subsurface || subsurface->parent != parent) continue;
    if (subsurface->stackLayer != subsurface->pendingStackLayer || subsurface->order != subsurface->pendingOrder) {
      subsurface->stackLayer = subsurface->pendingStackLayer;
      subsurface->order = subsurface->pendingOrder;
      changed = true;
    }
  }
  return changed;
}

bool applySubsurfacePendingOrder(WaylandServer::Impl* server, WaylandServer::Impl::Surface* parent) {
  return applySubsurfacePendingOrder(subsurfacePointers(server), parent);
}

bool subsurfaceIsEffectivelySynchronized(WaylandServer::Impl::Subsurface const* subsurface) {
  for (auto const* current = subsurface; current;) {
    if (current->synchronized) return true;
    Surface const* parent = current->parent;
    if (!surfaceIsSubsurface(parent)) return false;
    current = parent->subsurfaceRole;
  }
  return false;
}

SurfacePendingCommitState takeSurfacePendingCommit(WaylandServer::Impl::Surface* surface) {
  SurfacePendingCommitState state;
  if (!surface) return state;
  state.bufferState = surface->pendingBufferState;
  state.viewportState = surface->pendingViewportState;
  state.viewportChanged = viewportPendingChanged(surface);
  state.regionState = std::move(surface->pendingRegionState);
  state.damageState = std::move(surface->pendingDamageState);
  state.pendingBackgroundBlurRects = std::move(surface->pendingBackgroundBlurRects);
  state.pendingBackgroundEffectState = surface->pendingBackgroundEffectState;
  state.backgroundBlurPending = surface->backgroundBlurPending;
  state.backgroundEffectStatePending = surface->backgroundEffectStatePending;
  state.pendingPresentationFeedbacks = std::move(surface->pendingPresentationFeedbacks);
  state.pendingFrameCallbacks = std::move(surface->pendingFrameCallbacks);
  clearSurfacePendingCommit(surface);
  return state;
}

void restoreSurfacePendingCommit(WaylandServer::Impl::Surface* surface,
                                 SurfacePendingCommitState&& state) {
  if (!surface) return;
  surface->pendingBufferState = state.bufferState;
  surface->pendingViewportState = state.viewportChanged ? state.viewportState : surface->viewportState;
  surface->pendingRegionState = std::move(state.regionState);
  surface->pendingDamageState = std::move(state.damageState);
  surface->pendingBackgroundBlurRects = std::move(state.pendingBackgroundBlurRects);
  surface->pendingBackgroundEffectState = state.pendingBackgroundEffectState;
  surface->backgroundBlurPending = state.backgroundBlurPending;
  surface->backgroundEffectStatePending = state.backgroundEffectStatePending;
  surface->pendingPresentationFeedbacks = std::move(state.pendingPresentationFeedbacks);
  surface->pendingFrameCallbacks = std::move(state.pendingFrameCallbacks);
}

bool cacheSynchronizedSubsurfaceCommit(WaylandServer::Impl::Surface* surface) {
  if (!surface || !surface->subsurfaceRole || !subsurfaceIsEffectivelySynchronized(surface->subsurfaceRole)) {
    return false;
  }
  SurfacePendingCommitState incoming = takeSurfacePendingCommit(surface);
  if (surface->cachedSubsurfaceCommit) {
    mergePendingCommitState(*surface->cachedSubsurfaceCommit, std::move(incoming));
  } else {
    surface->cachedSubsurfaceCommit = std::move(incoming);
  }
  return true;
}

bool surfaceHasCachedSubsurfaceCommit(WaylandServer::Impl::Surface const* surface) {
  return surface && surface->cachedSubsurfaceCommit.has_value();
}

bool restoreCachedSubsurfaceCommit(WaylandServer::Impl::Surface* surface) {
  if (!surface || !surface->cachedSubsurfaceCommit) return false;
  restoreSurfacePendingCommit(surface, std::move(*surface->cachedSubsurfaceCommit));
  surface->cachedSubsurfaceCommit.reset();
  return true;
}

bool setSubsurfacePendingPlaceAbove(WaylandServer::Impl::Subsurface* subsurface,
                                    WaylandServer::Impl::Surface* siblingSurface,
                                    wl_resource* errorResource) {
  return setPendingPlacement(subsurfacePointers(subsurface ? subsurface->server : nullptr),
                             subsurface,
                             siblingSurface,
                             true,
                             errorResource);
}

bool setSubsurfacePendingPlaceAbove(std::vector<WaylandServer::Impl::Subsurface*> subsurfaces,
                                    WaylandServer::Impl::Subsurface* subsurface,
                                    WaylandServer::Impl::Surface* siblingSurface,
                                    wl_resource* errorResource) {
  return setPendingPlacement(subsurfaces, subsurface, siblingSurface, true, errorResource);
}

bool setSubsurfacePendingPlaceBelow(WaylandServer::Impl::Subsurface* subsurface,
                                    WaylandServer::Impl::Surface* siblingSurface,
                                    wl_resource* errorResource) {
  return setPendingPlacement(subsurfacePointers(subsurface ? subsurface->server : nullptr),
                             subsurface,
                             siblingSurface,
                             false,
                             errorResource);
}

bool setSubsurfacePendingPlaceBelow(std::vector<WaylandServer::Impl::Subsurface*> subsurfaces,
                                    WaylandServer::Impl::Subsurface* subsurface,
                                    WaylandServer::Impl::Surface* siblingSurface,
                                    wl_resource* errorResource) {
  return setPendingPlacement(subsurfaces, subsurface, siblingSurface, false, errorResource);
}

std::vector<WaylandServer::Impl::Subsurface const*> orderedSubsurfacesForParent(
    std::vector<WaylandServer::Impl::Subsurface const*> subsurfaces,
    WaylandServer::Impl::Surface const* parent,
    SubsurfaceStackLayer layer) {
  std::vector<WaylandServer::Impl::Subsurface const*> result;
  if (!parent) return result;
  for (auto const* subsurface : subsurfaces) {
    if (!subsurface || subsurface->parent != parent || subsurface->stackLayer != layer) continue;
    result.push_back(subsurface);
  }
  std::stable_sort(result.begin(), result.end(), [](Subsurface const* a, Subsurface const* b) {
    return a->order < b->order;
  });
  return result;
}

std::vector<WaylandServer::Impl::Subsurface const*> orderedSubsurfacesForParent(
    WaylandServer::Impl const* server,
    WaylandServer::Impl::Surface const* parent,
    SubsurfaceStackLayer layer) {
  return orderedSubsurfacesForParent(constSubsurfacePointers(server), parent, layer);
}

} // namespace lambda::compositor
