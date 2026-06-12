#include "Compositor/Wayland/CursorShapeState.hpp"

#include <doctest/doctest.h>

#include <cstdint>

namespace lambda::compositor {

TEST_CASE("cursor shape resources use the implemented protocol version") {
  CHECK(kCursorShapeVersion == 1);
  CHECK(cursorShapeResourceVersion(1) == 1);
  CHECK(cursorShapeResourceVersion(2) == 1);
}

TEST_CASE("cursor shape devices are matched by dependent wl_pointer") {
  auto* pointerA = reinterpret_cast<wl_resource*>(static_cast<std::uintptr_t>(1));
  auto* pointerB = reinterpret_cast<wl_resource*>(static_cast<std::uintptr_t>(2));

  WaylandServer::Impl::CursorShapeDevice device{};
  CHECK_FALSE(cursorShapeDeviceUsesPointer(nullptr, pointerA));
  CHECK_FALSE(cursorShapeDeviceUsesPointer(&device, pointerA));

  device.pointer = pointerA;
  CHECK(cursorShapeDeviceUsesPointer(&device, pointerA));
  CHECK_FALSE(cursorShapeDeviceUsesPointer(&device, pointerB));
}

TEST_CASE("cursor shape device lifetime follows pointer resources, not surfaces") {
  WaylandServer::Impl::CursorShapeDevice device{};
  WaylandServer::Impl::Surface surface{};

  CHECK_FALSE(cursorShapeDeviceShouldClearForSurfaceDestroy(nullptr, &surface));
  CHECK_FALSE(cursorShapeDeviceShouldClearForSurfaceDestroy(&device, nullptr));
  CHECK_FALSE(cursorShapeDeviceShouldClearForSurfaceDestroy(&device, &surface));
}

} // namespace lambda::compositor
