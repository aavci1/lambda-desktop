#pragma once

#include <algorithm>
#include <memory>
#include <vector>

#include <wayland-server-core.h>

namespace flux::compositor {

template <typename T>
T* resourceData(wl_resource* resource) {
  return static_cast<T*>(wl_resource_get_user_data(resource));
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

} // namespace flux::compositor
