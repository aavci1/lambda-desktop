#include "Compositor/Wayland/Globals/Core.hpp"

#include "Compositor/Wayland/ResourceTemplates.hpp"
#include "Compositor/Wayland/WaylandServerImpl.hpp"
#include "Detail/ResizeTrace.hpp"
#include "presentation-time-server-protocol.h"
#include "viewporter-server-protocol.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

namespace flux::compositor {
namespace {

void inertDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void compositorCreateSurface(wl_client* client, wl_resource* resource, std::uint32_t id) {
  WaylandServer::Impl* server = serverFrom(resource);
  server->createSurface(client, wl_resource_get_version(resource), id);
}

void compositorCreateRegion(wl_client* client, wl_resource*, std::uint32_t id) {
  static struct wl_region_interface const regionImpl{
      inertDestroy,
      [](wl_client*, wl_resource*, std::int32_t, std::int32_t, std::int32_t, std::int32_t) {},
      [](wl_client*, wl_resource*, std::int32_t, std::int32_t, std::int32_t, std::int32_t) {},
  };
  wl_resource* region = wl_resource_create(client, &wl_region_interface, 1, id);
  wl_resource_set_implementation(region, &regionImpl, nullptr, nullptr);
}

struct wl_compositor_interface const compositorImpl{
    .create_surface = compositorCreateSurface,
    .create_region = compositorCreateRegion,
    .release = inertDestroy,
};

void subcompositorDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void subsurfaceDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void subsurfaceSetPosition(wl_client*, wl_resource* resource, std::int32_t x, std::int32_t y) {
  auto* subsurface = resourceData<WaylandServer::Impl::Subsurface>(resource);
  if (!subsurface) return;
  subsurface->x = x;
  subsurface->y = y;
}

void subsurfacePlaceAbove(wl_client*, wl_resource*, wl_resource*) {}
void subsurfacePlaceBelow(wl_client*, wl_resource*, wl_resource*) {}
void subsurfaceSetSync(wl_client*, wl_resource*) {}
void subsurfaceSetDesync(wl_client*, wl_resource*) {}

struct wl_subsurface_interface const subsurfaceImpl{
    .destroy = subsurfaceDestroy,
    .set_position = subsurfaceSetPosition,
    .place_above = subsurfacePlaceAbove,
    .place_below = subsurfacePlaceBelow,
    .set_sync = subsurfaceSetSync,
    .set_desync = subsurfaceSetDesync,
};

void subcompositorGetSubsurface(wl_client* client,
                                wl_resource* resource,
                                std::uint32_t id,
                                wl_resource* surfaceResource,
                                wl_resource* parentResource) {
  auto* server = serverFrom(resource);
  auto* surface = resourceData<WaylandServer::Impl::Surface>(surfaceResource);
  auto* parent = resourceData<WaylandServer::Impl::Surface>(parentResource);
  if (!server || !surface || !parent || surface == parent) {
    wl_resource_post_error(resource, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE, "invalid subsurface or parent surface");
    return;
  }
  if (surface->toplevel || surface->popup || surface->layerSurface || surface->cursor || surface->subsurface) {
    wl_resource_post_error(resource, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE, "wl_surface already has another role");
    return;
  }

  auto subsurface = std::make_unique<WaylandServer::Impl::Subsurface>();
  subsurface->server = server;
  subsurface->surface = surface;
  subsurface->parent = parent;
  wl_resource* subsurfaceResource = wl_resource_create(client, &wl_subsurface_interface, 1, id);
  if (!subsurfaceResource) {
    wl_client_post_no_memory(client);
    return;
  }
  subsurface->resource = subsurfaceResource;
  auto* raw = subsurface.get();
  surface->subsurface = true;
  surface->subsurfaceRole = raw;
  server->subsurfaces_.push_back(std::move(subsurface));
  wl_resource_set_implementation(subsurfaceResource,
                                 &subsurfaceImpl,
                                 raw,
                                 destroyResourceCallback<WaylandServer::Impl::Subsurface,
                                                         WaylandServer::Impl,
                                                         &WaylandServer::Impl::destroySubsurface>);
}

struct wl_subcompositor_interface const subcompositorImpl{
    .destroy = subcompositorDestroy,
    .get_subsurface = subcompositorGetSubsurface,
};

void surfaceDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void surfaceAttach(wl_client*, wl_resource* resource, wl_resource* buffer, std::int32_t x, std::int32_t y) {
  auto* surface = resourceData<WaylandServer::Impl::Surface>(resource);
  surface->pendingBuffer = buffer;
  surface->pendingBufferAttached = true;
  surface->x = x;
  surface->y = y;
}

void surfaceFrame(wl_client* client, wl_resource* resource, std::uint32_t id) {
  auto* surface = resourceData<WaylandServer::Impl::Surface>(resource);
  wl_resource* callback = wl_resource_create(client, &wl_callback_interface, 1, id);
  if (!callback) {
    wl_client_post_no_memory(client);
    return;
  }
  surface->frameCallbacks.push_back(callback);
}

WaylandServer::Impl::ShmBuffer* shmBufferFor(WaylandServer::Impl* server, wl_resource* resource) {
  auto found = std::find_if(server->shmBuffers_.begin(), server->shmBuffers_.end(),
                            [resource](auto const& buffer) { return buffer->resource == resource; });
  return found == server->shmBuffers_.end() ? nullptr : found->get();
}

WaylandServer::Impl::DmabufBuffer* dmabufBufferFor(WaylandServer::Impl* server, wl_resource* resource) {
  auto found = std::find_if(server->dmabufBuffers_.begin(), server->dmabufBuffers_.end(),
                            [resource](auto const& buffer) { return buffer->resource == resource; });
  return found == server->dmabufBuffers_.end() ? nullptr : found->get();
}

bool copyShmBufferToRgba(WaylandServer::Impl::ShmBuffer const& buffer, std::vector<std::uint8_t>& out) {
  if (!buffer.data || buffer.width <= 0 || buffer.height <= 0 || buffer.stride <= 0) {
    return false;
  }
  if (buffer.format != WL_SHM_FORMAT_ARGB8888 && buffer.format != WL_SHM_FORMAT_XRGB8888) {
    return false;
  }

  std::size_t const rowBytes = static_cast<std::size_t>(buffer.width) * 4u;
  if (buffer.offset < 0 || static_cast<std::size_t>(buffer.stride) < rowBytes) {
    return false;
  }
  std::size_t const lastRow = static_cast<std::size_t>(buffer.height - 1) * static_cast<std::size_t>(buffer.stride);
  std::size_t const end = static_cast<std::size_t>(buffer.offset) + lastRow + rowBytes;
  if (end > static_cast<std::size_t>(buffer.size)) {
    return false;
  }

  out.resize(static_cast<std::size_t>(buffer.width) * static_cast<std::size_t>(buffer.height) * 4u);
  auto const* base = static_cast<std::uint8_t const*>(buffer.data) + buffer.offset;
  for (std::int32_t y = 0; y < buffer.height; ++y) {
    auto const* src = base + static_cast<std::size_t>(y) * static_cast<std::size_t>(buffer.stride);
    auto* dst = out.data() + static_cast<std::size_t>(y) * rowBytes;
    for (std::int32_t x = 0; x < buffer.width; ++x) {
      // wl_shm ARGB/XRGB are little-endian words, so memory order is B, G, R, A/X.
      dst[static_cast<std::size_t>(x) * 4u + 0u] = src[static_cast<std::size_t>(x) * 4u + 2u];
      dst[static_cast<std::size_t>(x) * 4u + 1u] = src[static_cast<std::size_t>(x) * 4u + 1u];
      dst[static_cast<std::size_t>(x) * 4u + 2u] = src[static_cast<std::size_t>(x) * 4u + 0u];
      dst[static_cast<std::size_t>(x) * 4u + 3u] =
          buffer.format == WL_SHM_FORMAT_XRGB8888 ? 255u : src[static_cast<std::size_t>(x) * 4u + 3u];
    }
  }
  return true;
}

bool isIntegerSize(float value) {
  return std::floor(value) == value;
}

std::int32_t committedDisplayWidth(WaylandServer::Impl::Surface const* surface) {
  if (!surface) return 0;
  if (surface->destinationSet) return surface->destinationWidth;
  if (surface->sourceSet) return static_cast<std::int32_t>(surface->sourceWidth);
  return std::max(1, surface->width / std::max(1, surface->scale));
}

std::int32_t committedDisplayHeight(WaylandServer::Impl::Surface const* surface) {
  if (!surface) return 0;
  if (surface->destinationSet) return surface->destinationHeight;
  if (surface->sourceSet) return static_cast<std::int32_t>(surface->sourceHeight);
  return std::max(1, surface->height / std::max(1, surface->scale));
}

void clearMatchedConfigureCommit(WaylandServer::Impl::Surface* surface) {
  if (!surface || !surface->awaitingConfigureCommit) return;
  if (committedDisplayWidth(surface) != surface->awaitingConfigureWidth ||
      committedDisplayHeight(surface) != surface->awaitingConfigureHeight) {
    return;
  }
  surface->awaitingConfigureCommit = false;
  surface->awaitingConfigureWidth = 0;
  surface->awaitingConfigureHeight = 0;
}

bool applyViewportState(WaylandServer::Impl::Surface* surface) {
  surface->sourceSet = surface->pendingSourceSet;
  surface->sourceX = surface->pendingSourceX;
  surface->sourceY = surface->pendingSourceY;
  surface->sourceWidth = surface->pendingSourceWidth;
  surface->sourceHeight = surface->pendingSourceHeight;
  surface->destinationSet = surface->pendingDestinationSet;
  surface->destinationWidth = surface->pendingDestinationWidth;
  surface->destinationHeight = surface->pendingDestinationHeight;

  if (surface->width <= 0 || surface->height <= 0) {
    setConfiguredFrameSize(surface, 0, 0);
    return true;
  }

  if (surface->sourceSet) {
    if (surface->sourceX < 0.f || surface->sourceY < 0.f ||
        surface->sourceWidth <= 0.f || surface->sourceHeight <= 0.f) {
      if (surface->viewport && surface->viewport->resource) {
        wl_resource_post_error(surface->viewport->resource,
                               WP_VIEWPORT_ERROR_BAD_VALUE,
                               "invalid viewport source rectangle");
      }
      return false;
    }
    if (surface->sourceX + surface->sourceWidth > static_cast<float>(surface->width) ||
        surface->sourceY + surface->sourceHeight > static_cast<float>(surface->height)) {
      if (surface->viewport && surface->viewport->resource) {
        wl_resource_post_error(surface->viewport->resource,
                               WP_VIEWPORT_ERROR_OUT_OF_BUFFER,
                               "viewport source rectangle exceeds buffer");
      }
      return false;
    }
  }

  bool const activeSizing = surface->server->resizeSurface_ == surface ||
                            surface->geometryAnimationActive;
  bool const matchedConfigure = surface->awaitingConfigureCommit &&
                                committedDisplayWidth(surface) == surface->awaitingConfigureWidth &&
                                committedDisplayHeight(surface) == surface->awaitingConfigureHeight;
  if (activeSizing || matchedConfigure) {
    if (surface->frameWidth <= 0 || surface->frameHeight <= 0) {
      if (surface->server->resizeSurface_ == surface &&
          surface->server->resizeLastWidth_ > 0 &&
          surface->server->resizeLastHeight_ > 0) {
        setConfiguredFrameSize(surface, surface->server->resizeLastWidth_, surface->server->resizeLastHeight_);
      } else if (surface->geometryAnimationActive &&
                 surface->geometryAnimationTargetWidth > 0 &&
                 surface->geometryAnimationTargetHeight > 0) {
        setConfiguredFrameSize(surface,
                               surface->geometryAnimationTargetWidth,
                               surface->geometryAnimationTargetHeight);
      }
    }
    return true;
  }
  if (surface->awaitingConfigureCommit) {
    surface->awaitingConfigureCommit = false;
    surface->awaitingConfigureWidth = 0;
    surface->awaitingConfigureHeight = 0;
  }
  if ((surface->snapped || surface->maximized) &&
      surface->frameWidth > 0 &&
      surface->frameHeight > 0) {
    return true;
  }

  if (surface->destinationSet) {
    surface->frameWidth = surface->destinationWidth;
    surface->frameHeight = surface->destinationHeight;
  } else if (surface->sourceSet) {
    if (!isIntegerSize(surface->sourceWidth) || !isIntegerSize(surface->sourceHeight)) {
      if (surface->viewport && surface->viewport->resource) {
        wl_resource_post_error(surface->viewport->resource,
                               WP_VIEWPORT_ERROR_BAD_SIZE,
                               "viewport source size must be integer without a destination size");
      }
      return false;
    }
    surface->frameWidth = static_cast<std::int32_t>(surface->sourceWidth);
    surface->frameHeight = static_cast<std::int32_t>(surface->sourceHeight);
  } else {
    surface->frameWidth = committedDisplayWidth(surface);
    surface->frameHeight = committedDisplayHeight(surface);
  }

  return true;
}

bool pendingViewportSourceFitsCurrentBuffer(WaylandServer::Impl::Surface const* surface) {
  if (!surface || !surface->pendingSourceSet) return true;
  return surface->pendingSourceX >= 0.f &&
         surface->pendingSourceY >= 0.f &&
         surface->pendingSourceWidth > 0.f &&
         surface->pendingSourceHeight > 0.f &&
         surface->pendingSourceX + surface->pendingSourceWidth <= static_cast<float>(surface->width) &&
         surface->pendingSourceY + surface->pendingSourceHeight <= static_cast<float>(surface->height);
}

void surfaceCommit(wl_client*, wl_resource* resource) {
  auto* surface = resourceData<WaylandServer::Impl::Surface>(resource);
  bool const hasBufferAttach = surface->pendingBufferAttached;
  if (surface->layerSurface && !surface->layerSurface->configured && !hasBufferAttach) {
    surface->layerSurface->configured = true;
    sendLayerConfigure(surface->layerSurface);
    surface->server->flushClients();
    return;
  }
  std::vector<WaylandServer::Impl::PresentationFeedback*> supersededFeedbacks =
      std::move(surface->presentationFeedbacks);
  surface->presentationFeedbacks.clear();
  for (auto* feedback : supersededFeedbacks) {
    wp_presentation_feedback_send_discarded(feedback->resource);
    wl_resource_destroy(feedback->resource);
  }
  surface->presentationFeedbacks = std::move(surface->pendingPresentationFeedbacks);
  surface->pendingPresentationFeedbacks.clear();

  if (!hasBufferAttach) {
    if (surface->pendingSourceSet || surface->pendingDestinationSet) {
      traceResizeSurface("commit-state-defer-viewport", surface);
    } else if (pendingViewportSourceFitsCurrentBuffer(surface)) {
      if (!applyViewportState(surface)) return;
      applyLayerGeometry(surface->layerSurface);
      traceResizeSurface("commit-state", surface);
    }
    return;
  }

  surface->currentBuffer = surface->pendingBuffer;
  surface->pendingBuffer = nullptr;
  surface->pendingBufferAttached = false;
  if (surface->currentBuffer) {
    if (auto* shmBuffer = shmBufferFor(surface->server, surface->currentBuffer)) {
      std::vector<std::uint8_t> pixels;
      if (copyShmBufferToRgba(*shmBuffer, pixels)) {
        surface->rgbaPixels = std::move(pixels);
        surface->width = shmBuffer->width;
        surface->height = shmBuffer->height;
        if (!applyViewportState(surface)) return;
        clearMatchedConfigureCommit(surface);
        applyLayerGeometry(surface->layerSurface);
        surface->dmabufBuffer = nullptr;
        ++surface->serial;
        traceResizeSurface("commit-shm", surface);
      }
    } else if (auto* dmabufBuffer = dmabufBufferFor(surface->server, surface->currentBuffer)) {
      if (dmabufBuffer->width > 0 && dmabufBuffer->height > 0 && !dmabufBuffer->planes.empty()) {
        surface->rgbaPixels.clear();
        surface->width = dmabufBuffer->width;
        surface->height = dmabufBuffer->height;
        if (!applyViewportState(surface)) return;
        clearMatchedConfigureCommit(surface);
        applyLayerGeometry(surface->layerSurface);
        surface->dmabufBuffer = dmabufBuffer;
        ++surface->serial;
        traceResizeSurface("commit-dmabuf", surface);
        std::fprintf(stderr,
                     "flux-compositor: received %dx%d DMABUF buffer format=0x%08x stride=%u modifier=0x%016llx\n",
                     dmabufBuffer->width,
                     dmabufBuffer->height,
                     dmabufBuffer->format,
                     dmabufBuffer->planes.front().stride,
                     static_cast<unsigned long long>(dmabufBuffer->planes.front().modifier));
      }
    }
    wl_buffer_send_release(surface->currentBuffer);
  } else {
    surface->rgbaPixels.clear();
    surface->width = 0;
    surface->height = 0;
    setConfiguredFrameSize(surface, 0, 0);
    surface->awaitingConfigureCommit = false;
    surface->awaitingConfigureWidth = 0;
    surface->awaitingConfigureHeight = 0;
    surface->dmabufBuffer = nullptr;
    ++surface->serial;
    traceResizeSurface("commit-empty", surface);
  }
}

void surfaceSetBufferScale(wl_client*, wl_resource* resource, std::int32_t scale) {
  auto* surface = resourceData<WaylandServer::Impl::Surface>(resource);
  surface->scale = std::max(1, scale);
}

struct wl_surface_interface const surfaceImpl{
    .destroy = surfaceDestroy,
    .attach = surfaceAttach,
    .damage = [](wl_client*, wl_resource*, std::int32_t, std::int32_t, std::int32_t, std::int32_t) {},
    .frame = surfaceFrame,
    .set_opaque_region = [](wl_client*, wl_resource*, wl_resource*) {},
    .set_input_region = [](wl_client*, wl_resource*, wl_resource*) {},
    .commit = surfaceCommit,
    .set_buffer_transform = [](wl_client*, wl_resource*, std::int32_t) {},
    .set_buffer_scale = surfaceSetBufferScale,
    .damage_buffer = [](wl_client*, wl_resource*, std::int32_t, std::int32_t, std::int32_t, std::int32_t) {},
    .offset = [](wl_client*, wl_resource*, std::int32_t, std::int32_t) {},
    .get_release = surfaceFrame,
};

} // namespace

void setConfiguredFrameSize(WaylandServer::Impl::Surface* surface, std::int32_t width, std::int32_t height) {
  if (!surface) return;
  surface->frameWidth = width;
  surface->frameHeight = height;
}

std::int32_t displayWidth(WaylandServer::Impl::Surface const* surface) {
  return surface && surface->frameWidth > 0 ? surface->frameWidth : surface ? surface->width : 0;
}

std::int32_t displayHeight(WaylandServer::Impl::Surface const* surface) {
  return surface && surface->frameHeight > 0 ? surface->frameHeight : surface ? surface->height : 0;
}

void traceResizeSurface(char const* event, WaylandServer::Impl::Surface const* surface) {
  if (!surface) return;
  flux::detail::resizeTrace(
      "compositor",
      "%s surface=%llu window=%d,%d frame=%dx%d activeSizing=%d awaiting=%d %dx%d buffer=%dx%d scale=%d "
      "source=%d %.1f,%.1f %.1fx%.1f dest=%d %dx%d serial=%llu snapped=%d maximized=%d anim=%d\n",
      event,
      static_cast<unsigned long long>(surface->id),
      surface->windowX,
      surface->windowY,
      surface->frameWidth,
      surface->frameHeight,
      (surface->server->resizeSurface_ == surface ||
       surface->geometryAnimationActive ||
       surface->awaitingConfigureCommit) ? 1 : 0,
      surface->awaitingConfigureCommit ? 1 : 0,
      surface->awaitingConfigureWidth,
      surface->awaitingConfigureHeight,
      surface->width,
      surface->height,
      surface->scale,
      surface->sourceSet ? 1 : 0,
      surface->sourceX,
      surface->sourceY,
      surface->sourceWidth,
      surface->sourceHeight,
      surface->destinationSet ? 1 : 0,
      surface->destinationWidth,
      surface->destinationHeight,
      static_cast<unsigned long long>(surface->serial),
      surface->snapped ? 1 : 0,
      surface->maximized ? 1 : 0,
      surface->geometryAnimationActive ? 1 : 0);
}

void bindCompositor(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  wl_resource* resource = wl_resource_create(client, &wl_compositor_interface, std::min(version, 5u), id);
  wl_resource_set_implementation(resource, &compositorImpl, data, nullptr);
}

void bindSubcompositor(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  wl_resource* resource = wl_resource_create(client, &wl_subcompositor_interface, std::min(version, 1u), id);
  if (!resource) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(resource, &subcompositorImpl, data, nullptr);
}

wl_resource* WaylandServer::Impl::createSurface(wl_client* client, std::uint32_t version, std::uint32_t id) {
  auto surface = std::make_unique<Surface>();
  surface->server = this;
  surface->id = nextSurfaceId_++;
  wl_resource* resource = wl_resource_create(client, &wl_surface_interface, std::min(version, 5u), id);
  surface->resource = resource;
  auto* raw = surface.get();
  surfaces_.push_back(std::move(surface));
  wl_resource_set_implementation(resource,
                                 &surfaceImpl,
                                 raw,
                                 destroyResourceCallback<WaylandServer::Impl::Surface,
                                                         WaylandServer::Impl,
                                                         &WaylandServer::Impl::destroySurface>);
  for (wl_resource* output : outputResources_) {
    if (output && wl_resource_get_client(output) == client) {
      wl_surface_send_enter(resource, output);
    }
  }
  return resource;
}

} // namespace flux::compositor
