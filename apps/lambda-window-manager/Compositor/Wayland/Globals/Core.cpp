#include "Compositor/Wayland/Globals/Core.hpp"

#include "Compositor/Diagnostics/CrashLog.hpp"
#include "Compositor/Diagnostics/CpuTrace.hpp"
#include "Compositor/Wayland/Globals/PointerExtensions.hpp"
#include "Compositor/Wayland/ResourceTemplates.hpp"
#include "Compositor/Wayland/WaylandServerImpl.hpp"
#include "Compositor/Wayland/XdgPopupState.hpp"
#include "Compositor/Wayland/XdgSurfaceState.hpp"
#include "Compositor/Wayland/XdgToplevelState.hpp"
#include "Compositor/Window/WindowManagerInternal.hpp"
#include "Detail/ResizeTrace.hpp"
#include "presentation-time-server-protocol.h"
#include "viewporter-server-protocol.h"
#include "xdg-shell-server-protocol.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <utility>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

namespace lambda::compositor {
namespace {

void commitSurfacePendingState(WaylandServer::Impl::Surface* surface,
                               wl_resource* resource,
                               bool allowSynchronizedSubsurfaceCache);

void inertDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

using RegionRect = CommittedSurfaceSnapshot::RegionRect;

RegionRect normalizedRegionRect(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height) {
  if (width < 0) {
    x += width;
    width = -width;
  }
  if (height < 0) {
    y += height;
    height = -height;
  }
  return {.x = x, .y = y, .width = width, .height = height};
}

bool emptyRegionRect(RegionRect const& rect) {
  return rect.width <= 0 || rect.height <= 0;
}

void appendRegionRect(std::vector<RegionRect>& rects, RegionRect rect) {
  if (!emptyRegionRect(rect)) rects.push_back(rect);
}

std::vector<RegionRect> copyRegionRects(wl_resource* regionResource) {
  auto* region = resourceData<WaylandServer::Impl::Region>(regionResource);
  return region ? region->rects : std::vector<RegionRect>{};
}

bool regionsEqual(std::vector<RegionRect> const& a, std::vector<RegionRect> const& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i].x != b[i].x ||
        a[i].y != b[i].y ||
        a[i].width != b[i].width ||
        a[i].height != b[i].height) {
      return false;
    }
  }
  return true;
}

void subtractRegionRect(std::vector<RegionRect>& rects, RegionRect cut) {
  if (emptyRegionRect(cut)) return;

  std::vector<RegionRect> result;
  result.reserve(rects.size() + 4u);
  std::int64_t const cutLeft = cut.x;
  std::int64_t const cutTop = cut.y;
  std::int64_t const cutRight = cutLeft + cut.width;
  std::int64_t const cutBottom = cutTop + cut.height;

  for (RegionRect const& rect : rects) {
    std::int64_t const left = rect.x;
    std::int64_t const top = rect.y;
    std::int64_t const right = left + rect.width;
    std::int64_t const bottom = top + rect.height;
    std::int64_t const overlapLeft = std::max(left, cutLeft);
    std::int64_t const overlapTop = std::max(top, cutTop);
    std::int64_t const overlapRight = std::min(right, cutRight);
    std::int64_t const overlapBottom = std::min(bottom, cutBottom);

    if (overlapLeft >= overlapRight || overlapTop >= overlapBottom) {
      result.push_back(rect);
      continue;
    }

    appendRegionRect(result, RegionRect{rect.x, rect.y, rect.width,
                                        static_cast<std::int32_t>(overlapTop - top)});
    appendRegionRect(result, RegionRect{rect.x, static_cast<std::int32_t>(overlapBottom),
                                        rect.width, static_cast<std::int32_t>(bottom - overlapBottom)});
    appendRegionRect(result, RegionRect{rect.x, static_cast<std::int32_t>(overlapTop),
                                        static_cast<std::int32_t>(overlapLeft - left),
                                        static_cast<std::int32_t>(overlapBottom - overlapTop)});
    appendRegionRect(result, RegionRect{static_cast<std::int32_t>(overlapRight),
                                        static_cast<std::int32_t>(overlapTop),
                                        static_cast<std::int32_t>(right - overlapRight),
                                        static_cast<std::int32_t>(overlapBottom - overlapTop)});
  }

  rects = std::move(result);
}

void compositorCreateSurface(wl_client* client, wl_resource* resource, std::uint32_t id) {
  WaylandServer::Impl* server = serverFrom(resource);
  server->createSurface(client, wl_resource_get_version(resource), id);
}

void regionAdd(wl_client*, wl_resource* resource, std::int32_t x, std::int32_t y,
               std::int32_t width, std::int32_t height) {
  auto* region = resourceData<WaylandServer::Impl::Region>(resource);
  if (!region) return;
  appendRegionRect(region->rects, normalizedRegionRect(x, y, width, height));
}

void regionSubtract(wl_client*, wl_resource* resource, std::int32_t x, std::int32_t y,
                    std::int32_t width, std::int32_t height) {
  auto* region = resourceData<WaylandServer::Impl::Region>(resource);
  if (!region) return;
  subtractRegionRect(region->rects, normalizedRegionRect(x, y, width, height));
}

void compositorCreateRegion(wl_client* client, wl_resource* resource, std::uint32_t id) {
  WaylandServer::Impl* server = serverFrom(resource);
  static struct wl_region_interface const regionImpl{
      inertDestroy,
      regionAdd,
      regionSubtract,
  };
  auto region = std::make_unique<WaylandServer::Impl::Region>();
  region->server = server;
  wl_resource* regionResource = wl_resource_create(client, &wl_region_interface, 1, id);
  if (!regionResource) {
    wl_client_post_no_memory(client);
    return;
  }
  region->resource = regionResource;
  auto* raw = region.get();
  server->regions_.push_back(std::move(region));
  wl_resource_set_implementation(regionResource,
                                 &regionImpl,
                                 raw,
                                 destroyResourceCallback<WaylandServer::Impl::Region,
                                                         WaylandServer::Impl,
                                                         &WaylandServer::Impl::destroyRegion>);
}

struct wl_compositor_interface const compositorImpl{
    .create_surface = compositorCreateSurface,
    .create_region = compositorCreateRegion,
    .release = inertDestroy,
};

void subcompositorDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void subsurfaceDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void subsurfaceSetPosition(wl_client*, wl_resource* resource, std::int32_t x, std::int32_t y) {
  auto* subsurface = resourceData<WaylandServer::Impl::Subsurface>(resource);
  if (!subsurface) return;
  subsurface->pendingX = x;
  subsurface->pendingY = y;
}

void subsurfacePlaceAbove(wl_client*, wl_resource* resource, wl_resource* siblingResource) {
  auto* subsurface = resourceData<WaylandServer::Impl::Subsurface>(resource);
  auto* sibling = resourceData<WaylandServer::Impl::Surface>(siblingResource);
  (void)setSubsurfacePendingPlaceAbove(subsurface, sibling, resource);
}

void subsurfacePlaceBelow(wl_client*, wl_resource* resource, wl_resource* siblingResource) {
  auto* subsurface = resourceData<WaylandServer::Impl::Subsurface>(resource);
  auto* sibling = resourceData<WaylandServer::Impl::Surface>(siblingResource);
  (void)setSubsurfacePendingPlaceBelow(subsurface, sibling, resource);
}

void subsurfaceSetSync(wl_client*, wl_resource* resource) {
  auto* subsurface = resourceData<WaylandServer::Impl::Subsurface>(resource);
  if (!subsurface) return;
  subsurface->synchronized = true;
}

void subsurfaceSetDesync(wl_client*, wl_resource* resource) {
  auto* subsurface = resourceData<WaylandServer::Impl::Subsurface>(resource);
  if (!subsurface) return;
  subsurface->synchronized = false;
  if (!subsurfaceIsEffectivelySynchronized(subsurface) &&
      subsurface->surface &&
      surfaceHasCachedSubsurfaceCommit(subsurface->surface)) {
    auto livePending = takeSurfacePendingCommit(subsurface->surface);
    if (restoreCachedSubsurfaceCommit(subsurface->surface)) {
      commitSurfacePendingState(subsurface->surface, subsurface->surface->resource, false);
    }
    restoreSurfacePendingCommit(subsurface->surface, std::move(livePending));
  }
}

struct wl_subsurface_interface const subsurfaceImpl{
    .destroy = subsurfaceDestroy,
    .set_position = subsurfaceSetPosition,
    .place_above = subsurfacePlaceAbove,
    .place_below = subsurfacePlaceBelow,
    .set_sync = subsurfaceSetSync,
    .set_desync = subsurfaceSetDesync,
};

void subcompositorGetSubsurface(wl_client* client,
                                wl_resource* resource,
                                std::uint32_t id,
                                wl_resource* surfaceResource,
                                wl_resource* parentResource) {
  auto* server = serverFrom(resource);
  auto* surface = resourceData<WaylandServer::Impl::Surface>(surfaceResource);
  auto* parent = resourceData<WaylandServer::Impl::Surface>(parentResource);
  if (!server || !surface || !parent || surface == parent) {
    wl_resource_post_error(resource, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE, "invalid subsurface or parent surface");
    return;
  }
  if (!surfaceHasNoRole(surface)) {
    wl_resource_post_error(resource, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE, "wl_surface already has another role");
    return;
  }
  for (auto* ancestor = parent; surfaceIsSubsurface(ancestor);) {
    auto* role = ancestor->subsurfaceRole;
    if (!role || !role->parent) break;
    ancestor = role->parent;
    if (ancestor == surface) {
      wl_resource_post_error(resource, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE, "wl_surface cannot be an ancestor of its parent");
      return;
    }
  }

  auto subsurface = std::make_unique<WaylandServer::Impl::Subsurface>();
  subsurface->server = server;
  subsurface->surface = surface;
  subsurface->parent = parent;
  subsurface->order = server->nextSubsurfaceOrder_++;
  subsurface->pendingOrder = subsurface->order;
  wl_resource* subsurfaceResource = wl_resource_create(client, &wl_subsurface_interface, 1, id);
  if (!subsurfaceResource) {
    wl_client_post_no_memory(client);
    return;
  }
  subsurface->resource = subsurfaceResource;
  auto* raw = subsurface.get();
  surface->role = SurfaceRole::Subsurface;
  surface->subsurfaceRole = raw;
  server->subsurfaces_.push_back(std::move(subsurface));
  wl_resource_set_implementation(subsurfaceResource,
                                 &subsurfaceImpl,
                                 raw,
                                 destroyResourceCallback<WaylandServer::Impl::Subsurface,
                                                         WaylandServer::Impl,
                                                         &WaylandServer::Impl::destroySubsurface>);
}

struct wl_subcompositor_interface const subcompositorImpl{
    .destroy = subcompositorDestroy,
    .get_subsurface = subcompositorGetSubsurface,
};

void surfaceDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void surfaceAttach(wl_client*, wl_resource* resource, wl_resource* buffer, std::int32_t x, std::int32_t y) {
  auto* surface = resourceData<WaylandServer::Impl::Surface>(resource);
  if (wl_resource_get_version(resource) >= WL_SURFACE_OFFSET_SINCE_VERSION && (x != 0 || y != 0)) {
    wl_resource_post_error(resource, WL_SURFACE_ERROR_INVALID_OFFSET,
                           "wl_surface.attach x/y must be 0 for wl_surface version 5 or newer");
    return;
  }
  surface->pendingBufferState.buffer = buffer;
  surface->pendingBufferState.bufferAttached = true;
  if (x != 0 || y != 0) {
    surface->pendingBufferState.offsetX = x;
    surface->pendingBufferState.offsetY = y;
    surface->pendingBufferState.offsetSet = true;
  }
}

void surfaceFrame(wl_client* client, wl_resource* resource, std::uint32_t id) {
  auto* surface = resourceData<WaylandServer::Impl::Surface>(resource);
  wl_resource* callback = wl_resource_create(client, &wl_callback_interface, 1, id);
  if (!callback) {
    wl_client_post_no_memory(client);
    return;
  }
  surface->pendingFrameCallbacks.push_back(callback);
}

void surfaceDamage(wl_client*, wl_resource* resource, std::int32_t x, std::int32_t y,
                   std::int32_t width, std::int32_t height) {
  auto* surface = resourceData<WaylandServer::Impl::Surface>(resource);
  if (!surface) return;
  appendRegionRect(surface->pendingDamageState.surfaceRects, normalizedRegionRect(x, y, width, height));
}

void surfaceSetOpaqueRegion(wl_client*, wl_resource* resource, wl_resource* regionResource) {
  auto* surface = resourceData<WaylandServer::Impl::Surface>(resource);
  if (!surface) return;
  surface->pendingRegionState.opaqueRegionRects = copyRegionRects(regionResource);
  surface->pendingRegionState.opaqueRegionSet = true;
}

void surfaceSetInputRegion(wl_client*, wl_resource* resource, wl_resource* regionResource) {
  auto* surface = resourceData<WaylandServer::Impl::Surface>(resource);
  if (!surface) return;
  surface->pendingRegionState.inputRegionInfinite = regionResource == nullptr;
  surface->pendingRegionState.inputRegionRects = copyRegionRects(regionResource);
  surface->pendingRegionState.inputRegionSet = true;
}

WaylandServer::Impl::ShmBuffer* shmBufferFor(WaylandServer::Impl* server, wl_resource* resource) {
  auto found = std::find_if(server->shmBuffers_.begin(), server->shmBuffers_.end(),
                            [resource](auto const& buffer) { return buffer->resource == resource; });
  return found == server->shmBuffers_.end() ? nullptr : found->get();
}

WaylandServer::Impl::DmabufBuffer* dmabufBufferFor(WaylandServer::Impl* server, wl_resource* resource) {
  auto found = std::find_if(server->dmabufBuffers_.begin(), server->dmabufBuffers_.end(),
                            [resource](auto const& buffer) { return buffer->resource == resource; });
  return found == server->dmabufBuffers_.end() ? nullptr : found->get();
}

bool copyShmBufferToPixels(WaylandServer::Impl::ShmBuffer const& buffer,
                           std::vector<std::uint8_t>& out,
                           Image::PixelFormat& pixelFormat,
                           bool& fullyOpaque) {
  if (!buffer.data || buffer.width <= 0 || buffer.height <= 0 || buffer.stride <= 0) {
    return false;
  }
  if (buffer.format != WL_SHM_FORMAT_ARGB8888 && buffer.format != WL_SHM_FORMAT_XRGB8888) {
    return false;
  }

  std::size_t const rowBytes = static_cast<std::size_t>(buffer.width) * 4u;
  if (buffer.offset < 0 || static_cast<std::size_t>(buffer.stride) < rowBytes) {
    return false;
  }
  std::size_t const lastRow = static_cast<std::size_t>(buffer.height - 1) * static_cast<std::size_t>(buffer.stride);
  std::size_t const end = static_cast<std::size_t>(buffer.offset) + lastRow + rowBytes;
  if (end > static_cast<std::size_t>(buffer.size)) {
    return false;
  }

  out.resize(static_cast<std::size_t>(buffer.width) * static_cast<std::size_t>(buffer.height) * 4u);
  pixelFormat = Image::PixelFormat::Bgra8888;
  fullyOpaque = buffer.format == WL_SHM_FORMAT_XRGB8888;
  auto const* base = static_cast<std::uint8_t const*>(buffer.data) + buffer.offset;
  for (std::int32_t y = 0; y < buffer.height; ++y) {
    auto const* src = base + static_cast<std::size_t>(y) * static_cast<std::size_t>(buffer.stride);
    auto* dst = out.data() + static_cast<std::size_t>(y) * rowBytes;
    // wl_shm ARGB/XRGB are little-endian words, so memory order is B, G, R, A/X.
    std::memcpy(dst, src, rowBytes);
  }
  return true;
}

bool mapTightShmBufferPixels(WaylandServer::Impl::ShmBuffer const& buffer,
                             std::uint8_t const*& pixels,
                             std::size_t& pixelBytes,
                             Image::PixelFormat& pixelFormat,
                             bool& fullyOpaque) {
  pixels = nullptr;
  pixelBytes = 0;
  if (!buffer.data || buffer.width <= 0 || buffer.height <= 0 || buffer.stride <= 0) return false;
  if (buffer.format != WL_SHM_FORMAT_ARGB8888 && buffer.format != WL_SHM_FORMAT_XRGB8888) return false;
  std::size_t const rowBytes = static_cast<std::size_t>(buffer.width) * 4u;
  if (buffer.offset < 0 || static_cast<std::size_t>(buffer.stride) != rowBytes) return false;
  std::size_t const size = rowBytes * static_cast<std::size_t>(buffer.height);
  std::size_t const end = static_cast<std::size_t>(buffer.offset) + size;
  if (end > static_cast<std::size_t>(buffer.size)) return false;

  pixels = static_cast<std::uint8_t const*>(buffer.data) + buffer.offset;
  pixelBytes = size;
  pixelFormat = Image::PixelFormat::Bgra8888;
  fullyOpaque = buffer.format == WL_SHM_FORMAT_XRGB8888;
  return true;
}

bool isIntegerSize(float value) {
  return std::floor(value) == value;
}

std::int32_t committedDisplayWidth(WaylandServer::Impl::Surface const* surface) {
  return surfaceCommittedDisplayWidth(surface);
}

std::int32_t committedDisplayHeight(WaylandServer::Impl::Surface const* surface) {
  return surfaceCommittedDisplayHeight(surface);
}

std::int32_t committedWindowDisplayWidth(WaylandServer::Impl::Surface const* surface) {
  return surface && surface->xdgRoleState.windowGeometrySet
             ? surface->xdgRoleState.windowGeometry.width
             : committedDisplayWidth(surface);
}

std::int32_t committedWindowDisplayHeight(WaylandServer::Impl::Surface const* surface) {
  return surface && surface->xdgRoleState.windowGeometrySet
             ? surface->xdgRoleState.windowGeometry.height
             : committedDisplayHeight(surface);
}

bool xdgSurfaceCommittedConfigureSerial(WaylandServer::Impl::Surface const* surface, std::uint32_t serial) {
  if (!surface || !surface->server || serial == 0) return false;
  return std::any_of(surface->server->xdgSurfaces_.begin(),
                     surface->server->xdgSurfaces_.end(),
                     [surface, serial](auto const& xdgSurface) {
                       return xdgSurface &&
                              xdgSurface->surface == surface &&
                              xdgSurface->currentConfigure &&
                              xdgSurface->currentConfigure->serial >= serial;
                     });
}

bool clearMatchedConfigureCommit(WaylandServer::Impl::Surface* surface) {
  if (!surface || !surface->resizeConfigureInFlight || !surface->resizeConfigureAcked) return false;
  if (!xdgSurfaceCommittedConfigureSerial(surface, surface->resizeConfigureSerial)) return false;
  std::int32_t const committedWidth = committedWindowDisplayWidth(surface);
  std::int32_t const committedHeight = committedWindowDisplayHeight(surface);
  if (committedWidth <= 0 || committedHeight <= 0) {
    return false;
  }
  bool const exactSizeMatch = committedWidth == surface->resizeConfigureWidth &&
                              committedHeight == surface->resizeConfigureHeight;
  bool const hasPending = surface->pendingResizeConfigure;
  std::int32_t const pendingX = surface->pendingResizeConfigureX;
  std::int32_t const pendingY = surface->pendingResizeConfigureY;
  std::int32_t const pendingWidth = surface->pendingResizeConfigureWidth;
  std::int32_t const pendingHeight = surface->pendingResizeConfigureHeight;
  std::int32_t const committedX = surface->resizeConfigureX;
  std::int32_t const committedY = surface->resizeConfigureY;
  std::int32_t const requestedWidth = surface->resizeConfigureWidth;
  std::int32_t const requestedHeight = surface->resizeConfigureHeight;
  surface->windowX = committedX;
  surface->windowY = committedY;
  setConfiguredFrameSize(surface, committedWidth, committedHeight);
  surface->server->noteResizePacingActivity();
  surface->resizeConfigureInFlight = false;
  surface->resizeConfigureAcked = false;
  surface->resizeConfigureSerial = 0;
  surface->resizeConfigureX = 0;
  surface->resizeConfigureY = 0;
  surface->resizeConfigureWidth = 0;
  surface->resizeConfigureHeight = 0;
  surface->awaitingConfigureCommit = false;
  surface->awaitingConfigureWidth = 0;
  surface->awaitingConfigureHeight = 0;
  surface->pendingResizeConfigure = false;
  surface->pendingResizeConfigureX = 0;
  surface->pendingResizeConfigureY = 0;
  surface->pendingResizeConfigureWidth = 0;
  surface->pendingResizeConfigureHeight = 0;
  if (hasPending && pendingWidth > 0 && pendingHeight > 0 &&
      (pendingX != committedX || pendingY != committedY ||
       pendingWidth != committedWidth || pendingHeight != committedHeight)) {
    if (requestToplevelResizeConfigure(surface->server, surface, pendingX, pendingY, pendingWidth, pendingHeight)) {
      surface->server->flushClients();
    }
  }
  LAMBDA_RESIZE_TRACE("compositor",
                              "resize-configure-commit surface=%llu committed=%d,%d %dx%d "
                              "requested=%dx%d exact=%d pending=%d %d,%d %dx%d\n",
                              static_cast<unsigned long long>(surface->id),
                              committedX,
                              committedY,
                              committedWidth,
                              committedHeight,
                              requestedWidth,
                              requestedHeight,
                              exactSizeMatch ? 1 : 0,
                              hasPending ? 1 : 0,
                              pendingX,
                              pendingY,
                              pendingWidth,
                              pendingHeight);
  return true;
}

void traceConfigureCommitLag(char const* event, WaylandServer::Impl::Surface const* surface) {
  if (!surface || !lambda::detail::resizeTraceEnabled() || surface->lastConfigureSerial == 0) return;
  std::int32_t const committedWidth = committedDisplayWidth(surface);
  std::int32_t const committedHeight = committedDisplayHeight(surface);
  if (!surface->awaitingConfigureCommit && committedWidth == surface->lastConfigureWidth &&
      committedHeight == surface->lastConfigureHeight) {
    return;
  }
  std::uint64_t const now = lambda::detail::resizeTraceTimestampNanoseconds();
  double const lagMs = surface->lastConfigureSentNsec > 0
                           ? static_cast<double>(now - surface->lastConfigureSentNsec) / 1'000'000.0
                           : 0.0;
  LAMBDA_RESIZE_TRACE("compositor",
                            "%s surface=%llu configureSerial=%u configure=%dx%d committed=%dx%d "
                            "frame=%dx%d buffer=%dx%d lag=%.3fms delta=%dx%d serial=%llu\n",
                            event,
                            static_cast<unsigned long long>(surface->id),
                            surface->lastConfigureSerial,
                            surface->lastConfigureWidth,
                            surface->lastConfigureHeight,
                            committedWidth,
                            committedHeight,
                            surface->frameWidth,
                            surface->frameHeight,
                            surface->width,
                            surface->height,
                            lagMs,
                            committedWidth - surface->lastConfigureWidth,
                            committedHeight - surface->lastConfigureHeight,
                            static_cast<unsigned long long>(surface->serial));
}

bool applyViewportState(WaylandServer::Impl::Surface* surface) {
  surface->viewportState.sourceSet = surface->pendingViewportState.sourceSet;
  surface->viewportState.sourceX = surface->pendingViewportState.sourceX;
  surface->viewportState.sourceY = surface->pendingViewportState.sourceY;
  surface->viewportState.sourceWidth = surface->pendingViewportState.sourceWidth;
  surface->viewportState.sourceHeight = surface->pendingViewportState.sourceHeight;
  surface->viewportState.destinationSet = surface->pendingViewportState.destinationSet;
  surface->viewportState.destinationWidth = surface->pendingViewportState.destinationWidth;
  surface->viewportState.destinationHeight = surface->pendingViewportState.destinationHeight;

  if (surface->width <= 0 || surface->height <= 0) {
    setConfiguredFrameSize(surface, 0, 0);
    return true;
  }

  if (surface->viewportState.sourceSet) {
    if (surface->viewportState.sourceX < 0.f || surface->viewportState.sourceY < 0.f ||
        surface->viewportState.sourceWidth <= 0.f || surface->viewportState.sourceHeight <= 0.f) {
      if (surface->viewport && surface->viewport->resource) {
        wl_resource_post_error(surface->viewport->resource,
                               WP_VIEWPORT_ERROR_BAD_VALUE,
                               "invalid viewport source rectangle");
      }
      return false;
    }
    if (surface->viewportState.sourceX + surface->viewportState.sourceWidth > static_cast<float>(surface->width) ||
        surface->viewportState.sourceY + surface->viewportState.sourceHeight > static_cast<float>(surface->height)) {
      if (surface->viewport && surface->viewport->resource) {
        wl_resource_post_error(surface->viewport->resource,
                               WP_VIEWPORT_ERROR_OUT_OF_BUFFER,
                               "viewport source rectangle exceeds buffer");
      }
      return false;
    }
  }

  bool const activeResizeSizing = surface->server->resizeSurface_ == surface ||
                                  surface->geometryAnimationActive ||
                                  surface->resizeConfigureInFlight ||
                                  surface->pendingResizeConfigure;
  bool const matchedConfigure = surface->awaitingConfigureCommit &&
                                committedWindowDisplayWidth(surface) == surface->awaitingConfigureWidth &&
                                committedWindowDisplayHeight(surface) == surface->awaitingConfigureHeight;
  if (!activeResizeSizing && matchedConfigure) {
    std::int32_t const committedWidth = committedWindowDisplayWidth(surface);
    std::int32_t const committedHeight = committedWindowDisplayHeight(surface);
    if (committedWidth > 0 && committedHeight > 0) {
      setConfiguredFrameSize(surface, committedWidth, committedHeight);
    }
    return true;
  }
  if (activeResizeSizing) {
    if (surface->frameWidth <= 0 || surface->frameHeight <= 0) {
      if (surface->geometryAnimationTargetWidth > 0 &&
          surface->geometryAnimationTargetHeight > 0) {
        setConfiguredFrameSize(surface,
                               surface->geometryAnimationTargetWidth,
                               surface->geometryAnimationTargetHeight);
      }
    }
    return true;
  }
  if (surface->awaitingConfigureCommit) {
    surface->awaitingConfigureCommit = false;
    surface->awaitingConfigureWidth = 0;
    surface->awaitingConfigureHeight = 0;
    surface->resizeConfigureInFlight = false;
    surface->resizeConfigureAcked = false;
    surface->resizeConfigureSerial = 0;
    surface->resizeConfigureX = 0;
    surface->resizeConfigureY = 0;
    surface->resizeConfigureWidth = 0;
    surface->resizeConfigureHeight = 0;
    surface->pendingResizeConfigure = false;
    surface->pendingResizeConfigureX = 0;
    surface->pendingResizeConfigureY = 0;
    surface->pendingResizeConfigureWidth = 0;
    surface->pendingResizeConfigureHeight = 0;
  }
  if ((surface->snapped || surface->maximized) &&
      surface->frameWidth > 0 &&
      surface->frameHeight > 0) {
    return true;
  }

  if (surface->xdgRoleState.windowGeometrySet) {
    surface->frameWidth = surface->xdgRoleState.windowGeometry.width;
    surface->frameHeight = surface->xdgRoleState.windowGeometry.height;
  } else if (surface->viewportState.destinationSet) {
    surface->frameWidth = surface->viewportState.destinationWidth;
    surface->frameHeight = surface->viewportState.destinationHeight;
  } else if (surface->viewportState.sourceSet) {
    if (!isIntegerSize(surface->viewportState.sourceWidth) || !isIntegerSize(surface->viewportState.sourceHeight)) {
      if (surface->viewport && surface->viewport->resource) {
        wl_resource_post_error(surface->viewport->resource,
                               WP_VIEWPORT_ERROR_BAD_SIZE,
                               "viewport source size must be integer without a destination size");
      }
      return false;
    }
    surface->frameWidth = static_cast<std::int32_t>(surface->viewportState.sourceWidth);
    surface->frameHeight = static_cast<std::int32_t>(surface->viewportState.sourceHeight);
  } else {
    surface->frameWidth = committedDisplayWidth(surface);
    surface->frameHeight = committedDisplayHeight(surface);
  }

  return true;
}

bool pendingViewportSourceFitsCurrentBuffer(WaylandServer::Impl::Surface const* surface) {
  if (!surface || !surface->pendingViewportState.sourceSet) return true;
  return surface->pendingViewportState.sourceX >= 0.f &&
         surface->pendingViewportState.sourceY >= 0.f &&
         surface->pendingViewportState.sourceWidth > 0.f &&
         surface->pendingViewportState.sourceHeight > 0.f &&
         surface->pendingViewportState.sourceX + surface->pendingViewportState.sourceWidth <=
             static_cast<float>(surface->width) &&
         surface->pendingViewportState.sourceY + surface->pendingViewportState.sourceHeight <=
             static_cast<float>(surface->height);
}

bool applyBackgroundBlurState(WaylandServer::Impl::Surface* surface) {
  if (!surface) return false;
  bool changed = false;
  if (surface->backgroundBlurPending) {
    surface->backgroundBlurRects = surface->pendingBackgroundBlurRects;
    surface->backgroundBlurPending = false;
    changed = true;
  }
  if (surface->backgroundEffectStatePending) {
    surface->backgroundEffectState = surface->pendingBackgroundEffectState;
    surface->backgroundEffectStatePending = false;
    changed = true;
  }
  return changed;
}

bool applyXdgProtocolState(WaylandServer::Impl::Surface* surface) {
  if (!surface || !surface->server) return false;
  bool renderStateChanged = false;
  for (auto const& xdgSurface : surface->server->xdgSurfaces_) {
    if (!xdgSurface || xdgSurface->surface != surface || !xdgSurface->pendingWindowGeometrySet) continue;
    if (!xdgSurface->windowGeometrySet ||
        xdgSurface->windowGeometryX != xdgSurface->pendingWindowGeometryX ||
        xdgSurface->windowGeometryY != xdgSurface->pendingWindowGeometryY ||
        xdgSurface->windowGeometryWidth != xdgSurface->pendingWindowGeometryWidth ||
        xdgSurface->windowGeometryHeight != xdgSurface->pendingWindowGeometryHeight) {
      renderStateChanged = true;
    }
    xdgSurface->windowGeometryX = xdgSurface->pendingWindowGeometryX;
    xdgSurface->windowGeometryY = xdgSurface->pendingWindowGeometryY;
    xdgSurface->windowGeometryWidth = xdgSurface->pendingWindowGeometryWidth;
    xdgSurface->windowGeometryHeight = xdgSurface->pendingWindowGeometryHeight;
    xdgSurface->windowGeometrySet = true;
    xdgSurface->pendingWindowGeometrySet = false;
    surface->xdgRoleState.windowGeometrySet = true;
    surface->xdgRoleState.windowGeometry = WindowGeometry{
        .x = xdgSurface->windowGeometryX,
        .y = xdgSurface->windowGeometryY,
        .width = xdgSurface->windowGeometryWidth,
        .height = xdgSurface->windowGeometryHeight,
    };
  }

  if (auto* toplevel = toplevelForSurface(surface->server, surface)) {
    if (toplevel->pendingMinSizeSet) {
      toplevel->minWidth = toplevel->pendingMinWidth;
      toplevel->minHeight = toplevel->pendingMinHeight;
      toplevel->pendingMinSizeSet = false;
    }
    if (toplevel->pendingMaxSizeSet) {
      toplevel->maxWidth = toplevel->pendingMaxWidth;
      toplevel->maxHeight = toplevel->pendingMaxHeight;
      toplevel->pendingMaxSizeSet = false;
    }
  }

  return renderStateChanged;
}

bool validateXdgToplevelPendingSizeHints(WaylandServer::Impl::Surface* surface, wl_resource* fallback) {
  auto* toplevel = surface && surface->server ? toplevelForSurface(surface->server, surface) : nullptr;
  if (!toplevel || wm::toplevelPendingSizeHintsValid(toplevel)) return true;
  wl_resource_post_error(toplevel->resource ? toplevel->resource : fallback,
                         XDG_TOPLEVEL_ERROR_INVALID_SIZE,
                         "client provided an invalid min or max size");
  return false;
}

void markXdgToplevelMapped(WaylandServer::Impl::Surface* surface) {
  auto* toplevel = surface && surface->server ? toplevelForSurface(surface->server, surface) : nullptr;
  if (!toplevel || toplevel->mapped) return;
  toplevel->mapped = true;
  surface->server->notifyShellStateChanged();
}

void markXdgPopupCommitted(WaylandServer::Impl::Surface* surface) {
  auto* popup = surface && surface->server ? wm::popupForSurface(surface->server, surface) : nullptr;
  if (!popup) return;
  popup->committed = true;
}

void markXdgPopupMapped(WaylandServer::Impl::Surface* surface) {
  auto* popup = surface && surface->server ? wm::popupForSurface(surface->server, surface) : nullptr;
  if (!popup) return;
  popup->committed = true;
  popup->mapped = true;
}

void resetXdgPopupForUnmap(WaylandServer::Impl::Surface* surface) {
  auto* popup = surface && surface->server ? wm::popupForSurface(surface->server, surface) : nullptr;
  if (!popup) return;
  popup->mapped = false;
  if (xdgPopupGrabContains(surface->server->popupGrab_, popup)) {
    releasePopupGrab(surface->server, popup, 0);
  }
}

void resetXdgToplevelForUnmap(WaylandServer::Impl::Surface* surface) {
  auto* toplevel = surface && surface->server ? toplevelForSurface(surface->server, surface) : nullptr;
  if (!toplevel) return;

  bool changed = false;
  WaylandServer::Impl::XdgToplevel* replacementParent = xdgToplevelRetainedParent(toplevel->parent);
  for (auto& child : surface->server->toplevels_) {
    if (child && child.get() != toplevel && child->parent == toplevel) {
      child->parent = replacementParent;
      changed = true;
    }
  }
  changed = resetXdgToplevelClientStateForUnmap(toplevel) || changed;
  if (changed) surface->server->notifyShellStateChanged();
}

bool applyXdgConfigureState(WaylandServer::Impl::Surface* surface) {
  if (!surface || !surface->server) return false;
  for (auto const& xdgSurface : surface->server->xdgSurfaces_) {
    if (!xdgSurface || xdgSurface->surface != surface || !xdgSurface->pendingConfigure) continue;
    xdgSurface->currentConfigure = xdgSurface->pendingConfigure;
    xdgSurface->pendingConfigure.reset();
    LAMBDA_RESIZE_TRACE("compositor",
                        "configure-commit-state surface=%llu serial=%u role=%u configure=%dx%d "
                        "window=%d %d,%d %dx%d\n",
                        static_cast<unsigned long long>(surface->id),
                        xdgSurface->currentConfigure->serial,
                        static_cast<unsigned int>(xdgSurface->currentConfigure->role),
                        xdgSurface->currentConfigure->width,
                        xdgSurface->currentConfigure->height,
                        xdgSurface->currentConfigure->hasWindowGeometry ? 1 : 0,
                        xdgSurface->currentConfigure->windowX,
                        xdgSurface->currentConfigure->windowY,
                        xdgSurface->currentConfigure->windowWidth,
                        xdgSurface->currentConfigure->windowHeight);
    return true;
  }
  return false;
}

bool surfaceHasPendingDamage(WaylandServer::Impl::Surface const* surface) {
  return surface &&
         (!surface->pendingDamageState.surfaceRects.empty() || !surface->pendingDamageState.bufferRects.empty());
}

void clearPendingDamage(WaylandServer::Impl::Surface* surface) {
  if (!surface) return;
  surface->pendingDamageState.surfaceRects.clear();
  surface->pendingDamageState.bufferRects.clear();
}

RegionRect clippedBufferRect(RegionRect rect, std::int32_t width, std::int32_t height) {
  std::int64_t const left = std::clamp<std::int64_t>(rect.x, 0, width);
  std::int64_t const top = std::clamp<std::int64_t>(rect.y, 0, height);
  std::int64_t const right = std::clamp<std::int64_t>(static_cast<std::int64_t>(rect.x) + rect.width, 0, width);
  std::int64_t const bottom = std::clamp<std::int64_t>(static_cast<std::int64_t>(rect.y) + rect.height, 0, height);
  return RegionRect{
      .x = static_cast<std::int32_t>(left),
      .y = static_cast<std::int32_t>(top),
      .width = static_cast<std::int32_t>(std::max<std::int64_t>(0, right - left)),
      .height = static_cast<std::int32_t>(std::max<std::int64_t>(0, bottom - top)),
  };
}

void appendCommittedBufferDamage(WaylandServer::Impl::Surface* surface) {
  if (!surface || surface->width <= 0 || surface->height <= 0) return;
  if (surface->pendingDamageState.surfaceRects.empty() && surface->pendingDamageState.bufferRects.empty()) return;

  constexpr std::size_t kMaxCommittedDamageRects = 32;
  auto appendFullBufferDamage = [&] {
    surface->damageState.bufferRects.clear();
    appendRegionRect(surface->damageState.bufferRects,
                     RegionRect{0, 0, surface->width, surface->height});
  };

  // wl_surface.damage is in surface coordinates and can interact with scale,
  // transform, and viewport state. Preserve correctness by treating it as full
  // buffer damage; wl_surface.damage_buffer carries exact buffer-space rects.
  if (!surface->pendingDamageState.surfaceRects.empty()) {
    appendFullBufferDamage();
    return;
  }

  for (RegionRect const& rect : surface->pendingDamageState.bufferRects) {
    appendRegionRect(surface->damageState.bufferRects,
                     clippedBufferRect(rect, surface->width, surface->height));
    if (surface->damageState.bufferRects.size() > kMaxCommittedDamageRects) {
      appendFullBufferDamage();
      return;
    }
  }
}

void queueBufferRelease(WaylandServer::Impl::Surface* surface, wl_resource* buffer) {
  if (!surface || !buffer) return;
  if (std::find(surface->pendingBufferReleases.begin(), surface->pendingBufferReleases.end(), buffer) ==
      surface->pendingBufferReleases.end()) {
    surface->pendingBufferReleases.push_back(buffer);
  }
}

bool applySurfaceProtocolState(WaylandServer::Impl::Surface* surface,
                               bool hasBufferAttach,
                               bool& inputRegionChanged) {
  if (!surface) return false;
  bool renderStateChanged = false;
  inputRegionChanged = false;

  if (surface->pendingBufferState.scaleSet) {
    if (surface->bufferState.scale != surface->pendingBufferState.scale) {
      surface->bufferState.scale = surface->pendingBufferState.scale;
      renderStateChanged = true;
    }
    surface->pendingBufferState.scaleSet = false;
  }

  if (surface->pendingBufferState.transformSet) {
    if (surface->bufferState.transform != surface->pendingBufferState.transform) {
      surface->bufferState.transform = surface->pendingBufferState.transform;
      renderStateChanged = true;
    }
    surface->pendingBufferState.transformSet = false;
  }

  if (surface->pendingBufferState.offsetSet) {
    if (surface->bufferState.offsetX != surface->pendingBufferState.offsetX ||
        surface->bufferState.offsetY != surface->pendingBufferState.offsetY) {
      surface->bufferState.offsetX = surface->pendingBufferState.offsetX;
      surface->bufferState.offsetY = surface->pendingBufferState.offsetY;
      surface->x = surface->bufferState.offsetX;
      surface->y = surface->bufferState.offsetY;
      renderStateChanged = true;
      if (hasBufferAttach && surfaceIsTopLevelRenderable(surface)) {
        surface->windowX += surface->bufferState.offsetX;
        surface->windowY += surface->bufferState.offsetY;
      }
    }
    surface->pendingBufferState.offsetX = 0;
    surface->pendingBufferState.offsetY = 0;
    surface->pendingBufferState.offsetSet = false;
  }

  if (surface->pendingRegionState.opaqueRegionSet) {
    if (!regionsEqual(surface->regionState.opaqueRegionRects, surface->pendingRegionState.opaqueRegionRects)) {
      surface->regionState.opaqueRegionRects = surface->pendingRegionState.opaqueRegionRects;
      renderStateChanged = true;
    }
    surface->pendingRegionState.opaqueRegionSet = false;
  }

  if (surface->pendingRegionState.inputRegionSet) {
    if (surface->regionState.inputRegionInfinite != surface->pendingRegionState.inputRegionInfinite ||
        !regionsEqual(surface->regionState.inputRegionRects, surface->pendingRegionState.inputRegionRects)) {
      surface->regionState.inputRegionInfinite = surface->pendingRegionState.inputRegionInfinite;
      surface->regionState.inputRegionRects = surface->pendingRegionState.inputRegionRects;
      inputRegionChanged = true;
    }
    surface->pendingRegionState.inputRegionSet = false;
  }

  return renderStateChanged;
}

bool bufferSizeValidForScale(WaylandServer::Impl::Surface const* surface) {
  if (!surface || surface->width <= 0 || surface->height <= 0) return true;
  std::int32_t const scale = std::max(1, surface->bufferState.scale);
  return surfaceTransformedBufferWidth(surface) % scale == 0 &&
         surfaceTransformedBufferHeight(surface) % scale == 0;
}

bool refreshCurrentShmBuffer(WaylandServer::Impl::Surface* surface,
                             WaylandServer::Impl::ShmBuffer const& shmBuffer) {
  std::uint8_t const* shmPixels = nullptr;
  std::size_t shmPixelBytes = 0;
  Image::PixelFormat mappedPixelFormat = Image::PixelFormat::Rgba8888;
  bool mappedFullyOpaque = false;
  if (mapTightShmBufferPixels(shmBuffer, shmPixels, shmPixelBytes, mappedPixelFormat, mappedFullyOpaque)) {
    surface->rgbaPixels.reset();
    surface->shmPixels = shmPixels;
    surface->shmPixelBytes = shmPixelBytes;
    surface->pixelFormat = mappedPixelFormat;
    surface->rgbaFullyOpaque = mappedFullyOpaque;
    surface->width = shmBuffer.width;
    surface->height = shmBuffer.height;
    surface->dmabufBuffer = nullptr;
    return true;
  }

  std::vector<std::uint8_t> pixels;
  Image::PixelFormat pixelFormat = Image::PixelFormat::Rgba8888;
  bool fullyOpaque = false;
  auto const copyStart = diagnostics::cpuTraceNow();
  if (!copyShmBufferToPixels(shmBuffer, pixels, pixelFormat, fullyOpaque)) {
    return false;
  }
  diagnostics::recordShmCopy(pixels.size(), diagnostics::cpuTraceElapsedMilliseconds(copyStart));
  surface->rgbaPixels = std::make_shared<std::vector<std::uint8_t> const>(std::move(pixels));
  surface->shmPixels = nullptr;
  surface->shmPixelBytes = 0;
  surface->pixelFormat = pixelFormat;
  surface->rgbaFullyOpaque = fullyOpaque;
  surface->width = shmBuffer.width;
  surface->height = shmBuffer.height;
  surface->dmabufBuffer = nullptr;
  return true;
}

bool pendingViewportStateChanged(WaylandServer::Impl::Surface const* surface) {
  if (!surface) return false;
  return surface->pendingViewportState.sourceSet != surface->viewportState.sourceSet ||
         surface->pendingViewportState.sourceX != surface->viewportState.sourceX ||
         surface->pendingViewportState.sourceY != surface->viewportState.sourceY ||
         surface->pendingViewportState.sourceWidth != surface->viewportState.sourceWidth ||
         surface->pendingViewportState.sourceHeight != surface->viewportState.sourceHeight ||
         surface->pendingViewportState.destinationSet != surface->viewportState.destinationSet ||
         surface->pendingViewportState.destinationWidth != surface->viewportState.destinationWidth ||
         surface->pendingViewportState.destinationHeight != surface->viewportState.destinationHeight;
}

bool surfaceHasActiveResizeSizing(WaylandServer::Impl::Surface const* surface) {
  return surface && surface->server &&
         (surface->server->resizeSurface_ == surface ||
          surface->geometryAnimationActive ||
          surface->resizeConfigureInFlight ||
          surface->pendingResizeConfigure ||
          surface->awaitingConfigureCommit);
}

bool shouldDeferAtomicResizeState(WaylandServer::Impl::Surface const* surface,
                                  bool hasBufferAttach,
                                  bool viewportChanged,
                                  bool needsBufferRefresh) {
  return surface &&
         !hasBufferAttach &&
         viewportChanged &&
         !needsBufferRefresh &&
         surfaceHasActiveResizeSizing(surface);
}

void traceCrashSurfaceCommit(WaylandServer::Impl::Surface* surface,
                             char const* kindName,
                             std::uint32_t kind,
                             std::uint32_t format) {
  if (!surface) return;
  ++surface->commitCount;
  diagnostics::recordSurfaceCommit(
      surface->id,
      kind == 1u ? diagnostics::CpuSurfaceCommitKind::Shm
                 : kind == 2u ? diagnostics::CpuSurfaceCommitKind::Dmabuf
                              : kindName && std::strcmp(kindName, "empty") == 0
                                    ? diagnostics::CpuSurfaceCommitKind::Empty
                                    : kind == 0u ? diagnostics::CpuSurfaceCommitKind::State
                                                 : diagnostics::CpuSurfaceCommitKind::Other,
      surface->width,
      surface->height);
  bool const isStateOnlyCommit = kind == 0u;
  bool const changed = surface->width != surface->lastLoggedCommitWidth ||
                       surface->height != surface->lastLoggedCommitHeight ||
                       (!isStateOnlyCommit && format != surface->lastLoggedCommitFormat);
  std::uint64_t const sampleInterval = isStateOnlyCommit ? 3000u : 300u;
  if (!changed && surface->commitCount > 3 && surface->commitCount % sampleInterval != 0) return;
  surface->lastLoggedCommitWidth = surface->width;
  surface->lastLoggedCommitHeight = surface->height;
  surface->lastLoggedCommitKind = kind;
  if (!isStateOnlyCommit) surface->lastLoggedCommitFormat = format;
  diagnostics::crashLog("surface-commit surface=%llu role=%u kind=%s commits=%llu buffer=%dx%d "
                        "frame=%dx%d scale=%d serial=%llu format=0x%08x",
                        static_cast<unsigned long long>(surface->id),
                        static_cast<unsigned int>(surface->role),
                        kindName ? kindName : "unknown",
                        static_cast<unsigned long long>(surface->commitCount),
                        surface->width,
                        surface->height,
                        surface->frameWidth,
                        surface->frameHeight,
                        surface->bufferState.scale,
                        static_cast<unsigned long long>(surface->serial),
                        format);
}

void bumpSurfaceSerial(WaylandServer::Impl::Surface* surface) {
  if (!surface) return;
  ++surface->serial;
  if (surface->server && (surfaceIsTopLevelRenderable(surface) || surfaceIsSubsurface(surface))) {
    ++surface->server->contentSerial_;
  }
}

void commitCachedSubsurfaceState(WaylandServer::Impl::Surface* surface) {
  if (!surface || !surfaceHasCachedSubsurfaceCommit(surface)) return;
  auto livePending = takeSurfacePendingCommit(surface);
  if (restoreCachedSubsurfaceCommit(surface)) {
    commitSurfacePendingState(surface, surface->resource, false);
  }
  restoreSurfacePendingCommit(surface, std::move(livePending));
}

void releaseCachedSubsurfaceCommits(WaylandServer::Impl::Surface* parent) {
  if (!parent || !parent->server) return;
  auto releaseLayer = [&](SubsurfaceStackLayer layer) {
    auto subsurfaces = orderedSubsurfacesForParent(parent->server, parent, layer);
    for (auto const* subsurface : subsurfaces) {
      if (!subsurface || !subsurface->surface) continue;
      commitCachedSubsurfaceState(subsurface->surface);
      releaseCachedSubsurfaceCommits(subsurface->surface);
    }
  };
  releaseLayer(SubsurfaceStackLayer::Below);
  releaseLayer(SubsurfaceStackLayer::Above);
}

void commitSurfacePendingState(WaylandServer::Impl::Surface* surface,
                               wl_resource* resource,
                               bool allowSynchronizedSubsurfaceCache) {
  if (!surface) return;
  if (allowSynchronizedSubsurfaceCache && cacheSynchronizedSubsurfaceCommit(surface)) {
    traceCrashSurfaceCommit(surface, "state", 0u, 0u);
    return;
  }
  bool const hasBufferAttach = surface->pendingBufferState.bufferAttached;
  bool const hasNonNullBufferAttach = hasBufferAttach && surface->pendingBufferState.buffer != nullptr;
  bool const hasNullBufferAttach = hasBufferAttach && surface->pendingBufferState.buffer == nullptr;
  if (hasNonNullBufferAttach) {
    auto* xdgSurface = xdgSurfaceForSurface(surface->server, surface);
    switch (xdgSurfaceBufferCommitReadiness(xdgSurface)) {
    case XdgSurfaceBufferCommitReadiness::Ready:
      break;
    case XdgSurfaceBufferCommitReadiness::MissingRoleObject:
      wl_resource_post_error(xdgSurface && xdgSurface->resource ? xdgSurface->resource : resource,
                             XDG_SURFACE_ERROR_NOT_CONSTRUCTED,
                             "xdg_surface must have a role object before committing a buffer");
      return;
    case XdgSurfaceBufferCommitReadiness::Unconfigured:
      wl_resource_post_error(xdgSurface && xdgSurface->resource ? xdgSurface->resource : resource,
                             XDG_SURFACE_ERROR_UNCONFIGURED_BUFFER,
                             "xdg_surface committed a buffer before ack_configure");
      return;
    }
  }
  if (!validateXdgToplevelPendingSizeHints(surface, resource)) return;
  LayerSurfaceCommitResult layerCommit;
  bool flushLayerConfigure = false;
  if (surface->layerSurface) {
    layerCommit = applyLayerSurfacePendingState(surface->layerSurface, resource);
    if (!layerCommit.valid) return;
    if (layerCommit.stateChanged) {
      bool const geometryChanged = applyLayerGeometry(surface->layerSurface);
      refreshShellReservedZones(surface->server);
      if (geometryChanged) ++surface->server->contentSerial_;
    }
    if (hasNullBufferAttach && resetLayerSurfaceForUnmap(surface->layerSurface)) {
      refreshShellReservedZones(surface->server);
    }
    if (!surface->layerSurface->initialized && !hasBufferAttach) {
      surface->layerSurface->initialized = true;
      sendLayerConfigure(surface->layerSurface);
      flushLayerConfigure = true;
    } else if (hasNonNullBufferAttach && !surface->layerSurface->configured) {
      wl_resource_post_error(surface->layerSurface->resource,
                             ZWLR_LAYER_SHELL_V1_ERROR_ALREADY_CONSTRUCTED,
                             "layer-shell surface committed a buffer before ack_configure");
      return;
    } else if (layerCommit.configureNeeded && surface->layerSurface->initialized) {
      sendLayerConfigure(surface->layerSurface);
      flushLayerConfigure = true;
    }
  }
  bool subsurfaceStateChanged = false;
  if (surface->subsurfaceRole) {
    subsurfaceStateChanged |= applySubsurfacePendingPosition(surface->subsurfaceRole);
  }
  subsurfaceStateChanged |= applySubsurfacePendingOrder(surface->server, surface);
  std::vector<WaylandServer::Impl::PresentationFeedback*> supersededFeedbacks =
      std::move(surface->presentationFeedbacks);
  surface->presentationFeedbacks.clear();
  for (auto* feedback : supersededFeedbacks) {
    wp_presentation_feedback_send_discarded(feedback->resource);
    wl_resource_destroy(feedback->resource);
  }
  surface->presentationFeedbacks = std::move(surface->pendingPresentationFeedbacks);
  surface->pendingPresentationFeedbacks.clear();
  surface->frameCallbacks.insert(surface->frameCallbacks.end(),
                                 surface->pendingFrameCallbacks.begin(),
                                 surface->pendingFrameCallbacks.end());
  surface->pendingFrameCallbacks.clear();
  bool const viewportChanged = pendingViewportStateChanged(surface);
  bool const damagePending = surfaceHasPendingDamage(surface);
  bool const needsStateBufferRefresh = !hasBufferAttach && damagePending && surface->bufferState.buffer;
  if (shouldDeferAtomicResizeState(surface, hasBufferAttach, viewportChanged, needsStateBufferRefresh)) {
    traceResizeSurface("commit-state-defer-atomic-resize", surface);
    clearPendingDamage(surface);
    traceCrashSurfaceCommit(surface, "state", 0u, 0u);
    return;
  }
  bool const backgroundBlurChanged = applyBackgroundBlurState(surface);
  bool inputRegionChanged = false;
  bool const protocolRenderStateChanged =
      applySurfaceProtocolState(surface, hasBufferAttach, inputRegionChanged);
  bool const xdgConfigureStateChanged = applyXdgConfigureState(surface);
  bool const xdgRenderStateChanged = applyXdgProtocolState(surface);

  if (!hasBufferAttach) {
    markXdgPopupCommitted(surface);
    bool const pointerConstraintStateChanged =
        applyPointerConstraintsPendingState(surface->server, surface, allowSynchronizedSubsurfaceCache);
    bool serialBumped = false;
    bool const needsBufferRefresh = damagePending && surface->bufferState.buffer;
    if (!viewportChanged && !backgroundBlurChanged && !protocolRenderStateChanged && !xdgRenderStateChanged &&
        !xdgConfigureStateChanged && !inputRegionChanged && !needsBufferRefresh && !layerCommit.stateChanged &&
        !subsurfaceStateChanged && !pointerConstraintStateChanged && surface->presentationFeedbacks.empty()) {
      if (flushLayerConfigure) surface->server->flushClients();
      traceCrashSurfaceCommit(surface, "state", 0u, 0u);
      clearPendingDamage(surface);
      releaseCachedSubsurfaceCommits(surface);
      return;
    }
    if (needsBufferRefresh) {
      if (auto* shmBuffer = shmBufferFor(surface->server, surface->bufferState.buffer)) {
        if (refreshCurrentShmBuffer(surface, *shmBuffer)) {
          if (!bufferSizeValidForScale(surface)) {
            wl_resource_post_error(resource,
                                   WL_SURFACE_ERROR_INVALID_SIZE,
                                   "buffer size is not divisible by wl_surface buffer scale");
            return;
          }
          if (!applyViewportState(surface)) return;
          applyLayerGeometry(surface->layerSurface);
          maybeSendInitialCutoutsConfigure(surface->server, surface);
          appendCommittedBufferDamage(surface);
          bumpSurfaceSerial(surface);
          serialBumped = true;
          if (lambda::detail::resizeTraceMetadataEnabled()) {
            surface->lastCommitNsec = lambda::detail::resizeTraceTimestampNanoseconds();
          }
          traceResizeSurface("commit-damage-shm", surface);
        }
      } else if (auto* dmabufBuffer = dmabufBufferFor(surface->server, surface->bufferState.buffer)) {
        surface->dmabufBuffer = dmabufBuffer;
        if (!bufferSizeValidForScale(surface)) {
          wl_resource_post_error(resource,
                                 WL_SURFACE_ERROR_INVALID_SIZE,
                                 "buffer size is not divisible by wl_surface buffer scale");
          return;
        }
        if (!applyViewportState(surface)) return;
        applyLayerGeometry(surface->layerSurface);
        maybeSendInitialCutoutsConfigure(surface->server, surface);
        appendCommittedBufferDamage(surface);
        bumpSurfaceSerial(surface);
        serialBumped = true;
        if (lambda::detail::resizeTraceMetadataEnabled()) {
          surface->lastCommitNsec = lambda::detail::resizeTraceTimestampNanoseconds();
        }
        traceResizeSurface("commit-damage-dmabuf", surface);
      }
    } else if (surface->pendingViewportState.sourceSet || surface->pendingViewportState.destinationSet) {
      traceResizeSurface("commit-state-defer-viewport", surface);
    } else if (pendingViewportSourceFitsCurrentBuffer(surface)) {
      if (!applyViewportState(surface)) return;
      applyLayerGeometry(surface->layerSurface);
      maybeSendInitialCutoutsConfigure(surface->server, surface);
      traceResizeSurface("commit-state", surface);
    }
    if ((backgroundBlurChanged || protocolRenderStateChanged || xdgConfigureStateChanged ||
         xdgRenderStateChanged || viewportChanged || layerCommit.stateChanged || subsurfaceStateChanged) &&
        !serialBumped) {
      bumpSurfaceSerial(surface);
    }
    if (flushLayerConfigure) surface->server->flushClients();
    clearPendingDamage(surface);
    traceCrashSurfaceCommit(surface, "state", 0u, 0u);
    releaseCachedSubsurfaceCommits(surface);
    return;
  }

  wl_resource* const previousBuffer = surface->bufferState.buffer;
  bool const previousDmabufHeld = surface->dmabufBuffer != nullptr;
  bool const previousShmHeld = surface->shmPixels != nullptr;
  if (previousBuffer &&
      (previousDmabufHeld || previousShmHeld) &&
      previousBuffer != surface->pendingBufferState.buffer) {
    queueBufferRelease(surface, previousBuffer);
  }
  surface->bufferState.buffer = surface->pendingBufferState.buffer;
  surface->pendingBufferState.buffer = nullptr;
  surface->pendingBufferState.bufferAttached = false;
  bool releaseCurrentBufferImmediately = false;
  if (surface->bufferState.buffer) {
    if (auto* shmBuffer = shmBufferFor(surface->server, surface->bufferState.buffer)) {
      if (refreshCurrentShmBuffer(surface, *shmBuffer)) {
        surface->width = shmBuffer->width;
        surface->height = shmBuffer->height;
        if (!bufferSizeValidForScale(surface)) {
          wl_resource_post_error(resource,
                                 WL_SURFACE_ERROR_INVALID_SIZE,
                                 "buffer size is not divisible by wl_surface buffer scale");
          return;
        }
        if (!applyViewportState(surface)) return;
        traceConfigureCommitLag("commit-match-shm", surface);
        clearMatchedConfigureCommit(surface);
        applyLayerGeometry(surface->layerSurface);
        if (markLayerSurfaceMapped(surface->layerSurface)) refreshShellReservedZones(surface->server);
        maybeSendInitialCutoutsConfigure(surface->server, surface);
        markXdgToplevelMapped(surface);
        markXdgPopupMapped(surface);
        surface->dmabufBuffer = nullptr;
        appendCommittedBufferDamage(surface);
        bumpSurfaceSerial(surface);
        if (lambda::detail::resizeTraceMetadataEnabled()) {
          surface->lastCommitNsec = lambda::detail::resizeTraceTimestampNanoseconds();
        }
        traceResizeSurface("commit-shm", surface);
        traceCrashSurfaceCommit(surface, "shm", 1u, static_cast<std::uint32_t>(shmBuffer->format));
        releaseCurrentBufferImmediately = surface->shmPixels == nullptr;
      }
    } else if (auto* dmabufBuffer = dmabufBufferFor(surface->server, surface->bufferState.buffer)) {
      if (dmabufBuffer->width > 0 && dmabufBuffer->height > 0 && !dmabufBuffer->planes.empty()) {
        surface->rgbaPixels.reset();
        surface->shmPixels = nullptr;
        surface->shmPixelBytes = 0;
        surface->pixelFormat = Image::PixelFormat::Rgba8888;
        surface->rgbaFullyOpaque = false;
        surface->width = dmabufBuffer->width;
        surface->height = dmabufBuffer->height;
        if (!bufferSizeValidForScale(surface)) {
          wl_resource_post_error(resource,
                                 WL_SURFACE_ERROR_INVALID_SIZE,
                                 "buffer size is not divisible by wl_surface buffer scale");
          return;
        }
        if (!applyViewportState(surface)) return;
        traceConfigureCommitLag("commit-match-dmabuf", surface);
        clearMatchedConfigureCommit(surface);
        applyLayerGeometry(surface->layerSurface);
        if (markLayerSurfaceMapped(surface->layerSurface)) refreshShellReservedZones(surface->server);
        maybeSendInitialCutoutsConfigure(surface->server, surface);
        markXdgToplevelMapped(surface);
        markXdgPopupMapped(surface);
        surface->dmabufBuffer = dmabufBuffer;
        appendCommittedBufferDamage(surface);
        bumpSurfaceSerial(surface);
        if (lambda::detail::resizeTraceMetadataEnabled()) {
          surface->lastCommitNsec = lambda::detail::resizeTraceTimestampNanoseconds();
        }
        traceResizeSurface("commit-dmabuf", surface);
        traceCrashSurfaceCommit(surface, "dmabuf", 2u, dmabufBuffer->format);
      }
    } else {
      releaseCurrentBufferImmediately = true;
    }
    if (releaseCurrentBufferImmediately) wl_buffer_send_release(surface->bufferState.buffer);
  } else {
    resetXdgToplevelForUnmap(surface);
    resetXdgPopupForUnmap(surface);
    bool const xdgSurfaceConfigureReset =
        resetXdgSurfaceConfigureStateForUnmap(xdgSurfaceForSurface(surface->server, surface));
    surface->rgbaPixels.reset();
    surface->shmPixels = nullptr;
    surface->shmPixelBytes = 0;
    surface->pixelFormat = Image::PixelFormat::Rgba8888;
    surface->rgbaFullyOpaque = false;
    surface->width = 0;
    surface->height = 0;
    setConfiguredFrameSize(surface, 0, 0);
    surface->awaitingConfigureCommit = false;
    surface->awaitingConfigureWidth = 0;
    surface->awaitingConfigureHeight = 0;
    surface->resizeConfigureInFlight = false;
    surface->resizeConfigureAcked = false;
    surface->resizeConfigureSerial = 0;
    surface->resizeConfigureX = 0;
    surface->resizeConfigureY = 0;
    surface->resizeConfigureWidth = 0;
    surface->resizeConfigureHeight = 0;
    surface->pendingResizeConfigure = false;
    surface->pendingResizeConfigureX = 0;
    surface->pendingResizeConfigureY = 0;
    surface->pendingResizeConfigureWidth = 0;
    surface->pendingResizeConfigureHeight = 0;
    surface->dmabufBuffer = nullptr;
    surface->damageState.bufferRects.clear();
    bumpSurfaceSerial(surface);
    if (xdgSurfaceConfigureReset) {
      if (auto* toplevel = toplevelForSurface(surface->server, surface)) {
        sendToplevelStateConfigure(surface->server, toplevel);
        surface->server->flushClients();
      }
    }
    traceResizeSurface("commit-empty", surface);
    traceCrashSurfaceCommit(surface, "empty", 0u, 0u);
  }
  applyPointerConstraintsPendingState(surface->server, surface, allowSynchronizedSubsurfaceCache);
  clearPendingDamage(surface);
  releaseCachedSubsurfaceCommits(surface);
}

void surfaceCommit(wl_client*, wl_resource* resource) {
  commitSurfacePendingState(resourceData<WaylandServer::Impl::Surface>(resource), resource, true);
}

void surfaceSetBufferScale(wl_client*, wl_resource* resource, std::int32_t scale) {
  auto* surface = resourceData<WaylandServer::Impl::Surface>(resource);
  if (scale <= 0) {
    wl_resource_post_error(resource, WL_SURFACE_ERROR_INVALID_SCALE, "invalid wl_surface buffer scale");
    return;
  }
  surface->pendingBufferState.scale = scale;
  surface->pendingBufferState.scaleSet = true;
}

void surfaceSetBufferTransform(wl_client*, wl_resource* resource, std::int32_t transform) {
  auto* surface = resourceData<WaylandServer::Impl::Surface>(resource);
  if (!wl_output_transform_is_valid(static_cast<std::uint32_t>(transform), 1)) {
    wl_resource_post_error(resource, WL_SURFACE_ERROR_INVALID_TRANSFORM, "invalid wl_surface buffer transform");
    return;
  }
  surface->pendingBufferState.transform = transform;
  surface->pendingBufferState.transformSet = true;
}

void surfaceDamageBuffer(wl_client*, wl_resource* resource, std::int32_t x, std::int32_t y,
                         std::int32_t width, std::int32_t height) {
  auto* surface = resourceData<WaylandServer::Impl::Surface>(resource);
  if (!surface) return;
  appendRegionRect(surface->pendingDamageState.bufferRects, normalizedRegionRect(x, y, width, height));
}

void surfaceOffset(wl_client*, wl_resource* resource, std::int32_t x, std::int32_t y) {
  auto* surface = resourceData<WaylandServer::Impl::Surface>(resource);
  surface->pendingBufferState.offsetX = x;
  surface->pendingBufferState.offsetY = y;
  surface->pendingBufferState.offsetSet = true;
}

struct wl_surface_interface const surfaceImpl{
    .destroy = surfaceDestroy,
    .attach = surfaceAttach,
    .damage = surfaceDamage,
    .frame = surfaceFrame,
    .set_opaque_region = surfaceSetOpaqueRegion,
    .set_input_region = surfaceSetInputRegion,
    .commit = surfaceCommit,
    .set_buffer_transform = surfaceSetBufferTransform,
    .set_buffer_scale = surfaceSetBufferScale,
    .damage_buffer = surfaceDamageBuffer,
    .offset = surfaceOffset,
    .get_release = surfaceFrame,
};

} // namespace

void setConfiguredFrameSize(WaylandServer::Impl::Surface* surface, std::int32_t width, std::int32_t height) {
  if (!surface) return;
  surface->frameWidth = width;
  surface->frameHeight = height;
}

std::int32_t displayWidth(WaylandServer::Impl::Surface const* surface) {
  return surface && surface->frameWidth > 0
             ? surface->frameWidth
             : surface ? std::max(0,
                                   surfaceTransformedBufferWidth(surface) /
                                       std::max(1, surface->bufferState.scale))
                       : 0;
}

std::int32_t displayHeight(WaylandServer::Impl::Surface const* surface) {
  return surface && surface->frameHeight > 0
             ? surface->frameHeight
             : surface ? std::max(0,
                                   surfaceTransformedBufferHeight(surface) /
                                       std::max(1, surface->bufferState.scale))
                       : 0;
}

void traceResizeSurface(char const* event, WaylandServer::Impl::Surface const* surface) {
  if (!surface) return;
  LAMBDA_RESIZE_TRACE(
      "compositor",
      "%s surface=%llu window=%d,%d frame=%dx%d activeSizing=%d awaiting=%d %dx%d buffer=%dx%d scale=%d "
      "source=%d %.1f,%.1f %.1fx%.1f dest=%d %dx%d serial=%llu configureSerial=%u configure=%dx%d "
      "inFlight=%d acked=%d pending=%d %d,%d %dx%d target=%d,%d %dx%d snapped=%d maximized=%d anim=%d\n",
      event,
      static_cast<unsigned long long>(surface->id),
      surface->windowX,
      surface->windowY,
      surface->frameWidth,
      surface->frameHeight,
      (surface->server->resizeSurface_ == surface ||
       surface->geometryAnimationActive ||
       surface->awaitingConfigureCommit) ? 1 : 0,
      surface->awaitingConfigureCommit ? 1 : 0,
      surface->awaitingConfigureWidth,
      surface->awaitingConfigureHeight,
      surface->width,
      surface->height,
      surface->bufferState.scale,
      surface->viewportState.sourceSet ? 1 : 0,
      surface->viewportState.sourceX,
      surface->viewportState.sourceY,
      surface->viewportState.sourceWidth,
      surface->viewportState.sourceHeight,
      surface->viewportState.destinationSet ? 1 : 0,
      surface->viewportState.destinationWidth,
      surface->viewportState.destinationHeight,
      static_cast<unsigned long long>(surface->serial),
      surface->lastConfigureSerial,
      surface->lastConfigureWidth,
      surface->lastConfigureHeight,
      surface->resizeConfigureInFlight ? 1 : 0,
      surface->resizeConfigureAcked ? 1 : 0,
      surface->pendingResizeConfigure ? 1 : 0,
      surface->pendingResizeConfigureX,
      surface->pendingResizeConfigureY,
      surface->pendingResizeConfigureWidth,
      surface->pendingResizeConfigureHeight,
      surface->geometryAnimationTargetX,
      surface->geometryAnimationTargetY,
      surface->geometryAnimationTargetWidth,
      surface->geometryAnimationTargetHeight,
      surface->snapped ? 1 : 0,
      surface->maximized ? 1 : 0,
      surface->geometryAnimationActive ? 1 : 0);
}

void bindCompositor(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  wl_resource* resource = wl_resource_create(client, &wl_compositor_interface, std::min(version, 5u), id);
  wl_resource_set_implementation(resource, &compositorImpl, data, nullptr);
}

void bindSubcompositor(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  wl_resource* resource = wl_resource_create(client, &wl_subcompositor_interface, std::min(version, 1u), id);
  if (!resource) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(resource, &subcompositorImpl, data, nullptr);
}

wl_resource* WaylandServer::Impl::createSurface(wl_client* client, std::uint32_t version, std::uint32_t id) {
  auto surface = std::make_unique<Surface>();
  surface->server = this;
  surface->id = nextSurfaceId_++;
  wl_resource* resource = wl_resource_create(client, &wl_surface_interface, std::min(version, 5u), id);
  surface->resource = resource;
  auto* raw = surface.get();
  surfaces_.push_back(std::move(surface));
  wl_resource_set_implementation(resource,
                                 &surfaceImpl,
                                 raw,
                                 destroyResourceCallback<WaylandServer::Impl::Surface,
                                                         WaylandServer::Impl,
                                                         &WaylandServer::Impl::destroySurface>);
  for (wl_resource* output : outputResources_) {
    if (output && wl_resource_get_client(output) == client) {
      wl_surface_send_enter(resource, output);
    }
  }
  diagnostics::crashLog("surface-create surface=%llu version=%u total=%zu",
                        static_cast<unsigned long long>(raw->id),
                        std::min(version, 5u),
                        surfaces_.size());
  return resource;
}

} // namespace lambda::compositor
