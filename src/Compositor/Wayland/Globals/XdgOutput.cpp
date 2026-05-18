#include "Compositor/Wayland/Globals/XdgOutput.hpp"

#include "Compositor/Wayland/ResourceTemplates.hpp"
#include "Compositor/Wayland/WaylandServerImpl.hpp"
#include "xdg-output-unstable-v1-server-protocol.h"

#include <algorithm>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

namespace flux::compositor {
namespace {

void xdgOutputManagerDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void xdgOutputDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

struct zxdg_output_v1_interface const xdgOutputImpl{
    .destroy = xdgOutputDestroy,
};

void xdgOutputManagerGetXdgOutput(wl_client* client,
                                  wl_resource* resource,
                                  std::uint32_t id,
                                  wl_resource* outputResource) {
  auto* server = serverFrom(resource);
  std::uint32_t const version = static_cast<std::uint32_t>(wl_resource_get_version(resource));
  wl_resource* xdgOutput = wl_resource_create(client, &zxdg_output_v1_interface, std::min(version, 3u), id);
  if (!xdgOutput) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(xdgOutput, &xdgOutputImpl, server, nullptr);

  WaylandOutputInfo const& output = server->output_;
  zxdg_output_v1_send_logical_position(xdgOutput, 0, 0);
  zxdg_output_v1_send_logical_size(xdgOutput, output.width, output.height);
  if (version >= ZXDG_OUTPUT_V1_NAME_SINCE_VERSION) {
    zxdg_output_v1_send_name(xdgOutput, output.name.c_str());
  }
  if (version >= ZXDG_OUTPUT_V1_DESCRIPTION_SINCE_VERSION) {
    zxdg_output_v1_send_description(xdgOutput, "Flux compositor output");
  }

  if (version >= 3) {
    wl_output_send_done(outputResource);
  } else {
    zxdg_output_v1_send_done(xdgOutput);
  }
}

struct zxdg_output_manager_v1_interface const xdgOutputManagerImpl{
    .destroy = xdgOutputManagerDestroy,
    .get_xdg_output = xdgOutputManagerGetXdgOutput,
};

} // namespace

void bindXdgOutputManager(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  wl_resource* resource = wl_resource_create(client, &zxdg_output_manager_v1_interface,
                                             std::min(version, 3u), id);
  wl_resource_set_implementation(resource, &xdgOutputManagerImpl, data, nullptr);
}

} // namespace flux::compositor
