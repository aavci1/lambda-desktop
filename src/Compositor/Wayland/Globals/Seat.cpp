#include "Compositor/Wayland/Globals/Seat.hpp"

#include "Compositor/Wayland/ResourceTemplates.hpp"
#include "Compositor/Wayland/WaylandServerImpl.hpp"

#include <sys/mman.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace flux::compositor {
namespace {

void seatDestroyResource(wl_resource* resource) {
  if (auto* server = serverFrom(resource)) removeResource(server->seatResources_, resource);
}

void pointerDestroyResource(wl_resource* resource) {
  if (auto* server = serverFrom(resource)) {
    removeResource(server->pointerResources_, resource);
    for (auto& relativePointer : server->relativePointers_) {
      if (relativePointer->pointer == resource) relativePointer->pointer = nullptr;
    }
    for (auto& constraint : server->pointerConstraints_) {
      if (constraint->pointer == resource) constraint->pointer = nullptr;
    }
  }
}

void keyboardDestroyResource(wl_resource* resource) {
  if (auto* server = serverFrom(resource)) removeResource(server->keyboardResources_, resource);
}

int createKeymapFd(std::uint32_t& size) {
  size = 0;
  xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (!context) return -1;
  xkb_rule_names names{};
  xkb_keymap* keymap = xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
  if (!keymap) {
    xkb_context_unref(context);
    return -1;
  }
  char* keymapString = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
  xkb_keymap_unref(keymap);
  xkb_context_unref(context);
  if (!keymapString) return -1;

  std::size_t const length = std::strlen(keymapString) + 1u;
  int fd = memfd_create("flux-compositor-keymap", MFD_CLOEXEC | MFD_ALLOW_SEALING);
  if (fd >= 0 && ftruncate(fd, static_cast<off_t>(length)) == 0) {
    std::size_t written = 0;
    while (written < length) {
      ssize_t n = write(fd, keymapString + written, length - written);
      if (n <= 0) break;
      written += static_cast<std::size_t>(n);
    }
    if (written == length) {
      lseek(fd, 0, SEEK_SET);
      size = static_cast<std::uint32_t>(length);
      free(keymapString);
      return fd;
    }
  }
  if (fd >= 0) close(fd);
  free(keymapString);
  return -1;
}

void pointerSetCursor(wl_client*, wl_resource* resource, std::uint32_t, wl_resource* surfaceResource,
                      std::int32_t hotspotX, std::int32_t hotspotY) {
  auto* server = serverFrom(resource);
  if (!surfaceResource) {
    server->cursorSurface_ = nullptr;
    server->cursorShape_ = CursorShape::Arrow;
    return;
  }

  auto* surface = resourceData<WaylandServer::Impl::Surface>(surfaceResource);
  if (!surface || surface->toplevel || surface->subsurface) return;
  surface->cursor = true;
  server->cursorSurface_ = surface;
  server->cursorHotspotX_ = hotspotX;
  server->cursorHotspotY_ = hotspotY;
}

void pointerRelease(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

struct wl_pointer_interface const pointerImpl{
    .set_cursor = pointerSetCursor,
    .release = pointerRelease,
};

void keyboardRelease(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

struct wl_keyboard_interface const keyboardImpl{
    .release = keyboardRelease,
};

void seatGetPointer(wl_client* client, wl_resource* resource, std::uint32_t id) {
  auto* server = serverFrom(resource);
  wl_resource* pointer = wl_resource_create(client, &wl_pointer_interface,
                                            std::min(wl_resource_get_version(resource), 7), id);
  if (!pointer) {
    wl_client_post_no_memory(client);
    return;
  }
  server->pointerResources_.push_back(pointer);
  wl_resource_set_implementation(pointer, &pointerImpl, server, pointerDestroyResource);
}

void seatGetKeyboard(wl_client* client, wl_resource* resource, std::uint32_t id) {
  auto* server = serverFrom(resource);
  wl_resource* keyboard = wl_resource_create(client, &wl_keyboard_interface,
                                             std::min(wl_resource_get_version(resource), 7), id);
  if (!keyboard) {
    wl_client_post_no_memory(client);
    return;
  }
  server->keyboardResources_.push_back(keyboard);
  wl_resource_set_implementation(keyboard, &keyboardImpl, server, keyboardDestroyResource);

  std::uint32_t keymapSize = 0;
  int keymapFd = createKeymapFd(keymapSize);
  if (keymapFd >= 0) {
    wl_keyboard_send_keymap(keyboard, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, keymapFd, keymapSize);
    close(keymapFd);
  }
  if (wl_resource_get_version(keyboard) >= WL_KEYBOARD_REPEAT_INFO_SINCE_VERSION) {
    wl_keyboard_send_repeat_info(keyboard, 25, 600);
  }
}

void seatRelease(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

struct wl_seat_interface const seatImpl{
    .get_pointer = seatGetPointer,
    .get_keyboard = seatGetKeyboard,
    .get_touch = [](wl_client*, wl_resource*, std::uint32_t) {},
    .release = seatRelease,
};

} // namespace

void bindSeat(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  wl_resource* resource = wl_resource_create(client, &wl_seat_interface, std::min(version, 7u), id);
  auto* server = static_cast<WaylandServer::Impl*>(data);
  server->seatResources_.push_back(resource);
  wl_resource_set_implementation(resource, &seatImpl, server, seatDestroyResource);
  wl_seat_send_capabilities(resource, WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD);
  if (version >= 2) wl_seat_send_name(resource, "seat0");
}

} // namespace flux::compositor
