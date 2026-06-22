#pragma once

#include "Compositor/WaylandServer.hpp"

#include <algorithm>
#include <memory>
#include <vector>

#include <wayland-server-core.h>

namespace lambda::compositor {

template <typename T>
T* resourceData(wl_resource* resource) {
  if (!resource) return nullptr;
  return static_cast<T*>(wl_resource_get_user_data(resource));
}

inline WaylandServer::Impl* serverFrom(wl_resource* resource) {
  return resourceData<WaylandServer::Impl>(resource);
}

template <typename T, typename Owner, void (Owner::*Destroy)(T*)>
void destroyResourceCallback(wl_resource* resource) {
  if (auto* object = resourceData<T>(resource)) {
    (object->server->*Destroy)(object);
  }
}

template <typename T>
void eraseResource(std::vector<std::unique_ptr<T>>& resources, T const* resource) {
  resources.erase(std::remove_if(resources.begin(),
                                 resources.end(),
                                 [resource](auto const& candidate) {
                                   return candidate.get() == resource;
                                 }),
                  resources.end());
}

inline void removeResource(std::vector<wl_resource*>& resources, wl_resource* resource) {
  resources.erase(std::remove(resources.begin(), resources.end(), resource), resources.end());
}

} // namespace lambda::compositor
