#include "Compositor/Wayland/Globals/Output.hpp"

#include "Compositor/Wayland/OutputState.hpp"
#include "Compositor/Wayland/WaylandServerImpl.hpp"

#include <algorithm>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

namespace lambdaui::compositor {
namespace {

void outputRelease(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void outputResourceDestroyed(wl_resource* resource) {
  auto* server = static_cast<WaylandServer::Impl*>(wl_resource_get_user_data(resource));
  if (!server) return;
  auto& resources = server->outputResources_;
  resources.erase(std::remove(resources.begin(), resources.end(), resource), resources.end());
  for (auto const& xdgOutput : server->xdgOutputs_) {
    if (xdgOutput && xdgOutput->outputResource == resource) xdgOutput->outputResource = nullptr;
  }
}

} // namespace

void bindOutput(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  auto* server = static_cast<WaylandServer::Impl*>(data);
  WaylandOutputInfo const& output = server->output_;
  std::uint32_t const resourceVersion = outputResourceVersion(version);
  wl_resource* resource = wl_resource_create(client, &wl_output_interface, resourceVersion, id);
  if (!resource) {
    wl_client_post_no_memory(client);
    return;
  }
  static struct wl_output_interface const outputImpl{outputRelease};
  wl_resource_set_implementation(resource, &outputImpl, server, outputResourceDestroyed);
  server->outputResources_.push_back(resource);
  OutputLayoutBox const layout = selectedOutputLayoutBox(output.width,
                                                         output.height,
                                                         server->preferredScale());
  wl_output_send_geometry(resource, layout.x, layout.y, output.physicalWidthMm, output.physicalHeightMm,
                          WL_OUTPUT_SUBPIXEL_UNKNOWN, "Lambda", output.name.c_str(),
                          WL_OUTPUT_TRANSFORM_NORMAL);
  wl_output_send_mode(resource, WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED,
                      output.width, output.height, output.refreshMilliHz);
  if (resourceVersion >= 2) wl_output_send_scale(resource, outputIntegerScale(server->preferredScale()));
  if (resourceVersion >= 4) {
    wl_output_send_name(resource, output.name.c_str());
    wl_output_send_description(resource, "Lambda compositor output");
  }
  if (resourceVersion >= 2) wl_output_send_done(resource);

  for (auto const& surface : server->surfaces_) {
    if (surface && surface->resource &&
        surfaceShouldReceiveOutputEnter(true, wl_resource_get_client(surface->resource) == client)) {
      wl_surface_send_enter(surface->resource, resource);
    }
  }
}

} // namespace lambdaui::compositor
