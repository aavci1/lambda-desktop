#include "Compositor/Wayland/Globals/Seat.hpp"

#include "Compositor/Wayland/ResourceTemplates.hpp"
#include "Compositor/Wayland/WaylandServerImpl.hpp"
#include "Compositor/Window/WindowManagerInternal.hpp"

#include <sys/mman.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <cstdio>
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

int createKeymapFd(WaylandServer::Impl* server, std::uint32_t& size) {
  size = 0;
  if (!server || !server->xkbKeymap_) return -1;
  char* keymapString = xkb_keymap_get_as_string(server->xkbKeymap_, XKB_KEYMAP_FORMAT_TEXT_V1);
  if (!keymapString) return -1;

  std::size_t const length = std::strlen(keymapString) + 1u;
  int fd = memfd_create("lambda-window-manager-keymap", MFD_CLOEXEC | MFD_ALLOW_SEALING);
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
  if (!surfaceHasNoRole(surface) && !surfaceIsCursor(surface)) return;
  surface->role = SurfaceRole::Cursor;
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
  int keymapFd = createKeymapFd(server, keymapSize);
  if (keymapFd >= 0) {
    wl_keyboard_send_keymap(keyboard, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, keymapFd, keymapSize);
    close(keymapFd);
  }
  if (wl_resource_get_version(keyboard) >= WL_KEYBOARD_REPEAT_INFO_SINCE_VERSION) {
    wl_keyboard_send_repeat_info(keyboard, server->keyboardRepeatRate_, server->keyboardRepeatDelayMs_);
  }
  if (server->keyboardFocus_ && server->keyboardFocus_->resource &&
      wl_resource_get_client(server->keyboardFocus_->resource) == client) {
    wl_array keys;
    wl_array_init(&keys);
    std::uint32_t const modifiers = wm::keyboardModifierMask(server);
    wl_keyboard_send_enter(keyboard,
                           server->nextInputSerial_++,
                           server->keyboardFocus_->resource,
                           &keys);
    wl_keyboard_send_modifiers(keyboard, server->nextInputSerial_++, modifiers, 0, 0, 0);
    wl_array_release(&keys);
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

void sendKeyboardConfiguration(WaylandServer::Impl* server) {
  if (!server) return;
  for (wl_resource* keyboard : server->keyboardResources_) {
    if (!keyboard) continue;
    std::uint32_t keymapSize = 0;
    int keymapFd = createKeymapFd(server, keymapSize);
    if (keymapFd >= 0) {
      wl_keyboard_send_keymap(keyboard, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, keymapFd, keymapSize);
      close(keymapFd);
    } else {
      std::fprintf(stderr, "lambda-window-manager: failed to send updated keyboard keymap\n");
    }
    if (wl_resource_get_version(keyboard) >= WL_KEYBOARD_REPEAT_INFO_SINCE_VERSION) {
      wl_keyboard_send_repeat_info(keyboard, server->keyboardRepeatRate_, server->keyboardRepeatDelayMs_);
    }
  }
}

void bindSeat(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  wl_resource* resource = wl_resource_create(client, &wl_seat_interface, std::min(version, 7u), id);
  auto* server = static_cast<WaylandServer::Impl*>(data);
  server->seatResources_.push_back(resource);
  wl_resource_set_implementation(resource, &seatImpl, server, seatDestroyResource);
  wl_seat_send_capabilities(resource, WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD);
  if (version >= 2) wl_seat_send_name(resource, "seat0");
}

} // namespace flux::compositor
