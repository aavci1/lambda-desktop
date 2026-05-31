#include "Compositor/Wayland/Globals/CursorShape.hpp"

#include "Compositor/Wayland/ResourceTemplates.hpp"
#include "Compositor/Wayland/WaylandServerImpl.hpp"
#include "cursor-shape-v1-server-protocol.h"

#include <algorithm>
#include <array>
#include <memory>
#include <wayland-server-core.h>

namespace lambda::compositor {
namespace {

void cursorShapeManagerDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

CursorShape compositorCursorShape(std::uint32_t shape) {
  switch (shape) {
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT:
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_VERTICAL_TEXT:
    return CursorShape::IBeam;
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER:
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRAB:
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRABBING:
    return CursorShape::Hand;
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CROSSHAIR:
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CELL:
    return CursorShape::Crosshair;
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_E_RESIZE:
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_W_RESIZE:
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_EW_RESIZE:
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_COL_RESIZE:
    return CursorShape::ResizeEW;
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_N_RESIZE:
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_S_RESIZE:
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NS_RESIZE:
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ROW_RESIZE:
    return CursorShape::ResizeNS;
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NE_RESIZE:
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_SW_RESIZE:
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NESW_RESIZE:
    return CursorShape::ResizeNESW;
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NW_RESIZE:
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_SE_RESIZE:
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NWSE_RESIZE:
    return CursorShape::ResizeNWSE;
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ALL_SCROLL:
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_MOVE:
    return CursorShape::ResizeAll;
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NO_DROP:
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NOT_ALLOWED:
    return CursorShape::NotAllowed;
  default:
    return CursorShape::Arrow;
  }
}

void cursorShapeDeviceDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

constexpr std::array<SeatSerialKind, 2> kPointerCursorSerialKinds{
    SeatSerialKind::PointerEnter,
    SeatSerialKind::PointerButton,
};

void cursorShapeDeviceSetShape(wl_client*, wl_resource* resource, std::uint32_t serial, std::uint32_t shape) {
  auto* device = resourceData<WaylandServer::Impl::CursorShapeDevice>(resource);
  if (!device || !device->server) return;
  std::uint32_t const version = static_cast<std::uint32_t>(wl_resource_get_version(resource));
  if (!wp_cursor_shape_device_v1_shape_is_valid(shape, version)) {
    wl_resource_post_error(resource, WP_CURSOR_SHAPE_DEVICE_V1_ERROR_INVALID_SHAPE,
                           "invalid cursor shape %u", shape);
    return;
  }
  auto* server = device->server;
  if (!server->pointerFocus_ || !device->pointer) return;
  wl_client* const client = wl_resource_get_client(resource);
  if (wl_resource_get_client(device->pointer) != client) return;
  if (!seatSerialIsValid(server, serial, client, server->pointerFocus_, kPointerCursorSerialKinds)) return;

  CursorShape const nextShape = compositorCursorShape(shape);
  bool const changed = server->cursorSurface_ || server->cursorShape_ != nextShape;
  server->cursorSurface_ = nullptr;
  server->cursorShape_ = nextShape;
  if (changed) ++server->contentSerial_;
}

struct wp_cursor_shape_device_v1_interface const cursorShapeDeviceImpl{
    .destroy = cursorShapeDeviceDestroy,
    .set_shape = cursorShapeDeviceSetShape,
};

void cursorShapeManagerGetPointer(wl_client* client, wl_resource* resource, std::uint32_t id,
                                  wl_resource* pointer) {
  auto* server = serverFrom(resource);
  auto device = std::make_unique<WaylandServer::Impl::CursorShapeDevice>();
  device->server = server;
  device->pointer = pointer;
  wl_resource* deviceResource =
      wl_resource_create(client, &wp_cursor_shape_device_v1_interface, wl_resource_get_version(resource), id);
  if (!deviceResource) {
    wl_client_post_no_memory(client);
    return;
  }
  device->resource = deviceResource;
  auto* raw = device.get();
  server->cursorShapeDevices_.push_back(std::move(device));
  wl_resource_set_implementation(deviceResource, &cursorShapeDeviceImpl, raw, destroyResourceCallback<WaylandServer::Impl::CursorShapeDevice, WaylandServer::Impl, &WaylandServer::Impl::destroyCursorShapeDevice>);
}

struct wp_cursor_shape_manager_v1_interface const cursorShapeManagerImpl{
    .destroy = cursorShapeManagerDestroy,
    .get_pointer = cursorShapeManagerGetPointer,
};


void bindCursorShapeManagerImpl(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  wl_resource* resource =
      wl_resource_create(client, &wp_cursor_shape_manager_v1_interface, std::min(version, 1u), id);
  wl_resource_set_implementation(resource, &cursorShapeManagerImpl, data, nullptr);
}


} // namespace

void bindCursorShapeManager(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  bindCursorShapeManagerImpl(client, data, version, id);
}

} // namespace lambda::compositor
