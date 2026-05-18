#include "Compositor/Wayland/Globals/PointerExtensions.hpp"

#include "Compositor/Wayland/ResourceTemplates.hpp"
#include "Compositor/Wayland/WaylandServerImpl.hpp"
#include "pointer-constraints-unstable-v1-server-protocol.h"
#include "relative-pointer-unstable-v1-server-protocol.h"

#include <algorithm>
#include <memory>
#include <wayland-server-core.h>

namespace flux::compositor {
namespace {

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
  wl_resource* relativePointerResource = wl_resource_create(client, &zwp_relative_pointer_v1_interface, 1, id);
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
      wl_resource_create(client, &zwp_relative_pointer_manager_v1_interface, std::min(version, 1u), id);
  if (!resource) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(resource, &relativePointerManagerImpl, data, nullptr);
}

void updatePointerConstraintActivation(WaylandServer::Impl* server, WaylandServer::Impl::PointerConstraint* constraint) {
  if (!constraint || constraint->defunct || !constraint->resource || !constraint->surface || !constraint->pointer) return;
  bool const shouldActivate = server->pointerFocus_ == constraint->surface;
  if (shouldActivate == constraint->active) return;

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
  if (!constraint->active && constraint->lifetime == ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_ONESHOT) {
    constraint->defunct = true;
  }
}

void updatePointerConstraintsForFocusImpl(WaylandServer::Impl* server) {
  for (auto const& constraint : server->pointerConstraints_) {
    updatePointerConstraintActivation(server, constraint.get());
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
  if (!constraint) return;
  constraint->cursorHintX = static_cast<float>(wl_fixed_to_double(surfaceX));
  constraint->cursorHintY = static_cast<float>(wl_fixed_to_double(surfaceY));
}

void lockedPointerSetRegion(wl_client*, wl_resource*, wl_resource*) {}

struct zwp_locked_pointer_v1_interface const lockedPointerImpl{
    .destroy = lockedPointerDestroy,
    .set_cursor_position_hint = lockedPointerSetCursorPositionHint,
    .set_region = lockedPointerSetRegion,
};

void confinedPointerDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void confinedPointerSetRegion(wl_client*, wl_resource*, wl_resource*) {}

struct zwp_confined_pointer_v1_interface const confinedPointerImpl{
    .destroy = confinedPointerDestroy,
    .set_region = confinedPointerSetRegion,
};

void createPointerConstraint(wl_client* client,
                             wl_resource* resource,
                             std::uint32_t id,
                             wl_resource* surfaceResource,
                             wl_resource* pointerResource,
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
  wl_resource* constraintResource = wl_resource_create(client, interface, 1, id);
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
  auto* raw = constraint.get();
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
                                   wl_resource*,
                                   std::uint32_t lifetime) {
  createPointerConstraint(client,
                          resource,
                          id,
                          surface,
                          pointer,
                          lifetime,
                          WaylandServer::Impl::PointerConstraint::Kind::Lock);
}

void pointerConstraintsConfinePointer(wl_client* client,
                                      wl_resource* resource,
                                      std::uint32_t id,
                                      wl_resource* surface,
                                      wl_resource* pointer,
                                      wl_resource*,
                                      std::uint32_t lifetime) {
  createPointerConstraint(client,
                          resource,
                          id,
                          surface,
                          pointer,
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
      wl_resource_create(client, &zwp_pointer_constraints_v1_interface, std::min(version, 1u), id);
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

void bindRelativePointerManager(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  bindRelativePointerManagerImpl(client, data, version, id);
}

void bindPointerConstraints(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  bindPointerConstraintsImpl(client, data, version, id);
}

} // namespace flux::compositor
