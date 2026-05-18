#include "Compositor/WaylandServer.hpp"

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
#include "Compositor/Wayland/ResourceTemplates.hpp"
#include "Compositor/Wayland/WaylandServerImpl.hpp"
#include "Compositor/Window/WindowGeometry.hpp"
#include "Detail/ResizeTrace.hpp"
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

#include <drm_fourcc.h>
#include <linux/input-event-codes.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <ctime>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <vector>
#include <utility>

namespace flux::compositor {
namespace {

constexpr std::int32_t kTitleBarHeight = 28;
constexpr std::int32_t kMinWindowWidth = kCompositorMinWindowWidth;
constexpr std::int32_t kMinWindowHeight = kCompositorMinWindowHeight;
constexpr std::uint32_t kGeometryAnimationMs = 180;

} // namespace

void focusSurface(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface, std::uint32_t timeMs);

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
  return std::max(1, static_cast<std::int32_t>(std::lround(static_cast<float>(physicalSize) / std::max(0.5f, scale))));
}

float clamp01(float value) {
  return std::clamp(value, 0.f, 1.f);
}

float easeInOutCubic(float value) {
  float const t = clamp01(value);
  if (t < 0.5f) return 4.f * t * t * t;
  float const inverse = -2.f * t + 2.f;
  return 1.f - inverse * inverse * inverse * 0.5f;
}

std::int32_t lerpInt(std::int32_t from, std::int32_t to, float t) {
  return static_cast<std::int32_t>(std::lround(static_cast<float>(from) +
                                               static_cast<float>(to - from) * t));
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

bool surfaceIsRenderable(WaylandServer::Impl::Surface const* surface) {
  return surface && surface->width > 0 && surface->height > 0 &&
         (!surface->rgbaPixels.empty() || surface->dmabufBuffer);
}

CommittedSurfaceSnapshot snapshotForSurface(WaylandServer::Impl const* server,
                                            WaylandServer::Impl::Surface const* surface,
                                            std::int32_t x,
                                            std::int32_t y,
                                            bool withChrome) {
  CommittedSurfaceSnapshot snapshot{
      .id = surface->id,
      .x = x,
      .y = y,
      .width = displayWidth(surface),
      .height = displayHeight(surface),
      .bufferWidth = surface->width,
      .bufferHeight = surface->height,
      .sourceX = surface->sourceSet ? surface->sourceX : 0.f,
      .sourceY = surface->sourceSet ? surface->sourceY : 0.f,
      .sourceWidth = surface->sourceSet ? surface->sourceWidth : static_cast<float>(surface->width),
      .sourceHeight = surface->sourceSet ? surface->sourceHeight : static_cast<float>(surface->height),
      .destinationWidth = surface->destinationSet ? surface->destinationWidth : displayWidth(surface),
      .destinationHeight = surface->destinationSet ? surface->destinationHeight : displayHeight(surface),
      .titleBarHeight = withChrome && !surface->layerSurface && !surface->popup ? kTitleBarHeight : 0,
      .title = withChrome && !surface->layerSurface && !surface->popup ? titleForSurface(server, surface) : std::string{},
      .focused = server->keyboardFocus_ == surface,
      .activeSizing = server->resizeSurface_ == surface ||
                      surface->geometryAnimationActive ||
                      surface->awaitingConfigureCommit,
      .serial = surface->serial,
      .rgbaPixels = surface->rgbaPixels,
      .dmabufFormat = 0,
      .dmabufPlanes = {},
  };
  if (surface->dmabufBuffer) {
    snapshot.dmabufFormat = surface->dmabufBuffer->format;
    snapshot.dmabufPlanes.reserve(surface->dmabufBuffer->planes.size());
    for (DmabufPlane const& plane : surface->dmabufBuffer->planes) {
      snapshot.dmabufPlanes.push_back({
          .offset = plane.offset,
          .stride = plane.stride,
          .modifier = plane.modifier,
      });
    }
  }
  return snapshot;
}

void appendSubsurfaceSnapshots(WaylandServer::Impl const* server,
                               std::vector<CommittedSurfaceSnapshot>& snapshots,
                               WaylandServer::Impl::Surface const* parent,
                               std::int32_t parentX,
                               std::int32_t parentY) {
  for (auto const& subsurface : server->subsurfaces_) {
    if (!subsurface || subsurface->parent != parent || !subsurface->surface) continue;
    WaylandServer::Impl::Surface const* surface = subsurface->surface;
    if (!surfaceIsRenderable(surface)) continue;
    std::int32_t const x = parentX + subsurface->x;
    std::int32_t const y = parentY + subsurface->y;
    snapshots.push_back(snapshotForSurface(server, surface, x, y, false));
    appendSubsurfaceSnapshots(server, snapshots, surface, x, y);
  }
}

std::vector<CommittedSurfaceSnapshot> WaylandServer::Impl::committedSurfaces() const {
  std::vector<CommittedSurfaceSnapshot> snapshots;
  snapshots.reserve(surfaces_.size());
  for (auto const& surface : surfaces_) {
    if (!surface->toplevel) continue;
    if (surface->xdgPopup && surface->xdgPopup->dismissed) continue;
    if (surfaceIsRenderable(surface.get())) {
      snapshots.push_back(snapshotForSurface(this, surface.get(), surface->windowX, surface->windowY, true));
    }
    appendSubsurfaceSnapshots(this, snapshots, surface.get(), surface->windowX, surface->windowY);
  }
  return snapshots;
}

std::optional<CommittedSurfaceSnapshot> WaylandServer::Impl::cursorSurface() const {
  Surface* surface = cursorSurface_;
  if (!surface || surface->width <= 0 || surface->height <= 0) return std::nullopt;
  if (surface->rgbaPixels.empty() && !surface->dmabufBuffer) return std::nullopt;

  CommittedSurfaceSnapshot snapshot{
      .id = surface->id,
      .x = static_cast<std::int32_t>(pointerX_) - cursorHotspotX_,
      .y = static_cast<std::int32_t>(pointerY_) - cursorHotspotY_,
      .width = surface->width,
      .height = surface->height,
      .bufferWidth = surface->width,
      .bufferHeight = surface->height,
      .sourceX = surface->sourceSet ? surface->sourceX : 0.f,
      .sourceY = surface->sourceSet ? surface->sourceY : 0.f,
      .sourceWidth = surface->sourceSet ? surface->sourceWidth : static_cast<float>(surface->width),
      .sourceHeight = surface->sourceSet ? surface->sourceHeight : static_cast<float>(surface->height),
      .destinationWidth = surface->destinationSet ? surface->destinationWidth : displayWidth(surface),
      .destinationHeight = surface->destinationSet ? surface->destinationHeight : displayHeight(surface),
      .titleBarHeight = 0,
      .title = {},
      .focused = false,
      .activeSizing = false,
      .serial = surface->serial,
      .rgbaPixels = surface->rgbaPixels,
      .dmabufFormat = 0,
      .dmabufPlanes = {},
  };
  if (surface->dmabufBuffer) {
    snapshot.dmabufFormat = surface->dmabufBuffer->format;
    snapshot.dmabufPlanes.reserve(surface->dmabufBuffer->planes.size());
    for (DmabufPlane const& plane : surface->dmabufBuffer->planes) {
      snapshot.dmabufPlanes.push_back({
          .offset = plane.offset,
          .stride = plane.stride,
          .modifier = plane.modifier,
      });
    }
  }
  return snapshot;
}

std::vector<int> WaylandServer::Impl::duplicateDmabufFds(std::uint64_t surfaceId) const {
  auto surface = std::find_if(surfaces_.begin(), surfaces_.end(),
                              [surfaceId](auto const& candidate) { return candidate->id == surfaceId; });
  if (surface == surfaces_.end() || !(*surface)->dmabufBuffer) return {};

  std::vector<int> fds;
  fds.reserve((*surface)->dmabufBuffer->planes.size());
  for (DmabufPlane const& plane : (*surface)->dmabufBuffer->planes) {
    int copied = dup(plane.fd);
    if (copied < 0) {
      for (int fd : fds) close(fd);
      return {};
    }
    fds.push_back(copied);
  }
  return fds;
}

std::optional<SnapPreviewSnapshot> WaylandServer::Impl::snapPreview() const {
  return snapPreviewForDrag(this);
}


bool WaylandServer::Impl::copyDmabufToRgba(std::uint64_t surfaceId, std::vector<std::uint8_t>& out) const {
  auto surface = std::find_if(surfaces_.begin(), surfaces_.end(),
                              [surfaceId](auto const& candidate) { return candidate->id == surfaceId; });
  if (surface == surfaces_.end() || !(*surface)->dmabufBuffer) return false;

  DmabufBuffer const& buffer = *(*surface)->dmabufBuffer;
  if (buffer.width <= 0 || buffer.height <= 0 || buffer.planes.size() != 1) return false;
  if (!isSupportedDmabufFormat(buffer.format)) return false;

  DmabufPlane const& plane = buffer.planes.front();
  if (plane.fd < 0 || plane.stride < static_cast<std::uint32_t>(buffer.width) * 4u) return false;
  if (plane.modifier != DRM_FORMAT_MOD_LINEAR && plane.modifier != DRM_FORMAT_MOD_INVALID) return false;

  std::size_t const rowBytes = static_cast<std::size_t>(buffer.width) * 4u;
  std::size_t const dataSize = static_cast<std::size_t>(plane.offset) +
                               static_cast<std::size_t>(plane.stride) *
                                   static_cast<std::size_t>(buffer.height);
  void* mapping = mmap(nullptr, dataSize, PROT_READ, MAP_SHARED, plane.fd, 0);
  if (mapping == MAP_FAILED) {
    std::fprintf(stderr, "flux-compositor: dmabuf CPU fallback mmap failed: %s\n", std::strerror(errno));
    return false;
  }

  out.resize(static_cast<std::size_t>(buffer.width) * static_cast<std::size_t>(buffer.height) * 4u);
  auto const* base = static_cast<std::uint8_t const*>(mapping) + plane.offset;
  for (std::int32_t y = 0; y < buffer.height; ++y) {
    auto const* src = base + static_cast<std::size_t>(y) * plane.stride;
    auto* dst = out.data() + static_cast<std::size_t>(y) * rowBytes;
    for (std::int32_t x = 0; x < buffer.width; ++x) {
      std::uint8_t const b0 = src[static_cast<std::size_t>(x) * 4u + 0u];
      std::uint8_t const b1 = src[static_cast<std::size_t>(x) * 4u + 1u];
      std::uint8_t const b2 = src[static_cast<std::size_t>(x) * 4u + 2u];
      std::uint8_t const b3 = src[static_cast<std::size_t>(x) * 4u + 3u];
      if (buffer.format == DRM_FORMAT_ARGB8888 || buffer.format == DRM_FORMAT_XRGB8888) {
        dst[static_cast<std::size_t>(x) * 4u + 0u] = b2;
        dst[static_cast<std::size_t>(x) * 4u + 1u] = b1;
        dst[static_cast<std::size_t>(x) * 4u + 2u] = b0;
        dst[static_cast<std::size_t>(x) * 4u + 3u] =
            buffer.format == DRM_FORMAT_XRGB8888 ? 255u : b3;
      } else {
        dst[static_cast<std::size_t>(x) * 4u + 0u] = b0;
        dst[static_cast<std::size_t>(x) * 4u + 1u] = b1;
        dst[static_cast<std::size_t>(x) * 4u + 2u] = b2;
        dst[static_cast<std::size_t>(x) * 4u + 3u] =
            buffer.format == DRM_FORMAT_XBGR8888 ? 255u : b3;
      }
    }
  }

  munmap(mapping, dataSize);
  return true;
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

void WaylandServer::Impl::updateAnimations(std::uint32_t timeMs, bool animationsEnabled) {
  bool sentConfigure = false;
  for (auto const& surface : surfaces_) {
    if (!surface->geometryAnimationActive) continue;

    float const linearProgress =
        animationsEnabled
            ? static_cast<float>(timeMs - surface->geometryAnimationStartedAtMs) /
                  static_cast<float>(kGeometryAnimationMs)
            : 1.f;
    float const progress = easeInOutCubic(linearProgress);
    std::int32_t const nextX = lerpInt(surface->geometryAnimationStartX, surface->geometryAnimationTargetX, progress);
    std::int32_t const nextY = lerpInt(surface->geometryAnimationStartY, surface->geometryAnimationTargetY, progress);
    std::int32_t const nextWidth =
        std::max(kMinWindowWidth,
                 lerpInt(surface->geometryAnimationStartWidth, surface->geometryAnimationTargetWidth, progress));
    std::int32_t const nextHeight =
        std::max(kMinWindowHeight,
                 lerpInt(surface->geometryAnimationStartHeight, surface->geometryAnimationTargetHeight, progress));

    surface->windowX = nextX;
    surface->windowY = nextY;
    setConfiguredFrameSize(surface.get(), nextWidth, nextHeight);
    traceResizeSurface("animation-frame", surface.get());
    if (nextWidth != surface->geometryAnimationLastConfigureWidth ||
        nextHeight != surface->geometryAnimationLastConfigureHeight) {
      surface->geometryAnimationLastConfigureWidth = nextWidth;
      surface->geometryAnimationLastConfigureHeight = nextHeight;
      sendToplevelConfigure(this, toplevelForSurface(this, surface.get()), nextWidth, nextHeight);
      sentConfigure = true;
    }

    if (linearProgress >= 1.f) {
      surface->windowX = surface->geometryAnimationTargetX;
      surface->windowY = surface->geometryAnimationTargetY;
      setConfiguredFrameSize(surface.get(),
                             surface->geometryAnimationTargetWidth,
                             surface->geometryAnimationTargetHeight);
      surface->geometryAnimationActive = false;
      if (surface->frameWidth != surface->geometryAnimationLastConfigureWidth ||
          surface->frameHeight != surface->geometryAnimationLastConfigureHeight) {
        surface->geometryAnimationLastConfigureWidth = surface->frameWidth;
        surface->geometryAnimationLastConfigureHeight = surface->frameHeight;
        sendToplevelConfigure(this, toplevelForSurface(this, surface.get()), surface->frameWidth, surface->frameHeight);
        sentConfigure = true;
      }
    }
  }
  if (sentConfigure) flushClients();
}

void WaylandServer::Impl::sendFrameCallbacks(std::uint32_t timeMs) {
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  std::uint64_t const seconds = static_cast<std::uint64_t>(now.tv_sec);
  std::uint32_t const tvSecHi = static_cast<std::uint32_t>(seconds >> 32u);
  std::uint32_t const tvSecLo = static_cast<std::uint32_t>(seconds & 0xffffffffu);
  std::uint32_t const tvNsec = static_cast<std::uint32_t>(now.tv_nsec);
  std::uint32_t const refreshNsec =
      output_.refreshMilliHz > 0
          ? static_cast<std::uint32_t>(1'000'000'000'000ull / static_cast<std::uint64_t>(output_.refreshMilliHz))
          : 0u;

  for (auto const& surface : surfaces_) {
    std::vector<PresentationFeedback*> feedbacks = std::move(surface->presentationFeedbacks);
    surface->presentationFeedbacks.clear();
    for (auto* feedback : feedbacks) {
      if (!feedback || !feedback->resource) continue;
      wp_presentation_feedback_send_presented(feedback->resource,
                                              tvSecHi,
                                              tvSecLo,
                                              tvNsec,
                                              refreshNsec,
                                              0,
                                              0,
                                              0);
      wl_resource_destroy(feedback->resource);
    }
    std::vector<wl_resource*> callbacks = std::move(surface->frameCallbacks);
    surface->frameCallbacks.clear();
    for (wl_resource* callback : callbacks) {
      wl_callback_send_done(callback, timeMs);
      wl_resource_destroy(callback);
    }
  }
  flushClients();
}

void WaylandServer::Impl::destroySurface(Surface* surface) {
  if (pointerFocus_ == surface) pointerFocus_ = nullptr;
  if (keyboardFocus_ == surface) keyboardFocus_ = nullptr;
  if (primarySelectionSource_ &&
      wl_resource_get_client(primarySelectionSource_->resource) == wl_resource_get_client(surface->resource)) {
    primarySelectionSource_ = nullptr;
    sendPrimarySelectionForFocus(this);
  }
  if (selectionSource_ && wl_resource_get_client(selectionSource_->resource) == wl_resource_get_client(surface->resource)) {
    selectionSource_ = nullptr;
    sendSelectionForFocus(this);
  }
  if (dragSurface_ == surface) dragSurface_ = nullptr;
  if (resizeSurface_ == surface) resizeSurface_ = nullptr;
  if (closePressSurface_ == surface) closePressSurface_ = nullptr;
  if (lastTitleClickSurface_ == surface) lastTitleClickSurface_ = nullptr;
  if (cursorSurface_ == surface) cursorSurface_ = nullptr;
  if (lastActivationSurface_ == surface) lastActivationSurface_ = nullptr;
  for (auto& token : activationTokens_) {
    if (token->surface == surface) token->surface = nullptr;
  }
  for (auto& popup : popups_) {
    if (popup->parentSurface == surface) popup->parentSurface = nullptr;
  }
  for (auto it = subsurfaces_.begin(); it != subsurfaces_.end();) {
    if ((*it)->surface == surface || (*it)->parent == surface) {
      wl_resource_destroy((*it)->resource);
      it = subsurfaces_.begin();
    } else {
      ++it;
    }
  }
  if (dndOrigin_ == surface || dndTarget_ == surface) clearDnd(this);
  for (auto& device : cursorShapeDevices_) {
    if (device->pointer && wl_resource_get_client(device->pointer) == wl_resource_get_client(surface->resource)) {
      device->pointer = nullptr;
    }
  }
  if (surface->viewport) wl_resource_destroy(surface->viewport->resource);
  if (surface->fractionalScale) wl_resource_destroy(surface->fractionalScale->resource);
  if (surface->layerSurface) wl_resource_destroy(surface->layerSurface->resource);
  if (surface->xdgPopup) wl_resource_destroy(surface->xdgPopup->resource);
  for (auto it = pointerConstraints_.begin(); it != pointerConstraints_.end();) {
    if ((*it)->surface == surface) {
      wl_resource_destroy((*it)->resource);
      it = pointerConstraints_.begin();
    } else {
      ++it;
    }
  }
  std::vector<PresentationFeedback*> pendingFeedbacks = std::move(surface->pendingPresentationFeedbacks);
  surface->pendingPresentationFeedbacks.clear();
  for (auto* feedback : pendingFeedbacks) {
    if (!feedback || !feedback->resource) continue;
    wp_presentation_feedback_send_discarded(feedback->resource);
    wl_resource_destroy(feedback->resource);
  }
  std::vector<PresentationFeedback*> committedFeedbacks = std::move(surface->presentationFeedbacks);
  surface->presentationFeedbacks.clear();
  for (auto* feedback : committedFeedbacks) {
    if (!feedback || !feedback->resource) continue;
    wp_presentation_feedback_send_discarded(feedback->resource);
    wl_resource_destroy(feedback->resource);
  }
  for (auto it = idleInhibitors_.begin(); it != idleInhibitors_.end();) {
    if ((*it)->surface == surface) {
      wl_resource_destroy((*it)->resource);
      it = idleInhibitors_.begin();
    } else {
      ++it;
    }
  }
  for (wl_resource* callback : surface->frameCallbacks) {
    wl_resource_destroy(callback);
  }
  surface->frameCallbacks.clear();
  eraseResource(surfaces_, surface);
}

void WaylandServer::Impl::destroySubsurface(Subsurface* subsurface) {
  if (subsurface && subsurface->surface && subsurface->surface->subsurfaceRole == subsurface) {
    subsurface->surface->subsurfaceRole = nullptr;
    subsurface->surface->subsurface = false;
  }
  eraseResource(subsurfaces_, subsurface);
}

void WaylandServer::Impl::destroyXdgSurface(XdgSurface* surface) {
  eraseResource(xdgSurfaces_, surface);
}

void WaylandServer::Impl::destroyXdgPositioner(XdgPositioner* positioner) {
  eraseResource(xdgPositioners_, positioner);
}

void WaylandServer::Impl::destroyXdgToplevel(XdgToplevel* toplevel) {
  while (auto* decoration = decorationFor(this, toplevel)) {
    wl_resource_destroy(decoration->resource);
  }
  eraseResource(toplevels_, toplevel);
}

void WaylandServer::Impl::destroyXdgPopup(XdgPopup* popup) {
  if (popup && popup->xdgSurface && popup->xdgSurface->surface && popup->xdgSurface->surface->xdgPopup == popup) {
    popup->xdgSurface->surface->xdgPopup = nullptr;
    popup->xdgSurface->surface->popup = false;
    popup->xdgSurface->surface->toplevel = false;
  }
  eraseResource(popups_, popup);
}

void WaylandServer::Impl::destroyShmPool(ShmPool* pool) {
  for (auto& buffer : shmBuffers_) {
    if (buffer->pool == pool) buffer->pool = nullptr;
  }
  if (pool->data) munmap(pool->data, static_cast<std::size_t>(pool->size));
  if (pool->fd >= 0) close(pool->fd);
  eraseResource(shmPools_, pool);
}

void WaylandServer::Impl::destroyShmBuffer(ShmBuffer* buffer) {
  if (buffer->data) munmap(buffer->data, static_cast<std::size_t>(buffer->size));
  if (buffer->fd >= 0) close(buffer->fd);
  eraseResource(shmBuffers_, buffer);
}

void WaylandServer::Impl::destroyDmabufParams(DmabufParams* params) {
  for (auto& plane : params->planes) {
    if (plane.fd >= 0) close(plane.fd);
    plane.fd = -1;
  }
  eraseResource(dmabufParams_, params);
}

void WaylandServer::Impl::destroyDmabufBuffer(DmabufBuffer* buffer) {
  for (auto const& surface : surfaces_) {
    if (surface->dmabufBuffer == buffer) {
      surface->dmabufBuffer = nullptr;
      surface->width = 0;
      surface->height = 0;
      surface->rgbaPixels.clear();
      ++surface->serial;
    }
  }
  for (auto& plane : buffer->planes) {
    if (plane.fd >= 0) close(plane.fd);
    plane.fd = -1;
  }
  eraseResource(dmabufBuffers_, buffer);
}

void WaylandServer::Impl::destroyToplevelDecoration(ToplevelDecoration* decoration) {
  eraseResource(toplevelDecorations_, decoration);
}

void WaylandServer::Impl::destroyViewport(Viewport* viewport) {
  if (viewport->surface && viewport->surface->viewport == viewport) {
    viewport->surface->viewport = nullptr;
    viewport->surface->pendingSourceSet = false;
    viewport->surface->pendingSourceX = 0.f;
    viewport->surface->pendingSourceY = 0.f;
    viewport->surface->pendingSourceWidth = 0.f;
    viewport->surface->pendingSourceHeight = 0.f;
    viewport->surface->pendingDestinationSet = false;
    viewport->surface->pendingDestinationWidth = 0;
    viewport->surface->pendingDestinationHeight = 0;
  }
  eraseResource(viewports_, viewport);
}

void WaylandServer::Impl::destroyFractionalScale(FractionalScale* fractionalScale) {
  if (fractionalScale->surface && fractionalScale->surface->fractionalScale == fractionalScale) {
    fractionalScale->surface->fractionalScale = nullptr;
  }
  eraseResource(fractionalScales_, fractionalScale);
}

void WaylandServer::Impl::destroyCursorShapeDevice(CursorShapeDevice* device) {
  eraseResource(cursorShapeDevices_, device);
}

void WaylandServer::Impl::destroyIdleInhibitor(IdleInhibitor* inhibitor) {
  eraseResource(idleInhibitors_, inhibitor);
  std::fprintf(stderr, "flux-compositor: idle inhibitors active=%zu\n", idleInhibitors_.size());
}

void WaylandServer::Impl::destroyLayerSurface(LayerSurface* layerSurface) {
  if (layerSurface && layerSurface->surface && layerSurface->surface->layerSurface == layerSurface) {
    layerSurface->surface->layerSurface = nullptr;
  }
  eraseResource(layerSurfaces_, layerSurface);
}

void WaylandServer::Impl::destroyPresentationFeedback(PresentationFeedback* feedback) {
  if (feedback && feedback->surface) {
    auto eraseFeedback = [feedback](std::vector<PresentationFeedback*>& feedbacks) {
      feedbacks.erase(std::remove(feedbacks.begin(), feedbacks.end(), feedback), feedbacks.end());
    };
    eraseFeedback(feedback->surface->pendingPresentationFeedbacks);
    eraseFeedback(feedback->surface->presentationFeedbacks);
  }
  eraseResource(presentationFeedbacks_, feedback);
}

void WaylandServer::Impl::destroyRelativePointer(RelativePointer* relativePointer) {
  eraseResource(relativePointers_, relativePointer);
}

void WaylandServer::Impl::destroyPointerConstraint(PointerConstraint* constraint) {
  if (constraint && constraint->active && constraint->resource) {
    if (constraint->kind == PointerConstraint::Kind::Lock) {
      zwp_locked_pointer_v1_send_unlocked(constraint->resource);
    } else {
      zwp_confined_pointer_v1_send_unconfined(constraint->resource);
    }
  }
  eraseResource(pointerConstraints_, constraint);
}

void WaylandServer::Impl::destroyPrimarySelectionDevice(PrimarySelectionDevice* device) {
  eraseResource(primarySelectionDevices_, device);
}

void WaylandServer::Impl::destroyPrimarySelectionSource(PrimarySelectionSource* source) {
  if (primarySelectionSource_ == source) {
    primarySelectionSource_ = nullptr;
    sendPrimarySelectionForFocus(this);
  }
  for (auto& offer : primarySelectionOffers_) {
    if (offer->source == source) offer->source = nullptr;
  }
  eraseResource(primarySelectionSources_, source);
}

void WaylandServer::Impl::destroyPrimarySelectionOffer(PrimarySelectionOffer* offer) {
  eraseResource(primarySelectionOffers_, offer);
}

void WaylandServer::Impl::destroyDataDevice(DataDevice* device) {
  if (dndTarget_ && device->resource &&
      wl_resource_get_client(device->resource) == wl_resource_get_client(dndTarget_->resource)) {
    clearDnd(this);
  }
  eraseResource(dataDevices_, device);
}

void WaylandServer::Impl::destroyDataSource(DataSource* source) {
  if (selectionSource_ == source) {
    selectionSource_ = nullptr;
    sendSelectionForFocus(this);
  }
  if (dndSource_ == source) clearDnd(this);
  for (auto& offer : dataOffers_) {
    if (offer->source == source) offer->source = nullptr;
  }
  eraseResource(dataSources_, source);
}

void WaylandServer::Impl::destroyDataOffer(DataOffer* offer) {
  if (dndOffer_ == offer) dndOffer_ = nullptr;
  eraseResource(dataOffers_, offer);
}

void WaylandServer::Impl::destroyActivationToken(ActivationToken* token) {
  eraseResource(activationTokens_, token);
}

WaylandServer::WaylandServer(WaylandOutputInfo output) : impl_(std::make_unique<Impl>(std::move(output))) {}

WaylandServer::~WaylandServer() = default;

char const* WaylandServer::socketName() const noexcept {
  return impl_->socketName();
}

int WaylandServer::eventFd() const noexcept {
  return impl_->eventFd();
}

float WaylandServer::preferredScale() const noexcept {
  return impl_->preferredScale();
}

std::int32_t WaylandServer::logicalOutputWidth() const noexcept {
  return impl_->logicalOutputWidth();
}

std::int32_t WaylandServer::logicalOutputHeight() const noexcept {
  return impl_->logicalOutputHeight();
}

std::size_t WaylandServer::toplevelCount() const noexcept {
  return impl_->toplevelCount();
}

std::vector<CommittedSurfaceSnapshot> WaylandServer::committedSurfaces() const {
  return impl_->committedSurfaces();
}

std::optional<CommittedSurfaceSnapshot> WaylandServer::cursorSurface() const {
  return impl_->cursorSurface();
}

std::optional<SnapPreviewSnapshot> WaylandServer::snapPreview() const {
  return impl_->snapPreview();
}

std::vector<int> WaylandServer::duplicateDmabufFds(std::uint64_t surfaceId) const {
  return impl_->duplicateDmabufFds(surfaceId);
}

bool WaylandServer::copyDmabufToRgba(std::uint64_t surfaceId, std::vector<std::uint8_t>& out) const {
  return impl_->copyDmabufToRgba(surfaceId, out);
}

void WaylandServer::dispatch() {
  impl_->dispatch();
}

void WaylandServer::flushClients() {
  impl_->flushClients();
}

void WaylandServer::setShortcutBindings(std::vector<ShortcutBinding> bindings) {
  impl_->setShortcutBindings(std::move(bindings));
}

void WaylandServer::setPreferredScale(float scale) {
  impl_->setPreferredScale(scale);
}

void WaylandServer::updateAnimations(std::uint32_t timeMs, bool animationsEnabled) {
  impl_->updateAnimations(timeMs, animationsEnabled);
}

void WaylandServer::sendFrameCallbacks(std::uint32_t timeMs) {
  impl_->sendFrameCallbacks(timeMs);
}

void WaylandServer::handlePointerMotion(double dx, double dy, std::uint32_t timeMs) {
  impl_->handlePointerMotion(dx, dy, timeMs);
}

void WaylandServer::handlePointerPosition(double x, double y, std::uint32_t timeMs) {
  impl_->handlePointerPosition(x, y, timeMs);
}

void WaylandServer::handlePointerButton(std::uint32_t button, bool pressed, std::uint32_t timeMs) {
  impl_->handlePointerButton(button, pressed, timeMs);
}

void WaylandServer::handlePointerAxis(double dx, double dy, std::uint32_t timeMs) {
  impl_->handlePointerAxis(dx, dy, timeMs);
}

void WaylandServer::handleKeyboardKey(std::uint32_t key, bool pressed, std::uint32_t timeMs) {
  impl_->handleKeyboardKey(key, pressed, timeMs);
}

float WaylandServer::pointerX() const noexcept {
  return impl_->pointerX_;
}

float WaylandServer::pointerY() const noexcept {
  return impl_->pointerY_;
}

CursorShape WaylandServer::cursorShape() const noexcept {
  return impl_->cursorShape_;
}

} // namespace flux::compositor
