#include "Compositor/Wayland/Globals/XdgOutput.hpp"

#include "Compositor/Wayland/OutputState.hpp"
#include "Compositor/Wayland/ResourceTemplates.hpp"
#include "Compositor/Wayland/WaylandServerImpl.hpp"
#include "Compositor/Wayland/XdgOutputState.hpp"
#include "xdg-output-unstable-v1-server-protocol.h"

#include <memory>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

namespace lambdaui::compositor {
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

void sendXdgOutputDone(WaylandServer::Impl::XdgOutput* xdgOutput, bool includeWlOutputDone) {
  if (!xdgOutput || !xdgOutput->resource) return;
  std::uint32_t const xdgVersion = static_cast<std::uint32_t>(wl_resource_get_version(xdgOutput->resource));
  std::uint32_t const outputVersion = xdgOutput->outputResource
                                          ? static_cast<std::uint32_t>(wl_resource_get_version(xdgOutput->outputResource))
                                          : 0u;
  switch (xdgOutputDoneKind(xdgVersion, outputVersion, includeWlOutputDone)) {
    case XdgOutputDoneKind::XdgOutput:
      zxdg_output_v1_send_done(xdgOutput->resource);
      break;
    case XdgOutputDoneKind::WlOutput:
      wl_output_send_done(xdgOutput->outputResource);
      break;
    case XdgOutputDoneKind::None:
      break;
  }
}

bool sendXdgOutputLogicalDetails(WaylandServer::Impl& server,
                                 WaylandServer::Impl::XdgOutput* xdgOutput,
                                 bool force,
                                 bool includeWlOutputDone) {
  if (!xdgOutput || !xdgOutput->resource) return false;
  OutputLayoutBox const layout = selectedOutputLayoutBox(server.output_.width,
                                                         server.output_.height,
                                                         server.preferredScale());
  if (!force &&
      !xdgOutputLogicalGeometryChanged(xdgOutput->lastLogicalX,
                                       xdgOutput->lastLogicalY,
                                       xdgOutput->lastLogicalWidth,
                                       xdgOutput->lastLogicalHeight,
                                       layout.x,
                                       layout.y,
                                       layout.width,
                                       layout.height)) {
    return false;
  }

  xdgOutput->lastLogicalX = layout.x;
  xdgOutput->lastLogicalY = layout.y;
  xdgOutput->lastLogicalWidth = layout.width;
  xdgOutput->lastLogicalHeight = layout.height;
  zxdg_output_v1_send_logical_position(xdgOutput->resource, layout.x, layout.y);
  zxdg_output_v1_send_logical_size(xdgOutput->resource, layout.width, layout.height);
  sendXdgOutputDone(xdgOutput, includeWlOutputDone);
  return true;
}

void xdgOutputManagerGetXdgOutput(wl_client* client,
                                  wl_resource* resource,
                                  std::uint32_t id,
                                  wl_resource* outputResource) {
  auto* server = serverFrom(resource);
  std::uint32_t const resourceVersion =
      xdgOutputResourceVersion(static_cast<std::uint32_t>(wl_resource_get_version(resource)));
  wl_resource* xdgOutputResource = wl_resource_create(client, &zxdg_output_v1_interface, resourceVersion, id);
  if (!xdgOutputResource) {
    wl_client_post_no_memory(client);
    return;
  }

  auto xdgOutput = std::make_unique<WaylandServer::Impl::XdgOutput>();
  xdgOutput->server = server;
  xdgOutput->resource = xdgOutputResource;
  xdgOutput->outputResource = outputResource;
  auto* raw = xdgOutput.get();
  server->xdgOutputs_.push_back(std::move(xdgOutput));
  wl_resource_set_implementation(xdgOutputResource,
                                 &xdgOutputImpl,
                                 raw,
                                 destroyResourceCallback<WaylandServer::Impl::XdgOutput, WaylandServer::Impl, &WaylandServer::Impl::destroyXdgOutput>);

  WaylandOutputInfo const& output = server->output_;
  if (resourceVersion >= ZXDG_OUTPUT_V1_NAME_SINCE_VERSION) {
    zxdg_output_v1_send_name(xdgOutputResource, output.name.c_str());
  }
  if (resourceVersion >= ZXDG_OUTPUT_V1_DESCRIPTION_SINCE_VERSION) {
    zxdg_output_v1_send_description(xdgOutputResource, "Lambda compositor output");
  }
  sendXdgOutputLogicalDetails(*server, raw, true, true);
}

struct zxdg_output_manager_v1_interface const xdgOutputManagerImpl{
    .destroy = xdgOutputManagerDestroy,
    .get_xdg_output = xdgOutputManagerGetXdgOutput,
};

} // namespace

void bindXdgOutputManager(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  wl_resource* resource = wl_resource_create(client, &zxdg_output_manager_v1_interface,
                                             xdgOutputResourceVersion(version), id);
  if (!resource) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(resource, &xdgOutputManagerImpl, data, nullptr);
}

void sendXdgOutputUpdatesForOutputGeometry(WaylandServer::Impl* server, bool includeWlOutputDone) {
  if (!server) return;
  for (auto const& xdgOutput : server->xdgOutputs_) {
    sendXdgOutputLogicalDetails(*server, xdgOutput.get(), false, includeWlOutputDone);
  }
}

} // namespace lambdaui::compositor
