#include "Compositor/Wayland/WaylandServerImpl.hpp"

#include "Compositor/Diagnostics/CrashLog.hpp"
#include "Compositor/Wayland/CursorShapeState.hpp"
#include "Compositor/Wayland/FractionalScaleState.hpp"
#include "Compositor/Wayland/Globals/Activation.hpp"
#include "Compositor/Wayland/Globals/BackgroundEffect.hpp"
#include "Compositor/Wayland/Globals/Core.hpp"
#include "Compositor/Wayland/Globals/CursorShape.hpp"
#include "Compositor/Wayland/Globals/Cutouts.hpp"
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
#include "Compositor/Wayland/IdleInhibitState.hpp"
#include "Compositor/Wayland/OutputState.hpp"
#include "Compositor/Wayland/PointerExtensionState.hpp"
#include "Compositor/Wayland/PresentationState.hpp"
#include "Compositor/Wayland/ViewporterState.hpp"
#include "Compositor/Wayland/XdgOutputState.hpp"
#include "cursor-shape-v1-server-protocol.h"
#include "ext-background-effect-v1-server-protocol.h"
#include "fractional-scale-v1-server-protocol.h"
#include "idle-inhibit-unstable-v1-server-protocol.h"
#include "linux-dmabuf-unstable-v1-server-protocol.h"
#include "presentation-time-server-protocol.h"
#include "pointer-constraints-unstable-v1-server-protocol.h"
#include "primary-selection-unstable-v1-server-protocol.h"
#include "relative-pointer-unstable-v1-server-protocol.h"
#include "viewporter-server-protocol.h"
#include "wlr-layer-shell-unstable-v1-server-protocol.h"
#include "xx-cutouts-v1-server-protocol.h"
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

namespace lambda::compositor {
namespace {

char const* xkbName(std::string const& value) {
  return value.empty() ? nullptr : value.c_str();
}

void refreshKeyboardModifierIndices(WaylandServer::Impl* server) {
  if (!server || !server->xkbKeymap_) return;
  server->shiftModifierIndex_ = xkb_keymap_mod_get_index(server->xkbKeymap_, XKB_MOD_NAME_SHIFT);
  server->ctrlModifierIndex_ = xkb_keymap_mod_get_index(server->xkbKeymap_, XKB_MOD_NAME_CTRL);
  server->altModifierIndex_ = xkb_keymap_mod_get_index(server->xkbKeymap_, XKB_MOD_NAME_ALT);
  server->logoModifierIndex_ = xkb_keymap_mod_get_index(server->xkbKeymap_, XKB_MOD_NAME_LOGO);
}

bool installKeyboardKeymap(WaylandServer::Impl* server, xkb_keymap* keymap) {
  if (!server || !keymap) return false;
  xkb_state* state = xkb_state_new(keymap);
  if (!state) {
    xkb_keymap_unref(keymap);
    return false;
  }
  if (server->xkbState_) xkb_state_unref(server->xkbState_);
  if (server->xkbKeymap_) xkb_keymap_unref(server->xkbKeymap_);
  server->xkbKeymap_ = keymap;
  server->xkbState_ = state;
  refreshKeyboardModifierIndices(server);
  server->metaDown_ = false;
  server->ctrlDown_ = false;
  server->altDown_ = false;
  server->shiftDown_ = false;
  return true;
}

xkb_keymap* createKeyboardKeymap(WaylandServer::Impl* server, CompositorKeyboardConfig const& config) {
  if (!server || !server->xkbContext_) return nullptr;
  xkb_rule_names names{
      .rules = xkbName(config.rules),
      .model = xkbName(config.model),
      .layout = xkbName(config.layout),
      .variant = xkbName(config.variant),
      .options = xkbName(config.options),
  };
  return xkb_keymap_new_from_names(server->xkbContext_, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
}

bool applyKeyboardConfig(WaylandServer::Impl* server, CompositorKeyboardConfig const& config) {
  if (!server || !server->xkbContext_) return false;
  xkb_keymap* keymap = createKeyboardKeymap(server, config);
  if (keymap && installKeyboardKeymap(server, keymap)) {
    return true;
  }

  std::fprintf(stderr,
               "lambda-window-manager: invalid input.keyboard keymap; falling back to xkb defaults\n");
  xkb_rule_names fallbackNames{};
  keymap = xkb_keymap_new_from_names(server->xkbContext_, &fallbackNames, XKB_KEYMAP_COMPILE_NO_FLAGS);
  return installKeyboardKeymap(server, keymap);
}

void initializeKeyboard(WaylandServer::Impl* server) {
  server->xkbContext_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (!server->xkbContext_) return;
  if (!applyKeyboardConfig(server, server->keyboardConfig_)) {
    xkb_context_unref(server->xkbContext_);
    server->xkbContext_ = nullptr;
  }
}

} // namespace

WaylandServer::Impl::Impl(WaylandOutputInfo output) : output_(std::move(output)) {
  initializeKeyboard(this);
  shortcutBindings_ = {
      {.action = ShortcutAction::CloseFocused, .key = KEY_Q, .meta = true},
      {.action = ShortcutAction::CycleFocus, .key = KEY_TAB, .meta = true},
      {.action = ShortcutAction::CycleFocus, .key = KEY_TAB, .meta = true, .shift = true},
      {.action = ShortcutAction::SnapLeft, .key = KEY_LEFT, .meta = true},
      {.action = ShortcutAction::SnapRight, .key = KEY_RIGHT, .meta = true},
      {.action = ShortcutAction::Maximize, .key = KEY_UP, .meta = true},
      {.action = ShortcutAction::Restore, .key = KEY_DOWN, .meta = true},
      {.action = ShortcutAction::LaunchCommand, .key = KEY_SPACE, .meta = true},
      {.action = ShortcutAction::Screenshot, .key = KEY_3, .meta = true, .shift = true},
      {.action = ShortcutAction::Screenshot, .key = KEY_SYSRQ},
      {.action = ShortcutAction::Screenshot, .key = KEY_PRINT},
      {.action = ShortcutAction::ScreenshotRegion, .key = KEY_4, .meta = true, .shift = true},
      {.action = ShortcutAction::ScreenshotActiveWindow, .key = KEY_5, .meta = true, .shift = true},
      {.action = ShortcutAction::ScreenshotActiveWindow, .key = KEY_SYSRQ, .alt = true},
      {.action = ShortcutAction::ScreenshotActiveWindow, .key = KEY_PRINT, .alt = true},
      {.action = ShortcutAction::Terminate, .key = KEY_BACKSPACE, .ctrl = true, .alt = true},
  };

  display_ = wl_display_create();
  if (!display_) throw std::runtime_error("wl_display_create failed");

  compositorGlobal_ = wl_global_create(display_, &wl_compositor_interface, kCompositorVersion, this, bindCompositor);
  subcompositorGlobal_ =
      wl_global_create(display_, &wl_subcompositor_interface, kSubcompositorVersion, this, bindSubcompositor);
  shmGlobal_ = wl_global_create(display_, &wl_shm_interface, kShmVersion, this, bindShm);
  outputGlobal_ = wl_global_create(display_, &wl_output_interface, kOutputVersion, this, bindOutput);
  seatGlobal_ = wl_global_create(display_, &wl_seat_interface, kSeatVersion, this, bindSeat);
  xdgWmBaseGlobal_ = wl_global_create(display_, &xdg_wm_base_interface, kXdgWmBaseVersion, this, bindXdgWmBase);
  linuxDmabufGlobal_ =
      wl_global_create(display_, &zwp_linux_dmabuf_v1_interface, kLinuxDmabufVersion, this, bindLinuxDmabuf);
  xdgDecorationManagerGlobal_ =
      wl_global_create(display_,
                       &zxdg_decoration_manager_v1_interface,
                       kXdgDecorationManagerVersion,
                       this,
                       bindXdgDecorationManager);
  xdgOutputManagerGlobal_ =
      wl_global_create(display_, &zxdg_output_manager_v1_interface, kXdgOutputVersion, this, bindXdgOutputManager);
  viewporterGlobal_ = wl_global_create(display_, &wp_viewporter_interface, kViewporterVersion, this, bindViewporter);
  fractionalScaleManagerGlobal_ =
      wl_global_create(display_,
                       &wp_fractional_scale_manager_v1_interface,
                       kFractionalScaleVersion,
                       this,
                       bindFractionalScaleManager);
  cursorShapeManagerGlobal_ = wl_global_create(display_,
                                               &wp_cursor_shape_manager_v1_interface,
                                               kCursorShapeVersion,
                                               this,
                                               bindCursorShapeManager);
  idleInhibitManagerGlobal_ = wl_global_create(display_,
                                               &zwp_idle_inhibit_manager_v1_interface,
                                               kIdleInhibitVersion,
                                               this,
                                               bindIdleInhibitManager);
  layerShellGlobal_ =
      wl_global_create(display_, &zwlr_layer_shell_v1_interface, kLayerShellVersion, this, bindLayerShell);
  presentationGlobal_ =
      wl_global_create(display_, &wp_presentation_interface, kPresentationVersion, this, bindPresentation);
  relativePointerManagerGlobal_ =
      wl_global_create(display_,
                       &zwp_relative_pointer_manager_v1_interface,
                       kRelativePointerVersion,
                       this,
                       bindRelativePointerManager);
  pointerConstraintsGlobal_ =
      wl_global_create(display_,
                       &zwp_pointer_constraints_v1_interface,
                       kPointerConstraintsVersion,
                       this,
                       bindPointerConstraints);
  primarySelectionManagerGlobal_ =
      wl_global_create(display_,
                       &zwp_primary_selection_device_manager_v1_interface,
                       kPrimarySelectionVersion,
                       this,
                       bindPrimarySelectionManager);
  dataDeviceManagerGlobal_ =
      wl_global_create(display_, &wl_data_device_manager_interface, kDataDeviceVersion, this, bindDataDeviceManager);
  activationGlobal_ =
      wl_global_create(display_, &xdg_activation_v1_interface, kActivationVersion, this, bindActivation);
  cutoutsManagerGlobal_ =
      wl_global_create(display_, &xx_cutouts_manager_v1_interface, kCutoutsVersion, this, bindCutoutsManager);
  backgroundEffectManagerGlobal_ =
      wl_global_create(display_,
                       &ext_background_effect_manager_v1_interface,
                       kBackgroundEffectVersion,
                       this,
                       bindBackgroundEffectManager);
  if (!compositorGlobal_ || !subcompositorGlobal_ || !shmGlobal_ || !outputGlobal_ || !seatGlobal_ ||
      !xdgWmBaseGlobal_ || !linuxDmabufGlobal_ || !xdgDecorationManagerGlobal_ || !xdgOutputManagerGlobal_ ||
      !viewporterGlobal_ || !fractionalScaleManagerGlobal_ || !cursorShapeManagerGlobal_ ||
      !idleInhibitManagerGlobal_ || !layerShellGlobal_ || !presentationGlobal_ || !relativePointerManagerGlobal_ ||
      !pointerConstraintsGlobal_ || !primarySelectionManagerGlobal_ || !dataDeviceManagerGlobal_ ||
      !activationGlobal_ || !cutoutsManagerGlobal_ || !backgroundEffectManagerGlobal_) {
    throw std::runtime_error("failed to create Wayland globals");
  }

  char const* socket = wl_display_add_socket_auto(display_);
  if (!socket) throw std::runtime_error("wl_display_add_socket_auto failed");
  socketName_ = socket;
  setenv("WAYLAND_DISPLAY", socketName_.c_str(), 1);
  if (char const* runtimeDir = std::getenv("XDG_RUNTIME_DIR"); runtimeDir && *runtimeDir) {
    displayNameFile_ = std::string(runtimeDir) + "/lambda-window-manager-display";
    std::ofstream file(displayNameFile_, std::ios::trunc);
    file << socketName_ << '\n';
  }
  std::fprintf(stderr, "lambda-window-manager: Wayland display %s\n", socketName_.c_str());
  initializeShellIpc();
  diagnostics::crashLog("wayland-start socket=%s globals=ready", socketName_.c_str());
}

WaylandServer::Impl::~Impl() {
  diagnostics::crashLog("wayland-stop socket=%s surfaces=%zu toplevels=%zu dmabufs=%zu",
                        socketName_.c_str(),
                        surfaces_.size(),
                        toplevels_.size(),
                        dmabufBuffers_.size());
  if (!displayNameFile_.empty()) unlink(displayNameFile_.c_str());
  shutdownShellIpc();
  if (display_) {
    wl_display_destroy_clients(display_);
    wl_display_destroy(display_);
  }
  if (xkbState_) xkb_state_unref(xkbState_);
  if (xkbKeymap_) xkb_keymap_unref(xkbKeymap_);
  if (xkbContext_) xkb_context_unref(xkbContext_);
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
  return selectedOutputLayoutBox(output_.width, output_.height, preferredScale_).width;
}

std::int32_t WaylandServer::Impl::logicalOutputHeight() const noexcept {
  return selectedOutputLayoutBox(output_.width, output_.height, preferredScale_).height;
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

void WaylandServer::Impl::refreshActiveChromeConfig() {
  chromeConfig_ = chromeBaseConfig_;
  if (shellThemeDark_ && chromeDarkConfig_) {
    ChromeConfig const& dark = *chromeDarkConfig_;
    chromeConfig_.titleTextColor = dark.titleTextColor;
    chromeConfig_.glass = dark.glass;
    chromeConfig_.windowBorderColor = dark.windowBorderColor;
    chromeConfig_.borderLineColor = dark.borderLineColor;
    chromeConfig_.closeGlyphColor = dark.closeGlyphColor;
    chromeConfig_.minimizeGlyphColor = dark.minimizeGlyphColor;
    chromeConfig_.minimizeGlyphHoverColor = dark.minimizeGlyphHoverColor;
  }
}

void WaylandServer::Impl::setChromeThemeConfig(ChromeConfig base, std::optional<ChromeConfig> dark) {
  chromeBaseConfig_ = std::move(base);
  chromeDarkConfig_ = std::move(dark);
  refreshActiveChromeConfig();
  for (auto const& toplevel : toplevels_) {
    if (!toplevel || !toplevel->cutouts) continue;
    toplevel->cutouts->lastSent = false;
    sendToplevelStateConfigure(this, toplevel.get());
  }
  flushClients();
}

void WaylandServer::Impl::setShellThemeDark(bool dark) {
  if (shellThemeDark_ == dark) return;
  shellThemeDark_ = dark;
  refreshActiveChromeConfig();
  ++contentSerial_;
  notifyShellStateChanged();
}

void WaylandServer::Impl::setChromeConfig(ChromeConfig config) {
  setChromeThemeConfig(std::move(config), chromeDarkConfig_);
}

void WaylandServer::Impl::setInputConfig(CompositorInputConfig config) {
  popupGrabsEnabled_ = config.popupGrabs;
  if (keyboardConfig_ == config.keyboard &&
      keyboardRepeatRate_ == config.keyboard.repeatRate &&
      keyboardRepeatDelayMs_ == config.keyboard.repeatDelayMs) {
    return;
  }
  keyboardConfig_ = std::move(config.keyboard);
  keyboardRepeatRate_ = keyboardConfig_.repeatRate;
  keyboardRepeatDelayMs_ = keyboardConfig_.repeatDelayMs;
  if (applyKeyboardConfig(this, keyboardConfig_)) {
    sendKeyboardConfiguration(this);
  }
}

void WaylandServer::Impl::setPreferredScale(float scale) {
  float const previousScale = preferredScale_;
  std::int32_t const previousLogicalWidth = logicalOutputWidth();
  std::int32_t const previousLogicalHeight = logicalOutputHeight();

  preferredScale_ = std::clamp(scale, 0.5f, 4.f);
  bool const outputGeometryChanged = std::abs(previousScale - preferredScale_) > 0.001f ||
                                     previousLogicalWidth != logicalOutputWidth() ||
                                     previousLogicalHeight != logicalOutputHeight();
  std::int32_t const integerScale = outputIntegerScale(preferredScale_);
  for (wl_resource* output : outputResources_) {
    if (output && wl_resource_get_version(output) >= 2) {
      wl_output_send_scale(output, integerScale);
    }
  }
  std::uint32_t const preferred = fractionalScalePreferredScale120(preferredScale_);
  for (auto const& fractionalScale : fractionalScales_) {
    if (fractionalScale && fractionalScale->resource) {
      wp_fractional_scale_v1_send_preferred_scale(fractionalScale->resource, preferred);
    }
  }
  if (outputGeometryChanged) {
    sendXdgOutputUpdatesForOutputGeometry(this, false);
    reconfigureLayerSurfacesForOutputGeometry(this);
    ++contentSerial_;
    notifyShellStateChanged();
  }
  for (wl_resource* output : outputResources_) {
    if (output && wl_resource_get_version(output) >= 2) {
      wl_output_send_done(output);
    }
  }
  flushClients();
}

void WaylandServer::Impl::setDmabufFormatModifierPreferences(
    std::vector<DmabufFormatModifierPreference> preferences) {
  dmabufFormatModifierPreferences_ = std::move(preferences);
}

void WaylandServer::Impl::setRetainedDmabufBufferIds(std::vector<std::uint64_t> bufferIds) {
  retainedDmabufBufferIds_ = std::move(bufferIds);
}

} // namespace lambda::compositor
