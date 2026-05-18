#include "DemoClientSupport.hpp"
#include "xdg-shell-client-protocol.h"

#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

namespace {

constexpr int kParentWidth = 420;
constexpr int kParentHeight = 260;
constexpr int kPopupWidth = 180;
constexpr int kPopupHeight = 96;

std::atomic<bool> gRunning{true};

struct Buffer {
  wl_buffer* buffer = nullptr;
  void* pixels = nullptr;
  int fd = -1;
  int width = 0;
  int height = 0;
  int stride = 0;
  int size = 0;
};

struct DemoClient {
  wl_display* display = nullptr;
  wl_registry* registry = nullptr;
  wl_compositor* compositor = nullptr;
  wl_subcompositor* subcompositor = nullptr;
  wl_shm* shm = nullptr;
  xdg_wm_base* wmBase = nullptr;
  wl_surface* surface = nullptr;
  xdg_surface* xdgSurface = nullptr;
  xdg_toplevel* toplevel = nullptr;
  Buffer parentBuffer;
  bool configured = false;
  wl_surface* childSurface = nullptr;
  wl_subsurface* childSubsurface = nullptr;
  Buffer childBuffer;
  wl_surface* popupSurface = nullptr;
  xdg_surface* popupXdgSurface = nullptr;
  xdg_popup* popup = nullptr;
  Buffer popupBuffer;
  bool popupCommitted = false;
};

int createSharedMemoryFile(char const* name, std::size_t size) {
  int fd = memfd_create(name, MFD_CLOEXEC | MFD_ALLOW_SEALING);
  if (fd < 0) throw std::runtime_error(std::string("memfd_create failed: ") + std::strerror(errno));
  if (ftruncate(fd, static_cast<off_t>(size)) != 0) {
    close(fd);
    throw std::runtime_error(std::string("ftruncate failed: ") + std::strerror(errno));
  }
  return fd;
}

void fillBuffer(Buffer& buffer, bool popup) {
  auto* dst = static_cast<std::uint8_t*>(buffer.pixels);
  for (int y = 0; y < buffer.height; ++y) {
    for (int x = 0; x < buffer.width; ++x) {
      std::size_t const offset = static_cast<std::size_t>(y) * buffer.stride + static_cast<std::size_t>(x) * 4u;
      bool const checker = ((x / 18 + y / 18) % 2) == 0;
      dst[offset + 0u] = popup ? static_cast<std::uint8_t>(checker ? 48 : 210) : static_cast<std::uint8_t>(90);
      dst[offset + 1u] = popup ? static_cast<std::uint8_t>(checker ? 190 : 235)
                                : static_cast<std::uint8_t>((y * 220) / std::max(1, buffer.height - 1));
      dst[offset + 2u] = popup ? static_cast<std::uint8_t>(checker ? 235 : 80)
                                : static_cast<std::uint8_t>((x * 220) / std::max(1, buffer.width - 1));
      dst[offset + 3u] = 0xff;
    }
  }
}

Buffer createBuffer(wl_shm* shm, char const* name, int width, int height, bool popup) {
  Buffer buffer;
  buffer.width = width;
  buffer.height = height;
  buffer.stride = width * 4;
  buffer.size = buffer.stride * height;
  buffer.fd = createSharedMemoryFile(name, static_cast<std::size_t>(buffer.size));
  buffer.pixels = mmap(nullptr, buffer.size, PROT_READ | PROT_WRITE, MAP_SHARED, buffer.fd, 0);
  if (buffer.pixels == MAP_FAILED) {
    buffer.pixels = nullptr;
    throw std::runtime_error(std::string("mmap failed: ") + std::strerror(errno));
  }
  fillBuffer(buffer, popup);
  wl_shm_pool* pool = wl_shm_create_pool(shm, buffer.fd, buffer.size);
  buffer.buffer = wl_shm_pool_create_buffer(pool, 0, width, height, buffer.stride, WL_SHM_FORMAT_XRGB8888);
  wl_shm_pool_destroy(pool);
  return buffer;
}

void destroyBuffer(Buffer& buffer) {
  if (buffer.buffer) wl_buffer_destroy(buffer.buffer);
  if (buffer.pixels) munmap(buffer.pixels, buffer.size);
  if (buffer.fd >= 0) close(buffer.fd);
  buffer = {};
  buffer.fd = -1;
}

void commitBuffer(wl_surface* surface, Buffer const& buffer) {
  wl_surface_attach(surface, buffer.buffer, 0, 0);
  wl_surface_damage_buffer(surface, 0, 0, buffer.width, buffer.height);
  wl_surface_commit(surface);
}

void wmBasePing(void*, xdg_wm_base* wmBase, std::uint32_t serial) {
  xdg_wm_base_pong(wmBase, serial);
}

xdg_wm_base_listener const kWmBaseListener{wmBasePing};

void parentConfigure(void* data, xdg_surface* surface, std::uint32_t serial) {
  auto* client = static_cast<DemoClient*>(data);
  xdg_surface_ack_configure(surface, serial);
  client->configured = true;
}

void popupSurfaceConfigure(void* data, xdg_surface* surface, std::uint32_t serial) {
  auto* client = static_cast<DemoClient*>(data);
  xdg_surface_ack_configure(surface, serial);
  if (!client->popupCommitted) {
    commitBuffer(client->popupSurface, client->popupBuffer);
    client->popupCommitted = true;
    std::fprintf(stderr, "flux-compositor-popup-demo: committed popup buffer\n");
  }
}

xdg_surface_listener const kParentSurfaceListener{parentConfigure};
xdg_surface_listener const kPopupSurfaceListener{popupSurfaceConfigure};

void toplevelConfigure(void*, xdg_toplevel*, std::int32_t, std::int32_t, wl_array*) {}
void toplevelClose(void*, xdg_toplevel*) { gRunning.store(false, std::memory_order_relaxed); }
void toplevelConfigureBounds(void*, xdg_toplevel*, std::int32_t, std::int32_t) {}
void toplevelWmCapabilities(void*, xdg_toplevel*, wl_array*) {}

xdg_toplevel_listener const kToplevelListener{
    .configure = toplevelConfigure,
    .close = toplevelClose,
    .configure_bounds = toplevelConfigureBounds,
    .wm_capabilities = toplevelWmCapabilities,
};

void popupConfigure(void*, xdg_popup*, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height) {
  std::fprintf(stderr, "flux-compositor-popup-demo: popup configured at %d,%d size %dx%d\n", x, y, width, height);
}

void popupDone(void*, xdg_popup*) {
  std::fprintf(stderr, "flux-compositor-popup-demo: popup dismissed\n");
  gRunning.store(false, std::memory_order_relaxed);
}

void popupRepositioned(void*, xdg_popup*, std::uint32_t token) {
  std::fprintf(stderr, "flux-compositor-popup-demo: popup repositioned token %u\n", token);
}

xdg_popup_listener const kPopupListener{
    .configure = popupConfigure,
    .popup_done = popupDone,
    .repositioned = popupRepositioned,
};

void createPopup(DemoClient& client) {
  if (client.popup) return;
  client.popupSurface = wl_compositor_create_surface(client.compositor);
  client.popupXdgSurface = xdg_wm_base_get_xdg_surface(client.wmBase, client.popupSurface);
  xdg_surface_add_listener(client.popupXdgSurface, &kPopupSurfaceListener, &client);

  xdg_positioner* positioner = xdg_wm_base_create_positioner(client.wmBase);
  xdg_positioner_set_size(positioner, kPopupWidth, kPopupHeight);
  xdg_positioner_set_anchor_rect(positioner, 80, 70, 1, 1);
  xdg_positioner_set_anchor(positioner, XDG_POSITIONER_ANCHOR_BOTTOM_LEFT);
  xdg_positioner_set_gravity(positioner, XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);
  xdg_positioner_set_offset(positioner, 8, 8);
  client.popup = xdg_surface_get_popup(client.popupXdgSurface, client.xdgSurface, positioner);
  xdg_popup_add_listener(client.popup, &kPopupListener, &client);
  xdg_positioner_destroy(positioner);

  client.popupBuffer =
      createBuffer(client.shm, "flux-compositor-popup-demo-popup", kPopupWidth, kPopupHeight, true);
  wl_surface_commit(client.popupSurface);
  wl_display_flush(client.display);
  std::fprintf(stderr, "flux-compositor-popup-demo: requested popup; waiting for configure\n");
}

void createSubsurface(DemoClient& client) {
  if (client.childSubsurface) return;
  client.childSurface = wl_compositor_create_surface(client.compositor);
  client.childSubsurface = wl_subcompositor_get_subsurface(client.subcompositor, client.childSurface, client.surface);
  wl_subsurface_set_position(client.childSubsurface, 18, 18);
  client.childBuffer = createBuffer(client.shm, "flux-compositor-popup-demo-child", 72, 48, true);
  commitBuffer(client.childSurface, client.childBuffer);
  wl_display_flush(client.display);
  std::fprintf(stderr, "flux-compositor-popup-demo: committed child subsurface buffer\n");
}

void registryGlobal(void* data, wl_registry* registry, std::uint32_t name, char const* interface,
                    std::uint32_t version) {
  auto* client = static_cast<DemoClient*>(data);
  if (std::strcmp(interface, wl_compositor_interface.name) == 0) {
    client->compositor = static_cast<wl_compositor*>(
        wl_registry_bind(registry, name, &wl_compositor_interface, std::min(version, 4u)));
  } else if (std::strcmp(interface, wl_subcompositor_interface.name) == 0) {
    client->subcompositor = static_cast<wl_subcompositor*>(
        wl_registry_bind(registry, name, &wl_subcompositor_interface, std::min(version, 1u)));
  } else if (std::strcmp(interface, wl_shm_interface.name) == 0) {
    client->shm = static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
  } else if (std::strcmp(interface, xdg_wm_base_interface.name) == 0) {
    client->wmBase = static_cast<xdg_wm_base*>(
        wl_registry_bind(registry, name, &xdg_wm_base_interface, std::min(version, 6u)));
    xdg_wm_base_add_listener(client->wmBase, &kWmBaseListener, client);
  }
}

void registryRemove(void*, wl_registry*, std::uint32_t) {}

wl_registry_listener const kRegistryListener{registryGlobal, registryRemove};

void destroyClient(DemoClient& client) {
  destroyBuffer(client.popupBuffer);
  if (client.popup) xdg_popup_destroy(client.popup);
  if (client.popupXdgSurface) xdg_surface_destroy(client.popupXdgSurface);
  if (client.popupSurface) wl_surface_destroy(client.popupSurface);
  destroyBuffer(client.childBuffer);
  if (client.childSubsurface) wl_subsurface_destroy(client.childSubsurface);
  if (client.childSurface) wl_surface_destroy(client.childSurface);
  destroyBuffer(client.parentBuffer);
  if (client.toplevel) xdg_toplevel_destroy(client.toplevel);
  if (client.xdgSurface) xdg_surface_destroy(client.xdgSurface);
  if (client.surface) wl_surface_destroy(client.surface);
  if (client.subcompositor) wl_subcompositor_destroy(client.subcompositor);
  if (client.wmBase) xdg_wm_base_destroy(client.wmBase);
  if (client.shm) wl_shm_destroy(client.shm);
  if (client.compositor) wl_compositor_destroy(client.compositor);
  if (client.registry) wl_registry_destroy(client.registry);
  if (client.display) wl_display_disconnect(client.display);
}

} // namespace

int main() {
  DemoClient client;
  try {
    client.display = flux::compositor::demo::connectDisplay("flux-compositor-popup-demo");
    if (!client.display) throw std::runtime_error("wl_display_connect failed");

    client.registry = wl_display_get_registry(client.display);
    wl_registry_add_listener(client.registry, &kRegistryListener, &client);
    if (!flux::compositor::demo::roundtripWithTimeout(client.display, 3000)) {
      throw std::runtime_error("registry roundtrip timed out");
    }
    if (!client.compositor || !client.subcompositor || !client.shm || !client.wmBase) {
      throw std::runtime_error("compositor is missing wl_compositor, wl_subcompositor, wl_shm, or xdg_wm_base");
    }

    client.surface = wl_compositor_create_surface(client.compositor);
    client.xdgSurface = xdg_wm_base_get_xdg_surface(client.wmBase, client.surface);
    xdg_surface_add_listener(client.xdgSurface, &kParentSurfaceListener, &client);
    client.toplevel = xdg_surface_get_toplevel(client.xdgSurface);
    xdg_toplevel_add_listener(client.toplevel, &kToplevelListener, &client);
    xdg_toplevel_set_title(client.toplevel, "Flux popup demo");
    xdg_toplevel_set_app_id(client.toplevel, "flux-compositor-popup-demo");
    wl_surface_commit(client.surface);

    if (!flux::compositor::demo::waitUntil(client.display, [&] { return client.configured; }, 3000)) {
      throw std::runtime_error("initial configure timed out");
    }

    client.parentBuffer =
        createBuffer(client.shm, "flux-compositor-popup-demo-parent", kParentWidth, kParentHeight, false);
    commitBuffer(client.surface, client.parentBuffer);
    wl_display_flush(client.display);
    createSubsurface(client);
    createPopup(client);
    std::fprintf(stderr,
                 "flux-compositor-popup-demo: expect a parent window, a small child rectangle, and a popup rectangle; click outside the popup or press Escape to dismiss it\n");

    while (gRunning.load(std::memory_order_relaxed)) {
      if (flux::compositor::demo::dispatchWithTimeout(client.display, 250) < 0) break;
    }

    destroyClient(client);
    return 0;
  } catch (std::exception const& e) {
    std::fprintf(stderr, "flux-compositor-popup-demo: %s\n", e.what());
    destroyClient(client);
    return 1;
  }
}
