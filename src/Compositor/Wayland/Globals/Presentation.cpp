#include "Compositor/Wayland/Globals/Presentation.hpp"

#include "Compositor/Wayland/ResourceTemplates.hpp"
#include "Compositor/Wayland/WaylandServerImpl.hpp"
#include "presentation-time-server-protocol.h"

#include <algorithm>
#include <ctime>
#include <memory>
#include <wayland-server-core.h>

namespace flux::compositor {
namespace {

void presentationDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void presentationFeedback(wl_client* client, wl_resource* resource, wl_resource* surfaceResource, std::uint32_t id) {
  auto* server = serverFrom(resource);
  auto* surface = resourceData<WaylandServer::Impl::Surface>(surfaceResource);
  if (!surface) {
    wl_resource_post_error(resource, 0, "invalid surface for presentation feedback");
    return;
  }

  auto feedback = std::make_unique<WaylandServer::Impl::PresentationFeedback>();
  feedback->server = server;
  feedback->surface = surface;
  wl_resource* feedbackResource = wl_resource_create(client, &wp_presentation_feedback_interface, 2, id);
  if (!feedbackResource) {
    wl_client_post_no_memory(client);
    return;
  }
  feedback->resource = feedbackResource;
  auto* raw = feedback.get();
  server->presentationFeedbacks_.push_back(std::move(feedback));
  surface->pendingPresentationFeedbacks.push_back(raw);
  wl_resource_set_implementation(feedbackResource, nullptr, raw, destroyResourceCallback<WaylandServer::Impl::PresentationFeedback, WaylandServer::Impl, &WaylandServer::Impl::destroyPresentationFeedback>);
}

struct wp_presentation_interface const presentationImpl{
    .destroy = presentationDestroy,
    .feedback = presentationFeedback,
};


void bindPresentationImpl(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  wl_resource* resource = wl_resource_create(client, &wp_presentation_interface, std::min(version, 2u), id);
  if (!resource) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(resource, &presentationImpl, data, nullptr);
  wp_presentation_send_clock_id(resource, CLOCK_MONOTONIC);
}


} // namespace

void bindPresentation(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  bindPresentationImpl(client, data, version, id);
}

} // namespace flux::compositor
