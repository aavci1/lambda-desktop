#include "Compositor/Wayland/Globals/PointerExtensions.hpp"

#include "Compositor/Wayland/PointerConstraintState.hpp"
#include "Compositor/Wayland/PointerExtensionState.hpp"
#include "Compositor/Wayland/ResourceTemplates.hpp"
#include "Compositor/Wayland/WaylandServerImpl.hpp"
#include "pointer-constraints-unstable-v1-server-protocol.h"
#include "relative-pointer-unstable-v1-server-protocol.h"

#include <algorithm>
#include <memory>
#include <wayland-server-core.h>

namespace lambdaui::compositor {
namespace {

std::vector<PointerConstraintRegionRect> copyPointerConstraintRegion(wl_resource* regionResource) {
  auto* region = resourceData<WaylandServer::Impl::Region>(regionResource);
  return region ? region->rects : std::vector<PointerConstraintRegionRect>{};
}

void relativePointerManagerDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void relativePointerDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

struct zwp_relative_pointer_v1_interface const relativePointerImpl{
    .destroy = relativePointerDestroy,
};

void relativePointerManagerGetRelativePointer(wl_client* client,
                                              wl_resource* resource,
                                              std::uint32_t id,
                                              wl_resource* pointerResource) {
  auto* server = serverFrom(resource);
  auto relativePointer = std::make_unique<WaylandServer::Impl::RelativePointer>();
  relativePointer->server = server;
  relativePointer->pointer = pointerResource;
  auto const version =
      relativePointerResourceVersion(static_cast<std::uint32_t>(wl_resource_get_version(resource)));
  wl_resource* relativePointerResource = wl_resource_create(client,
                                                           &zwp_relative_pointer_v1_interface,
                                                           version,
                                                           id);
  if (!relativePointerResource) {
    wl_client_post_no_memory(client);
    return;
  }
  relativePointer->resource = relativePointerResource;
  auto* raw = relativePointer.get();
  server->relativePointers_.push_back(std::move(relativePointer));
  wl_resource_set_implementation(relativePointerResource, &relativePointerImpl, raw, destroyResourceCallback<WaylandServer::Impl::RelativePointer, WaylandServer::Impl, &WaylandServer::Impl::destroyRelativePointer>);
}

struct zwp_relative_pointer_manager_v1_interface const relativePointerManagerImpl{
    .destroy = relativePointerManagerDestroy,
    .get_relative_pointer = relativePointerManagerGetRelativePointer,
};

void bindRelativePointerManagerImpl(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  wl_resource* resource =
      wl_resource_create(client,
                         &zwp_relative_pointer_manager_v1_interface,
                         relativePointerResourceVersion(version),
                         id);
  if (!resource) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(resource, &relativePointerManagerImpl, data, nullptr);
}

void makeRelativePointerResourceInert(WaylandServer::Impl::RelativePointer* relativePointer) {
  if (!relativePointer || !relativePointer->resource) return;
  wl_resource_set_user_data(relativePointer->resource, nullptr);
  relativePointer->resource = nullptr;
}

void makePointerConstraintResourceInert(WaylandServer::Impl::PointerConstraint* constraint) {
  if (!constraint || !constraint->resource) return;
  wl_resource_set_user_data(constraint->resource, nullptr);
  constraint->resource = nullptr;
}

bool updatePointerConstraintActivation(WaylandServer::Impl* server, WaylandServer::Impl::PointerConstraint* constraint) {
  if (!constraint || constraint->defunct || !constraint->resource || !constraint->surface || !constraint->pointer) {
    return false;
  }
  bool const shouldActivate = server->pointerFocus_ == constraint->surface;
  if (shouldActivate == constraint->active) return false;

  constraint->active = shouldActivate;
  if (constraint->kind == WaylandServer::Impl::PointerConstraint::Kind::Lock) {
    if (constraint->active) {
      zwp_locked_pointer_v1_send_locked(constraint->resource);
    } else {
      zwp_locked_pointer_v1_send_unlocked(constraint->resource);
    }
  } else {
    if (constraint->active) {
      zwp_confined_pointer_v1_send_confined(constraint->resource);
    } else {
      zwp_confined_pointer_v1_send_unconfined(constraint->resource);
    }
  }
  if (pointerConstraintShouldDestroyAfterDeactivation(constraint)) {
    constraint->defunct = true;
    makePointerConstraintResourceInert(constraint);
    return true;
  }
  return false;
}

void updatePointerConstraintsForFocusImpl(WaylandServer::Impl* server) {
  for (auto it = server->pointerConstraints_.begin(); it != server->pointerConstraints_.end();) {
    auto* constraint = it->get();
    if (updatePointerConstraintActivation(server, constraint)) {
      server->destroyPointerConstraint(constraint);
      it = server->pointerConstraints_.begin();
    } else {
      ++it;
    }
  }
}

WaylandServer::Impl::PointerConstraint* activePointerConstraintImpl(WaylandServer::Impl* server) {
  for (auto const& constraint : server->pointerConstraints_) {
    if (constraint->active && !constraint->defunct && constraint->surface == server->pointerFocus_) {
      return constraint.get();
    }
  }
  return nullptr;
}

bool surfaceAlreadyConstrained(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface, wl_resource* pointer) {
  if (!surface || !pointer) return false;
  wl_client* pointerClient = wl_resource_get_client(pointer);
  for (auto const& constraint : server->pointerConstraints_) {
    if (constraint->surface == surface && constraint->pointer &&
        wl_resource_get_client(constraint->pointer) == pointerClient && !constraint->defunct) {
      return true;
    }
  }
  return false;
}

void pointerConstraintsDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void lockedPointerDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void lockedPointerSetCursorPositionHint(wl_client*, wl_resource* resource, wl_fixed_t surfaceX, wl_fixed_t surfaceY) {
  auto* constraint = resourceData<WaylandServer::Impl::PointerConstraint>(resource);
  if (!constraint || constraint->defunct) return;
  constraint->pendingCursorHintX = static_cast<float>(wl_fixed_to_double(surfaceX));
  constraint->pendingCursorHintY = static_cast<float>(wl_fixed_to_double(surfaceY));
  constraint->pendingCursorHintSet = true;
}

void pointerConstraintSetRegion(wl_resource* resource, wl_resource* regionResource) {
  auto* constraint = resourceData<WaylandServer::Impl::PointerConstraint>(resource);
  if (!constraint || constraint->defunct) return;
  constraint->pendingRegionInfinite = regionResource == nullptr;
  constraint->pendingRegionRects = copyPointerConstraintRegion(regionResource);
  constraint->pendingRegionSet = true;
}

void lockedPointerSetRegion(wl_client*, wl_resource* resource, wl_resource* regionResource) {
  pointerConstraintSetRegion(resource, regionResource);
}

struct zwp_locked_pointer_v1_interface const lockedPointerImpl{
    .destroy = lockedPointerDestroy,
    .set_cursor_position_hint = lockedPointerSetCursorPositionHint,
    .set_region = lockedPointerSetRegion,
};

void confinedPointerDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void confinedPointerSetRegion(wl_client*, wl_resource* resource, wl_resource* regionResource) {
  pointerConstraintSetRegion(resource, regionResource);
}

struct zwp_confined_pointer_v1_interface const confinedPointerImpl{
    .destroy = confinedPointerDestroy,
    .set_region = confinedPointerSetRegion,
};

void createPointerConstraint(wl_client* client,
                             wl_resource* resource,
                             std::uint32_t id,
                             wl_resource* surfaceResource,
                             wl_resource* pointerResource,
                             wl_resource* regionResource,
                             std::uint32_t lifetime,
                             WaylandServer::Impl::PointerConstraint::Kind kind) {
  auto* server = serverFrom(resource);
  auto* surface = resourceData<WaylandServer::Impl::Surface>(surfaceResource);
  if (surfaceAlreadyConstrained(server, surface, pointerResource)) {
    wl_resource_post_error(resource,
                           ZWP_POINTER_CONSTRAINTS_V1_ERROR_ALREADY_CONSTRAINED,
                           "surface already has a pointer constraint for this seat");
    return;
  }

  wl_interface const* interface = kind == WaylandServer::Impl::PointerConstraint::Kind::Lock
                                      ? &zwp_locked_pointer_v1_interface
                                      : &zwp_confined_pointer_v1_interface;
  wl_resource* constraintResource = wl_resource_create(client,
                                                       interface,
                                                       pointerConstraintsResourceVersion(
                                                           static_cast<std::uint32_t>(wl_resource_get_version(resource))),
                                                       id);
  if (!constraintResource) {
    wl_client_post_no_memory(client);
    return;
  }

  auto constraint = std::make_unique<WaylandServer::Impl::PointerConstraint>();
  constraint->server = server;
  constraint->resource = constraintResource;
  constraint->surface = surface;
  constraint->pointer = pointerResource;
  constraint->kind = kind;
  constraint->lifetime = lifetime;
  constraint->regionInfinite = regionResource == nullptr;
  constraint->regionRects = copyPointerConstraintRegion(regionResource);
  auto* raw = constraint.get();
  rebuildPointerConstraintEffectiveRegion(raw);
  server->pointerConstraints_.push_back(std::move(constraint));
  wl_resource_set_implementation(constraintResource,
                                 kind == WaylandServer::Impl::PointerConstraint::Kind::Lock
                                     ? static_cast<void const*>(&lockedPointerImpl)
                                     : static_cast<void const*>(&confinedPointerImpl),
                                 raw,
                                 destroyResourceCallback<WaylandServer::Impl::PointerConstraint, WaylandServer::Impl, &WaylandServer::Impl::destroyPointerConstraint>);
  updatePointerConstraintActivation(server, raw);
}

void pointerConstraintsLockPointer(wl_client* client,
                                   wl_resource* resource,
                                   std::uint32_t id,
                                   wl_resource* surface,
                                   wl_resource* pointer,
                                   wl_resource* region,
                                   std::uint32_t lifetime) {
  createPointerConstraint(client,
                          resource,
                          id,
                          surface,
                          pointer,
                          region,
                          lifetime,
                          WaylandServer::Impl::PointerConstraint::Kind::Lock);
}

void pointerConstraintsConfinePointer(wl_client* client,
                                      wl_resource* resource,
                                      std::uint32_t id,
                                      wl_resource* surface,
                                      wl_resource* pointer,
                                      wl_resource* region,
                                      std::uint32_t lifetime) {
  createPointerConstraint(client,
                          resource,
                          id,
                          surface,
                          pointer,
                          region,
                          lifetime,
                          WaylandServer::Impl::PointerConstraint::Kind::Confine);
}

struct zwp_pointer_constraints_v1_interface const pointerConstraintsImpl{
    .destroy = pointerConstraintsDestroy,
    .lock_pointer = pointerConstraintsLockPointer,
    .confine_pointer = pointerConstraintsConfinePointer,
};

void bindPointerConstraintsImpl(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  wl_resource* resource =
      wl_resource_create(client, &zwp_pointer_constraints_v1_interface, pointerConstraintsResourceVersion(version), id);
  if (!resource) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(resource, &pointerConstraintsImpl, data, nullptr);
}


} // namespace


void updatePointerConstraintsForFocus(WaylandServer::Impl* server) {
  updatePointerConstraintsForFocusImpl(server);
}

WaylandServer::Impl::PointerConstraint* activePointerConstraint(WaylandServer::Impl* server) {
  return activePointerConstraintImpl(server);
}

void destroyPointerExtensionResourcesForPointer(WaylandServer::Impl* server, wl_resource* pointerResource) {
  if (!server || !pointerResource) return;

  while (true) {
    auto found = std::find_if(server->relativePointers_.begin(),
                              server->relativePointers_.end(),
                              [pointerResource](auto const& relativePointer) {
                                return relativePointerUsesPointer(relativePointer.get(), pointerResource);
                              });
    if (found == server->relativePointers_.end()) break;
    auto* relativePointer = found->get();
    makeRelativePointerResourceInert(relativePointer);
    server->destroyRelativePointer(relativePointer);
  }

  while (true) {
    auto found = std::find_if(server->pointerConstraints_.begin(),
                              server->pointerConstraints_.end(),
                              [pointerResource](auto const& constraint) {
                                return pointerConstraintUsesPointer(constraint.get(), pointerResource);
                              });
    if (found == server->pointerConstraints_.end()) break;
    auto* constraint = found->get();
    makePointerConstraintResourceInert(constraint);
    server->destroyPointerConstraint(constraint);
  }
}

bool applyPointerConstraintsPendingState(WaylandServer::Impl* server,
                                         WaylandServer::Impl::Surface* surface,
                                         bool includeLivePendingState) {
  if (!server || !surface) return false;
  bool changed = false;
  for (auto const& state : surface->pendingPointerConstraintStates) {
    changed |= applyPointerConstraintCommitState(state);
  }
  surface->pendingPointerConstraintStates.clear();
  for (auto const& constraint : server->pointerConstraints_) {
    if (constraint->surface != surface) continue;
    changed |= includeLivePendingState ? applyPointerConstraintPendingState(constraint.get())
                                       : rebuildPointerConstraintEffectiveRegion(constraint.get());
  }
  return changed;
}

void bindRelativePointerManager(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  bindRelativePointerManagerImpl(client, data, version, id);
}

void bindPointerConstraints(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  bindPointerConstraintsImpl(client, data, version, id);
}

} // namespace lambdaui::compositor
