#include "Compositor/Wayland/Globals/XdgShell.hpp"

#include "Compositor/Wayland/DecorationState.hpp"
#include "Compositor/Window/WindowGeometry.hpp"
#include "Compositor/Wayland/ResourceTemplates.hpp"
#include "Compositor/Wayland/WaylandServerImpl.hpp"
#include "Detail/ResizeTrace.hpp"
#include "xdg-decoration-unstable-v1-server-protocol.h"
#include "xdg-shell-server-protocol.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <memory>
#include <optional>
#include <wayland-server-core.h>

namespace flux::compositor {

namespace {

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
  if (server->resizeSurface_ == surface) appendToplevelState(states, XDG_TOPLEVEL_STATE_RESIZING);
  if (server->keyboardFocus_ == surface) appendToplevelState(states, XDG_TOPLEVEL_STATE_ACTIVATED);
}

void sendToplevelConfigureInternal(WaylandServer::Impl* server,
                                   WaylandServer::Impl::XdgToplevel* toplevel,
                                   std::int32_t width,
                                   std::int32_t height,
                                   bool awaitSizedCommit) {
  if (!toplevel || !toplevel->resource || !toplevel->xdgSurface || !toplevel->xdgSurface->resource) return;
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
  xdg_toplevel_send_configure(toplevel->resource, width, height, &states);
  wl_array_release(&states);
  sendCutoutsConfigureIfNeeded(server, toplevel, width, height);
  flux::detail::resizeTrace("compositor",
                            "configure surface=%llu size=%dx%d serial=%u\n",
                            static_cast<unsigned long long>(toplevel->xdgSurface->surface
                                                                ? toplevel->xdgSurface->surface->id
                                                                : 0),
                            width,
                            height,
                            server->nextConfigureSerial_);
  xdg_surface_send_configure(toplevel->xdgSurface->resource, server->nextConfigureSerial_++);
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

  CutoutBox const box = compositorControlsCutout(width,
                                                 height,
                                                 server->chromeConfig_.controlsWidth,
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

std::uint32_t monotonicMilliseconds() {
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<std::uint32_t>(static_cast<std::uint64_t>(now.tv_sec) * 1000ull +
                                    static_cast<std::uint64_t>(now.tv_nsec) / 1'000'000ull);
}

void sendDecorationConfigure(WaylandServer::Impl::ToplevelDecoration* decoration) {
  zxdg_toplevel_decoration_v1_send_configure(decoration->resource, decoration->mode);
  if (decoration->toplevel && decoration->toplevel->xdgSurface && decoration->toplevel->xdgSurface->resource) {
    xdg_surface_send_configure(decoration->toplevel->xdgSurface->resource,
                               decoration->server->nextConfigureSerial_++);
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
  decoration->mode = decorationModeForClientRequest(mode,
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

void xdgPositionerDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void xdgPositionerSetSize(wl_client*, wl_resource* resource, std::int32_t width, std::int32_t height) {
  auto* positioner = resourceData<WaylandServer::Impl::XdgPositioner>(resource);
  positioner->width = width;
  positioner->height = height;
}

void xdgPositionerSetAnchorRect(wl_client*, wl_resource* resource, std::int32_t x, std::int32_t y,
                                std::int32_t width, std::int32_t height) {
  auto* positioner = resourceData<WaylandServer::Impl::XdgPositioner>(resource);
  positioner->anchorRectX = x;
  positioner->anchorRectY = y;
  positioner->anchorRectWidth = width;
  positioner->anchorRectHeight = height;
}

void xdgPositionerSetAnchor(wl_client*, wl_resource* resource, std::uint32_t anchor) {
  resourceData<WaylandServer::Impl::XdgPositioner>(resource)->anchor = anchor;
}

void xdgPositionerSetGravity(wl_client*, wl_resource* resource, std::uint32_t gravity) {
  resourceData<WaylandServer::Impl::XdgPositioner>(resource)->gravity = gravity;
}

void xdgPositionerSetConstraintAdjustment(wl_client*, wl_resource* resource, std::uint32_t adjustment) {
  resourceData<WaylandServer::Impl::XdgPositioner>(resource)->constraintAdjustment = adjustment;
}

void xdgPositionerSetOffset(wl_client*, wl_resource* resource, std::int32_t x, std::int32_t y) {
  auto* positioner = resourceData<WaylandServer::Impl::XdgPositioner>(resource);
  positioner->offsetX = x;
  positioner->offsetY = y;
}

struct xdg_positioner_interface const positionerImpl{
    .destroy = xdgPositionerDestroy,
    .set_size = xdgPositionerSetSize,
    .set_anchor_rect = xdgPositionerSetAnchorRect,
    .set_anchor = xdgPositionerSetAnchor,
    .set_gravity = xdgPositionerSetGravity,
    .set_constraint_adjustment = xdgPositionerSetConstraintAdjustment,
    .set_offset = xdgPositionerSetOffset,
    .set_reactive = [](wl_client*, wl_resource*) {},
    .set_parent_size = [](wl_client*, wl_resource*, std::int32_t, std::int32_t) {},
    .set_parent_configure = [](wl_client*, wl_resource*, std::uint32_t) {},
};

void xdgWmBaseCreatePositioner(wl_client* client, wl_resource* resource, std::uint32_t id) {
  auto* server = serverFrom(resource);
  auto positioner = std::make_unique<WaylandServer::Impl::XdgPositioner>();
  positioner->server = server;
  wl_resource* positionerResource = wl_resource_create(client, &xdg_positioner_interface, 6, id);
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
  wl_resource_set_implementation(toplevelResource,
                                 &xdgToplevelImpl,
                                 raw,
                                 destroyResourceCallback<WaylandServer::Impl::XdgToplevel,
                                                         WaylandServer::Impl,
                                                         &WaylandServer::Impl::destroyXdgToplevel>);
  focusSurface(xdgSurface->server, xdgSurface->surface, monotonicMilliseconds());
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
  });

  surface->windowX = geometry.window.x;
  surface->windowY = geometry.window.y;
  setConfiguredFrameSize(surface, geometry.window.width, geometry.window.height);
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
  xdg_surface_send_configure(popup->xdgSurface->resource, popup->server->nextConfigureSerial_++);
}

void xdgPopupDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void xdgPopupGrab(wl_client*, wl_resource* resource, wl_resource*, std::uint32_t) {
  auto* popup = resourceData<WaylandServer::Impl::XdgPopup>(resource);
  if (!popup) return;
  popup->grabbed = true;
  std::fprintf(stderr,
               "flux-compositor: xdg_popup grab surface=%llu parent=%llu\n",
               static_cast<unsigned long long>(popup->xdgSurface && popup->xdgSurface->surface
                                                   ? popup->xdgSurface->surface->id
                                                   : 0),
               static_cast<unsigned long long>(popup->parentSurface ? popup->parentSurface->id : 0));
}

void xdgPopupReposition(wl_client*, wl_resource* resource, wl_resource* positionerResource, std::uint32_t token) {
  auto* popup = resourceData<WaylandServer::Impl::XdgPopup>(resource);
  auto* positioner = resourceData<WaylandServer::Impl::XdgPositioner>(positionerResource);
  if (!popup || !positioner || popup->dismissed) return;
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
  if (!xdgSurface || !xdgSurface->surface || !positioner ||
      positioner->width <= 0 || positioner->height <= 0 ||
      positioner->anchorRectWidth <= 0 || positioner->anchorRectHeight <= 0) {
    wl_resource_post_error(resource, XDG_WM_BASE_ERROR_INVALID_POSITIONER, "invalid xdg_popup positioner");
    return;
  }
  if (!surfaceHasNoRole(xdgSurface->surface)) {
    wl_resource_post_error(resource, XDG_WM_BASE_ERROR_ROLE, "wl_surface already has another role");
    return;
  }

  auto* parentXdgSurface = resourceData<WaylandServer::Impl::XdgSurface>(parentResource);
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
               "flux-compositor: xdg_popup created surface=%llu parent=%llu geometry=%d,%d %dx%d\n",
               static_cast<unsigned long long>(xdgSurface->surface->id),
               static_cast<unsigned long long>(raw->parentSurface ? raw->parentSurface->id : 0),
               raw->configuredX,
               raw->configuredY,
               raw->configuredWidth,
               raw->configuredHeight);
  sendPopupConfigure(raw);
}

void xdgSurfaceAckConfigure(wl_client*, wl_resource* resource, std::uint32_t) {
  auto* xdgSurface = resourceData<WaylandServer::Impl::XdgSurface>(resource);
  if (!xdgSurface) return;
  xdgSurface->configured = true;

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
    sendToplevelConfigure(xdgSurface->server, toplevel, nextWidth, nextHeight);
  }
}

struct xdg_surface_interface const xdgSurfaceImpl{
    .destroy = xdgSurfaceDestroy,
    .get_toplevel = xdgSurfaceGetToplevel,
    .get_popup = xdgSurfaceGetPopup,
    .set_window_geometry = [](wl_client*, wl_resource*, std::int32_t, std::int32_t, std::int32_t, std::int32_t) {},
    .ack_configure = xdgSurfaceAckConfigure,
};

void xdgToplevelDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void xdgToplevelSetTitle(wl_client*, wl_resource* resource, char const* title) {
  resourceData<WaylandServer::Impl::XdgToplevel>(resource)->title = title ? title : "";
}

void xdgToplevelSetAppId(wl_client*, wl_resource* resource, char const* appId) {
  resourceData<WaylandServer::Impl::XdgToplevel>(resource)->appId = appId ? appId : "";
}

void xdgToplevelResize(wl_client*, wl_resource* resource, wl_resource*, std::uint32_t, std::uint32_t edges) {
  auto* toplevel = resourceData<WaylandServer::Impl::XdgToplevel>(resource);
  if (!toplevel || !toplevel->xdgSurface || !toplevel->xdgSurface->surface) return;
  auto* server = toplevel->server;
  auto* surface = toplevel->xdgSurface->surface;
  std::int32_t const width = displayWidth(surface);
  std::int32_t const height = displayHeight(surface);
  if (edges == XDG_TOPLEVEL_RESIZE_EDGE_NONE || width <= 0 || height <= 0) return;
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
  if (serial == 0 ||
      serial != server->lastPointerButtonSerial_ ||
      server->lastPointerButtonSurface_ != surface) {
    return;
  }
  focusSurface(server, surface, monotonicMilliseconds());
  server->dragSurface_ = surface;
  server->dragOffsetX_ = server->pointerX_ - static_cast<float>(surface->windowX);
  server->dragOffsetY_ = server->pointerY_ - static_cast<float>(surface->windowY);
}

struct xdg_toplevel_interface const xdgToplevelImpl{
    .destroy = xdgToplevelDestroy,
    .set_parent = [](wl_client*, wl_resource*, wl_resource*) {},
    .set_title = xdgToplevelSetTitle,
    .set_app_id = xdgToplevelSetAppId,
    .show_window_menu = [](wl_client*, wl_resource*, wl_resource*, std::uint32_t, std::int32_t, std::int32_t) {},
    .move = xdgToplevelMove,
    .resize = xdgToplevelResize,
    .set_max_size = [](wl_client*, wl_resource*, std::int32_t, std::int32_t) {},
    .set_min_size = [](wl_client*, wl_resource*, std::int32_t, std::int32_t) {},
    .set_maximized = [](wl_client*, wl_resource*) {},
    .unset_maximized = [](wl_client*, wl_resource*) {},
    .set_fullscreen = [](wl_client*, wl_resource*, wl_resource*) {},
    .unset_fullscreen = [](wl_client*, wl_resource*) {},
    .set_minimized = [](wl_client*, wl_resource*) {},
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

} // namespace flux::compositor
