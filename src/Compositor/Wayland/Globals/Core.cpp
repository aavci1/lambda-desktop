#include "Compositor/Wayland/Globals/Core.hpp"

#include "Compositor/Diagnostics/CrashLog.hpp"
#include "Compositor/Diagnostics/CpuTrace.hpp"
#include "Compositor/Wayland/ResourceTemplates.hpp"
#include "Compositor/Wayland/WaylandServerImpl.hpp"
#include "Compositor/Window/WindowManagerInternal.hpp"
#include "Detail/ResizeTrace.hpp"
#include "presentation-time-server-protocol.h"
#include "viewporter-server-protocol.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

namespace flux::compositor {
namespace {

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

bool transformSwapsAxes(std::int32_t transform) {
  return transform == WL_OUTPUT_TRANSFORM_90 ||
         transform == WL_OUTPUT_TRANSFORM_270 ||
         transform == WL_OUTPUT_TRANSFORM_FLIPPED_90 ||
         transform == WL_OUTPUT_TRANSFORM_FLIPPED_270;
}

std::int32_t transformedBufferWidth(WaylandServer::Impl::Surface const* surface) {
  if (!surface) return 0;
  return transformSwapsAxes(surface->bufferTransform) ? surface->height : surface->width;
}

std::int32_t transformedBufferHeight(WaylandServer::Impl::Surface const* surface) {
  if (!surface) return 0;
  return transformSwapsAxes(surface->bufferTransform) ? surface->width : surface->height;
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
  subsurface->x = x;
  subsurface->y = y;
}

void subsurfacePlaceAbove(wl_client*, wl_resource*, wl_resource*) {}
void subsurfacePlaceBelow(wl_client*, wl_resource*, wl_resource*) {}
void subsurfaceSetSync(wl_client*, wl_resource*) {}
void subsurfaceSetDesync(wl_client*, wl_resource*) {}

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

  auto subsurface = std::make_unique<WaylandServer::Impl::Subsurface>();
  subsurface->server = server;
  subsurface->surface = surface;
  subsurface->parent = parent;
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
  surface->pendingBuffer = buffer;
  surface->pendingBufferAttached = true;
  if (x != 0 || y != 0) {
    surface->pendingBufferOffsetX = x;
    surface->pendingBufferOffsetY = y;
    surface->pendingBufferOffsetSet = true;
  }
}

void surfaceFrame(wl_client* client, wl_resource* resource, std::uint32_t id) {
  auto* surface = resourceData<WaylandServer::Impl::Surface>(resource);
  wl_resource* callback = wl_resource_create(client, &wl_callback_interface, 1, id);
  if (!callback) {
    wl_client_post_no_memory(client);
    return;
  }
  surface->frameCallbacks.push_back(callback);
}

void surfaceDamage(wl_client*, wl_resource* resource, std::int32_t x, std::int32_t y,
                   std::int32_t width, std::int32_t height) {
  auto* surface = resourceData<WaylandServer::Impl::Surface>(resource);
  if (!surface) return;
  appendRegionRect(surface->pendingSurfaceDamageRects, normalizedRegionRect(x, y, width, height));
}

void surfaceSetOpaqueRegion(wl_client*, wl_resource* resource, wl_resource* regionResource) {
  auto* surface = resourceData<WaylandServer::Impl::Surface>(resource);
  if (!surface) return;
  surface->pendingOpaqueRegionRects = copyRegionRects(regionResource);
  surface->pendingOpaqueRegionSet = true;
}

void surfaceSetInputRegion(wl_client*, wl_resource* resource, wl_resource* regionResource) {
  auto* surface = resourceData<WaylandServer::Impl::Surface>(resource);
  if (!surface) return;
  surface->pendingInputRegionInfinite = regionResource == nullptr;
  surface->pendingInputRegionRects = copyRegionRects(regionResource);
  surface->pendingInputRegionSet = true;
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
  bool const scanAlpha = buffer.format == WL_SHM_FORMAT_ARGB8888;
  fullyOpaque = true;
  auto const* base = static_cast<std::uint8_t const*>(buffer.data) + buffer.offset;
  for (std::int32_t y = 0; y < buffer.height; ++y) {
    auto const* src = base + static_cast<std::size_t>(y) * static_cast<std::size_t>(buffer.stride);
    auto* dst = out.data() + static_cast<std::size_t>(y) * rowBytes;
    // wl_shm ARGB/XRGB are little-endian words, so memory order is B, G, R, A/X.
    std::memcpy(dst, src, rowBytes);
    if (scanAlpha && fullyOpaque) {
      for (std::int32_t x = 0; x < buffer.width; ++x) {
        fullyOpaque = src[static_cast<std::size_t>(x) * 4u + 3u] == 255u;
        if (!fullyOpaque) break;
      }
    }
  }
  return true;
}

bool isIntegerSize(float value) {
  return std::floor(value) == value;
}

std::int32_t committedDisplayWidth(WaylandServer::Impl::Surface const* surface) {
  if (!surface) return 0;
  if (surface->destinationSet) return surface->destinationWidth;
  if (surface->sourceSet) return static_cast<std::int32_t>(surface->sourceWidth);
  return std::max(1, transformedBufferWidth(surface) / std::max(1, surface->scale));
}

std::int32_t committedDisplayHeight(WaylandServer::Impl::Surface const* surface) {
  if (!surface) return 0;
  if (surface->destinationSet) return surface->destinationHeight;
  if (surface->sourceSet) return static_cast<std::int32_t>(surface->sourceHeight);
  return std::max(1, transformedBufferHeight(surface) / std::max(1, surface->scale));
}

std::int32_t committedWindowDisplayWidth(WaylandServer::Impl::Surface const* surface) {
  return surface && surface->xdgWindowGeometrySet
             ? surface->xdgWindowGeometryWidth
             : committedDisplayWidth(surface);
}

std::int32_t committedWindowDisplayHeight(WaylandServer::Impl::Surface const* surface) {
  return surface && surface->xdgWindowGeometrySet
             ? surface->xdgWindowGeometryHeight
             : committedDisplayHeight(surface);
}

bool clearMatchedConfigureCommit(WaylandServer::Impl::Surface* surface) {
  if (!surface || !surface->awaitingConfigureCommit) return false;
  if (committedWindowDisplayWidth(surface) != surface->awaitingConfigureWidth ||
      committedWindowDisplayHeight(surface) != surface->awaitingConfigureHeight) {
    return false;
  }
  surface->awaitingConfigureCommit = false;
  surface->awaitingConfigureWidth = 0;
  surface->awaitingConfigureHeight = 0;
  return true;
}

void sendPendingResizeConfigureIfNeeded(WaylandServer::Impl::Surface* surface) {
  if (!surface || !surface->server || surface->server->resizeSurface_ != surface) {
    return;
  }
  if (surface->awaitingConfigureCommit) {
    return;
  }
  std::int32_t const width = surface->server->resizeLastWidth_;
  std::int32_t const height = surface->server->resizeLastHeight_;
  if (width <= 0 || height <= 0) {
    return;
  }
  if (width == surface->lastConfigureWidth && height == surface->lastConfigureHeight) {
    return;
  }
  sendToplevelConfigure(surface->server, toplevelForSurface(surface->server, surface), width, height);
  surface->server->flushClients();
}

void traceConfigureCommitLag(char const* event, WaylandServer::Impl::Surface const* surface) {
  if (!surface || !flux::detail::resizeTraceEnabled() || surface->lastConfigureSerial == 0) return;
  std::int32_t const committedWidth = committedDisplayWidth(surface);
  std::int32_t const committedHeight = committedDisplayHeight(surface);
  if (!surface->awaitingConfigureCommit && committedWidth == surface->lastConfigureWidth &&
      committedHeight == surface->lastConfigureHeight) {
    return;
  }
  std::uint64_t const now = flux::detail::resizeTraceTimestampNanoseconds();
  double const lagMs = surface->lastConfigureSentNsec > 0
                           ? static_cast<double>(now - surface->lastConfigureSentNsec) / 1'000'000.0
                           : 0.0;
  flux::detail::resizeTrace("compositor",
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
  surface->sourceSet = surface->pendingSourceSet;
  surface->sourceX = surface->pendingSourceX;
  surface->sourceY = surface->pendingSourceY;
  surface->sourceWidth = surface->pendingSourceWidth;
  surface->sourceHeight = surface->pendingSourceHeight;
  surface->destinationSet = surface->pendingDestinationSet;
  surface->destinationWidth = surface->pendingDestinationWidth;
  surface->destinationHeight = surface->pendingDestinationHeight;

  if (surface->width <= 0 || surface->height <= 0) {
    setConfiguredFrameSize(surface, 0, 0);
    return true;
  }

  if (surface->sourceSet) {
    if (surface->sourceX < 0.f || surface->sourceY < 0.f ||
        surface->sourceWidth <= 0.f || surface->sourceHeight <= 0.f) {
      if (surface->viewport && surface->viewport->resource) {
        wl_resource_post_error(surface->viewport->resource,
                               WP_VIEWPORT_ERROR_BAD_VALUE,
                               "invalid viewport source rectangle");
      }
      return false;
    }
    if (surface->sourceX + surface->sourceWidth > static_cast<float>(surface->width) ||
        surface->sourceY + surface->sourceHeight > static_cast<float>(surface->height)) {
      if (surface->viewport && surface->viewport->resource) {
        wl_resource_post_error(surface->viewport->resource,
                               WP_VIEWPORT_ERROR_OUT_OF_BUFFER,
                               "viewport source rectangle exceeds buffer");
      }
      return false;
    }
  }

  bool const interactiveSizing = surface->server->resizeSurface_ == surface;
  bool const animationSizing = surface->geometryAnimationActive;
  bool const matchedConfigure = surface->awaitingConfigureCommit &&
                                committedWindowDisplayWidth(surface) == surface->awaitingConfigureWidth &&
                                committedWindowDisplayHeight(surface) == surface->awaitingConfigureHeight;
  if (interactiveSizing || (matchedConfigure && !animationSizing)) {
    std::int32_t const committedWidth = committedWindowDisplayWidth(surface);
    std::int32_t const committedHeight = committedWindowDisplayHeight(surface);
    if (committedWidth > 0 && committedHeight > 0) {
      setConfiguredFrameSize(surface, committedWidth, committedHeight);
    }
    return true;
  }
  if (animationSizing) {
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
  }
  if ((surface->snapped || surface->maximized) &&
      surface->frameWidth > 0 &&
      surface->frameHeight > 0) {
    return true;
  }

  if (surface->xdgWindowGeometrySet) {
    surface->frameWidth = surface->xdgWindowGeometryWidth;
    surface->frameHeight = surface->xdgWindowGeometryHeight;
  } else if (surface->destinationSet) {
    surface->frameWidth = surface->destinationWidth;
    surface->frameHeight = surface->destinationHeight;
  } else if (surface->sourceSet) {
    if (!isIntegerSize(surface->sourceWidth) || !isIntegerSize(surface->sourceHeight)) {
      if (surface->viewport && surface->viewport->resource) {
        wl_resource_post_error(surface->viewport->resource,
                               WP_VIEWPORT_ERROR_BAD_SIZE,
                               "viewport source size must be integer without a destination size");
      }
      return false;
    }
    surface->frameWidth = static_cast<std::int32_t>(surface->sourceWidth);
    surface->frameHeight = static_cast<std::int32_t>(surface->sourceHeight);
  } else {
    surface->frameWidth = committedDisplayWidth(surface);
    surface->frameHeight = committedDisplayHeight(surface);
  }

  return true;
}

bool pendingViewportSourceFitsCurrentBuffer(WaylandServer::Impl::Surface const* surface) {
  if (!surface || !surface->pendingSourceSet) return true;
  return surface->pendingSourceX >= 0.f &&
         surface->pendingSourceY >= 0.f &&
         surface->pendingSourceWidth > 0.f &&
         surface->pendingSourceHeight > 0.f &&
         surface->pendingSourceX + surface->pendingSourceWidth <= static_cast<float>(surface->width) &&
         surface->pendingSourceY + surface->pendingSourceHeight <= static_cast<float>(surface->height);
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
    surface->xdgWindowGeometrySet = true;
    surface->xdgWindowGeometryX = xdgSurface->windowGeometryX;
    surface->xdgWindowGeometryY = xdgSurface->windowGeometryY;
    surface->xdgWindowGeometryWidth = xdgSurface->windowGeometryWidth;
    surface->xdgWindowGeometryHeight = xdgSurface->windowGeometryHeight;
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

bool surfaceHasPendingDamage(WaylandServer::Impl::Surface const* surface) {
  return surface &&
         (!surface->pendingSurfaceDamageRects.empty() || !surface->pendingBufferDamageRects.empty());
}

void clearPendingDamage(WaylandServer::Impl::Surface* surface) {
  if (!surface) return;
  surface->pendingSurfaceDamageRects.clear();
  surface->pendingBufferDamageRects.clear();
}

bool applySurfaceProtocolState(WaylandServer::Impl::Surface* surface,
                               bool hasBufferAttach,
                               bool& inputRegionChanged) {
  if (!surface) return false;
  bool renderStateChanged = false;
  inputRegionChanged = false;

  if (surface->pendingScaleSet) {
    if (surface->scale != surface->pendingScale) {
      surface->scale = surface->pendingScale;
      renderStateChanged = true;
    }
    surface->pendingScaleSet = false;
  }

  if (surface->pendingBufferTransformSet) {
    if (surface->bufferTransform != surface->pendingBufferTransform) {
      surface->bufferTransform = surface->pendingBufferTransform;
      renderStateChanged = true;
    }
    surface->pendingBufferTransformSet = false;
  }

  if (surface->pendingBufferOffsetSet) {
    if (surface->bufferOffsetX != surface->pendingBufferOffsetX ||
        surface->bufferOffsetY != surface->pendingBufferOffsetY) {
      surface->bufferOffsetX = surface->pendingBufferOffsetX;
      surface->bufferOffsetY = surface->pendingBufferOffsetY;
      surface->x = surface->bufferOffsetX;
      surface->y = surface->bufferOffsetY;
      renderStateChanged = true;
      if (hasBufferAttach && surfaceIsTopLevelRenderable(surface)) {
        surface->windowX += surface->bufferOffsetX;
        surface->windowY += surface->bufferOffsetY;
      }
    }
    surface->pendingBufferOffsetX = 0;
    surface->pendingBufferOffsetY = 0;
    surface->pendingBufferOffsetSet = false;
  }

  if (surface->pendingOpaqueRegionSet) {
    if (!regionsEqual(surface->opaqueRegionRects, surface->pendingOpaqueRegionRects)) {
      surface->opaqueRegionRects = surface->pendingOpaqueRegionRects;
      renderStateChanged = true;
    }
    surface->pendingOpaqueRegionSet = false;
  }

  if (surface->pendingInputRegionSet) {
    if (surface->inputRegionInfinite != surface->pendingInputRegionInfinite ||
        !regionsEqual(surface->inputRegionRects, surface->pendingInputRegionRects)) {
      surface->inputRegionInfinite = surface->pendingInputRegionInfinite;
      surface->inputRegionRects = surface->pendingInputRegionRects;
      inputRegionChanged = true;
    }
    surface->pendingInputRegionSet = false;
  }

  return renderStateChanged;
}

bool bufferSizeValidForScale(WaylandServer::Impl::Surface const* surface) {
  if (!surface || surface->width <= 0 || surface->height <= 0) return true;
  std::int32_t const scale = std::max(1, surface->scale);
  return transformedBufferWidth(surface) % scale == 0 &&
         transformedBufferHeight(surface) % scale == 0;
}

bool refreshCurrentShmBuffer(WaylandServer::Impl::Surface* surface,
                             WaylandServer::Impl::ShmBuffer const& shmBuffer) {
  std::vector<std::uint8_t> pixels;
  Image::PixelFormat pixelFormat = Image::PixelFormat::Rgba8888;
  bool fullyOpaque = false;
  auto const copyStart = diagnostics::cpuTraceNow();
  if (!copyShmBufferToPixels(shmBuffer, pixels, pixelFormat, fullyOpaque)) {
    return false;
  }
  diagnostics::recordShmCopy(pixels.size(), diagnostics::cpuTraceElapsedMilliseconds(copyStart));
  surface->rgbaPixels = std::make_shared<std::vector<std::uint8_t> const>(std::move(pixels));
  surface->pixelFormat = pixelFormat;
  surface->rgbaFullyOpaque = fullyOpaque;
  surface->width = shmBuffer.width;
  surface->height = shmBuffer.height;
  surface->dmabufBuffer = nullptr;
  return true;
}

bool pendingViewportStateChanged(WaylandServer::Impl::Surface const* surface) {
  if (!surface) return false;
  return surface->pendingSourceSet != surface->sourceSet ||
         surface->pendingSourceX != surface->sourceX ||
         surface->pendingSourceY != surface->sourceY ||
         surface->pendingSourceWidth != surface->sourceWidth ||
         surface->pendingSourceHeight != surface->sourceHeight ||
         surface->pendingDestinationSet != surface->destinationSet ||
         surface->pendingDestinationWidth != surface->destinationWidth ||
         surface->pendingDestinationHeight != surface->destinationHeight;
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
                        surface->scale,
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

void surfaceCommit(wl_client*, wl_resource* resource) {
  auto* surface = resourceData<WaylandServer::Impl::Surface>(resource);
  bool const hasBufferAttach = surface->pendingBufferAttached;
  if (surface->layerSurface && !surface->layerSurface->configured && !hasBufferAttach) {
    surface->layerSurface->configured = true;
    sendLayerConfigure(surface->layerSurface);
    surface->server->flushClients();
    return;
  }
  std::vector<WaylandServer::Impl::PresentationFeedback*> supersededFeedbacks =
      std::move(surface->presentationFeedbacks);
  surface->presentationFeedbacks.clear();
  for (auto* feedback : supersededFeedbacks) {
    wp_presentation_feedback_send_discarded(feedback->resource);
    wl_resource_destroy(feedback->resource);
  }
  surface->presentationFeedbacks = std::move(surface->pendingPresentationFeedbacks);
  surface->pendingPresentationFeedbacks.clear();
  bool const backgroundBlurChanged = applyBackgroundBlurState(surface);
  bool inputRegionChanged = false;
  bool const protocolRenderStateChanged =
      applySurfaceProtocolState(surface, hasBufferAttach, inputRegionChanged);
  bool const xdgRenderStateChanged = applyXdgProtocolState(surface);
  bool const viewportChanged = pendingViewportStateChanged(surface);
  bool const damagePending = surfaceHasPendingDamage(surface);

  if (!hasBufferAttach) {
    bool serialBumped = false;
    bool const needsBufferRefresh = damagePending && surface->currentBuffer;
    if (!viewportChanged && !backgroundBlurChanged && !protocolRenderStateChanged && !xdgRenderStateChanged &&
        !inputRegionChanged && !needsBufferRefresh &&
        surface->presentationFeedbacks.empty()) {
      traceCrashSurfaceCommit(surface, "state", 0u, 0u);
      clearPendingDamage(surface);
      return;
    }
    if (needsBufferRefresh) {
      if (auto* shmBuffer = shmBufferFor(surface->server, surface->currentBuffer)) {
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
          bumpSurfaceSerial(surface);
          serialBumped = true;
          surface->lastCommitNsec = flux::detail::resizeTraceTimestampNanoseconds();
          traceResizeSurface("commit-damage-shm", surface);
        }
      } else if (auto* dmabufBuffer = dmabufBufferFor(surface->server, surface->currentBuffer)) {
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
        bumpSurfaceSerial(surface);
        serialBumped = true;
        surface->lastCommitNsec = flux::detail::resizeTraceTimestampNanoseconds();
        traceResizeSurface("commit-damage-dmabuf", surface);
      }
    } else if (surface->pendingSourceSet || surface->pendingDestinationSet) {
      traceResizeSurface("commit-state-defer-viewport", surface);
    } else if (pendingViewportSourceFitsCurrentBuffer(surface)) {
      if (!applyViewportState(surface)) return;
      applyLayerGeometry(surface->layerSurface);
      maybeSendInitialCutoutsConfigure(surface->server, surface);
      traceResizeSurface("commit-state", surface);
    }
    if ((backgroundBlurChanged || protocolRenderStateChanged || xdgRenderStateChanged || viewportChanged) && !serialBumped) {
      bumpSurfaceSerial(surface);
    }
    clearPendingDamage(surface);
    traceCrashSurfaceCommit(surface, "state", 0u, 0u);
    return;
  }

  wl_resource* const previousBuffer = surface->currentBuffer;
  bool const previousDmabufHeld = surface->dmabufBuffer != nullptr;
  if (previousBuffer && previousDmabufHeld && previousBuffer != surface->pendingBuffer) {
    surface->pendingBufferReleases.push_back(previousBuffer);
  }
  surface->currentBuffer = surface->pendingBuffer;
  surface->pendingBuffer = nullptr;
  surface->pendingBufferAttached = false;
  bool releaseCurrentBufferImmediately = false;
  if (surface->currentBuffer) {
    if (auto* shmBuffer = shmBufferFor(surface->server, surface->currentBuffer)) {
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
        bool const matchedConfigureCommit = clearMatchedConfigureCommit(surface);
        applyLayerGeometry(surface->layerSurface);
        maybeSendInitialCutoutsConfigure(surface->server, surface);
        surface->dmabufBuffer = nullptr;
        bumpSurfaceSerial(surface);
        surface->lastCommitNsec = flux::detail::resizeTraceTimestampNanoseconds();
        traceResizeSurface("commit-shm", surface);
        if (matchedConfigureCommit) {
          sendPendingResizeConfigureIfNeeded(surface);
        }
        traceCrashSurfaceCommit(surface, "shm", 1u, static_cast<std::uint32_t>(shmBuffer->format));
        releaseCurrentBufferImmediately = true;
      }
    } else if (auto* dmabufBuffer = dmabufBufferFor(surface->server, surface->currentBuffer)) {
      if (dmabufBuffer->width > 0 && dmabufBuffer->height > 0 && !dmabufBuffer->planes.empty()) {
        surface->rgbaPixels.reset();
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
        bool const matchedConfigureCommit = clearMatchedConfigureCommit(surface);
        applyLayerGeometry(surface->layerSurface);
        maybeSendInitialCutoutsConfigure(surface->server, surface);
        surface->dmabufBuffer = dmabufBuffer;
        bumpSurfaceSerial(surface);
        surface->lastCommitNsec = flux::detail::resizeTraceTimestampNanoseconds();
        traceResizeSurface("commit-dmabuf", surface);
        if (matchedConfigureCommit) {
          sendPendingResizeConfigureIfNeeded(surface);
        }
        traceCrashSurfaceCommit(surface, "dmabuf", 2u, dmabufBuffer->format);
      }
    } else {
      releaseCurrentBufferImmediately = true;
    }
    if (releaseCurrentBufferImmediately) wl_buffer_send_release(surface->currentBuffer);
  } else {
    surface->rgbaPixels.reset();
    surface->pixelFormat = Image::PixelFormat::Rgba8888;
    surface->rgbaFullyOpaque = false;
    surface->width = 0;
    surface->height = 0;
    setConfiguredFrameSize(surface, 0, 0);
    surface->awaitingConfigureCommit = false;
    surface->awaitingConfigureWidth = 0;
    surface->awaitingConfigureHeight = 0;
    surface->dmabufBuffer = nullptr;
    bumpSurfaceSerial(surface);
    traceResizeSurface("commit-empty", surface);
    traceCrashSurfaceCommit(surface, "empty", 0u, 0u);
  }
  clearPendingDamage(surface);
}

void surfaceSetBufferScale(wl_client*, wl_resource* resource, std::int32_t scale) {
  auto* surface = resourceData<WaylandServer::Impl::Surface>(resource);
  if (scale <= 0) {
    wl_resource_post_error(resource, WL_SURFACE_ERROR_INVALID_SCALE, "invalid wl_surface buffer scale");
    return;
  }
  surface->pendingScale = scale;
  surface->pendingScaleSet = true;
}

void surfaceSetBufferTransform(wl_client*, wl_resource* resource, std::int32_t transform) {
  auto* surface = resourceData<WaylandServer::Impl::Surface>(resource);
  if (!wl_output_transform_is_valid(static_cast<std::uint32_t>(transform), 1)) {
    wl_resource_post_error(resource, WL_SURFACE_ERROR_INVALID_TRANSFORM, "invalid wl_surface buffer transform");
    return;
  }
  surface->pendingBufferTransform = transform;
  surface->pendingBufferTransformSet = true;
}

void surfaceDamageBuffer(wl_client*, wl_resource* resource, std::int32_t x, std::int32_t y,
                         std::int32_t width, std::int32_t height) {
  auto* surface = resourceData<WaylandServer::Impl::Surface>(resource);
  if (!surface) return;
  appendRegionRect(surface->pendingBufferDamageRects, normalizedRegionRect(x, y, width, height));
}

void surfaceOffset(wl_client*, wl_resource* resource, std::int32_t x, std::int32_t y) {
  auto* surface = resourceData<WaylandServer::Impl::Surface>(resource);
  surface->pendingBufferOffsetX = x;
  surface->pendingBufferOffsetY = y;
  surface->pendingBufferOffsetSet = true;
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
             : surface ? std::max(0, transformedBufferWidth(surface) / std::max(1, surface->scale)) : 0;
}

std::int32_t displayHeight(WaylandServer::Impl::Surface const* surface) {
  return surface && surface->frameHeight > 0
             ? surface->frameHeight
             : surface ? std::max(0, transformedBufferHeight(surface) / std::max(1, surface->scale)) : 0;
}

void traceResizeSurface(char const* event, WaylandServer::Impl::Surface const* surface) {
  if (!surface) return;
  flux::detail::resizeTrace(
      "compositor",
      "%s surface=%llu window=%d,%d frame=%dx%d activeSizing=%d awaiting=%d %dx%d buffer=%dx%d scale=%d "
      "source=%d %.1f,%.1f %.1fx%.1f dest=%d %dx%d serial=%llu configureSerial=%u configure=%dx%d "
      "snapped=%d maximized=%d anim=%d\n",
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
      surface->scale,
      surface->sourceSet ? 1 : 0,
      surface->sourceX,
      surface->sourceY,
      surface->sourceWidth,
      surface->sourceHeight,
      surface->destinationSet ? 1 : 0,
      surface->destinationWidth,
      surface->destinationHeight,
      static_cast<unsigned long long>(surface->serial),
      surface->lastConfigureSerial,
      surface->lastConfigureWidth,
      surface->lastConfigureHeight,
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

} // namespace flux::compositor
