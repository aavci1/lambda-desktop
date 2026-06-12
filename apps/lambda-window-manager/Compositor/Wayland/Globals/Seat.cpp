#include "Compositor/Wayland/Globals/Seat.hpp"

#include "Compositor/Wayland/CursorRequestState.hpp"
#include "Compositor/Wayland/Globals/CursorShape.hpp"
#include "Compositor/Wayland/Globals/PointerExtensions.hpp"
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

namespace lambda::compositor {
namespace {

void seatDestroyResource(wl_resource* resource) {
  if (auto* server = serverFrom(resource)) removeResource(server->seatResources_, resource);
}

void pointerDestroyResource(wl_resource* resource) {
  if (auto* server = serverFrom(resource)) {
    wl_client* const client = wl_resource_get_client(resource);
    removeResource(server->pointerResources_, resource);
    destroyCursorShapeDevicesForPointer(server, resource);
    destroyPointerExtensionResourcesForPointer(server, resource);
    bool const clientStillHasPointer = std::ranges::any_of(server->pointerResources_, [client](wl_resource* pointer) {
      return pointer && wl_resource_get_client(pointer) == client;
    });
    if (!clientStillHasPointer && server->pointerButtonGrabClient_ == client) {
      server->pointerButtonGrabSurface_ = nullptr;
      server->pointerButtonGrabClient_ = nullptr;
      server->pointerButtonCount_ = 0;
    }
  }
}

void keyboardDestroyResource(wl_resource* resource) {
  if (auto* server = serverFrom(resource)) removeResource(server->keyboardResources_, resource);
}

void touchDestroyResource(wl_resource* resource) {
  if (auto* server = serverFrom(resource)) removeResource(server->touchResources_, resource);
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

void pointerSetCursor(wl_client* client, wl_resource* resource, std::uint32_t serial, wl_resource* surfaceResource,
                      std::int32_t hotspotX, std::int32_t hotspotY) {
  auto* server = serverFrom(resource);
  if (!cursorRequestSerialValid(server, client, serial)) return;
  if (!surfaceResource) {
    bool const changed = server->cursorSurface_ || server->cursorShape_ != CursorShape::Arrow;
    server->cursorSurface_ = nullptr;
    server->cursorShape_ = CursorShape::Arrow;
    if (changed) ++server->contentSerial_;
    return;
  }

  auto* surface = resourceData<WaylandServer::Impl::Surface>(surfaceResource);
  if (!surfaceHasNoRole(surface) && !surfaceIsCursor(surface)) return;
  surface->role = SurfaceRole::Cursor;
  bool const changed = server->cursorSurface_ != surface ||
                       server->cursorShape_ != CursorShape::Arrow ||
                       server->cursorHotspotX_ != hotspotX ||
                       server->cursorHotspotY_ != hotspotY;
  server->cursorSurface_ = surface;
  server->cursorHotspotX_ = hotspotX;
  server->cursorHotspotY_ = hotspotY;
  if (changed) ++server->contentSerial_;
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

void touchRelease(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

struct wl_touch_interface const touchImpl{
    .release = touchRelease,
};

void seatGetPointer(wl_client* client, wl_resource* resource, std::uint32_t id) {
  auto* server = serverFrom(resource);
  auto const version = seatResourceVersion(static_cast<std::uint32_t>(wl_resource_get_version(resource)));
  wl_resource* pointer = wl_resource_create(client, &wl_pointer_interface,
                                            version, id);
  if (!pointer) {
    wl_client_post_no_memory(client);
    return;
  }
  server->pointerResources_.push_back(pointer);
  wl_resource_set_implementation(pointer, &pointerImpl, server, pointerDestroyResource);
}

void seatGetKeyboard(wl_client* client, wl_resource* resource, std::uint32_t id) {
  auto* server = serverFrom(resource);
  auto const version = seatResourceVersion(static_cast<std::uint32_t>(wl_resource_get_version(resource)));
  wl_resource* keyboard = wl_resource_create(client, &wl_keyboard_interface,
                                             version, id);
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
    std::uint32_t const latched = wm::keyboardLatchedModifierMask(server);
    std::uint32_t const locked = wm::keyboardLockedModifierMask(server);
    std::uint32_t const group = wm::keyboardLayoutIndex(server);
    std::uint32_t const enterSerial =
        issueSeatSerialForSurface(server, SeatSerialKind::KeyboardEnter, server->keyboardFocus_);
    std::uint32_t const modifiersSerial =
        issueSeatSerialForSurface(server, SeatSerialKind::KeyboardModifiers, server->keyboardFocus_);
    wl_keyboard_send_enter(keyboard,
                           enterSerial,
                           server->keyboardFocus_->resource,
                           &keys);
    wl_keyboard_send_modifiers(keyboard, modifiersSerial, modifiers, latched, locked, group);
    wl_array_release(&keys);
  }
}

void seatGetTouch(wl_client* client, wl_resource* resource, std::uint32_t id) {
  auto* server = serverFrom(resource);
  auto const version = seatResourceVersion(static_cast<std::uint32_t>(wl_resource_get_version(resource)));
  wl_resource* touch = wl_resource_create(client, &wl_touch_interface,
                                          version, id);
  if (!touch) {
    wl_client_post_no_memory(client);
    return;
  }
  server->touchResources_.push_back(touch);
  wl_resource_set_implementation(touch, &touchImpl, server, touchDestroyResource);
}

void seatRelease(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

struct wl_seat_interface const seatImpl{
    .get_pointer = seatGetPointer,
    .get_keyboard = seatGetKeyboard,
    .get_touch = seatGetTouch,
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
  wl_resource* resource = wl_resource_create(client, &wl_seat_interface, seatResourceVersion(version), id);
  if (!resource) {
    wl_client_post_no_memory(client);
    return;
  }
  auto* server = static_cast<WaylandServer::Impl*>(data);
  server->seatResources_.push_back(resource);
  wl_resource_set_implementation(resource, &seatImpl, server, seatDestroyResource);
  wl_seat_send_capabilities(resource, kSeatCapabilities);
  if (version >= 2) wl_seat_send_name(resource, "seat0");
}

} // namespace lambda::compositor
