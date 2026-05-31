#include "Compositor/Wayland/WaylandServerImpl.hpp"

#include <algorithm>
#include <vector>

namespace lambda::compositor {
namespace {

using Subsurface = WaylandServer::Impl::Subsurface;
using Surface = WaylandServer::Impl::Surface;

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
