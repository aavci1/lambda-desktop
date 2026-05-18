#include "Compositor/Wayland/Globals/LinuxDmabuf.hpp"

#include "Compositor/Wayland/ResourceTemplates.hpp"
#include "Compositor/Wayland/WaylandServerImpl.hpp"
#include "linux-dmabuf-unstable-v1-server-protocol.h"

#include <drm_fourcc.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

#include <algorithm>
#include <memory>
#include <optional>

namespace flux::compositor {
namespace {

void bufferDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

struct wl_buffer_interface const bufferImpl{bufferDestroy};

std::optional<DmabufPlane> findPlane(WaylandServer::Impl::DmabufParams const* params, std::uint32_t index) {
  auto found = std::find_if(params->planes.begin(), params->planes.end(),
                            [index](DmabufPlane const& plane) { return plane.index == index; });
  if (found == params->planes.end()) return std::nullopt;
  return *found;
}

bool validateDmabufParams(WaylandServer::Impl::DmabufParams* params, std::int32_t width, std::int32_t height,
                          std::uint32_t format) {
  if (params->used) {
    wl_resource_post_error(params->resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_ALREADY_USED,
                           "zwp_linux_buffer_params_v1 was already used");
    return false;
  }
  if (width <= 0 || height <= 0) {
    wl_resource_post_error(params->resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_DIMENSIONS,
                           "dmabuf dimensions must be positive");
    return false;
  }
  if (!isSupportedDmabufFormat(format)) {
    wl_resource_post_error(params->resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_FORMAT,
                           "unsupported dmabuf format 0x%08x", format);
    return false;
  }
  if (!findPlane(params, 0).has_value()) {
    wl_resource_post_error(params->resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE,
                           "dmabuf plane 0 is required");
    return false;
  }
  return true;
}

wl_resource* createDmabufBuffer(wl_client* client, WaylandServer::Impl::DmabufParams* params, std::uint32_t id,
                                std::int32_t width, std::int32_t height, std::uint32_t format,
                                std::uint32_t flags) {
  auto buffer = std::make_unique<WaylandServer::Impl::DmabufBuffer>();
  buffer->server = params->server;
  buffer->width = width;
  buffer->height = height;
  buffer->format = format;
  buffer->flags = flags;
  buffer->planes = std::move(params->planes);
  wl_resource* bufferResource = wl_resource_create(client, &wl_buffer_interface, 1, id);
  if (!bufferResource) {
    for (auto& plane : buffer->planes) {
      if (plane.fd >= 0) close(plane.fd);
      plane.fd = -1;
    }
    wl_client_post_no_memory(client);
    return nullptr;
  }
  buffer->resource = bufferResource;
  auto* raw = buffer.get();
  params->server->dmabufBuffers_.push_back(std::move(buffer));
  wl_resource_set_implementation(bufferResource,
                                 &bufferImpl,
                                 raw,
                                 destroyResourceCallback<WaylandServer::Impl::DmabufBuffer,
                                                         WaylandServer::Impl,
                                                         &WaylandServer::Impl::destroyDmabufBuffer>);
  return bufferResource;
}

void linuxBufferParamsDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void linuxBufferParamsAdd(wl_client*, wl_resource* resource, int fd, std::uint32_t planeIndex,
                          std::uint32_t offset, std::uint32_t stride, std::uint32_t modifierHi,
                          std::uint32_t modifierLo) {
  auto* params = resourceData<WaylandServer::Impl::DmabufParams>(resource);
  if (params->used) {
    close(fd);
    wl_resource_post_error(resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_ALREADY_USED,
                           "zwp_linux_buffer_params_v1 was already used");
    return;
  }
  if (planeIndex >= 4) {
    close(fd);
    wl_resource_post_error(resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_IDX,
                           "dmabuf plane index %u is out of bounds", planeIndex);
    return;
  }
  if (findPlane(params, planeIndex).has_value()) {
    close(fd);
    wl_resource_post_error(resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_SET,
                           "dmabuf plane index %u was already set", planeIndex);
    return;
  }

  DmabufPlane plane;
  plane.fd = fd;
  plane.index = planeIndex;
  plane.offset = offset;
  plane.stride = stride;
  plane.modifier = (static_cast<std::uint64_t>(modifierHi) << 32u) | modifierLo;
  params->planes.push_back(plane);
}

void linuxBufferParamsCreate(wl_client* client, wl_resource* resource, std::int32_t width,
                             std::int32_t height, std::uint32_t format, std::uint32_t flags) {
  auto* params = resourceData<WaylandServer::Impl::DmabufParams>(resource);
  if (!validateDmabufParams(params, width, height, format)) return;
  params->used = true;
  wl_resource* buffer = createDmabufBuffer(client, params, 0, width, height, format, flags);
  if (buffer) zwp_linux_buffer_params_v1_send_created(resource, buffer);
}

void linuxBufferParamsCreateImmed(wl_client* client, wl_resource* resource, std::uint32_t bufferId,
                                  std::int32_t width, std::int32_t height, std::uint32_t format,
                                  std::uint32_t flags) {
  auto* params = resourceData<WaylandServer::Impl::DmabufParams>(resource);
  if (!validateDmabufParams(params, width, height, format)) return;
  params->used = true;
  createDmabufBuffer(client, params, bufferId, width, height, format, flags);
}

struct zwp_linux_buffer_params_v1_interface const linuxBufferParamsImpl{
    .destroy = linuxBufferParamsDestroy,
    .add = linuxBufferParamsAdd,
    .create = linuxBufferParamsCreate,
    .create_immed = linuxBufferParamsCreateImmed,
};

void linuxDmabufDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void linuxDmabufCreateParams(wl_client* client, wl_resource* resource, std::uint32_t id) {
  auto* server = serverFrom(resource);
  auto params = std::make_unique<WaylandServer::Impl::DmabufParams>();
  params->server = server;
  wl_resource* paramsResource = wl_resource_create(client, &zwp_linux_buffer_params_v1_interface,
                                                   wl_resource_get_version(resource), id);
  if (!paramsResource) {
    wl_client_post_no_memory(client);
    return;
  }
  params->resource = paramsResource;
  auto* raw = params.get();
  server->dmabufParams_.push_back(std::move(params));
  wl_resource_set_implementation(paramsResource,
                                 &linuxBufferParamsImpl,
                                 raw,
                                 destroyResourceCallback<WaylandServer::Impl::DmabufParams,
                                                         WaylandServer::Impl,
                                                         &WaylandServer::Impl::destroyDmabufParams>);
}

struct zwp_linux_dmabuf_v1_interface const linuxDmabufImpl{
    .destroy = linuxDmabufDestroy,
    .create_params = linuxDmabufCreateParams,
};

void sendDmabufFormat(wl_resource* resource, std::uint32_t format) {
  if (wl_resource_get_version(resource) >= ZWP_LINUX_DMABUF_V1_MODIFIER_SINCE_VERSION) {
    for (std::uint64_t modifier : {DRM_FORMAT_MOD_INVALID, DRM_FORMAT_MOD_LINEAR}) {
      zwp_linux_dmabuf_v1_send_modifier(resource,
                                        format,
                                        static_cast<std::uint32_t>(modifier >> 32u),
                                        static_cast<std::uint32_t>(modifier & 0xffffffffu));
    }
    return;
  }
  zwp_linux_dmabuf_v1_send_format(resource, format);
}

} // namespace

bool isSupportedDmabufFormat(std::uint32_t format) {
  return format == DRM_FORMAT_ARGB8888 || format == DRM_FORMAT_XRGB8888 ||
         format == DRM_FORMAT_ABGR8888 || format == DRM_FORMAT_XBGR8888;
}

void bindLinuxDmabuf(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  wl_resource* resource = wl_resource_create(client, &zwp_linux_dmabuf_v1_interface, std::min(version, 3u), id);
  wl_resource_set_implementation(resource, &linuxDmabufImpl, data, nullptr);
  sendDmabufFormat(resource, DRM_FORMAT_ARGB8888);
  sendDmabufFormat(resource, DRM_FORMAT_XRGB8888);
  sendDmabufFormat(resource, DRM_FORMAT_ABGR8888);
  sendDmabufFormat(resource, DRM_FORMAT_XBGR8888);
}

} // namespace flux::compositor
