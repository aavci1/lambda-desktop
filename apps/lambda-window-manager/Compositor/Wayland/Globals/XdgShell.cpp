#include "Compositor/Wayland/Globals/XdgShell.hpp"

#include "Compositor/Chrome/ChromeMetrics.hpp"
#include "Compositor/Diagnostics/CrashLog.hpp"
#include "Compositor/Wayland/DecorationState.hpp"
#include "Compositor/Window/WindowGeometry.hpp"
#include "Compositor/Window/WindowManagerInternal.hpp"
#include "Compositor/Wayland/ResourceTemplates.hpp"
#include "Compositor/Wayland/WaylandServerImpl.hpp"
#include "Compositor/Wayland/XdgPopupState.hpp"
#include "Compositor/Wayland/XdgPositionerState.hpp"
#include "Compositor/Wayland/XdgSurfaceState.hpp"
#include "Detail/ResizeTrace.hpp"
#include "xdg-decoration-unstable-v1-server-protocol.h"
#include "xdg-shell-server-protocol.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <memory>
#include <optional>
#include <wayland-server-core.h>

namespace lambda::compositor {

namespace {

constexpr std::array<SeatSerialKind, 1> kToplevelGrabSerialKinds{
    SeatSerialKind::PointerButtonPress,
};

bool validToplevelGrabSerial(WaylandServer::Impl const* server,
                             wl_resource* requestResource,
                             WaylandServer::Impl::Surface const* surface,
                             std::uint32_t serial) {
  if (!server || !requestResource || !surface) return false;
  return seatSerialIsValid(server,
                           serial,
                           wl_resource_get_client(requestResource),
                           surface,
                           kToplevelGrabSerialKinds);
}

void appendToplevelState(wl_array* states, std::uint32_t state) {
  auto* value = static_cast<std::uint32_t*>(wl_array_add(states, sizeof(std::uint32_t)));
  if (value) *value = state;
}

void fillToplevelStates(WaylandServer::Impl* server,
                        WaylandServer::Impl::XdgToplevel* toplevel,
                        wl_array* states) {
  if (!server || !toplevel || !toplevel->xdgSurface || !toplevel->xdgSurface->surface || !states) return;
  WaylandServer::Impl::Surface* surface = toplevel->xdgSurface->surface;
  if (surface->maximized) appendToplevelState(states, XDG_TOPLEVEL_STATE_MAXIMIZED);
  if (surface->fullscreen) appendToplevelState(states, XDG_TOPLEVEL_STATE_FULLSCREEN);
  if (server->resizeSurface_ == surface) appendToplevelState(states, XDG_TOPLEVEL_STATE_RESIZING);
  if (server->keyboardFocus_ == surface) appendToplevelState(states, XDG_TOPLEVEL_STATE_ACTIVATED);
}

void sendToplevelWmCapabilities(WaylandServer::Impl::XdgToplevel* toplevel) {
  if (!toplevel || !toplevel->resource ||
      wl_resource_get_version(toplevel->resource) < XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION) {
    return;
  }
  wl_array capabilities;
  wl_array_init(&capabilities);
  appendToplevelState(&capabilities, XDG_TOPLEVEL_WM_CAPABILITIES_MAXIMIZE);
  appendToplevelState(&capabilities, XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN);
  appendToplevelState(&capabilities, XDG_TOPLEVEL_WM_CAPABILITIES_MINIMIZE);
  xdg_toplevel_send_wm_capabilities(toplevel->resource, &capabilities);
  wl_array_release(&capabilities);
}

std::uint32_t sendXdgSurfaceConfigure(WaylandServer::Impl* server,
                                      WaylandServer::Impl::XdgSurface* xdgSurface,
                                      WaylandServer::Impl::XdgConfigure configure) {
  if (!server || !xdgSurface || !xdgSurface->resource) return 0;
  std::uint32_t const serial = server->nextConfigureSerial_++;
  configure.serial = serial;
  xdgSurface->configureList.push_back(configure);
  xdg_surface_send_configure(xdgSurface->resource, serial);
  return serial;
}

std::uint32_t sendToplevelConfigureInternal(WaylandServer::Impl* server,
                                            WaylandServer::Impl::XdgToplevel* toplevel,
                                            std::int32_t width,
                                            std::int32_t height,
                                            bool awaitSizedCommit,
                                            std::optional<WindowGeometry> windowGeometry = std::nullopt) {
  if (!server || !toplevel || !toplevel->resource || !toplevel->xdgSurface || !toplevel->xdgSurface->resource) return 0;
  if (awaitSizedCommit) {
    if (auto* surface = toplevel->xdgSurface->surface; surface && width > 0 && height > 0) {
      surface->awaitingConfigureCommit = true;
      surface->awaitingConfigureWidth = width;
      surface->awaitingConfigureHeight = height;
    }
  }
  wl_array states;
  wl_array_init(&states);
  fillToplevelStates(server, toplevel, &states);
  if (server &&
      wl_resource_get_version(toplevel->resource) >= XDG_TOPLEVEL_CONFIGURE_BOUNDS_SINCE_VERSION) {
    std::int32_t const boundsWidth = std::max(1, server->logicalOutputWidth());
    std::int32_t const boundsHeight = std::max(1, server->logicalOutputHeight());
    xdg_toplevel_send_configure_bounds(toplevel->resource, boundsWidth, boundsHeight);
  }
  xdg_toplevel_send_configure(toplevel->resource, width, height, &states);
  wl_array_release(&states);
  sendCutoutsConfigureIfNeeded(server, toplevel, width, height);
  WaylandServer::Impl::XdgConfigure configure{
      .role = SurfaceRole::XdgToplevel,
      .width = width,
      .height = height,
      .hasWindowGeometry = windowGeometry.has_value(),
      .windowX = windowGeometry ? windowGeometry->x : 0,
      .windowY = windowGeometry ? windowGeometry->y : 0,
      .windowWidth = windowGeometry ? windowGeometry->width : 0,
      .windowHeight = windowGeometry ? windowGeometry->height : 0,
  };
  std::uint32_t const serial = sendXdgSurfaceConfigure(server, toplevel->xdgSurface, configure);
  if (serial == 0) return 0;
  if (auto* surface = toplevel->xdgSurface->surface) {
    surface->lastConfigureSerial = serial;
    if (lambda::detail::resizeTraceMetadataEnabled()) {
      surface->lastConfigureSentNsec = lambda::detail::resizeTraceTimestampNanoseconds();
      surface->lastConfigureAckNsec = 0;
    }
    surface->lastConfigureWidth = width;
    surface->lastConfigureHeight = height;
  }
  LAMBDA_RESIZE_TRACE("compositor",
                            "configure surface=%llu size=%dx%d serial=%u awaiting=%d\n",
                            static_cast<unsigned long long>(toplevel->xdgSurface->surface
                                                                ? toplevel->xdgSurface->surface->id
                                                                : 0),
                            width,
                            height,
                            serial,
                            awaitSizedCommit ? 1 : 0);
  return serial;
}

} // namespace

WaylandServer::Impl::ToplevelDecoration* decorationFor(WaylandServer::Impl* server,
                                                       WaylandServer::Impl::XdgToplevel* toplevel) {
  auto found = std::find_if(server->toplevelDecorations_.begin(), server->toplevelDecorations_.end(),
                            [toplevel](auto const& decoration) { return decoration->toplevel == toplevel; });
  return found == server->toplevelDecorations_.end() ? nullptr : found->get();
}

WaylandServer::Impl::XxCutouts* cutoutsFor(WaylandServer::Impl* server,
                                           WaylandServer::Impl::XdgToplevel* toplevel) {
  auto found = std::find_if(server->cutouts_.begin(), server->cutouts_.end(),
                            [toplevel](auto const& cutouts) { return cutouts->toplevel == toplevel; });
  return found == server->cutouts_.end() ? nullptr : found->get();
}

WaylandServer::Impl::XdgToplevel* toplevelForSurface(WaylandServer::Impl* server,
                                                     WaylandServer::Impl::Surface* surface) {
  auto found = std::find_if(server->toplevels_.begin(), server->toplevels_.end(),
                            [surface](auto const& toplevel) {
                              return toplevel->xdgSurface && toplevel->xdgSurface->surface == surface;
                            });
  return found == server->toplevels_.end() ? nullptr : found->get();
}

std::string titleForSurface(WaylandServer::Impl const* server, WaylandServer::Impl::Surface const* surface) {
  auto found = std::find_if(server->toplevels_.begin(), server->toplevels_.end(),
                            [surface](auto const& toplevel) {
                              return toplevel->xdgSurface && toplevel->xdgSurface->surface == surface;
                            });
  if (found == server->toplevels_.end()) return "Window";
  if (!(*found)->title.empty()) return (*found)->title;
  if (!(*found)->appId.empty()) return (*found)->appId;
  return "Window";
}

void sendToplevelConfigure(WaylandServer::Impl* server,
                           WaylandServer::Impl::XdgToplevel* toplevel,
                           std::int32_t width,
                           std::int32_t height) {
  sendToplevelConfigureInternal(server, toplevel, width, height, true);
}

bool requestToplevelResizeConfigure(WaylandServer::Impl* server,
                                     WaylandServer::Impl::Surface* surface,
                                     std::int32_t x,
                                     std::int32_t y,
                                     std::int32_t width,
                                     std::int32_t height) {
  if (!server || !surface || width <= 0 || height <= 0) return false;
  auto* toplevel = toplevelForSurface(server, surface);
  if (!toplevel) return false;

  if (surface->resizeConfigureInFlight) {
    bool const sameAsInFlight =
        x == surface->resizeConfigureX && y == surface->resizeConfigureY &&
        width == surface->resizeConfigureWidth && height == surface->resizeConfigureHeight;
    surface->pendingResizeConfigure = !sameAsInFlight;
    surface->pendingResizeConfigureX = sameAsInFlight ? 0 : x;
    surface->pendingResizeConfigureY = sameAsInFlight ? 0 : y;
    surface->pendingResizeConfigureWidth = sameAsInFlight ? 0 : width;
    surface->pendingResizeConfigureHeight = sameAsInFlight ? 0 : height;
    if (!sameAsInFlight) {
      LAMBDA_RESIZE_TRACE("compositor",
                                  "resize-configure-defer surface=%llu desired=%d,%d %dx%d inFlight=%u %d,%d %dx%d "
                                  "acked=%d\n",
                                  static_cast<unsigned long long>(surface->id),
                                  x,
                                  y,
                                  width,
                                  height,
                                  surface->resizeConfigureSerial,
                                  surface->resizeConfigureX,
                                  surface->resizeConfigureY,
                                  surface->resizeConfigureWidth,
                                  surface->resizeConfigureHeight,
                                  surface->resizeConfigureAcked ? 1 : 0);
    }
    return false;
  }

  if (!surface->awaitingConfigureCommit &&
      width == displayWidth(surface) &&
      height == displayHeight(surface)) {
    if (x != surface->windowX || y != surface->windowY) {
      surface->windowX = x;
      surface->windowY = y;
      ++server->contentSerial_;
    }
    return false;
  }

  if (x == surface->windowX && y == surface->windowY &&
      width == surface->lastConfigureWidth && height == surface->lastConfigureHeight &&
      !surface->pendingResizeConfigure && !surface->awaitingConfigureCommit) {
    return false;
  }

  std::uint32_t const serial =
      sendToplevelConfigureInternal(server,
                                    toplevel,
                                    width,
                                    height,
                                    false,
                                    WindowGeometry{.x = x, .y = y, .width = width, .height = height});
  if (serial == 0) return false;
  surface->resizeConfigureInFlight = true;
  surface->resizeConfigureAcked = false;
  surface->resizeConfigureSerial = serial;
  surface->resizeConfigureX = x;
  surface->resizeConfigureY = y;
  surface->resizeConfigureWidth = width;
  surface->resizeConfigureHeight = height;
  surface->awaitingConfigureCommit = true;
  surface->awaitingConfigureWidth = width;
  surface->awaitingConfigureHeight = height;
  surface->pendingResizeConfigure = false;
  surface->pendingResizeConfigureX = 0;
  surface->pendingResizeConfigureY = 0;
  surface->pendingResizeConfigureWidth = 0;
  surface->pendingResizeConfigureHeight = 0;
  return true;
}

void sendToplevelStateConfigure(WaylandServer::Impl* server,
                                WaylandServer::Impl::XdgToplevel* toplevel) {
  if (!toplevel || !toplevel->xdgSurface || !toplevel->xdgSurface->surface) return;
  WaylandServer::Impl::Surface* surface = toplevel->xdgSurface->surface;
  std::int32_t const width = surface->frameWidth > 0 ? surface->frameWidth : 0;
  std::int32_t const height = surface->frameHeight > 0 ? surface->frameHeight : 0;
  sendToplevelConfigureInternal(server, toplevel, width, height, false);
}

bool toplevelServerSideDecorated(WaylandServer::Impl* server,
                                 WaylandServer::Impl::XdgToplevel* toplevel) {
  auto* decoration = decorationFor(server, toplevel);
  return decoration && decoration->mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
}

bool toplevelUsesCutouts(WaylandServer::Impl* server,
                         WaylandServer::Impl::XdgToplevel* toplevel) {
  return toplevelServerSideDecorated(server, toplevel) &&
         toplevel &&
         toplevel->cutouts &&
         !toplevel->cutoutsRejected;
}

void markCutoutsUnsent(WaylandServer::Impl::XxCutouts* cutouts) {
  if (!cutouts) return;
  cutouts->lastSent = false;
  cutouts->lastX = 0;
  cutouts->lastY = 0;
  cutouts->lastWidth = 0;
  cutouts->lastHeight = 0;
}

void sendCutoutsConfigureIfNeeded(WaylandServer::Impl* server,
                                  WaylandServer::Impl::XdgToplevel* toplevel,
                                  std::int32_t width,
                                  std::int32_t height,
                                  bool force) {
  if (!server || !toplevel || !toplevel->cutouts || !toplevel->cutouts->resource) return;
  bool const usesCutouts = toplevelUsesCutouts(server, toplevel);
  if (shouldSendEmptyCutoutConfigure(true, toplevel->cutouts->lastSent, usesCutouts)) {
    xx_cutouts_v1_send_configure(toplevel->cutouts->resource);
    markCutoutsUnsent(toplevel->cutouts);
    return;
  }
  if (!usesCutouts) return;
  if (width <= 0 || height <= 0) return;

  ChromeControlsMetrics const controls = chromeControlsMetrics(server->chromeConfig_);
  CutoutBox const box = compositorControlsCutout(width,
                                                 height,
                                                 static_cast<std::int32_t>(std::ceil(controls.controlsWidth)),
                                                 server->chromeConfig_.titleBarHeight);
  auto* cutouts = toplevel->cutouts;
  CutoutSendState const state{
      .lastSent = cutouts->lastSent,
      .lastX = cutouts->lastX,
      .lastY = cutouts->lastY,
      .lastWidth = cutouts->lastWidth,
      .lastHeight = cutouts->lastHeight,
  };
  if (!shouldSendCutoutConfigure(state, box, force)) {
    return;
  }
  xx_cutouts_v1_send_cutout_box(cutouts->resource,
                                box.x,
                                box.y,
                                box.width,
                                box.height,
                                XX_CUTOUTS_V1_TYPE_CUTOUT,
                                box.id);
  xx_cutouts_v1_send_configure(cutouts->resource);
  cutouts->lastSent = true;
  cutouts->lastX = box.x;
  cutouts->lastY = box.y;
  cutouts->lastWidth = box.width;
  cutouts->lastHeight = box.height;
}

void maybeSendInitialCutoutsConfigure(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface) {
  auto* toplevel = toplevelForSurface(server, surface);
  if (!toplevel || !toplevel->cutouts) return;
  if (!shouldSendInitialCutoutConfigure(toplevel->cutouts != nullptr,
                                        toplevel->cutoutsRejected,
                                        toplevel->cutouts->lastSent,
                                        displayWidth(surface),
                                        displayHeight(surface))) {
    return;
  }
  sendToplevelStateConfigure(server, toplevel);
}

namespace {

extern struct zxdg_toplevel_decoration_v1_interface const xdgToplevelDecorationImpl;
extern struct xdg_surface_interface const xdgSurfaceImpl;
extern struct xdg_toplevel_interface const xdgToplevelImpl;

WindowGeometry initialToplevelPlacement(WaylandServer::Impl* server) {
  std::int32_t const step = 36;
  std::int32_t const start = 80;
  std::int32_t const maxX = std::max(0, server->logicalOutputWidth() - kCompositorMinWindowWidth);
  std::int32_t const maxY = std::max(kCompositorTitleBarHeight,
                                     server->logicalOutputHeight() - kCompositorMinWindowHeight);
  std::int32_t const slotsX = std::max(1, (maxX - start) / step + 1);
  std::int32_t const slotsY = std::max(1, (maxY - start) / step + 1);
  std::int32_t const slotCount = std::max(1, slotsX * slotsY);
  std::int32_t const slot = static_cast<std::int32_t>(server->toplevels_.size() % static_cast<std::size_t>(slotCount));
  return {
      .x = std::clamp(start + (slot % slotsX) * step, 0, maxX),
      .y = std::clamp(start + (slot / slotsX) * step, kCompositorTitleBarHeight, maxY),
      .width = 0,
      .height = 0,
  };
}

bool toplevelParentWouldCycle(WaylandServer::Impl::XdgToplevel* child,
                              WaylandServer::Impl::XdgToplevel* parent) {
  for (auto* current = parent; current; current = current->parent) {
    if (current == child) return true;
  }
  return false;
}

bool postInvalidToplevelSize(wl_resource* resource, wm::ToplevelSizeHints const& hints) {
  if (wm::toplevelSizeHintsValid(hints)) return false;
  wl_resource_post_error(resource,
                         XDG_TOPLEVEL_ERROR_INVALID_SIZE,
                         "invalid xdg_toplevel size hints min=%dx%d max=%dx%d",
                         hints.minWidth,
                         hints.minHeight,
                         hints.maxWidth,
                         hints.maxHeight);
  return true;
}

std::uint32_t monotonicMilliseconds() {
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<std::uint32_t>(static_cast<std::uint64_t>(now.tv_sec) * 1000ull +
                                    static_cast<std::uint64_t>(now.tv_nsec) / 1'000'000ull);
}

void sendDecorationConfigure(WaylandServer::Impl::ToplevelDecoration* decoration) {
  zxdg_toplevel_decoration_v1_send_configure(decoration->resource, decoration->mode);
  if (decoration->toplevel && decoration->toplevel->xdgSurface && decoration->toplevel->xdgSurface->resource) {
    sendXdgSurfaceConfigure(decoration->server,
                            decoration->toplevel->xdgSurface,
                            WaylandServer::Impl::XdgConfigure{});
  }
}

void xdgDecorationManagerDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void xdgDecorationManagerGetToplevelDecoration(wl_client* client,
                                               wl_resource* resource,
                                               std::uint32_t id,
                                               wl_resource* toplevelResource) {
  auto* server = serverFrom(resource);
  auto* toplevel = resourceData<WaylandServer::Impl::XdgToplevel>(toplevelResource);
  if (decorationFor(server, toplevel)) {
    wl_resource_post_error(resource,
                           ZXDG_TOPLEVEL_DECORATION_V1_ERROR_ALREADY_CONSTRUCTED,
                           "xdg_toplevel already has a decoration object");
    return;
  }

  auto decoration = std::make_unique<WaylandServer::Impl::ToplevelDecoration>();
  decoration->server = server;
  decoration->toplevel = toplevel;
  wl_resource* decorationResource = wl_resource_create(client,
                                                       &zxdg_toplevel_decoration_v1_interface,
                                                       wl_resource_get_version(resource),
                                                       id);
  if (!decorationResource) {
    wl_client_post_no_memory(client);
    return;
  }
  decoration->resource = decorationResource;
  auto* raw = decoration.get();
  server->toplevelDecorations_.push_back(std::move(decoration));
  wl_resource_set_implementation(decorationResource,
                                 &xdgToplevelDecorationImpl,
                                 raw,
                                 destroyResourceCallback<WaylandServer::Impl::ToplevelDecoration,
                                                         WaylandServer::Impl,
                                                         &WaylandServer::Impl::destroyToplevelDecoration>);
  sendDecorationConfigure(raw);
  if (toplevel && toplevel->cutouts) sendToplevelStateConfigure(server, toplevel);
}

struct zxdg_decoration_manager_v1_interface const xdgDecorationManagerImpl{
    .destroy = xdgDecorationManagerDestroy,
    .get_toplevel_decoration = xdgDecorationManagerGetToplevelDecoration,
};

void xdgToplevelDecorationDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void xdgToplevelDecorationSetMode(wl_client*, wl_resource* resource, std::uint32_t mode) {
  auto* decoration = resourceData<WaylandServer::Impl::ToplevelDecoration>(resource);
  if (mode != ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE && mode != ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE) {
    wl_resource_post_error(resource, ZXDG_TOPLEVEL_DECORATION_V1_ERROR_INVALID_MODE,
                           "invalid xdg-decoration mode %u", mode);
    return;
  }
  decoration->mode = xdgTitlebarModeForClientRequest(mode,
                                                     ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE,
                                                     ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
  sendDecorationConfigure(decoration);
  if (decoration->toplevel && decoration->toplevel->cutouts) {
    sendToplevelStateConfigure(decoration->server, decoration->toplevel);
  }
}

void xdgToplevelDecorationUnsetMode(wl_client*, wl_resource* resource) {
  auto* decoration = resourceData<WaylandServer::Impl::ToplevelDecoration>(resource);
  decoration->mode = defaultDecorationMode(ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
  sendDecorationConfigure(decoration);
  if (decoration->toplevel && decoration->toplevel->cutouts) {
    sendToplevelStateConfigure(decoration->server, decoration->toplevel);
  }
}

struct zxdg_toplevel_decoration_v1_interface const xdgToplevelDecorationImpl{
    .destroy = xdgToplevelDecorationDestroy,
    .set_mode = xdgToplevelDecorationSetMode,
    .unset_mode = xdgToplevelDecorationUnsetMode,
};

void xdgWmBaseDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

XdgPositionerRules positionerRules(WaylandServer::Impl::XdgPositioner const* positioner) {
  if (!positioner) return {};
  return {
      .width = positioner->width,
      .height = positioner->height,
      .anchorRectWidth = positioner->anchorRectWidth,
      .anchorRectHeight = positioner->anchorRectHeight,
  };
}

bool positionerIsComplete(WaylandServer::Impl::XdgPositioner const* positioner) {
  return xdgPositionerComplete(positionerRules(positioner));
}

void postInvalidPositionerInput(wl_resource* resource, char const* message) {
  wl_resource_post_error(resource, XDG_POSITIONER_ERROR_INVALID_INPUT, "%s", message);
}

void xdgPositionerDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void xdgPositionerSetSize(wl_client*, wl_resource* resource, std::int32_t width, std::int32_t height) {
  if (!xdgPositionerSizeInputValid(width, height)) {
    postInvalidPositionerInput(resource, "xdg_positioner width and height must be positive");
    return;
  }
  auto* positioner = resourceData<WaylandServer::Impl::XdgPositioner>(resource);
  positioner->width = width;
  positioner->height = height;
}

void xdgPositionerSetAnchorRect(wl_client*, wl_resource* resource, std::int32_t x, std::int32_t y,
                                std::int32_t width, std::int32_t height) {
  if (!xdgPositionerAnchorRectInputValid(width, height)) {
    postInvalidPositionerInput(resource, "xdg_positioner anchor rectangle width and height must be non-negative");
    return;
  }
  auto* positioner = resourceData<WaylandServer::Impl::XdgPositioner>(resource);
  positioner->anchorRectX = x;
  positioner->anchorRectY = y;
  positioner->anchorRectWidth = width;
  positioner->anchorRectHeight = height;
}

void xdgPositionerSetAnchor(wl_client*, wl_resource* resource, std::uint32_t anchor) {
  if (!xdg_positioner_anchor_is_valid(anchor, static_cast<std::uint32_t>(wl_resource_get_version(resource)))) {
    postInvalidPositionerInput(resource, "invalid xdg_positioner anchor value");
    return;
  }
  resourceData<WaylandServer::Impl::XdgPositioner>(resource)->anchor = anchor;
}

void xdgPositionerSetGravity(wl_client*, wl_resource* resource, std::uint32_t gravity) {
  if (!xdg_positioner_gravity_is_valid(gravity, static_cast<std::uint32_t>(wl_resource_get_version(resource)))) {
    postInvalidPositionerInput(resource, "invalid xdg_positioner gravity value");
    return;
  }
  resourceData<WaylandServer::Impl::XdgPositioner>(resource)->gravity = gravity;
}

void xdgPositionerSetConstraintAdjustment(wl_client*, wl_resource* resource, std::uint32_t adjustment) {
  if (!xdg_positioner_constraint_adjustment_is_valid(adjustment,
                                                    static_cast<std::uint32_t>(wl_resource_get_version(resource)))) {
    postInvalidPositionerInput(resource, "invalid xdg_positioner constraint adjustment value");
    return;
  }
  resourceData<WaylandServer::Impl::XdgPositioner>(resource)->constraintAdjustment = adjustment;
}

void xdgPositionerSetOffset(wl_client*, wl_resource* resource, std::int32_t x, std::int32_t y) {
  auto* positioner = resourceData<WaylandServer::Impl::XdgPositioner>(resource);
  positioner->offsetX = x;
  positioner->offsetY = y;
}

void xdgPositionerSetReactive(wl_client*, wl_resource* resource) {
  resourceData<WaylandServer::Impl::XdgPositioner>(resource)->reactive = true;
}

void xdgPositionerSetParentSize(wl_client*,
                                wl_resource* resource,
                                std::int32_t parentWidth,
                                std::int32_t parentHeight) {
  auto* positioner = resourceData<WaylandServer::Impl::XdgPositioner>(resource);
  positioner->parentWidth = parentWidth;
  positioner->parentHeight = parentHeight;
}

void xdgPositionerSetParentConfigure(wl_client*, wl_resource* resource, std::uint32_t serial) {
  auto* positioner = resourceData<WaylandServer::Impl::XdgPositioner>(resource);
  positioner->hasParentConfigureSerial = true;
  positioner->parentConfigureSerial = serial;
}

struct xdg_positioner_interface const positionerImpl{
    .destroy = xdgPositionerDestroy,
    .set_size = xdgPositionerSetSize,
    .set_anchor_rect = xdgPositionerSetAnchorRect,
    .set_anchor = xdgPositionerSetAnchor,
    .set_gravity = xdgPositionerSetGravity,
    .set_constraint_adjustment = xdgPositionerSetConstraintAdjustment,
    .set_offset = xdgPositionerSetOffset,
    .set_reactive = xdgPositionerSetReactive,
    .set_parent_size = xdgPositionerSetParentSize,
    .set_parent_configure = xdgPositionerSetParentConfigure,
};

void xdgWmBaseCreatePositioner(wl_client* client, wl_resource* resource, std::uint32_t id) {
  auto* server = serverFrom(resource);
  auto positioner = std::make_unique<WaylandServer::Impl::XdgPositioner>();
  positioner->server = server;
  wl_resource* positionerResource =
      wl_resource_create(client, &xdg_positioner_interface, wl_resource_get_version(resource), id);
  if (!positionerResource) {
    wl_client_post_no_memory(client);
    return;
  }
  positioner->resource = positionerResource;
  auto* raw = positioner.get();
  server->xdgPositioners_.push_back(std::move(positioner));
  wl_resource_set_implementation(positionerResource,
                                 &positionerImpl,
                                 raw,
                                 destroyResourceCallback<WaylandServer::Impl::XdgPositioner,
                                                         WaylandServer::Impl,
                                                         &WaylandServer::Impl::destroyXdgPositioner>);
}

void xdgWmBaseGetXdgSurface(wl_client* client, wl_resource* resource, std::uint32_t id,
                            wl_resource* surfaceResource) {
  auto* server = serverFrom(resource);
  auto* surface = resourceData<WaylandServer::Impl::Surface>(surfaceResource);
  auto xdgSurface = std::make_unique<WaylandServer::Impl::XdgSurface>();
  xdgSurface->server = server;
  xdgSurface->surface = surface;
  wl_resource* xdgResource = wl_resource_create(client, &xdg_surface_interface,
                                                wl_resource_get_version(resource), id);
  if (!xdgResource) {
    wl_client_post_no_memory(client);
    return;
  }
  xdgSurface->resource = xdgResource;
  auto* raw = xdgSurface.get();
  server->xdgSurfaces_.push_back(std::move(xdgSurface));
  wl_resource_set_implementation(xdgResource,
                                 &xdgSurfaceImpl,
                                 raw,
                                 destroyResourceCallback<WaylandServer::Impl::XdgSurface,
                                                         WaylandServer::Impl,
                                                         &WaylandServer::Impl::destroyXdgSurface>);
}

void xdgWmBasePong(wl_client*, wl_resource*, std::uint32_t) {}

struct xdg_wm_base_interface const xdgWmBaseImpl{
    .destroy = xdgWmBaseDestroy,
    .create_positioner = xdgWmBaseCreatePositioner,
    .get_xdg_surface = xdgWmBaseGetXdgSurface,
    .pong = xdgWmBasePong,
};

void xdgSurfaceDestroy(wl_client*, wl_resource* resource) {
  auto* xdgSurface = resourceData<WaylandServer::Impl::XdgSurface>(resource);
  if (xdgSurfaceHasConstructedRoleObject(xdgSurface)) {
    wl_resource_post_error(resource,
                           XDG_SURFACE_ERROR_DEFUNCT_ROLE_OBJECT,
                           "xdg_surface was destroyed before its role object");
    return;
  }
  wl_resource_destroy(resource);
}

void xdgSurfaceGetToplevel(wl_client* client, wl_resource* resource, std::uint32_t id) {
  auto* xdgSurface = resourceData<WaylandServer::Impl::XdgSurface>(resource);
  if (!surfaceHasNoRole(xdgSurface->surface)) {
    wl_resource_post_error(resource, XDG_WM_BASE_ERROR_ROLE, "wl_surface already has another role");
    return;
  }
  auto toplevel = std::make_unique<WaylandServer::Impl::XdgToplevel>();
  toplevel->server = xdgSurface->server;
  toplevel->xdgSurface = xdgSurface;
  wl_resource* toplevelResource = wl_resource_create(client, &xdg_toplevel_interface,
                                                     wl_resource_get_version(resource), id);
  if (!toplevelResource) {
    wl_client_post_no_memory(client);
    return;
  }
  xdgSurface->surface->role = SurfaceRole::XdgToplevel;
  if (xdgSurface->server->cursorSurface_ == xdgSurface->surface) xdgSurface->server->cursorSurface_ = nullptr;
  WindowGeometry const placement = initialToplevelPlacement(xdgSurface->server);
  xdgSurface->surface->windowX = placement.x;
  xdgSurface->surface->windowY = placement.y;
  toplevel->resource = toplevelResource;
  auto* raw = toplevel.get();
  xdgSurface->server->toplevels_.push_back(std::move(toplevel));
  xdgSurface->server->notifyShellStateChanged();
  wl_resource_set_implementation(toplevelResource,
                                 &xdgToplevelImpl,
                                 raw,
                                 destroyResourceCallback<WaylandServer::Impl::XdgToplevel,
                                                         WaylandServer::Impl,
                                                         &WaylandServer::Impl::destroyXdgToplevel>);
  focusSurface(xdgSurface->server, xdgSurface->surface, monotonicMilliseconds());
  diagnostics::crashLog("xdg-toplevel-create surface=%llu total=%zu window=%d,%d",
                        static_cast<unsigned long long>(xdgSurface->surface->id),
                        xdgSurface->server->toplevels_.size(),
                        xdgSurface->surface->windowX,
                        xdgSurface->surface->windowY);
  sendToplevelWmCapabilities(raw);
  sendToplevelConfigureInternal(xdgSurface->server, raw, 0, 0, false);
}

PopupAnchor popupAnchor(std::uint32_t anchor) {
  switch (anchor) {
  case XDG_POSITIONER_ANCHOR_TOP: return PopupAnchor::Top;
  case XDG_POSITIONER_ANCHOR_BOTTOM: return PopupAnchor::Bottom;
  case XDG_POSITIONER_ANCHOR_LEFT: return PopupAnchor::Left;
  case XDG_POSITIONER_ANCHOR_RIGHT: return PopupAnchor::Right;
  case XDG_POSITIONER_ANCHOR_TOP_LEFT: return PopupAnchor::TopLeft;
  case XDG_POSITIONER_ANCHOR_BOTTOM_LEFT: return PopupAnchor::BottomLeft;
  case XDG_POSITIONER_ANCHOR_TOP_RIGHT: return PopupAnchor::TopRight;
  case XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT: return PopupAnchor::BottomRight;
  default: return PopupAnchor::None;
  }
}

PopupGravity popupGravity(std::uint32_t gravity) {
  switch (gravity) {
  case XDG_POSITIONER_GRAVITY_TOP: return PopupGravity::Top;
  case XDG_POSITIONER_GRAVITY_BOTTOM: return PopupGravity::Bottom;
  case XDG_POSITIONER_GRAVITY_LEFT: return PopupGravity::Left;
  case XDG_POSITIONER_GRAVITY_RIGHT: return PopupGravity::Right;
  case XDG_POSITIONER_GRAVITY_TOP_LEFT: return PopupGravity::TopLeft;
  case XDG_POSITIONER_GRAVITY_BOTTOM_LEFT: return PopupGravity::BottomLeft;
  case XDG_POSITIONER_GRAVITY_TOP_RIGHT: return PopupGravity::TopRight;
  case XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT: return PopupGravity::BottomRight;
  default: return PopupGravity::None;
  }
}

void configurePopup(WaylandServer::Impl::XdgPopup* popup, WaylandServer::Impl::XdgPositioner const* positioner) {
  if (!popup || !popup->xdgSurface || !popup->xdgSurface->surface || !positioner) return;
  WaylandServer::Impl::Surface* surface = popup->xdgSurface->surface;
  WaylandServer::Impl::Surface const* parent = popup->parentSurface;
  auto const geometry = positionedPopupGeometry({
      .parent = parent ? std::optional<WindowGeometry>{WindowGeometry{
                             .x = parent->windowX,
                             .y = parent->windowY,
                             .width = parent->frameWidth,
                             .height = parent->frameHeight,
                         }}
                       : std::nullopt,
      .output = {.width = popup->server->logicalOutputWidth(), .height = popup->server->logicalOutputHeight()},
      .anchorRectX = positioner->anchorRectX,
      .anchorRectY = positioner->anchorRectY,
      .anchorRectWidth = positioner->anchorRectWidth,
      .anchorRectHeight = positioner->anchorRectHeight,
      .width = positioner->width,
      .height = positioner->height,
      .offsetX = positioner->offsetX,
      .offsetY = positioner->offsetY,
      .anchor = popupAnchor(positioner->anchor),
      .gravity = popupGravity(positioner->gravity),
      .constraintAdjustment = static_cast<PopupConstraintAdjustment>(positioner->constraintAdjustment),
  });

  surface->windowX = geometry.window.x;
  surface->windowY = geometry.window.y;
  setConfiguredFrameSize(surface, geometry.window.width, geometry.window.height);
  popup->reactive = positioner->reactive;
  popup->hasParentConfigureSerial = positioner->hasParentConfigureSerial;
  popup->parentConfigureSerial = positioner->parentConfigureSerial;
  popup->positionerParentWidth = positioner->parentWidth;
  popup->positionerParentHeight = positioner->parentHeight;
  popup->configuredX = geometry.configureX;
  popup->configuredY = geometry.configureY;
  popup->configuredWidth = geometry.configureWidth;
  popup->configuredHeight = geometry.configureHeight;
}

void sendPopupConfigure(WaylandServer::Impl::XdgPopup* popup) {
  if (!popup || !popup->resource || !popup->xdgSurface || !popup->xdgSurface->resource) return;
  xdg_popup_send_configure(popup->resource,
                           popup->configuredX,
                           popup->configuredY,
                           popup->configuredWidth,
                           popup->configuredHeight);
  sendXdgSurfaceConfigure(popup->server,
                          popup->xdgSurface,
                          WaylandServer::Impl::XdgConfigure{
                              .role = SurfaceRole::XdgPopup,
                              .width = popup->configuredWidth,
                              .height = popup->configuredHeight,
                              .hasWindowGeometry = true,
                              .windowX = popup->configuredX,
                              .windowY = popup->configuredY,
                              .windowWidth = popup->configuredWidth,
                              .windowHeight = popup->configuredHeight,
                          });
}

void xdgPopupDestroy(wl_client*, wl_resource* resource) {
  auto* popup = resourceData<WaylandServer::Impl::XdgPopup>(resource);
  if (xdgPopupHasLiveChild(popup ? popup->server : nullptr, popup)) {
    wl_resource_post_error(resource,
                           XDG_WM_BASE_ERROR_NOT_THE_TOPMOST_POPUP,
                           "xdg_popup was destroyed while it was not the topmost popup");
    return;
  }
  wl_resource_destroy(resource);
}

void xdgPopupGrab(wl_client*, wl_resource* resource, wl_resource* seat, std::uint32_t serial) {
  auto* popup = resourceData<WaylandServer::Impl::XdgPopup>(resource);
  if (!popup || !popup->server) return;
  if (!popup->server->popupGrabsEnabled_) {
    popup->grabbed = true;
    return;
  }
  establishPopupGrab(popup->server, popup, seat, serial);
}

void xdgPopupReposition(wl_client*, wl_resource* resource, wl_resource* positionerResource, std::uint32_t token) {
  auto* popup = resourceData<WaylandServer::Impl::XdgPopup>(resource);
  auto* positioner = resourceData<WaylandServer::Impl::XdgPositioner>(positionerResource);
  if (!popup || !positioner || popup->dismissed) return;
  if (!positionerIsComplete(positioner)) {
    wl_resource_post_error(resource, XDG_WM_BASE_ERROR_INVALID_POSITIONER, "invalid xdg_popup positioner");
    return;
  }
  configurePopup(popup, positioner);
  if (wl_resource_get_version(resource) >= XDG_POPUP_REPOSITIONED_SINCE_VERSION) {
    xdg_popup_send_repositioned(resource, token);
  }
  sendPopupConfigure(popup);
}

struct xdg_popup_interface const xdgPopupImpl{
    .destroy = xdgPopupDestroy,
    .grab = xdgPopupGrab,
    .reposition = xdgPopupReposition,
};

void xdgSurfaceGetPopup(wl_client* client, wl_resource* resource, std::uint32_t id,
                        wl_resource* parentResource, wl_resource* positionerResource) {
  auto* xdgSurface = resourceData<WaylandServer::Impl::XdgSurface>(resource);
  auto* positioner = resourceData<WaylandServer::Impl::XdgPositioner>(positionerResource);
  if (!xdgSurface || !xdgSurface->surface || !positionerIsComplete(positioner)) {
    wl_resource_post_error(resource, XDG_WM_BASE_ERROR_INVALID_POSITIONER, "invalid xdg_popup positioner");
    return;
  }
  if (!surfaceHasNoRole(xdgSurface->surface)) {
    wl_resource_post_error(resource, XDG_WM_BASE_ERROR_ROLE, "wl_surface already has another role");
    return;
  }

  auto* parentXdgSurface = resourceData<WaylandServer::Impl::XdgSurface>(parentResource);
  if (!xdgPopupParentHasValidRole(parentXdgSurface)) {
    wl_resource_post_error(resource,
                           XDG_WM_BASE_ERROR_INVALID_POPUP_PARENT,
                           "xdg_popup parent must have an xdg role");
    return;
  }
  auto popup = std::make_unique<WaylandServer::Impl::XdgPopup>();
  popup->server = xdgSurface->server;
  popup->xdgSurface = xdgSurface;
  popup->parentSurface = parentXdgSurface ? parentXdgSurface->surface : nullptr;
  wl_resource* popupResource = wl_resource_create(client, &xdg_popup_interface, wl_resource_get_version(resource), id);
  if (!popupResource) {
    wl_client_post_no_memory(client);
    return;
  }
  popup->resource = popupResource;
  auto* raw = popup.get();
  xdgSurface->surface->role = SurfaceRole::XdgPopup;
  xdgSurface->surface->xdgPopup = raw;
  if (xdgSurface->server->cursorSurface_ == xdgSurface->surface) xdgSurface->server->cursorSurface_ = nullptr;
  configurePopup(raw, positioner);
  xdgSurface->server->popups_.push_back(std::move(popup));
  wl_resource_set_implementation(popupResource,
                                 &xdgPopupImpl,
                                 raw,
                                 destroyResourceCallback<WaylandServer::Impl::XdgPopup,
                                                         WaylandServer::Impl,
                                                         &WaylandServer::Impl::destroyXdgPopup>);
  std::fprintf(stderr,
               "lambda-window-manager: xdg_popup created surface=%llu parent=%llu geometry=%d,%d %dx%d\n",
               static_cast<unsigned long long>(xdgSurface->surface->id),
               static_cast<unsigned long long>(raw->parentSurface ? raw->parentSurface->id : 0),
               raw->configuredX,
               raw->configuredY,
               raw->configuredWidth,
               raw->configuredHeight);
  diagnostics::crashLog("xdg-popup-create resource=%u surface=%llu parent=%llu geometry=%d,%d %dx%d",
                        popupResource ? wl_resource_get_id(popupResource) : 0u,
                        static_cast<unsigned long long>(xdgSurface->surface->id),
                        static_cast<unsigned long long>(raw->parentSurface ? raw->parentSurface->id : 0),
                        raw->configuredX,
                        raw->configuredY,
                        raw->configuredWidth,
                        raw->configuredHeight);
  sendPopupConfigure(raw);
}

void xdgSurfaceAckConfigure(wl_client*, wl_resource* resource, std::uint32_t serial) {
  auto* xdgSurface = resourceData<WaylandServer::Impl::XdgSurface>(resource);
  if (!xdgSurface) return;
  if (!xdgSurfaceHasConstructedRoleObject(xdgSurface)) {
    wl_resource_post_error(resource,
                           XDG_SURFACE_ERROR_NOT_CONSTRUCTED,
                           "xdg_surface must have a role object before ack_configure");
    return;
  }
  auto configure = std::find_if(xdgSurface->configureList.begin(),
                                xdgSurface->configureList.end(),
                                [serial](WaylandServer::Impl::XdgConfigure const& candidate) {
                                  return candidate.serial == serial;
                                });
  if (configure == xdgSurface->configureList.end()) {
    wl_resource_post_error(resource,
                           XDG_SURFACE_ERROR_INVALID_SERIAL,
                           "ack_configure received unknown serial %u",
                           serial);
    return;
  }
  WaylandServer::Impl::XdgConfigure const ackedConfigure = *configure;
  std::optional<WaylandServer::Impl::XdgConfigure> ackedResizeConfigure;
  if (auto* surface = xdgSurface->surface; surface && surface->resizeConfigureInFlight) {
    auto resizeConfigure = std::find_if(xdgSurface->configureList.begin(),
                                        configure + 1,
                                        [surface](WaylandServer::Impl::XdgConfigure const& candidate) {
                                          return candidate.serial == surface->resizeConfigureSerial;
                                        });
    if (resizeConfigure != configure + 1) {
      ackedResizeConfigure = *resizeConfigure;
    }
  }
  xdgSurface->configureList.erase(xdgSurface->configureList.begin(), configure + 1);
  xdgSurface->pendingConfigure = ackedConfigure;
  xdgSurface->configured = true;
  if (auto* surface = xdgSurface->surface) {
    std::uint64_t now = 0;
    if (lambda::detail::resizeTraceMetadataEnabled()) {
      now = lambda::detail::resizeTraceTimestampNanoseconds();
      if (serial == surface->lastConfigureSerial) surface->lastConfigureAckNsec = now;
    }
    if (surface->resizeConfigureInFlight && ackedResizeConfigure) {
      surface->resizeConfigureAcked = true;
      if (ackedResizeConfigure->hasWindowGeometry) {
        surface->resizeConfigureX = ackedResizeConfigure->windowX;
        surface->resizeConfigureY = ackedResizeConfigure->windowY;
        surface->resizeConfigureWidth = ackedResizeConfigure->windowWidth;
        surface->resizeConfigureHeight = ackedResizeConfigure->windowHeight;
      }
    }
    double const configureToAckMs =
        now > 0 && surface->lastConfigureSentNsec > 0 && now >= surface->lastConfigureSentNsec
            ? static_cast<double>(now - surface->lastConfigureSentNsec) / 1'000'000.0
            : 0.0;
    LAMBDA_RESIZE_TRACE("compositor",
                              "ack-configure surface=%llu serial=%u lastSerial=%u configure=%dx%d "
                              "configureToAck=%.3fms\n",
                              static_cast<unsigned long long>(surface->id),
                              serial,
                              surface->lastConfigureSerial,
                              ackedConfigure.width,
                              ackedConfigure.height,
                              configureToAckMs);
  }

  auto* toplevel = toplevelForSurface(xdgSurface->server, xdgSurface->surface);
  if (!toplevel || !toplevel->cutouts || !toplevel->cutouts->pendingControlsUnhandled) return;
  toplevel->cutouts->pendingControlsUnhandled = false;
  if (toplevel->cutoutsRejected) return;

  toplevel->cutoutsRejected = true;
  if (xdgSurface->surface) {
    std::int32_t const nextWidth = displayWidth(xdgSurface->surface);
    std::int32_t const nextHeight =
        std::max(kCompositorMinWindowHeight,
                 displayHeight(xdgSurface->surface) - xdgSurface->server->chromeConfig_.titleBarHeight);
    setConfiguredFrameSize(xdgSurface->surface, nextWidth, nextHeight);
    if (requestToplevelResizeConfigure(xdgSurface->server,
                                       xdgSurface->surface,
                                       xdgSurface->surface->windowX,
                                       xdgSurface->surface->windowY,
                                       nextWidth,
                                       nextHeight)) {
      xdgSurface->server->flushClients();
    }
  }
}

struct xdg_surface_interface const xdgSurfaceImpl{
    .destroy = xdgSurfaceDestroy,
    .get_toplevel = xdgSurfaceGetToplevel,
    .get_popup = xdgSurfaceGetPopup,
    .set_window_geometry = [](wl_client*,
                              wl_resource* resource,
                              std::int32_t x,
                              std::int32_t y,
                              std::int32_t width,
                              std::int32_t height) {
      auto* xdgSurface = resourceData<WaylandServer::Impl::XdgSurface>(resource);
      if (!xdgSurface) return;
      if (!xdgSurfaceHasConstructedRoleObject(xdgSurface)) {
        wl_resource_post_error(resource,
                               XDG_SURFACE_ERROR_NOT_CONSTRUCTED,
                               "xdg_surface must have a role object before set_window_geometry");
        return;
      }
      if (!wm::xdgWindowGeometrySizeValid(width, height)) {
        wl_resource_post_error(resource,
                               XDG_SURFACE_ERROR_INVALID_SIZE,
                               "invalid xdg_surface window geometry %dx%d",
                               width,
                               height);
        return;
      }
      xdgSurface->pendingWindowGeometryX = x;
      xdgSurface->pendingWindowGeometryY = y;
      xdgSurface->pendingWindowGeometryWidth = width;
      xdgSurface->pendingWindowGeometryHeight = height;
      xdgSurface->pendingWindowGeometrySet = true;
    },
    .ack_configure = xdgSurfaceAckConfigure,
};

void xdgToplevelDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void xdgToplevelSetTitle(wl_client*, wl_resource* resource, char const* title) {
  auto* toplevel = resourceData<WaylandServer::Impl::XdgToplevel>(resource);
  std::string const nextTitle = title ? title : "";
  if (toplevel->title == nextTitle) return;
  toplevel->title = nextTitle;
  if (toplevel->server) {
    toplevel->server->notifyShellStateChanged();
  }
  diagnostics::crashLog("xdg-toplevel-title surface=%llu title=%s",
                        static_cast<unsigned long long>(toplevel->xdgSurface && toplevel->xdgSurface->surface
                                                            ? toplevel->xdgSurface->surface->id
                                                            : 0),
                        toplevel->title.c_str());
}

void xdgToplevelSetAppId(wl_client*, wl_resource* resource, char const* appId) {
  auto* toplevel = resourceData<WaylandServer::Impl::XdgToplevel>(resource);
  std::string const nextAppId = appId ? appId : "";
  if (toplevel->appId == nextAppId) return;
  toplevel->appId = nextAppId;
  if (toplevel->server) {
    toplevel->server->notifyShellStateChanged();
  }
  diagnostics::crashLog("xdg-toplevel-appid surface=%llu appId=%s",
                        static_cast<unsigned long long>(toplevel->xdgSurface && toplevel->xdgSurface->surface
                                                            ? toplevel->xdgSurface->surface->id
                                                            : 0),
                        toplevel->appId.c_str());
}

void xdgToplevelSetParent(wl_client*, wl_resource* resource, wl_resource* parentResource) {
  auto* toplevel = resourceData<WaylandServer::Impl::XdgToplevel>(resource);
  if (!toplevel) return;
  auto* parent = parentResource ? resourceData<WaylandServer::Impl::XdgToplevel>(parentResource) : nullptr;
  if (parent && toplevelParentWouldCycle(toplevel, parent)) {
    wl_resource_post_error(resource,
                           XDG_TOPLEVEL_ERROR_INVALID_PARENT,
                           "xdg_toplevel parent would create a cycle");
    return;
  }
  toplevel->parent = parent;
  if (toplevel->server) toplevel->server->notifyShellStateChanged();
}

void xdgToplevelShowWindowMenu(wl_client*,
                               wl_resource* resource,
                               wl_resource*,
                               std::uint32_t serial,
                               std::int32_t,
                               std::int32_t) {
  auto* toplevel = resourceData<WaylandServer::Impl::XdgToplevel>(resource);
  if (!toplevel || !toplevel->server || !toplevel->xdgSurface) return;
  auto* surface = toplevel->xdgSurface->surface;
  if (!validToplevelGrabSerial(toplevel->server, resource, surface, serial)) return;
  focusSurface(toplevel->server, surface, monotonicMilliseconds());
}

void xdgToplevelResize(wl_client*, wl_resource* resource, wl_resource*, std::uint32_t serial, std::uint32_t edges) {
  auto* toplevel = resourceData<WaylandServer::Impl::XdgToplevel>(resource);
  if (!toplevel || !toplevel->xdgSurface || !toplevel->xdgSurface->surface) return;
  auto* server = toplevel->server;
  auto* surface = toplevel->xdgSurface->surface;
  std::int32_t const width = displayWidth(surface);
  std::int32_t const height = displayHeight(surface);
  if (!validToplevelGrabSerial(server, resource, surface, serial)) return;
  if (edges == XDG_TOPLEVEL_RESIZE_EDGE_NONE || width <= 0 || height <= 0 ||
      surface->fullscreen || surface->maximized) {
    return;
  }
  server->resizeSurface_ = surface;
  server->resizeStartX_ = server->pointerX_;
  server->resizeStartY_ = server->pointerY_;
  server->resizeStartWindowX_ = surface->windowX;
  server->resizeStartWindowY_ = surface->windowY;
  server->resizeStartWidth_ = width;
  server->resizeStartHeight_ = height;
  server->resizeLastWidth_ = width;
  server->resizeLastHeight_ = height;
  server->resizeEdges_ = edges;
  surface->snapped = false;
}

void xdgToplevelMove(wl_client*, wl_resource* resource, wl_resource*, std::uint32_t serial) {
  auto* toplevel = resourceData<WaylandServer::Impl::XdgToplevel>(resource);
  if (!toplevel || !toplevel->xdgSurface || !toplevel->xdgSurface->surface) return;
  auto* server = toplevel->server;
  auto* surface = toplevel->xdgSurface->surface;
  if (surface->fullscreen) return;
  if (!validToplevelGrabSerial(server, resource, surface, serial)) return;
  focusSurface(server, surface, monotonicMilliseconds());
  server->dragSurface_ = surface;
  server->dragOffsetX_ = server->pointerX_ - static_cast<float>(surface->windowX);
  server->dragOffsetY_ = server->pointerY_ - static_cast<float>(surface->windowY);
  server->dragSnapTarget_.reset();
  server->dragSnapTargetStartedAtMs_ = 0;
  server->snapPreviewVisible_ = false;
  server->snapPreviewDropPending_ = false;
  server->snapPreviewSurfaceId_ = 0;
  server->snapPreviewStartedAtMs_ = 0;
  server->snapPreviewStartWindow_ = {};
  server->snapPreviewTargetWindow_ = {};
}

void xdgToplevelSetMaximized(wl_client*, wl_resource* resource) {
  auto* toplevel = resourceData<WaylandServer::Impl::XdgToplevel>(resource);
  if (!toplevel || !toplevel->server || !toplevel->xdgSurface || !toplevel->xdgSurface->surface) return;
  if (toplevel->xdgSurface->surface->fullscreen) {
    sendToplevelStateConfigure(toplevel->server, toplevel);
    return;
  }
  maximizeToplevel(toplevel->server, toplevel->xdgSurface->surface);
}

void xdgToplevelUnsetMaximized(wl_client*, wl_resource* resource) {
  auto* toplevel = resourceData<WaylandServer::Impl::XdgToplevel>(resource);
  if (!toplevel || !toplevel->server || !toplevel->xdgSurface || !toplevel->xdgSurface->surface) return;
  if (toplevel->xdgSurface->surface->fullscreen) {
    sendToplevelStateConfigure(toplevel->server, toplevel);
    return;
  }
  restoreToplevel(toplevel->server, toplevel->xdgSurface->surface);
}

void xdgToplevelSetMaxSize(wl_client*, wl_resource* resource, std::int32_t width, std::int32_t height) {
  auto* toplevel = resourceData<WaylandServer::Impl::XdgToplevel>(resource);
  if (!toplevel) return;
  wm::ToplevelSizeHints hints = wm::pendingSizeHints(toplevel);
  hints.maxWidth = width;
  hints.maxHeight = height;
  if (postInvalidToplevelSize(resource, hints)) return;
  toplevel->pendingMaxWidth = width;
  toplevel->pendingMaxHeight = height;
  toplevel->pendingMaxSizeSet = true;
}

void xdgToplevelSetMinSize(wl_client*, wl_resource* resource, std::int32_t width, std::int32_t height) {
  auto* toplevel = resourceData<WaylandServer::Impl::XdgToplevel>(resource);
  if (!toplevel) return;
  wm::ToplevelSizeHints hints = wm::pendingSizeHints(toplevel);
  hints.minWidth = width;
  hints.minHeight = height;
  if (postInvalidToplevelSize(resource, hints)) return;
  toplevel->pendingMinWidth = width;
  toplevel->pendingMinHeight = height;
  toplevel->pendingMinSizeSet = true;
}

void xdgToplevelSetFullscreen(wl_client*, wl_resource* resource, wl_resource*) {
  auto* toplevel = resourceData<WaylandServer::Impl::XdgToplevel>(resource);
  if (!toplevel || !toplevel->server || !toplevel->xdgSurface || !toplevel->xdgSurface->surface) return;
  wm::fullscreenToplevel(toplevel->server, toplevel->xdgSurface->surface);
}

void xdgToplevelUnsetFullscreen(wl_client*, wl_resource* resource) {
  auto* toplevel = resourceData<WaylandServer::Impl::XdgToplevel>(resource);
  if (!toplevel || !toplevel->server || !toplevel->xdgSurface || !toplevel->xdgSurface->surface) return;
  auto* surface = toplevel->xdgSurface->surface;
  if (!surface->fullscreen) {
    sendToplevelStateConfigure(toplevel->server, toplevel);
    return;
  }
  restoreToplevel(toplevel->server, surface);
}

void xdgToplevelSetMinimized(wl_client*, wl_resource* resource) {
  auto* toplevel = resourceData<WaylandServer::Impl::XdgToplevel>(resource);
  if (!toplevel || !toplevel->server || !toplevel->xdgSurface || !toplevel->xdgSurface->surface) return;
  minimizeToplevel(toplevel->server, toplevel->xdgSurface->surface, monotonicMilliseconds());
}

struct xdg_toplevel_interface const xdgToplevelImpl{
    .destroy = xdgToplevelDestroy,
    .set_parent = xdgToplevelSetParent,
    .set_title = xdgToplevelSetTitle,
    .set_app_id = xdgToplevelSetAppId,
    .show_window_menu = xdgToplevelShowWindowMenu,
    .move = xdgToplevelMove,
    .resize = xdgToplevelResize,
    .set_max_size = xdgToplevelSetMaxSize,
    .set_min_size = xdgToplevelSetMinSize,
    .set_maximized = xdgToplevelSetMaximized,
    .unset_maximized = xdgToplevelUnsetMaximized,
    .set_fullscreen = xdgToplevelSetFullscreen,
    .unset_fullscreen = xdgToplevelUnsetFullscreen,
    .set_minimized = xdgToplevelSetMinimized,
};

} // namespace

void bindXdgWmBase(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  wl_resource* resource = wl_resource_create(client, &xdg_wm_base_interface, std::min(version, 6u), id);
  if (!resource) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(resource, &xdgWmBaseImpl, data, nullptr);
}

void bindXdgDecorationManager(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  wl_resource* resource = wl_resource_create(client, &zxdg_decoration_manager_v1_interface,
                                             std::min(version, 1u), id);
  wl_resource_set_implementation(resource, &xdgDecorationManagerImpl, data, nullptr);
}

} // namespace lambda::compositor
