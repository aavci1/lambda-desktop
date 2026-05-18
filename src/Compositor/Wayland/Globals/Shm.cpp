#include "Compositor/Wayland/Globals/Shm.hpp"

#include "Compositor/Wayland/ResourceTemplates.hpp"
#include "Compositor/Wayland/WaylandServerImpl.hpp"

#include <sys/mman.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

#include <algorithm>
#include <memory>

namespace flux::compositor {
namespace {

void shmPoolCreateBuffer(wl_client* client, wl_resource* resource, std::uint32_t id, std::int32_t offset,
                         std::int32_t width, std::int32_t height, std::int32_t stride, std::uint32_t format);
void shmPoolDestroy(wl_client*, wl_resource* resource);
void shmPoolResize(wl_client*, wl_resource* resource, std::int32_t size);

struct wl_shm_pool_interface const shmPoolImpl{shmPoolCreateBuffer, shmPoolDestroy, shmPoolResize};

void shmRelease(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void bufferDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

struct wl_buffer_interface const bufferImpl{bufferDestroy};

void shmCreatePool(wl_client* client, wl_resource* resource, std::uint32_t id, int fd, std::int32_t size) {
  auto* server = serverFrom(resource);
  auto pool = std::make_unique<WaylandServer::Impl::ShmPool>();
  pool->server = server;
  pool->fd = fd;
  pool->size = size;
  if (size > 0) {
    pool->data = mmap(nullptr, static_cast<std::size_t>(size), PROT_READ, MAP_SHARED, fd, 0);
    if (pool->data == MAP_FAILED) pool->data = nullptr;
  }
  wl_resource* poolResource = wl_resource_create(client, &wl_shm_pool_interface, 1, id);
  pool->resource = poolResource;
  auto* raw = pool.get();
  server->shmPools_.push_back(std::move(pool));
  wl_resource_set_implementation(poolResource,
                                 &shmPoolImpl,
                                 raw,
                                 destroyResourceCallback<WaylandServer::Impl::ShmPool,
                                                         WaylandServer::Impl,
                                                         &WaylandServer::Impl::destroyShmPool>);
}

struct wl_shm_interface const shmImpl{
    .create_pool = shmCreatePool,
    .release = shmRelease,
};

void shmPoolCreateBuffer(wl_client* client, wl_resource* resource, std::uint32_t id, std::int32_t offset,
                         std::int32_t width, std::int32_t height, std::int32_t stride, std::uint32_t format) {
  auto* pool = resourceData<WaylandServer::Impl::ShmPool>(resource);
  auto buffer = std::make_unique<WaylandServer::Impl::ShmBuffer>();
  buffer->server = pool->server;
  buffer->pool = pool;
  buffer->fd = pool->fd >= 0 ? dup(pool->fd) : -1;
  buffer->size = pool->size;
  if (buffer->fd >= 0 && buffer->size > 0) {
    buffer->data = mmap(nullptr, static_cast<std::size_t>(buffer->size), PROT_READ, MAP_SHARED, buffer->fd, 0);
    if (buffer->data == MAP_FAILED) buffer->data = nullptr;
  }
  buffer->offset = offset;
  buffer->width = width;
  buffer->height = height;
  buffer->stride = stride;
  buffer->format = format;
  wl_resource* bufferResource = wl_resource_create(client, &wl_buffer_interface, 1, id);
  buffer->resource = bufferResource;
  auto* raw = buffer.get();
  pool->server->shmBuffers_.push_back(std::move(buffer));
  wl_resource_set_implementation(bufferResource,
                                 &bufferImpl,
                                 raw,
                                 destroyResourceCallback<WaylandServer::Impl::ShmBuffer,
                                                         WaylandServer::Impl,
                                                         &WaylandServer::Impl::destroyShmBuffer>);
}

void shmPoolResize(wl_client*, wl_resource* resource, std::int32_t size) {
  auto* pool = resourceData<WaylandServer::Impl::ShmPool>(resource);
  if (pool->data) {
    munmap(pool->data, static_cast<std::size_t>(pool->size));
    pool->data = nullptr;
  }
  pool->size = size;
  if (pool->fd >= 0 && size > 0) {
    pool->data = mmap(nullptr, static_cast<std::size_t>(size), PROT_READ, MAP_SHARED, pool->fd, 0);
    if (pool->data == MAP_FAILED) pool->data = nullptr;
  }
}

void shmPoolDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

} // namespace

void bindShm(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  wl_resource* resource = wl_resource_create(client, &wl_shm_interface, std::min(version, 1u), id);
  wl_resource_set_implementation(resource, &shmImpl, data, nullptr);
  wl_shm_send_format(resource, WL_SHM_FORMAT_ARGB8888);
  wl_shm_send_format(resource, WL_SHM_FORMAT_XRGB8888);
}

} // namespace flux::compositor
