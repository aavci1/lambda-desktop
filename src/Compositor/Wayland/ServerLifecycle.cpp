#include "Compositor/Wayland/WaylandServerImpl.hpp"

#include "Compositor/Wayland/Globals/Activation.hpp"
#include "Compositor/Wayland/Globals/Core.hpp"
#include "Compositor/Wayland/Globals/CursorShape.hpp"
#include "Compositor/Wayland/Globals/FractionalScale.hpp"
#include "Compositor/Wayland/Globals/IdleInhibit.hpp"
#include "Compositor/Wayland/Globals/LayerShell.hpp"
#include "Compositor/Wayland/Globals/LinuxDmabuf.hpp"
#include "Compositor/Wayland/Globals/Output.hpp"
#include "Compositor/Wayland/Globals/PointerExtensions.hpp"
#include "Compositor/Wayland/Globals/Presentation.hpp"
#include "Compositor/Wayland/Globals/Seat.hpp"
#include "Compositor/Wayland/Globals/Selection.hpp"
#include "Compositor/Wayland/Globals/Shm.hpp"
#include "Compositor/Wayland/Globals/Viewporter.hpp"
#include "Compositor/Wayland/Globals/XdgOutput.hpp"
#include "Compositor/Wayland/Globals/XdgShell.hpp"
#include "cursor-shape-v1-server-protocol.h"
#include "fractional-scale-v1-server-protocol.h"
#include "idle-inhibit-unstable-v1-server-protocol.h"
#include "linux-dmabuf-unstable-v1-server-protocol.h"
#include "presentation-time-server-protocol.h"
#include "pointer-constraints-unstable-v1-server-protocol.h"
#include "primary-selection-unstable-v1-server-protocol.h"
#include "relative-pointer-unstable-v1-server-protocol.h"
#include "viewporter-server-protocol.h"
#include "wlr-layer-shell-unstable-v1-server-protocol.h"
#include "xdg-activation-v1-server-protocol.h"
#include "xdg-decoration-unstable-v1-server-protocol.h"
#include "xdg-output-unstable-v1-server-protocol.h"
#include "xdg-shell-server-protocol.h"

#include <linux/input-event-codes.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace flux::compositor {
namespace {

void initializeKeyboardModifierIndices(WaylandServer::Impl* server) {
  xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (!context) return;
  xkb_rule_names names{};
  xkb_keymap* keymap = xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
  if (!keymap) {
    xkb_context_unref(context);
    return;
  }

  server->shiftModifierIndex_ = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_SHIFT);
  server->ctrlModifierIndex_ = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_CTRL);
  server->altModifierIndex_ = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_ALT);
  server->logoModifierIndex_ = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_LOGO);

  xkb_keymap_unref(keymap);
  xkb_context_unref(context);
}

std::uint32_t preferredScale120(float scale) {
  return static_cast<std::uint32_t>(std::clamp(std::lround(scale * 120.f), 60l, 480l));
}

std::int32_t integerOutputScale(float scale) {
  float const rounded = std::round(scale);
  return std::abs(scale - rounded) < 0.001f ? std::max(1, static_cast<std::int32_t>(rounded)) : 1;
}

std::int32_t scaledLogicalSize(std::int32_t physicalSize, float scale) {
  return std::max(1, static_cast<std::int32_t>(std::lround(static_cast<float>(physicalSize) /
                                                           std::max(0.5f, scale))));
}

} // namespace

WaylandServer::Impl::Impl(WaylandOutputInfo output) : output_(std::move(output)) {
  initializeKeyboardModifierIndices(this);
  shortcutBindings_ = {
      {.action = ShortcutAction::CloseFocused, .key = KEY_Q, .meta = true},
      {.action = ShortcutAction::CycleFocus, .key = KEY_TAB, .meta = true},
      {.action = ShortcutAction::SnapLeft, .key = KEY_LEFT, .meta = true},
      {.action = ShortcutAction::SnapRight, .key = KEY_RIGHT, .meta = true},
      {.action = ShortcutAction::Maximize, .key = KEY_UP, .meta = true},
      {.action = ShortcutAction::Restore, .key = KEY_DOWN, .meta = true},
      {.action = ShortcutAction::Terminate, .key = KEY_BACKSPACE, .ctrl = true, .alt = true},
  };

  display_ = wl_display_create();
  if (!display_) throw std::runtime_error("wl_display_create failed");

  compositorGlobal_ = wl_global_create(display_, &wl_compositor_interface, 5, this, bindCompositor);
  subcompositorGlobal_ = wl_global_create(display_, &wl_subcompositor_interface, 1, this, bindSubcompositor);
  shmGlobal_ = wl_global_create(display_, &wl_shm_interface, 1, this, bindShm);
  outputGlobal_ = wl_global_create(display_, &wl_output_interface, 4, this, bindOutput);
  seatGlobal_ = wl_global_create(display_, &wl_seat_interface, 7, this, bindSeat);
  xdgWmBaseGlobal_ = wl_global_create(display_, &xdg_wm_base_interface, 6, this, bindXdgWmBase);
  linuxDmabufGlobal_ = wl_global_create(display_, &zwp_linux_dmabuf_v1_interface, 3, this, bindLinuxDmabuf);
  xdgDecorationManagerGlobal_ =
      wl_global_create(display_, &zxdg_decoration_manager_v1_interface, 1, this, bindXdgDecorationManager);
  xdgOutputManagerGlobal_ =
      wl_global_create(display_, &zxdg_output_manager_v1_interface, 3, this, bindXdgOutputManager);
  viewporterGlobal_ = wl_global_create(display_, &wp_viewporter_interface, 1, this, bindViewporter);
  fractionalScaleManagerGlobal_ =
      wl_global_create(display_, &wp_fractional_scale_manager_v1_interface, 1, this, bindFractionalScaleManager);
  cursorShapeManagerGlobal_ =
      wl_global_create(display_, &wp_cursor_shape_manager_v1_interface, 2, this, bindCursorShapeManager);
  idleInhibitManagerGlobal_ =
      wl_global_create(display_, &zwp_idle_inhibit_manager_v1_interface, 1, this, bindIdleInhibitManager);
  layerShellGlobal_ = wl_global_create(display_, &zwlr_layer_shell_v1_interface, 1, this, bindLayerShell);
  presentationGlobal_ = wl_global_create(display_, &wp_presentation_interface, 2, this, bindPresentation);
  relativePointerManagerGlobal_ =
      wl_global_create(display_, &zwp_relative_pointer_manager_v1_interface, 1, this, bindRelativePointerManager);
  pointerConstraintsGlobal_ =
      wl_global_create(display_, &zwp_pointer_constraints_v1_interface, 1, this, bindPointerConstraints);
  primarySelectionManagerGlobal_ =
      wl_global_create(display_, &zwp_primary_selection_device_manager_v1_interface, 1, this, bindPrimarySelectionManager);
  dataDeviceManagerGlobal_ = wl_global_create(display_, &wl_data_device_manager_interface, 3, this, bindDataDeviceManager);
  activationGlobal_ = wl_global_create(display_, &xdg_activation_v1_interface, 1, this, bindActivation);
  if (!compositorGlobal_ || !subcompositorGlobal_ || !shmGlobal_ || !outputGlobal_ || !seatGlobal_ ||
      !xdgWmBaseGlobal_ || !linuxDmabufGlobal_ || !xdgDecorationManagerGlobal_ || !xdgOutputManagerGlobal_ ||
      !viewporterGlobal_ || !fractionalScaleManagerGlobal_ || !cursorShapeManagerGlobal_ ||
      !idleInhibitManagerGlobal_ || !layerShellGlobal_ || !presentationGlobal_ || !relativePointerManagerGlobal_ ||
      !pointerConstraintsGlobal_ || !primarySelectionManagerGlobal_ || !dataDeviceManagerGlobal_ ||
      !activationGlobal_) {
    throw std::runtime_error("failed to create Wayland globals");
  }

  char const* socket = wl_display_add_socket_auto(display_);
  if (!socket) throw std::runtime_error("wl_display_add_socket_auto failed");
  socketName_ = socket;
  setenv("WAYLAND_DISPLAY", socketName_.c_str(), 1);
  if (char const* runtimeDir = std::getenv("XDG_RUNTIME_DIR"); runtimeDir && *runtimeDir) {
    displayNameFile_ = std::string(runtimeDir) + "/flux-compositor-display";
    std::ofstream file(displayNameFile_, std::ios::trunc);
    file << socketName_ << '\n';
  }
  std::fprintf(stderr, "flux-compositor: Wayland display %s\n", socketName_.c_str());
}

WaylandServer::Impl::~Impl() {
  if (!displayNameFile_.empty()) unlink(displayNameFile_.c_str());
  if (!display_) return;
  wl_display_destroy_clients(display_);
  wl_display_destroy(display_);
}

char const* WaylandServer::Impl::socketName() const noexcept {
  return socketName_.c_str();
}

int WaylandServer::Impl::eventFd() const noexcept {
  return display_ ? wl_event_loop_get_fd(wl_display_get_event_loop(display_)) : -1;
}

float WaylandServer::Impl::preferredScale() const noexcept {
  return preferredScale_;
}

std::int32_t WaylandServer::Impl::logicalOutputWidth() const noexcept {
  return scaledLogicalSize(output_.width, preferredScale_);
}

std::int32_t WaylandServer::Impl::logicalOutputHeight() const noexcept {
  return scaledLogicalSize(output_.height, preferredScale_);
}

std::size_t WaylandServer::Impl::toplevelCount() const noexcept {
  return toplevels_.size();
}

void WaylandServer::Impl::dispatch() {
  if (!display_) return;
  wl_event_loop_dispatch(wl_display_get_event_loop(display_), 0);
  wl_display_flush_clients(display_);
}

void WaylandServer::Impl::flushClients() {
  if (display_) wl_display_flush_clients(display_);
}

void WaylandServer::Impl::setShortcutBindings(std::vector<ShortcutBinding> bindings) {
  shortcutBindings_ = std::move(bindings);
}

void WaylandServer::Impl::setPreferredScale(float scale) {
  preferredScale_ = std::clamp(scale, 0.5f, 4.f);
  std::int32_t const integerScale = integerOutputScale(preferredScale_);
  for (wl_resource* output : outputResources_) {
    if (output && wl_resource_get_version(output) >= 2) {
      wl_output_send_scale(output, integerScale);
      wl_output_send_done(output);
    }
  }
  std::uint32_t const preferred = preferredScale120(preferredScale_);
  for (auto const& fractionalScale : fractionalScales_) {
    if (fractionalScale && fractionalScale->resource) {
      wp_fractional_scale_v1_send_preferred_scale(fractionalScale->resource, preferred);
    }
  }
  flushClients();
}

} // namespace flux::compositor
