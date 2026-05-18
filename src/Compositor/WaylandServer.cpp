#include "Compositor/WaylandServer.hpp"

#include "Compositor/Wayland/Globals/LinuxDmabuf.hpp"
#include "Compositor/Wayland/Globals/Output.hpp"
#include "Compositor/Wayland/Globals/Seat.hpp"
#include "Compositor/Wayland/Globals/Shm.hpp"
#include "Compositor/Wayland/Globals/XdgOutput.hpp"
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
constexpr std::int32_t kResizeGripSize = 14;
constexpr std::int32_t kSnapEdgeThreshold = kCompositorSnapEdgeThreshold;
constexpr std::int32_t kMinWindowWidth = kCompositorMinWindowWidth;
constexpr std::int32_t kMinWindowHeight = kCompositorMinWindowHeight;
constexpr std::uint32_t kGeometryAnimationMs = 180;
constexpr std::uint32_t kInvalidModifierIndex = ~0u;
constexpr std::int32_t kCloseButtonSize = 18;
constexpr std::int32_t kCloseButtonInset = 5;

void inertDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

extern struct zxdg_toplevel_decoration_v1_interface const xdgToplevelDecorationImpl;
extern struct xdg_surface_interface const xdgSurfaceImpl;
extern struct xdg_toplevel_interface const xdgToplevelImpl;
extern struct wp_viewport_interface const viewportImpl;
extern struct wp_fractional_scale_v1_interface const fractionalScaleImpl;
extern struct wp_cursor_shape_device_v1_interface const cursorShapeDeviceImpl;
extern struct zwlr_layer_surface_v1_interface const layerSurfaceImpl;
extern struct wp_presentation_interface const presentationImpl;
extern struct zwp_locked_pointer_v1_interface const lockedPointerImpl;
extern struct zwp_confined_pointer_v1_interface const confinedPointerImpl;
extern struct zwp_primary_selection_source_v1_interface const primarySelectionSourceImpl;
extern struct zwp_primary_selection_device_v1_interface const primarySelectionDeviceImpl;
extern struct zwp_primary_selection_offer_v1_interface const primarySelectionOfferImpl;
extern struct wl_data_source_interface const dataSourceImpl;
extern struct wl_data_device_interface const dataDeviceImpl;
extern struct wl_data_offer_interface const dataOfferImpl;
extern struct zwp_relative_pointer_v1_interface const relativePointerImpl;

} // namespace

namespace {

void applyLayerGeometry(WaylandServer::Impl::LayerSurface* layerSurface);
void sendLayerConfigure(WaylandServer::Impl::LayerSurface* layerSurface);

void bufferDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

struct wl_buffer_interface const bufferImpl{bufferDestroy};

void compositorCreateSurface(wl_client* client, wl_resource* resource, std::uint32_t id) {
  WaylandServer::Impl* server = serverFrom(resource);
  server->createSurface(client, wl_resource_get_version(resource), id);
}

void compositorCreateRegion(wl_client* client, wl_resource*, std::uint32_t id) {
  static struct wl_region_interface const regionImpl{
      inertDestroy,
      [](wl_client*, wl_resource*, std::int32_t, std::int32_t, std::int32_t, std::int32_t) {},
      [](wl_client*, wl_resource*, std::int32_t, std::int32_t, std::int32_t, std::int32_t) {},
  };
  wl_resource* region = wl_resource_create(client, &wl_region_interface, 1, id);
  wl_resource_set_implementation(region, &regionImpl, nullptr, nullptr);
}

struct wl_compositor_interface const compositorImpl{
    .create_surface = compositorCreateSurface,
    .create_region = compositorCreateRegion,
    .release = inertDestroy,
};

void surfaceDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void surfaceAttach(wl_client*, wl_resource* resource, wl_resource* buffer, std::int32_t x, std::int32_t y) {
  auto* surface = resourceData<WaylandServer::Impl::Surface>(resource);
  surface->pendingBuffer = buffer;
  surface->pendingBufferAttached = true;
  surface->x = x;
  surface->y = y;
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

bool copyShmBufferToRgba(WaylandServer::Impl::ShmBuffer const& buffer, std::vector<std::uint8_t>& out) {
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
  auto const* base = static_cast<std::uint8_t const*>(buffer.data) + buffer.offset;
  for (std::int32_t y = 0; y < buffer.height; ++y) {
    auto const* src = base + static_cast<std::size_t>(y) * static_cast<std::size_t>(buffer.stride);
    auto* dst = out.data() + static_cast<std::size_t>(y) * rowBytes;
    for (std::int32_t x = 0; x < buffer.width; ++x) {
      // wl_shm ARGB/XRGB are little-endian words, so memory order is B, G, R, A/X.
      dst[static_cast<std::size_t>(x) * 4u + 0u] = src[static_cast<std::size_t>(x) * 4u + 2u];
      dst[static_cast<std::size_t>(x) * 4u + 1u] = src[static_cast<std::size_t>(x) * 4u + 1u];
      dst[static_cast<std::size_t>(x) * 4u + 2u] = src[static_cast<std::size_t>(x) * 4u + 0u];
      dst[static_cast<std::size_t>(x) * 4u + 3u] =
          buffer.format == WL_SHM_FORMAT_XRGB8888 ? 255u : src[static_cast<std::size_t>(x) * 4u + 3u];
    }
  }
  return true;
}

bool isIntegerSize(float value) {
  return std::floor(value) == value;
}

void setConfiguredFrameSize(WaylandServer::Impl::Surface* surface, std::int32_t width, std::int32_t height) {
  if (!surface) return;
  surface->frameWidth = width;
  surface->frameHeight = height;
}

void traceResizeSurface(char const* event, WaylandServer::Impl::Surface const* surface) {
  if (!surface) return;
  flux::detail::resizeTrace(
      "compositor",
      "%s surface=%llu window=%d,%d frame=%dx%d activeSizing=%d buffer=%dx%d "
      "source=%d %.1f,%.1f %.1fx%.1f dest=%d %dx%d serial=%llu snapped=%d maximized=%d anim=%d\n",
      event,
      static_cast<unsigned long long>(surface->id),
      surface->windowX,
      surface->windowY,
      surface->frameWidth,
      surface->frameHeight,
      (surface->server->resizeSurface_ == surface || surface->geometryAnimationActive) ? 1 : 0,
      surface->width,
      surface->height,
      surface->sourceSet ? 1 : 0,
      surface->sourceX,
      surface->sourceY,
      surface->sourceWidth,
      surface->sourceHeight,
      surface->destinationSet ? 1 : 0,
      surface->destinationWidth,
      surface->destinationHeight,
      static_cast<unsigned long long>(surface->serial),
      surface->snapped ? 1 : 0,
      surface->maximized ? 1 : 0,
      surface->geometryAnimationActive ? 1 : 0);
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

  if (surface->server->resizeSurface_ == surface || surface->geometryAnimationActive) {
    if (surface->frameWidth <= 0 || surface->frameHeight <= 0) {
      if (surface->server->resizeSurface_ == surface &&
          surface->server->resizeLastWidth_ > 0 &&
          surface->server->resizeLastHeight_ > 0) {
        setConfiguredFrameSize(surface, surface->server->resizeLastWidth_, surface->server->resizeLastHeight_);
      } else if (surface->geometryAnimationActive &&
                 surface->geometryAnimationTargetWidth > 0 &&
                 surface->geometryAnimationTargetHeight > 0) {
        setConfiguredFrameSize(surface,
                               surface->geometryAnimationTargetWidth,
                               surface->geometryAnimationTargetHeight);
      }
    }
    return true;
  }

  if (surface->destinationSet) {
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
    surface->frameWidth = surface->width;
    surface->frameHeight = surface->height;
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

  if (!hasBufferAttach) {
    if (surface->pendingSourceSet || surface->pendingDestinationSet) {
      traceResizeSurface("commit-state-defer-viewport", surface);
    } else if (pendingViewportSourceFitsCurrentBuffer(surface)) {
      if (!applyViewportState(surface)) return;
      applyLayerGeometry(surface->layerSurface);
      traceResizeSurface("commit-state", surface);
    }
    return;
  }

  surface->currentBuffer = surface->pendingBuffer;
  surface->pendingBuffer = nullptr;
  surface->pendingBufferAttached = false;
  if (surface->currentBuffer) {
    if (auto* shmBuffer = shmBufferFor(surface->server, surface->currentBuffer)) {
      std::vector<std::uint8_t> pixels;
      if (copyShmBufferToRgba(*shmBuffer, pixels)) {
        surface->rgbaPixels = std::move(pixels);
        surface->width = shmBuffer->width;
        surface->height = shmBuffer->height;
        if (!applyViewportState(surface)) return;
        applyLayerGeometry(surface->layerSurface);
        surface->dmabufBuffer = nullptr;
        ++surface->serial;
        traceResizeSurface("commit-shm", surface);
      }
    } else if (auto* dmabufBuffer = dmabufBufferFor(surface->server, surface->currentBuffer)) {
      if (dmabufBuffer->width > 0 && dmabufBuffer->height > 0 && !dmabufBuffer->planes.empty()) {
        surface->rgbaPixels.clear();
        surface->width = dmabufBuffer->width;
        surface->height = dmabufBuffer->height;
        if (!applyViewportState(surface)) return;
        applyLayerGeometry(surface->layerSurface);
        surface->dmabufBuffer = dmabufBuffer;
        ++surface->serial;
        traceResizeSurface("commit-dmabuf", surface);
        std::fprintf(stderr,
                     "flux-compositor: received %dx%d DMABUF buffer format=0x%08x stride=%u modifier=0x%016llx\n",
                     dmabufBuffer->width,
                     dmabufBuffer->height,
                     dmabufBuffer->format,
                     dmabufBuffer->planes.front().stride,
                     static_cast<unsigned long long>(dmabufBuffer->planes.front().modifier));
      }
    }
    wl_buffer_send_release(surface->currentBuffer);
  } else {
    surface->rgbaPixels.clear();
    surface->width = 0;
    surface->height = 0;
    setConfiguredFrameSize(surface, 0, 0);
    surface->dmabufBuffer = nullptr;
    ++surface->serial;
    traceResizeSurface("commit-empty", surface);
  }
}

void surfaceSetBufferScale(wl_client*, wl_resource* resource, std::int32_t scale) {
  auto* surface = resourceData<WaylandServer::Impl::Surface>(resource);
  surface->scale = std::max(1, scale);
}

struct wl_surface_interface const surfaceImpl{
    .destroy = surfaceDestroy,
    .attach = surfaceAttach,
    .damage = [](wl_client*, wl_resource*, std::int32_t, std::int32_t, std::int32_t, std::int32_t) {},
    .frame = surfaceFrame,
    .set_opaque_region = [](wl_client*, wl_resource*, wl_resource*) {},
    .set_input_region = [](wl_client*, wl_resource*, wl_resource*) {},
    .commit = surfaceCommit,
    .set_buffer_transform = [](wl_client*, wl_resource*, std::int32_t) {},
    .set_buffer_scale = surfaceSetBufferScale,
    .damage_buffer = [](wl_client*, wl_resource*, std::int32_t, std::int32_t, std::int32_t, std::int32_t) {},
    .offset = [](wl_client*, wl_resource*, std::int32_t, std::int32_t) {},
    .get_release = surfaceFrame,
};

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

WaylandServer::Impl::ToplevelDecoration* decorationFor(WaylandServer::Impl* server, WaylandServer::Impl::XdgToplevel* toplevel) {
  auto found = std::find_if(server->toplevelDecorations_.begin(), server->toplevelDecorations_.end(),
                            [toplevel](auto const& decoration) { return decoration->toplevel == toplevel; });
  return found == server->toplevelDecorations_.end() ? nullptr : found->get();
}

WaylandServer::Impl::XdgToplevel* toplevelForSurface(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface) {
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
  if (!toplevel || !toplevel->resource || !toplevel->xdgSurface || !toplevel->xdgSurface->resource) return;
  wl_array states;
  wl_array_init(&states);
  xdg_toplevel_send_configure(toplevel->resource, width, height, &states);
  wl_array_release(&states);
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

std::int32_t displayWidth(WaylandServer::Impl::Surface const* surface) {
  return surface && surface->frameWidth > 0 ? surface->frameWidth : surface ? surface->width : 0;
}

std::int32_t displayHeight(WaylandServer::Impl::Surface const* surface) {
  return surface && surface->frameHeight > 0 ? surface->frameHeight : surface ? surface->height : 0;
}

WindowGeometry windowGeometryFor(WaylandServer::Impl::Surface const* surface) {
  return {
      .x = surface ? surface->windowX : 0,
      .y = surface ? surface->windowY : 0,
      .width = displayWidth(surface),
      .height = displayHeight(surface),
  };
}

OutputGeometry outputGeometryFor(WaylandServer::Impl const* server) {
  return {
      .width = server ? server->output_.width : 0,
      .height = server ? server->output_.height : 0,
  };
}

ResizeEdge resizeEdgesFromXdg(std::uint32_t edges) {
  switch (edges) {
  case XDG_TOPLEVEL_RESIZE_EDGE_TOP:
    return ResizeEdge::Top;
  case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM:
    return ResizeEdge::Bottom;
  case XDG_TOPLEVEL_RESIZE_EDGE_LEFT:
    return ResizeEdge::Left;
  case XDG_TOPLEVEL_RESIZE_EDGE_RIGHT:
    return ResizeEdge::Right;
  case XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT:
    return ResizeEdge::Top | ResizeEdge::Left;
  case XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT:
    return ResizeEdge::Top | ResizeEdge::Right;
  case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT:
    return ResizeEdge::Bottom | ResizeEdge::Left;
  case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT:
    return ResizeEdge::Bottom | ResizeEdge::Right;
  default:
    return ResizeEdge::None;
  }
}

bool hasAnchor(std::uint32_t anchor, std::uint32_t edge) {
  return (anchor & edge) != 0;
}

std::uint32_t monotonicMilliseconds() {
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<std::uint32_t>(static_cast<std::uint64_t>(now.tv_sec) * 1000ull +
                                    static_cast<std::uint64_t>(now.tv_nsec) / 1'000'000ull);
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

void applyLayerGeometry(WaylandServer::Impl::LayerSurface* layerSurface) {
  if (!layerSurface || !layerSurface->surface) return;
  auto* surface = layerSurface->surface;
  std::int32_t const width = displayWidth(surface);
  std::int32_t const height = displayHeight(surface);
  if (width <= 0 || height <= 0) return;

  if (hasAnchor(layerSurface->anchor, ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT)) {
    surface->windowX = layerSurface->marginLeft;
  } else if (hasAnchor(layerSurface->anchor, ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT)) {
    surface->windowX = layerSurface->server->output_.width - width - layerSurface->marginRight;
  } else {
    surface->windowX = (layerSurface->server->output_.width - width) / 2;
  }

  if (hasAnchor(layerSurface->anchor, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP)) {
    surface->windowY = layerSurface->marginTop;
  } else if (hasAnchor(layerSurface->anchor, ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM)) {
    surface->windowY = layerSurface->server->output_.height - height - layerSurface->marginBottom;
  } else {
    surface->windowY = (layerSurface->server->output_.height - height) / 2;
  }
}

void sendLayerConfigure(WaylandServer::Impl::LayerSurface* layerSurface) {
  if (!layerSurface || !layerSurface->resource) return;
  std::uint32_t width = layerSurface->width;
  std::uint32_t height = layerSurface->height;
  if (width == 0 &&
      hasAnchor(layerSurface->anchor, ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT) &&
      hasAnchor(layerSurface->anchor, ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT)) {
    width = static_cast<std::uint32_t>(
        std::max(1, layerSurface->server->output_.width - layerSurface->marginLeft - layerSurface->marginRight));
  }
  if (height == 0 &&
      hasAnchor(layerSurface->anchor, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP) &&
      hasAnchor(layerSurface->anchor, ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM)) {
    height = static_cast<std::uint32_t>(
        std::max(1, layerSurface->server->output_.height - layerSurface->marginTop - layerSurface->marginBottom));
  }
  zwlr_layer_surface_v1_send_configure(layerSurface->resource,
                                       layerSurface->server->nextConfigureSerial_++,
                                       width,
                                       height);
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

void xdgDecorationManagerGetToplevelDecoration(wl_client* client, wl_resource* resource, std::uint32_t id,
                                               wl_resource* toplevelResource) {
  auto* server = serverFrom(resource);
  auto* toplevel = resourceData<WaylandServer::Impl::XdgToplevel>(toplevelResource);
  if (decorationFor(server, toplevel)) {
    wl_resource_post_error(resource, ZXDG_TOPLEVEL_DECORATION_V1_ERROR_ALREADY_CONSTRUCTED,
                           "xdg_toplevel already has a decoration object");
    return;
  }

  auto decoration = std::make_unique<WaylandServer::Impl::ToplevelDecoration>();
  decoration->server = server;
  decoration->toplevel = toplevel;
  wl_resource* decorationResource = wl_resource_create(client, &zxdg_toplevel_decoration_v1_interface,
                                                       wl_resource_get_version(resource), id);
  if (!decorationResource) {
    wl_client_post_no_memory(client);
    return;
  }
  decoration->resource = decorationResource;
  auto* raw = decoration.get();
  server->toplevelDecorations_.push_back(std::move(decoration));
  wl_resource_set_implementation(decorationResource, &xdgToplevelDecorationImpl, raw,
                                 destroyResourceCallback<WaylandServer::Impl::ToplevelDecoration, WaylandServer::Impl, &WaylandServer::Impl::destroyToplevelDecoration>);
  sendDecorationConfigure(raw);
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
  decoration->mode = ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
  sendDecorationConfigure(decoration);
}

void xdgToplevelDecorationUnsetMode(wl_client*, wl_resource* resource) {
  auto* decoration = resourceData<WaylandServer::Impl::ToplevelDecoration>(resource);
  decoration->mode = ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
  sendDecorationConfigure(decoration);
}

struct zxdg_toplevel_decoration_v1_interface const xdgToplevelDecorationImpl{
    .destroy = xdgToplevelDecorationDestroy,
    .set_mode = xdgToplevelDecorationSetMode,
    .unset_mode = xdgToplevelDecorationUnsetMode,
};

void xdgWmBaseDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void xdgWmBaseCreatePositioner(wl_client* client, wl_resource* resource, std::uint32_t id) {
  static struct xdg_positioner_interface const positionerImpl{
      .destroy = inertDestroy,
      .set_size = [](wl_client*, wl_resource*, std::int32_t, std::int32_t) {},
      .set_anchor_rect = [](wl_client*, wl_resource*, std::int32_t, std::int32_t, std::int32_t, std::int32_t) {},
      .set_anchor = [](wl_client*, wl_resource*, std::uint32_t) {},
      .set_gravity = [](wl_client*, wl_resource*, std::uint32_t) {},
      .set_constraint_adjustment = [](wl_client*, wl_resource*, std::uint32_t) {},
      .set_offset = [](wl_client*, wl_resource*, std::int32_t, std::int32_t) {},
      .set_reactive = [](wl_client*, wl_resource*) {},
      .set_parent_size = [](wl_client*, wl_resource*, std::int32_t, std::int32_t) {},
      .set_parent_configure = [](wl_client*, wl_resource*, std::uint32_t) {},
  };
  (void)resource;
  wl_resource* positioner = wl_resource_create(client, &xdg_positioner_interface, 6, id);
  wl_resource_set_implementation(positioner, &positionerImpl, nullptr, nullptr);
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
  xdgSurface->resource = xdgResource;
  auto* raw = xdgSurface.get();
  server->xdgSurfaces_.push_back(std::move(xdgSurface));
  wl_resource_set_implementation(xdgResource, &xdgSurfaceImpl, raw, destroyResourceCallback<WaylandServer::Impl::XdgSurface, WaylandServer::Impl, &WaylandServer::Impl::destroyXdgSurface>);
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
  if (xdgSurface->surface->layerSurface) {
    wl_resource_post_error(resource, XDG_WM_BASE_ERROR_ROLE, "wl_surface already has a layer-shell role");
    return;
  }
  auto toplevel = std::make_unique<WaylandServer::Impl::XdgToplevel>();
  toplevel->server = xdgSurface->server;
  toplevel->xdgSurface = xdgSurface;
  xdgSurface->surface->toplevel = true;
  xdgSurface->surface->cursor = false;
  if (xdgSurface->server->cursorSurface_ == xdgSurface->surface) xdgSurface->server->cursorSurface_ = nullptr;
  xdgSurface->surface->windowX = 80 + static_cast<std::int32_t>(xdgSurface->server->toplevels_.size()) * 36;
  xdgSurface->surface->windowY = 80 + static_cast<std::int32_t>(xdgSurface->server->toplevels_.size()) * 36;
  wl_resource* toplevelResource = wl_resource_create(client, &xdg_toplevel_interface,
                                                     wl_resource_get_version(resource), id);
  toplevel->resource = toplevelResource;
  auto* raw = toplevel.get();
  xdgSurface->server->toplevels_.push_back(std::move(toplevel));
  wl_resource_set_implementation(toplevelResource, &xdgToplevelImpl, raw, destroyResourceCallback<WaylandServer::Impl::XdgToplevel, WaylandServer::Impl, &WaylandServer::Impl::destroyXdgToplevel>);
  wl_array states;
  wl_array_init(&states);
  xdg_toplevel_send_configure(toplevelResource, 0, 0, &states);
  wl_array_release(&states);
  xdg_surface_send_configure(resource, xdgSurface->server->nextConfigureSerial_++);
}

void xdgSurfaceGetPopup(wl_client* client, wl_resource* resource, std::uint32_t id, wl_resource*, wl_resource*) {
  wl_resource_post_error(resource, XDG_WM_BASE_ERROR_INVALID_POSITIONER,
                         "xdg_popup is not implemented in phase 2");
  (void)client;
  (void)id;
}

void xdgSurfaceAckConfigure(wl_client*, wl_resource* resource, std::uint32_t) {
  resourceData<WaylandServer::Impl::XdgSurface>(resource)->configured = true;
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

struct xdg_toplevel_interface const xdgToplevelImpl{
    .destroy = xdgToplevelDestroy,
    .set_parent = [](wl_client*, wl_resource*, wl_resource*) {},
    .set_title = xdgToplevelSetTitle,
    .set_app_id = xdgToplevelSetAppId,
    .show_window_menu = [](wl_client*, wl_resource*, wl_resource*, std::uint32_t, std::int32_t, std::int32_t) {},
    .move = [](wl_client*, wl_resource*, wl_resource*, std::uint32_t) {},
    .resize = xdgToplevelResize,
    .set_max_size = [](wl_client*, wl_resource*, std::int32_t, std::int32_t) {},
    .set_min_size = [](wl_client*, wl_resource*, std::int32_t, std::int32_t) {},
    .set_maximized = [](wl_client*, wl_resource*) {},
    .unset_maximized = [](wl_client*, wl_resource*) {},
    .set_fullscreen = [](wl_client*, wl_resource*, wl_resource*) {},
    .unset_fullscreen = [](wl_client*, wl_resource*) {},
    .set_minimized = [](wl_client*, wl_resource*) {},
};

void bindCompositor(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  wl_resource* resource = wl_resource_create(client, &wl_compositor_interface, std::min(version, 5u), id);
  wl_resource_set_implementation(resource, &compositorImpl, data, nullptr);
}

void bindXdgWmBase(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  wl_resource* resource = wl_resource_create(client, &xdg_wm_base_interface, std::min(version, 6u), id);
  wl_resource_set_implementation(resource, &xdgWmBaseImpl, data, nullptr);
}

void bindXdgDecorationManager(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  wl_resource* resource = wl_resource_create(client, &zxdg_decoration_manager_v1_interface,
                                             std::min(version, 1u), id);
  wl_resource_set_implementation(resource, &xdgDecorationManagerImpl, data, nullptr);
}

void viewporterDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void viewportDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void viewportSetSource(wl_client*, wl_resource* resource, wl_fixed_t x, wl_fixed_t y,
                       wl_fixed_t width, wl_fixed_t height) {
  auto* viewport = resourceData<WaylandServer::Impl::Viewport>(resource);
  if (!viewport || !viewport->surface) {
    wl_resource_post_error(resource, WP_VIEWPORT_ERROR_NO_SURFACE, "viewport surface was destroyed");
    return;
  }

  bool const unset = x == wl_fixed_from_int(-1) && y == wl_fixed_from_int(-1) &&
                     width == wl_fixed_from_int(-1) && height == wl_fixed_from_int(-1);
  if (unset) {
    viewport->surface->pendingSourceSet = false;
    viewport->surface->pendingSourceX = 0.f;
    viewport->surface->pendingSourceY = 0.f;
    viewport->surface->pendingSourceWidth = 0.f;
    viewport->surface->pendingSourceHeight = 0.f;
    return;
  }

  double const sourceX = wl_fixed_to_double(x);
  double const sourceY = wl_fixed_to_double(y);
  double const sourceWidth = wl_fixed_to_double(width);
  double const sourceHeight = wl_fixed_to_double(height);
  if (sourceX < 0.0 || sourceY < 0.0 || sourceWidth <= 0.0 || sourceHeight <= 0.0) {
    wl_resource_post_error(resource, WP_VIEWPORT_ERROR_BAD_VALUE, "invalid viewport source rectangle");
    return;
  }

  viewport->surface->pendingSourceSet = true;
  viewport->surface->pendingSourceX = static_cast<float>(sourceX);
  viewport->surface->pendingSourceY = static_cast<float>(sourceY);
  viewport->surface->pendingSourceWidth = static_cast<float>(sourceWidth);
  viewport->surface->pendingSourceHeight = static_cast<float>(sourceHeight);
}

void viewportSetDestination(wl_client*, wl_resource* resource, std::int32_t width, std::int32_t height) {
  auto* viewport = resourceData<WaylandServer::Impl::Viewport>(resource);
  if (!viewport || !viewport->surface) {
    wl_resource_post_error(resource, WP_VIEWPORT_ERROR_NO_SURFACE, "viewport surface was destroyed");
    return;
  }

  if (width == -1 && height == -1) {
    viewport->surface->pendingDestinationSet = false;
    viewport->surface->pendingDestinationWidth = 0;
    viewport->surface->pendingDestinationHeight = 0;
    return;
  }
  if (width <= 0 || height <= 0) {
    wl_resource_post_error(resource, WP_VIEWPORT_ERROR_BAD_VALUE, "invalid viewport destination size");
    return;
  }

  viewport->surface->pendingDestinationSet = true;
  viewport->surface->pendingDestinationWidth = width;
  viewport->surface->pendingDestinationHeight = height;
}

struct wp_viewport_interface const viewportImpl{
    .destroy = viewportDestroy,
    .set_source = viewportSetSource,
    .set_destination = viewportSetDestination,
};

void viewporterGetViewport(wl_client* client, wl_resource* resource, std::uint32_t id,
                           wl_resource* surfaceResource) {
  auto* server = serverFrom(resource);
  auto* surface = resourceData<WaylandServer::Impl::Surface>(surfaceResource);
  if (!surface) {
    wl_resource_post_error(resource, WP_VIEWPORT_ERROR_NO_SURFACE, "viewport surface was destroyed");
    return;
  }
  if (surface->viewport) {
    wl_resource_post_error(resource, WP_VIEWPORTER_ERROR_VIEWPORT_EXISTS, "surface already has a viewport");
    return;
  }

  auto viewport = std::make_unique<WaylandServer::Impl::Viewport>();
  viewport->server = server;
  viewport->surface = surface;
  wl_resource* viewportResource = wl_resource_create(client, &wp_viewport_interface, 1, id);
  if (!viewportResource) {
    wl_client_post_no_memory(client);
    return;
  }
  viewport->resource = viewportResource;
  auto* raw = viewport.get();
  surface->viewport = raw;
  server->viewports_.push_back(std::move(viewport));
  wl_resource_set_implementation(viewportResource, &viewportImpl, raw, destroyResourceCallback<WaylandServer::Impl::Viewport, WaylandServer::Impl, &WaylandServer::Impl::destroyViewport>);
}

struct wp_viewporter_interface const viewporterImpl{
    .destroy = viewporterDestroy,
    .get_viewport = viewporterGetViewport,
};

void bindViewporter(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  wl_resource* resource = wl_resource_create(client, &wp_viewporter_interface, std::min(version, 1u), id);
  wl_resource_set_implementation(resource, &viewporterImpl, data, nullptr);
}

void fractionalScaleManagerDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void fractionalScaleDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

struct wp_fractional_scale_v1_interface const fractionalScaleImpl{
    .destroy = fractionalScaleDestroy,
};

void fractionalScaleManagerGetFractionalScale(wl_client* client, wl_resource* resource, std::uint32_t id,
                                              wl_resource* surfaceResource) {
  auto* server = serverFrom(resource);
  auto* surface = resourceData<WaylandServer::Impl::Surface>(surfaceResource);
  if (!surface) {
    wl_resource_post_error(resource,
                           WP_FRACTIONAL_SCALE_MANAGER_V1_ERROR_FRACTIONAL_SCALE_EXISTS,
                           "fractional scale surface was destroyed");
    return;
  }
  if (surface->fractionalScale) {
    wl_resource_post_error(resource,
                           WP_FRACTIONAL_SCALE_MANAGER_V1_ERROR_FRACTIONAL_SCALE_EXISTS,
                           "surface already has a fractional scale object");
    return;
  }

  auto fractionalScale = std::make_unique<WaylandServer::Impl::FractionalScale>();
  fractionalScale->server = server;
  fractionalScale->surface = surface;
  wl_resource* fractionalScaleResource = wl_resource_create(client, &wp_fractional_scale_v1_interface, 1, id);
  if (!fractionalScaleResource) {
    wl_client_post_no_memory(client);
    return;
  }
  fractionalScale->resource = fractionalScaleResource;
  auto* raw = fractionalScale.get();
  surface->fractionalScale = raw;
  server->fractionalScales_.push_back(std::move(fractionalScale));
  wl_resource_set_implementation(fractionalScaleResource, &fractionalScaleImpl, raw, destroyResourceCallback<WaylandServer::Impl::FractionalScale, WaylandServer::Impl, &WaylandServer::Impl::destroyFractionalScale>);
  wp_fractional_scale_v1_send_preferred_scale(fractionalScaleResource, 120);
}

struct wp_fractional_scale_manager_v1_interface const fractionalScaleManagerImpl{
    .destroy = fractionalScaleManagerDestroy,
    .get_fractional_scale = fractionalScaleManagerGetFractionalScale,
};

void bindFractionalScaleManager(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  wl_resource* resource =
      wl_resource_create(client, &wp_fractional_scale_manager_v1_interface, std::min(version, 1u), id);
  wl_resource_set_implementation(resource, &fractionalScaleManagerImpl, data, nullptr);
}

void cursorShapeManagerDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

CursorShape compositorCursorShape(std::uint32_t shape) {
  switch (shape) {
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT:
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_VERTICAL_TEXT:
    return CursorShape::IBeam;
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER:
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRAB:
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRABBING:
    return CursorShape::Hand;
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CROSSHAIR:
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CELL:
    return CursorShape::Crosshair;
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_E_RESIZE:
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_W_RESIZE:
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_EW_RESIZE:
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_COL_RESIZE:
    return CursorShape::ResizeEW;
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_N_RESIZE:
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_S_RESIZE:
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NS_RESIZE:
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ROW_RESIZE:
    return CursorShape::ResizeNS;
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NE_RESIZE:
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_SW_RESIZE:
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NESW_RESIZE:
    return CursorShape::ResizeNESW;
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NW_RESIZE:
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_SE_RESIZE:
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NWSE_RESIZE:
    return CursorShape::ResizeNWSE;
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ALL_SCROLL:
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_MOVE:
    return CursorShape::ResizeAll;
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NO_DROP:
  case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NOT_ALLOWED:
    return CursorShape::NotAllowed;
  default:
    return CursorShape::Arrow;
  }
}

void cursorShapeDeviceDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void cursorShapeDeviceSetShape(wl_client*, wl_resource* resource, std::uint32_t serial, std::uint32_t shape) {
  auto* device = resourceData<WaylandServer::Impl::CursorShapeDevice>(resource);
  if (!device || !device->server) return;
  std::uint32_t const version = static_cast<std::uint32_t>(wl_resource_get_version(resource));
  if (!wp_cursor_shape_device_v1_shape_is_valid(shape, version)) {
    wl_resource_post_error(resource, WP_CURSOR_SHAPE_DEVICE_V1_ERROR_INVALID_SHAPE,
                           "invalid cursor shape %u", shape);
    return;
  }
  auto* server = device->server;
  if (serial != server->pointerEnterSerial_) return;
  if (!server->pointerFocus_ || !device->pointer) return;
  if (wl_resource_get_client(device->pointer) != wl_resource_get_client(server->pointerFocus_->resource)) return;

  server->cursorSurface_ = nullptr;
  server->cursorShape_ = compositorCursorShape(shape);
}

struct wp_cursor_shape_device_v1_interface const cursorShapeDeviceImpl{
    .destroy = cursorShapeDeviceDestroy,
    .set_shape = cursorShapeDeviceSetShape,
};

void cursorShapeManagerGetPointer(wl_client* client, wl_resource* resource, std::uint32_t id,
                                  wl_resource* pointer) {
  auto* server = serverFrom(resource);
  auto device = std::make_unique<WaylandServer::Impl::CursorShapeDevice>();
  device->server = server;
  device->pointer = pointer;
  wl_resource* deviceResource =
      wl_resource_create(client, &wp_cursor_shape_device_v1_interface, wl_resource_get_version(resource), id);
  if (!deviceResource) {
    wl_client_post_no_memory(client);
    return;
  }
  device->resource = deviceResource;
  auto* raw = device.get();
  server->cursorShapeDevices_.push_back(std::move(device));
  wl_resource_set_implementation(deviceResource, &cursorShapeDeviceImpl, raw, destroyResourceCallback<WaylandServer::Impl::CursorShapeDevice, WaylandServer::Impl, &WaylandServer::Impl::destroyCursorShapeDevice>);
}

struct wp_cursor_shape_manager_v1_interface const cursorShapeManagerImpl{
    .destroy = cursorShapeManagerDestroy,
    .get_pointer = cursorShapeManagerGetPointer,
    .get_tablet_tool_v2 = [](wl_client*, wl_resource*, std::uint32_t, wl_resource*) {},
};

void bindCursorShapeManager(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  wl_resource* resource =
      wl_resource_create(client, &wp_cursor_shape_manager_v1_interface, std::min(version, 2u), id);
  wl_resource_set_implementation(resource, &cursorShapeManagerImpl, data, nullptr);
}

void idleInhibitManagerDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void idleInhibitorDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

struct zwp_idle_inhibitor_v1_interface const idleInhibitorImpl{
    .destroy = idleInhibitorDestroy,
};

void idleInhibitManagerCreateInhibitor(wl_client* client,
                                       wl_resource* resource,
                                       std::uint32_t id,
                                       wl_resource* surfaceResource) {
  auto* server = serverFrom(resource);
  auto* surface = resourceData<WaylandServer::Impl::Surface>(surfaceResource);
  if (!surface) {
    wl_resource_post_error(resource, 0, "invalid idle inhibitor surface");
    return;
  }

  auto inhibitor = std::make_unique<WaylandServer::Impl::IdleInhibitor>();
  inhibitor->server = server;
  inhibitor->surface = surface;
  wl_resource* inhibitorResource = wl_resource_create(client, &zwp_idle_inhibitor_v1_interface, 1, id);
  if (!inhibitorResource) {
    wl_client_post_no_memory(client);
    return;
  }
  inhibitor->resource = inhibitorResource;
  auto* raw = inhibitor.get();
  server->idleInhibitors_.push_back(std::move(inhibitor));
  wl_resource_set_implementation(inhibitorResource, &idleInhibitorImpl, raw, destroyResourceCallback<WaylandServer::Impl::IdleInhibitor, WaylandServer::Impl, &WaylandServer::Impl::destroyIdleInhibitor>);
  std::fprintf(stderr,
               "flux-compositor: idle inhibitors active=%zu\n",
               server->idleInhibitors_.size());
}

struct zwp_idle_inhibit_manager_v1_interface const idleInhibitManagerImpl{
    .destroy = idleInhibitManagerDestroy,
    .create_inhibitor = idleInhibitManagerCreateInhibitor,
};

void bindIdleInhibitManager(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  wl_resource* resource = wl_resource_create(client, &zwp_idle_inhibit_manager_v1_interface,
                                             std::min(version, 1u), id);
  wl_resource_set_implementation(resource, &idleInhibitManagerImpl, data, nullptr);
}

void layerShellDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void layerSurfaceSetSize(wl_client*, wl_resource* resource, std::uint32_t width, std::uint32_t height) {
  auto* layerSurface = resourceData<WaylandServer::Impl::LayerSurface>(resource);
  if (!layerSurface) return;
  layerSurface->width = width;
  layerSurface->height = height;
}

void layerSurfaceSetAnchor(wl_client*, wl_resource* resource, std::uint32_t anchor) {
  auto* layerSurface = resourceData<WaylandServer::Impl::LayerSurface>(resource);
  if (!layerSurface) return;
  std::uint32_t const valid = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                             ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
  if ((anchor & ~valid) != 0) {
    wl_resource_post_error(resource, ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_ANCHOR, "invalid layer-shell anchor");
    return;
  }
  layerSurface->anchor = anchor;
}

void layerSurfaceSetExclusiveZone(wl_client*, wl_resource*, std::int32_t) {}

void layerSurfaceSetMargin(wl_client*, wl_resource* resource, std::int32_t top, std::int32_t right,
                           std::int32_t bottom, std::int32_t left) {
  auto* layerSurface = resourceData<WaylandServer::Impl::LayerSurface>(resource);
  if (!layerSurface) return;
  layerSurface->marginTop = top;
  layerSurface->marginRight = right;
  layerSurface->marginBottom = bottom;
  layerSurface->marginLeft = left;
}

void layerSurfaceSetKeyboardInteractivity(wl_client*, wl_resource*, std::uint32_t) {}

void layerSurfaceGetPopup(wl_client*, wl_resource* resource, wl_resource*) {
  wl_resource_post_error(resource, ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_SURFACE_STATE,
                         "layer-shell popups are not implemented yet");
}

void layerSurfaceAckConfigure(wl_client*, wl_resource*, std::uint32_t) {}

void layerSurfaceDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

struct zwlr_layer_surface_v1_interface const layerSurfaceImpl{
    .set_size = layerSurfaceSetSize,
    .set_anchor = layerSurfaceSetAnchor,
    .set_exclusive_zone = layerSurfaceSetExclusiveZone,
    .set_margin = layerSurfaceSetMargin,
    .set_keyboard_interactivity = layerSurfaceSetKeyboardInteractivity,
    .get_popup = layerSurfaceGetPopup,
    .ack_configure = layerSurfaceAckConfigure,
    .destroy = layerSurfaceDestroy,
};

void layerShellGetLayerSurface(wl_client* client, wl_resource* resource, std::uint32_t id,
                               wl_resource* surfaceResource, wl_resource*, std::uint32_t layer,
                               char const* nameSpace) {
  auto* server = serverFrom(resource);
  auto* surface = resourceData<WaylandServer::Impl::Surface>(surfaceResource);
  if (!surface || surface->toplevel || surface->layerSurface || surface->cursor) {
    wl_resource_post_error(resource, ZWLR_LAYER_SHELL_V1_ERROR_ROLE, "wl_surface already has a role");
    return;
  }
  if (layer > ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY) {
    wl_resource_post_error(resource, ZWLR_LAYER_SHELL_V1_ERROR_INVALID_LAYER, "invalid layer-shell layer");
    return;
  }

  auto layerSurface = std::make_unique<WaylandServer::Impl::LayerSurface>();
  layerSurface->server = server;
  layerSurface->surface = surface;
  layerSurface->layer = layer;
  layerSurface->nameSpace = nameSpace ? nameSpace : "";
  wl_resource* layerResource = wl_resource_create(client, &zwlr_layer_surface_v1_interface, 1, id);
  if (!layerResource) {
    wl_client_post_no_memory(client);
    return;
  }
  layerSurface->resource = layerResource;
  auto* raw = layerSurface.get();
  surface->layerSurface = raw;
  surface->toplevel = true;
  surface->cursor = false;
  if (server->cursorSurface_ == surface) server->cursorSurface_ = nullptr;
  server->layerSurfaces_.push_back(std::move(layerSurface));
  wl_resource_set_implementation(layerResource, &layerSurfaceImpl, raw, destroyResourceCallback<WaylandServer::Impl::LayerSurface, WaylandServer::Impl, &WaylandServer::Impl::destroyLayerSurface>);
}

struct zwlr_layer_shell_v1_interface const layerShellImpl{
    .get_layer_surface = layerShellGetLayerSurface,
    .destroy = layerShellDestroy,
};

void bindLayerShell(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  wl_resource* resource = wl_resource_create(client, &zwlr_layer_shell_v1_interface, std::min(version, 1u), id);
  wl_resource_set_implementation(resource, &layerShellImpl, data, nullptr);
}

void presentationDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void presentationFeedback(wl_client* client, wl_resource* resource, wl_resource* surfaceResource, std::uint32_t id) {
  auto* server = serverFrom(resource);
  auto* surface = resourceData<WaylandServer::Impl::Surface>(surfaceResource);
  if (!surface) {
    wl_resource_post_error(resource, 0, "invalid surface for presentation feedback");
    return;
  }

  auto feedback = std::make_unique<WaylandServer::Impl::PresentationFeedback>();
  feedback->server = server;
  feedback->surface = surface;
  wl_resource* feedbackResource = wl_resource_create(client, &wp_presentation_feedback_interface, 2, id);
  if (!feedbackResource) {
    wl_client_post_no_memory(client);
    return;
  }
  feedback->resource = feedbackResource;
  auto* raw = feedback.get();
  server->presentationFeedbacks_.push_back(std::move(feedback));
  surface->pendingPresentationFeedbacks.push_back(raw);
  wl_resource_set_implementation(feedbackResource, nullptr, raw, destroyResourceCallback<WaylandServer::Impl::PresentationFeedback, WaylandServer::Impl, &WaylandServer::Impl::destroyPresentationFeedback>);
}

struct wp_presentation_interface const presentationImpl{
    .destroy = presentationDestroy,
    .feedback = presentationFeedback,
};

void bindPresentation(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  wl_resource* resource = wl_resource_create(client, &wp_presentation_interface, std::min(version, 2u), id);
  if (!resource) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(resource, &presentationImpl, data, nullptr);
  wp_presentation_send_clock_id(resource, CLOCK_MONOTONIC);
}

void relativePointerManagerDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void relativePointerDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

struct zwp_relative_pointer_v1_interface const relativePointerImpl{
    .destroy = relativePointerDestroy,
};

void relativePointerManagerGetRelativePointer(wl_client* client,
                                              wl_resource* resource,
                                              std::uint32_t id,
                                              wl_resource* pointerResource) {
  auto* server = serverFrom(resource);
  auto relativePointer = std::make_unique<WaylandServer::Impl::RelativePointer>();
  relativePointer->server = server;
  relativePointer->pointer = pointerResource;
  wl_resource* relativePointerResource = wl_resource_create(client, &zwp_relative_pointer_v1_interface, 1, id);
  if (!relativePointerResource) {
    wl_client_post_no_memory(client);
    return;
  }
  relativePointer->resource = relativePointerResource;
  auto* raw = relativePointer.get();
  server->relativePointers_.push_back(std::move(relativePointer));
  wl_resource_set_implementation(relativePointerResource, &relativePointerImpl, raw, destroyResourceCallback<WaylandServer::Impl::RelativePointer, WaylandServer::Impl, &WaylandServer::Impl::destroyRelativePointer>);
}

struct zwp_relative_pointer_manager_v1_interface const relativePointerManagerImpl{
    .destroy = relativePointerManagerDestroy,
    .get_relative_pointer = relativePointerManagerGetRelativePointer,
};

void bindRelativePointerManager(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  wl_resource* resource =
      wl_resource_create(client, &zwp_relative_pointer_manager_v1_interface, std::min(version, 1u), id);
  if (!resource) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(resource, &relativePointerManagerImpl, data, nullptr);
}

void updatePointerConstraintActivation(WaylandServer::Impl* server, WaylandServer::Impl::PointerConstraint* constraint) {
  if (!constraint || constraint->defunct || !constraint->resource || !constraint->surface || !constraint->pointer) return;
  bool const shouldActivate = server->pointerFocus_ == constraint->surface;
  if (shouldActivate == constraint->active) return;

  constraint->active = shouldActivate;
  if (constraint->kind == WaylandServer::Impl::PointerConstraint::Kind::Lock) {
    if (constraint->active) {
      zwp_locked_pointer_v1_send_locked(constraint->resource);
    } else {
      zwp_locked_pointer_v1_send_unlocked(constraint->resource);
    }
  } else {
    if (constraint->active) {
      zwp_confined_pointer_v1_send_confined(constraint->resource);
    } else {
      zwp_confined_pointer_v1_send_unconfined(constraint->resource);
    }
  }
  if (!constraint->active && constraint->lifetime == ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_ONESHOT) {
    constraint->defunct = true;
  }
}

void updatePointerConstraintsForFocus(WaylandServer::Impl* server) {
  for (auto const& constraint : server->pointerConstraints_) {
    updatePointerConstraintActivation(server, constraint.get());
  }
}

WaylandServer::Impl::PointerConstraint* activePointerConstraint(WaylandServer::Impl* server) {
  for (auto const& constraint : server->pointerConstraints_) {
    if (constraint->active && !constraint->defunct && constraint->surface == server->pointerFocus_) {
      return constraint.get();
    }
  }
  return nullptr;
}

bool surfaceAlreadyConstrained(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface, wl_resource* pointer) {
  if (!surface || !pointer) return false;
  wl_client* pointerClient = wl_resource_get_client(pointer);
  for (auto const& constraint : server->pointerConstraints_) {
    if (constraint->surface == surface && constraint->pointer &&
        wl_resource_get_client(constraint->pointer) == pointerClient && !constraint->defunct) {
      return true;
    }
  }
  return false;
}

void pointerConstraintsDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void lockedPointerDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void lockedPointerSetCursorPositionHint(wl_client*, wl_resource* resource, wl_fixed_t surfaceX, wl_fixed_t surfaceY) {
  auto* constraint = resourceData<WaylandServer::Impl::PointerConstraint>(resource);
  if (!constraint) return;
  constraint->cursorHintX = static_cast<float>(wl_fixed_to_double(surfaceX));
  constraint->cursorHintY = static_cast<float>(wl_fixed_to_double(surfaceY));
}

void lockedPointerSetRegion(wl_client*, wl_resource*, wl_resource*) {}

struct zwp_locked_pointer_v1_interface const lockedPointerImpl{
    .destroy = lockedPointerDestroy,
    .set_cursor_position_hint = lockedPointerSetCursorPositionHint,
    .set_region = lockedPointerSetRegion,
};

void confinedPointerDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void confinedPointerSetRegion(wl_client*, wl_resource*, wl_resource*) {}

struct zwp_confined_pointer_v1_interface const confinedPointerImpl{
    .destroy = confinedPointerDestroy,
    .set_region = confinedPointerSetRegion,
};

void createPointerConstraint(wl_client* client,
                             wl_resource* resource,
                             std::uint32_t id,
                             wl_resource* surfaceResource,
                             wl_resource* pointerResource,
                             std::uint32_t lifetime,
                             WaylandServer::Impl::PointerConstraint::Kind kind) {
  auto* server = serverFrom(resource);
  auto* surface = resourceData<WaylandServer::Impl::Surface>(surfaceResource);
  if (surfaceAlreadyConstrained(server, surface, pointerResource)) {
    wl_resource_post_error(resource,
                           ZWP_POINTER_CONSTRAINTS_V1_ERROR_ALREADY_CONSTRAINED,
                           "surface already has a pointer constraint for this seat");
    return;
  }

  wl_interface const* interface = kind == WaylandServer::Impl::PointerConstraint::Kind::Lock
                                      ? &zwp_locked_pointer_v1_interface
                                      : &zwp_confined_pointer_v1_interface;
  wl_resource* constraintResource = wl_resource_create(client, interface, 1, id);
  if (!constraintResource) {
    wl_client_post_no_memory(client);
    return;
  }

  auto constraint = std::make_unique<WaylandServer::Impl::PointerConstraint>();
  constraint->server = server;
  constraint->resource = constraintResource;
  constraint->surface = surface;
  constraint->pointer = pointerResource;
  constraint->kind = kind;
  constraint->lifetime = lifetime;
  auto* raw = constraint.get();
  server->pointerConstraints_.push_back(std::move(constraint));
  wl_resource_set_implementation(constraintResource,
                                 kind == WaylandServer::Impl::PointerConstraint::Kind::Lock
                                     ? static_cast<void const*>(&lockedPointerImpl)
                                     : static_cast<void const*>(&confinedPointerImpl),
                                 raw,
                                 destroyResourceCallback<WaylandServer::Impl::PointerConstraint, WaylandServer::Impl, &WaylandServer::Impl::destroyPointerConstraint>);
  updatePointerConstraintActivation(server, raw);
}

void pointerConstraintsLockPointer(wl_client* client,
                                   wl_resource* resource,
                                   std::uint32_t id,
                                   wl_resource* surface,
                                   wl_resource* pointer,
                                   wl_resource*,
                                   std::uint32_t lifetime) {
  createPointerConstraint(client,
                          resource,
                          id,
                          surface,
                          pointer,
                          lifetime,
                          WaylandServer::Impl::PointerConstraint::Kind::Lock);
}

void pointerConstraintsConfinePointer(wl_client* client,
                                      wl_resource* resource,
                                      std::uint32_t id,
                                      wl_resource* surface,
                                      wl_resource* pointer,
                                      wl_resource*,
                                      std::uint32_t lifetime) {
  createPointerConstraint(client,
                          resource,
                          id,
                          surface,
                          pointer,
                          lifetime,
                          WaylandServer::Impl::PointerConstraint::Kind::Confine);
}

struct zwp_pointer_constraints_v1_interface const pointerConstraintsImpl{
    .destroy = pointerConstraintsDestroy,
    .lock_pointer = pointerConstraintsLockPointer,
    .confine_pointer = pointerConstraintsConfinePointer,
};

void bindPointerConstraints(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  wl_resource* resource =
      wl_resource_create(client, &zwp_pointer_constraints_v1_interface, std::min(version, 1u), id);
  if (!resource) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(resource, &pointerConstraintsImpl, data, nullptr);
}

void primarySelectionManagerDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void primarySelectionSourceOffer(wl_client*, wl_resource* resource, char const* mimeType) {
  auto* source = resourceData<WaylandServer::Impl::PrimarySelectionSource>(resource);
  if (!source || !mimeType) return;
  if (std::find(source->mimeTypes.begin(), source->mimeTypes.end(), mimeType) == source->mimeTypes.end()) {
    source->mimeTypes.emplace_back(mimeType);
  }
}

void primarySelectionSourceDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

struct zwp_primary_selection_source_v1_interface const primarySelectionSourceImpl{
    .offer = primarySelectionSourceOffer,
    .destroy = primarySelectionSourceDestroy,
};

void primarySelectionDeviceDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void sendPrimarySelectionToDevice(WaylandServer::Impl::PrimarySelectionDevice* device) {
  if (!device || !device->resource) return;
  auto* server = device->server;
  wl_client* deviceClient = wl_resource_get_client(device->resource);
  if (!server->keyboardFocus_ || wl_resource_get_client(server->keyboardFocus_->resource) != deviceClient ||
      !server->primarySelectionSource_ || server->primarySelectionSource_->mimeTypes.empty()) {
    zwp_primary_selection_device_v1_send_selection(device->resource, nullptr);
    return;
  }

  auto offer = std::make_unique<WaylandServer::Impl::PrimarySelectionOffer>();
  offer->server = server;
  offer->source = server->primarySelectionSource_;
  wl_resource* offerResource =
      wl_resource_create(deviceClient, &zwp_primary_selection_offer_v1_interface, 1, 0);
  if (!offerResource) {
    wl_client_post_no_memory(deviceClient);
    return;
  }
  offer->resource = offerResource;
  auto* raw = offer.get();
  server->primarySelectionOffers_.push_back(std::move(offer));
  wl_resource_set_implementation(offerResource, &primarySelectionOfferImpl, raw, destroyResourceCallback<WaylandServer::Impl::PrimarySelectionOffer, WaylandServer::Impl, &WaylandServer::Impl::destroyPrimarySelectionOffer>);
  zwp_primary_selection_device_v1_send_data_offer(device->resource, offerResource);
  for (auto const& mimeType : raw->source->mimeTypes) {
    zwp_primary_selection_offer_v1_send_offer(offerResource, mimeType.c_str());
  }
  zwp_primary_selection_device_v1_send_selection(device->resource, offerResource);
}

void sendPrimarySelectionForFocus(WaylandServer::Impl* server) {
  for (auto const& device : server->primarySelectionDevices_) {
    sendPrimarySelectionToDevice(device.get());
  }
}

void primarySelectionDeviceSetSelection(wl_client*, wl_resource* resource, wl_resource* sourceResource, std::uint32_t) {
  auto* device = resourceData<WaylandServer::Impl::PrimarySelectionDevice>(resource);
  if (!device) return;
  auto* server = device->server;
  auto* source = resourceData<WaylandServer::Impl::PrimarySelectionSource>(sourceResource);
  if (server->primarySelectionSource_ && server->primarySelectionSource_ != source &&
      server->primarySelectionSource_->resource) {
    zwp_primary_selection_source_v1_send_cancelled(server->primarySelectionSource_->resource);
  }
  server->primarySelectionSource_ = source;
  sendPrimarySelectionForFocus(server);
}

struct zwp_primary_selection_device_v1_interface const primarySelectionDeviceImpl{
    .set_selection = primarySelectionDeviceSetSelection,
    .destroy = primarySelectionDeviceDestroy,
};

void primarySelectionOfferReceive(wl_client*, wl_resource* resource, char const* mimeType, int fd) {
  auto* offer = resourceData<WaylandServer::Impl::PrimarySelectionOffer>(resource);
  if (!offer || !offer->source || !offer->source->resource || !mimeType) {
    close(fd);
    return;
  }
  zwp_primary_selection_source_v1_send_send(offer->source->resource, mimeType, fd);
  close(fd);
}

void primarySelectionOfferDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

struct zwp_primary_selection_offer_v1_interface const primarySelectionOfferImpl{
    .receive = primarySelectionOfferReceive,
    .destroy = primarySelectionOfferDestroy,
};

void primarySelectionManagerCreateSource(wl_client* client, wl_resource* resource, std::uint32_t id) {
  auto* server = serverFrom(resource);
  auto source = std::make_unique<WaylandServer::Impl::PrimarySelectionSource>();
  source->server = server;
  wl_resource* sourceResource = wl_resource_create(client, &zwp_primary_selection_source_v1_interface, 1, id);
  if (!sourceResource) {
    wl_client_post_no_memory(client);
    return;
  }
  source->resource = sourceResource;
  auto* raw = source.get();
  server->primarySelectionSources_.push_back(std::move(source));
  wl_resource_set_implementation(sourceResource, &primarySelectionSourceImpl, raw, destroyResourceCallback<WaylandServer::Impl::PrimarySelectionSource, WaylandServer::Impl, &WaylandServer::Impl::destroyPrimarySelectionSource>);
}

void primarySelectionManagerGetDevice(wl_client* client,
                                      wl_resource* resource,
                                      std::uint32_t id,
                                      wl_resource* seatResource) {
  auto* server = serverFrom(resource);
  auto device = std::make_unique<WaylandServer::Impl::PrimarySelectionDevice>();
  device->server = server;
  device->seat = seatResource;
  wl_resource* deviceResource = wl_resource_create(client, &zwp_primary_selection_device_v1_interface, 1, id);
  if (!deviceResource) {
    wl_client_post_no_memory(client);
    return;
  }
  device->resource = deviceResource;
  auto* raw = device.get();
  server->primarySelectionDevices_.push_back(std::move(device));
  wl_resource_set_implementation(deviceResource, &primarySelectionDeviceImpl, raw, destroyResourceCallback<WaylandServer::Impl::PrimarySelectionDevice, WaylandServer::Impl, &WaylandServer::Impl::destroyPrimarySelectionDevice>);
  sendPrimarySelectionToDevice(raw);
}

struct zwp_primary_selection_device_manager_v1_interface const primarySelectionManagerImpl{
    .create_source = primarySelectionManagerCreateSource,
    .get_device = primarySelectionManagerGetDevice,
    .destroy = primarySelectionManagerDestroy,
};

void bindPrimarySelectionManager(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  wl_resource* resource =
      wl_resource_create(client, &zwp_primary_selection_device_manager_v1_interface, std::min(version, 1u), id);
  if (!resource) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(resource, &primarySelectionManagerImpl, data, nullptr);
}

void dataDeviceManagerDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void dataSourceOffer(wl_client*, wl_resource* resource, char const* mimeType) {
  auto* source = resourceData<WaylandServer::Impl::DataSource>(resource);
  if (!source || !mimeType) return;
  if (std::find(source->mimeTypes.begin(), source->mimeTypes.end(), mimeType) == source->mimeTypes.end()) {
    source->mimeTypes.emplace_back(mimeType);
  }
}

void dataSourceDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void dataSourceSetActions(wl_client*, wl_resource*, std::uint32_t) {}

struct wl_data_source_interface const dataSourceImpl{
    .offer = dataSourceOffer,
    .destroy = dataSourceDestroy,
    .set_actions = dataSourceSetActions,
};

void dataOfferAccept(wl_client*, wl_resource* resource, std::uint32_t, char const* mimeType) {
  auto* offer = resourceData<WaylandServer::Impl::DataOffer>(resource);
  if (!offer || !offer->source || !offer->source->resource) return;
  wl_data_source_send_target(offer->source->resource, mimeType);
}

void dataOfferReceive(wl_client*, wl_resource* resource, char const* mimeType, int fd) {
  auto* offer = resourceData<WaylandServer::Impl::DataOffer>(resource);
  if (!offer || !offer->source || !offer->source->resource || !mimeType) {
    close(fd);
    return;
  }
  wl_data_source_send_send(offer->source->resource, mimeType, fd);
  close(fd);
}

void dataOfferDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void dataOfferFinish(wl_client*, wl_resource* resource) {
  auto* offer = resourceData<WaylandServer::Impl::DataOffer>(resource);
  if (!offer || !offer->source || !offer->source->resource) return;
  if (wl_resource_get_version(offer->source->resource) >= WL_DATA_SOURCE_DND_FINISHED_SINCE_VERSION) {
    wl_data_source_send_dnd_finished(offer->source->resource);
  }
}
void dataOfferSetActions(wl_client*, wl_resource*, std::uint32_t, std::uint32_t) {}

struct wl_data_offer_interface const dataOfferImpl{
    .accept = dataOfferAccept,
    .receive = dataOfferReceive,
    .destroy = dataOfferDestroy,
    .finish = dataOfferFinish,
    .set_actions = dataOfferSetActions,
};

void sendSelectionToDataDevice(WaylandServer::Impl::DataDevice* device) {
  if (!device || !device->resource) return;
  auto* server = device->server;
  wl_client* deviceClient = wl_resource_get_client(device->resource);
  if (!server->keyboardFocus_ || wl_resource_get_client(server->keyboardFocus_->resource) != deviceClient ||
      !server->selectionSource_ || server->selectionSource_->mimeTypes.empty()) {
    wl_data_device_send_selection(device->resource, nullptr);
    return;
  }

  auto offer = std::make_unique<WaylandServer::Impl::DataOffer>();
  offer->server = server;
  offer->source = server->selectionSource_;
  wl_resource* offerResource = wl_resource_create(deviceClient, &wl_data_offer_interface, 3, 0);
  if (!offerResource) {
    wl_client_post_no_memory(deviceClient);
    return;
  }
  offer->resource = offerResource;
  auto* raw = offer.get();
  server->dataOffers_.push_back(std::move(offer));
  wl_resource_set_implementation(offerResource, &dataOfferImpl, raw, destroyResourceCallback<WaylandServer::Impl::DataOffer, WaylandServer::Impl, &WaylandServer::Impl::destroyDataOffer>);
  wl_data_device_send_data_offer(device->resource, offerResource);
  for (auto const& mimeType : raw->source->mimeTypes) {
    wl_data_offer_send_offer(offerResource, mimeType.c_str());
  }
  wl_data_device_send_selection(device->resource, offerResource);
}

void sendSelectionForFocus(WaylandServer::Impl* server) {
  for (auto const& device : server->dataDevices_) {
    sendSelectionToDataDevice(device.get());
  }
}

WaylandServer::Impl::DataDevice* dataDeviceForClient(WaylandServer::Impl* server, wl_client* client) {
  for (auto const& device : server->dataDevices_) {
    if (device->resource && wl_resource_get_client(device->resource) == client) return device.get();
  }
  return nullptr;
}

void clearDnd(WaylandServer::Impl* server) {
  if (server->dndTarget_) {
    if (auto* device = dataDeviceForClient(server, wl_resource_get_client(server->dndTarget_->resource))) {
      wl_data_device_send_leave(device->resource);
    }
  }
  if (server->dndOffer_ && server->dndOffer_->resource) {
    wl_resource_destroy(server->dndOffer_->resource);
  }
  server->dndSource_ = nullptr;
  server->dndOrigin_ = nullptr;
  server->dndTarget_ = nullptr;
  server->dndOffer_ = nullptr;
}

WaylandServer::Impl::DataOffer* createDndOffer(WaylandServer::Impl* server, wl_client* client) {
  if (!server->dndSource_ || server->dndSource_->mimeTypes.empty()) return nullptr;
  auto offer = std::make_unique<WaylandServer::Impl::DataOffer>();
  offer->server = server;
  offer->source = server->dndSource_;
  wl_resource* offerResource = wl_resource_create(client, &wl_data_offer_interface, 3, 0);
  if (!offerResource) {
    wl_client_post_no_memory(client);
    return nullptr;
  }
  offer->resource = offerResource;
  auto* raw = offer.get();
  server->dataOffers_.push_back(std::move(offer));
  wl_resource_set_implementation(offerResource, &dataOfferImpl, raw, destroyResourceCallback<WaylandServer::Impl::DataOffer, WaylandServer::Impl, &WaylandServer::Impl::destroyDataOffer>);
  return raw;
}

void updateDndTarget(WaylandServer::Impl* server, WaylandServer::Impl::Surface* target, std::uint32_t timeMs) {
  if (!server->dndSource_) return;
  if (target == server->dndOrigin_) target = nullptr;
  if (target != server->dndTarget_) {
    if (server->dndTarget_) {
      if (auto* previousDevice = dataDeviceForClient(server, wl_resource_get_client(server->dndTarget_->resource))) {
        wl_data_device_send_leave(previousDevice->resource);
      }
    }
    if (server->dndOffer_ && server->dndOffer_->resource) {
      wl_resource_destroy(server->dndOffer_->resource);
    }
    server->dndTarget_ = target;
    server->dndOffer_ = nullptr;
    if (target) {
      wl_client* targetClient = wl_resource_get_client(target->resource);
      WaylandServer::Impl::DataDevice* targetDevice = dataDeviceForClient(server, targetClient);
      if (targetDevice) {
        server->dndOffer_ = createDndOffer(server, targetClient);
        wl_resource* offerResource = server->dndOffer_ ? server->dndOffer_->resource : nullptr;
        if (offerResource) {
          wl_data_device_send_data_offer(targetDevice->resource, offerResource);
          for (auto const& mimeType : server->dndOffer_->source->mimeTypes) {
            wl_data_offer_send_offer(offerResource, mimeType.c_str());
          }
        }
        std::uint32_t const serial = server->nextInputSerial_++;
        wl_fixed_t const x = wl_fixed_from_double(server->pointerX_ - static_cast<float>(target->windowX));
        wl_fixed_t const y = wl_fixed_from_double(server->pointerY_ - static_cast<float>(target->windowY));
        wl_data_device_send_enter(targetDevice->resource, serial, target->resource, x, y, offerResource);
      }
    }
  }
  if (server->dndTarget_) {
    if (auto* device = dataDeviceForClient(server, wl_resource_get_client(server->dndTarget_->resource))) {
      wl_fixed_t const x = wl_fixed_from_double(server->pointerX_ - static_cast<float>(server->dndTarget_->windowX));
      wl_fixed_t const y = wl_fixed_from_double(server->pointerY_ - static_cast<float>(server->dndTarget_->windowY));
      wl_data_device_send_motion(device->resource, timeMs, x, y);
    }
  }
}

void dataDeviceStartDrag(wl_client* client,
                         wl_resource* resource,
                         wl_resource* sourceResource,
                         wl_resource* originResource,
                         wl_resource*,
                         std::uint32_t) {
  auto* device = resourceData<WaylandServer::Impl::DataDevice>(resource);
  if (!device) return;
  auto* server = device->server;
  auto* source = resourceData<WaylandServer::Impl::DataSource>(sourceResource);
  auto* origin = resourceData<WaylandServer::Impl::Surface>(originResource);
  if (!origin || wl_resource_get_client(origin->resource) != client) return;
  if (server->dndSource_) clearDnd(server);
  server->dndSource_ = source;
  server->dndOrigin_ = origin;
  updateDndTarget(server, surfaceAt(server, server->pointerX_, server->pointerY_), 0);
}

void dataDeviceSetSelection(wl_client*, wl_resource* resource, wl_resource* sourceResource, std::uint32_t) {
  auto* device = resourceData<WaylandServer::Impl::DataDevice>(resource);
  if (!device) return;
  auto* server = device->server;
  auto* source = resourceData<WaylandServer::Impl::DataSource>(sourceResource);
  if (server->selectionSource_ && server->selectionSource_ != source && server->selectionSource_->resource) {
    wl_data_source_send_cancelled(server->selectionSource_->resource);
  }
  server->selectionSource_ = source;
  sendSelectionForFocus(server);
}

void dataDeviceRelease(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

struct wl_data_device_interface const dataDeviceImpl{
    .start_drag = dataDeviceStartDrag,
    .set_selection = dataDeviceSetSelection,
    .release = dataDeviceRelease,
};

void dataDeviceManagerCreateDataSource(wl_client* client, wl_resource* resource, std::uint32_t id) {
  auto* server = serverFrom(resource);
  auto source = std::make_unique<WaylandServer::Impl::DataSource>();
  source->server = server;
  wl_resource* sourceResource = wl_resource_create(client, &wl_data_source_interface, 3, id);
  if (!sourceResource) {
    wl_client_post_no_memory(client);
    return;
  }
  source->resource = sourceResource;
  auto* raw = source.get();
  server->dataSources_.push_back(std::move(source));
  wl_resource_set_implementation(sourceResource, &dataSourceImpl, raw, destroyResourceCallback<WaylandServer::Impl::DataSource, WaylandServer::Impl, &WaylandServer::Impl::destroyDataSource>);
}

void dataDeviceManagerGetDataDevice(wl_client* client, wl_resource* resource, std::uint32_t id, wl_resource* seat) {
  auto* server = serverFrom(resource);
  auto device = std::make_unique<WaylandServer::Impl::DataDevice>();
  device->server = server;
  device->seat = seat;
  wl_resource* deviceResource = wl_resource_create(client, &wl_data_device_interface, 3, id);
  if (!deviceResource) {
    wl_client_post_no_memory(client);
    return;
  }
  device->resource = deviceResource;
  auto* raw = device.get();
  server->dataDevices_.push_back(std::move(device));
  wl_resource_set_implementation(deviceResource, &dataDeviceImpl, raw, destroyResourceCallback<WaylandServer::Impl::DataDevice, WaylandServer::Impl, &WaylandServer::Impl::destroyDataDevice>);
  sendSelectionToDataDevice(raw);
}

struct wl_data_device_manager_interface const dataDeviceManagerImpl{
    .create_data_source = dataDeviceManagerCreateDataSource,
    .get_data_device = dataDeviceManagerGetDataDevice,
    .release = dataDeviceManagerDestroy,
};

void bindDataDeviceManager(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  wl_resource* resource = wl_resource_create(client, &wl_data_device_manager_interface, std::min(version, 3u), id);
  if (!resource) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(resource, &dataDeviceManagerImpl, data, nullptr);
}

} // namespace

WaylandServer::Impl::Impl(WaylandOutputInfo output) : output_(std::move(output)) {
  initializeKeyboardModifierIndices(this);
  shortcutBindings_ = {
      {.action = ShortcutAction::CloseFocused, .key = KEY_Q, .meta = true},
      {.action = ShortcutAction::CycleFocus, .key = KEY_TAB, .meta = true},
      {.action = ShortcutAction::SnapLeft, .key = KEY_LEFT, .meta = true},
      {.action = ShortcutAction::SnapRight, .key = KEY_RIGHT, .meta = true},
      {.action = ShortcutAction::Terminate, .key = KEY_BACKSPACE, .ctrl = true, .alt = true},
  };

  display_ = wl_display_create();
  if (!display_) throw std::runtime_error("wl_display_create failed");

  compositorGlobal_ = wl_global_create(display_, &wl_compositor_interface, 5, this, bindCompositor);
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
  if (!compositorGlobal_ || !shmGlobal_ || !outputGlobal_ || !seatGlobal_ || !xdgWmBaseGlobal_ ||
      !linuxDmabufGlobal_ || !xdgDecorationManagerGlobal_ || !xdgOutputManagerGlobal_ || !viewporterGlobal_ ||
      !fractionalScaleManagerGlobal_ || !cursorShapeManagerGlobal_ || !idleInhibitManagerGlobal_ ||
      !layerShellGlobal_ || !presentationGlobal_ || !relativePointerManagerGlobal_ || !pointerConstraintsGlobal_ ||
      !primarySelectionManagerGlobal_ || !dataDeviceManagerGlobal_) {
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

std::size_t WaylandServer::Impl::toplevelCount() const noexcept {
  return toplevels_.size();
}

std::vector<CommittedSurfaceSnapshot> WaylandServer::Impl::committedSurfaces() const {
  std::vector<CommittedSurfaceSnapshot> snapshots;
  snapshots.reserve(surfaces_.size());
  for (auto const& surface : surfaces_) {
    if (!surface->toplevel) continue;
    if (surface->width <= 0 || surface->height <= 0) continue;
    if (surface->rgbaPixels.empty() && !surface->dmabufBuffer) continue;
    CommittedSurfaceSnapshot snapshot{
        .id = surface->id,
        .x = surface->windowX,
        .y = surface->windowY,
        .width = displayWidth(surface.get()),
        .height = displayHeight(surface.get()),
        .bufferWidth = surface->width,
        .bufferHeight = surface->height,
        .sourceX = surface->sourceSet ? surface->sourceX : 0.f,
        .sourceY = surface->sourceSet ? surface->sourceY : 0.f,
        .sourceWidth = surface->sourceSet ? surface->sourceWidth : static_cast<float>(surface->width),
        .sourceHeight = surface->sourceSet ? surface->sourceHeight : static_cast<float>(surface->height),
        .destinationWidth = surface->destinationSet ? surface->destinationWidth : displayWidth(surface.get()),
        .destinationHeight = surface->destinationSet ? surface->destinationHeight : displayHeight(surface.get()),
        .titleBarHeight = surface->layerSurface ? 0 : kTitleBarHeight,
        .title = surface->layerSurface ? std::string{} : titleForSurface(this, surface.get()),
        .focused = keyboardFocus_ == surface.get(),
        .activeSizing = resizeSurface_ == surface.get() || surface->geometryAnimationActive,
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
    snapshots.push_back(std::move(snapshot));
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

WaylandServer::Impl::Surface* surfaceAt(WaylandServer::Impl* server, float x, float y) {
  for (auto it = server->surfaces_.rbegin(); it != server->surfaces_.rend(); ++it) {
    WaylandServer::Impl::Surface* surface = it->get();
    std::int32_t const width = displayWidth(surface);
    std::int32_t const height = displayHeight(surface);
    if (!surface || !surface->toplevel || width <= 0 || height <= 0) continue;
    float const left = static_cast<float>(surface->windowX);
    float const top = static_cast<float>(surface->windowY);
    float const right = left + static_cast<float>(width);
    float const bottom = top + static_cast<float>(height);
    if (x >= left && x < right && y >= top && y < bottom) return surface;
  }
  return nullptr;
}

WaylandServer::Impl::Surface* titlebarAt(WaylandServer::Impl* server, float x, float y) {
  for (auto it = server->surfaces_.rbegin(); it != server->surfaces_.rend(); ++it) {
    WaylandServer::Impl::Surface* surface = it->get();
    std::int32_t const width = displayWidth(surface);
    std::int32_t const height = displayHeight(surface);
    if (!surface || !surface->toplevel || width <= 0 || height <= 0) continue;
    float const left = static_cast<float>(surface->windowX);
    float const top = static_cast<float>(surface->windowY - kTitleBarHeight);
    float const right = left + static_cast<float>(width);
    float const bottom = static_cast<float>(surface->windowY);
    if (x >= left && x < right && y >= top && y < bottom) return surface;
  }
  return nullptr;
}

WaylandServer::Impl::Surface* closeButtonAt(WaylandServer::Impl* server, float x, float y) {
  for (auto it = server->surfaces_.rbegin(); it != server->surfaces_.rend(); ++it) {
    WaylandServer::Impl::Surface* surface = it->get();
    std::int32_t const width = displayWidth(surface);
    std::int32_t const height = displayHeight(surface);
    if (!surface || !surface->toplevel || width <= 0 || height <= 0) continue;
    float const left = static_cast<float>(surface->windowX + width - kCloseButtonInset - kCloseButtonSize);
    float const top = static_cast<float>(surface->windowY - kTitleBarHeight + kCloseButtonInset);
    float const right = left + static_cast<float>(kCloseButtonSize);
    float const bottom = top + static_cast<float>(kCloseButtonSize);
    if (x >= left && x < right && y >= top && y < bottom) return surface;
  }
  return nullptr;
}

WaylandServer::Impl::Surface* resizeGripAt(WaylandServer::Impl* server, float x, float y, std::uint32_t& edges) {
  edges = XDG_TOPLEVEL_RESIZE_EDGE_NONE;
  for (auto it = server->surfaces_.rbegin(); it != server->surfaces_.rend(); ++it) {
    WaylandServer::Impl::Surface* surface = it->get();
    std::int32_t const width = displayWidth(surface);
    std::int32_t const height = displayHeight(surface);
    if (!surface || !surface->toplevel || width <= 0 || height <= 0) continue;
    float const left = static_cast<float>(surface->windowX);
    float const top = static_cast<float>(surface->windowY);
    float const right = left + static_cast<float>(width);
    float const bottom = top + static_cast<float>(height);
    bool const nearLeft = x >= left && x < left + kResizeGripSize;
    bool const nearRight = x >= right - kResizeGripSize && x < right;
    bool const nearTop = y >= top && y < top + kResizeGripSize;
    bool const nearBottom = y >= bottom - kResizeGripSize && y < bottom;
    if (nearLeft && nearTop) edges = XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT;
    else if (nearRight && nearTop) edges = XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT;
    else if (nearLeft && nearBottom) edges = XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT;
    else if (nearRight && nearBottom) edges = XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT;
    else continue;
    return surface;
  }
  return nullptr;
}

void raiseSurface(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface) {
  auto found = std::find_if(server->surfaces_.begin(), server->surfaces_.end(),
                            [surface](auto const& candidate) { return candidate.get() == surface; });
  if (found == server->surfaces_.end() || std::next(found) == server->surfaces_.end()) return;
  auto item = std::move(*found);
  server->surfaces_.erase(found);
  server->surfaces_.push_back(std::move(item));
}

void sendPointerFocus(WaylandServer::Impl* server, WaylandServer::Impl::Surface* next, std::uint32_t timeMs) {
  if (server->pointerFocus_ == next) {
    if (!next) return;
    wl_fixed_t const x = wl_fixed_from_double(server->pointerX_ - static_cast<float>(next->windowX));
    wl_fixed_t const y = wl_fixed_from_double(server->pointerY_ - static_cast<float>(next->windowY));
    for (wl_resource* pointer : server->pointerResources_) {
      wl_pointer_send_motion(pointer, timeMs, x, y);
      if (wl_resource_get_version(pointer) >= WL_POINTER_FRAME_SINCE_VERSION) wl_pointer_send_frame(pointer);
    }
    return;
  }

  std::uint32_t serial = server->nextInputSerial_++;
  if (server->pointerFocus_) {
    for (wl_resource* pointer : server->pointerResources_) {
      wl_pointer_send_leave(pointer, serial, server->pointerFocus_->resource);
      if (wl_resource_get_version(pointer) >= WL_POINTER_FRAME_SINCE_VERSION) wl_pointer_send_frame(pointer);
    }
  }
  server->pointerFocus_ = next;
  server->cursorSurface_ = nullptr;
  server->cursorShape_ = CursorShape::Arrow;
  if (next) {
    wl_fixed_t const x = wl_fixed_from_double(server->pointerX_ - static_cast<float>(next->windowX));
    wl_fixed_t const y = wl_fixed_from_double(server->pointerY_ - static_cast<float>(next->windowY));
    server->pointerEnterSerial_ = serial;
    for (wl_resource* pointer : server->pointerResources_) {
      wl_pointer_send_enter(pointer, serial, next->resource, x, y);
      if (wl_resource_get_version(pointer) >= WL_POINTER_FRAME_SINCE_VERSION) wl_pointer_send_frame(pointer);
    }
  }
  updatePointerConstraintsForFocus(server);
}

void sendRelativePointerMotion(WaylandServer::Impl* server, double dx, double dy, std::uint32_t timeMs) {
  if (!server->pointerFocus_ || (dx == 0.0 && dy == 0.0)) return;
  wl_client* focusedClient = wl_resource_get_client(server->pointerFocus_->resource);
  std::uint64_t const timeUsec = static_cast<std::uint64_t>(timeMs) * 1000ull;
  std::uint32_t const timeHi = static_cast<std::uint32_t>(timeUsec >> 32u);
  std::uint32_t const timeLo = static_cast<std::uint32_t>(timeUsec & 0xffffffffu);
  wl_fixed_t const fixedDx = wl_fixed_from_double(dx);
  wl_fixed_t const fixedDy = wl_fixed_from_double(dy);
  for (auto const& relativePointer : server->relativePointers_) {
    if (!relativePointer->resource || !relativePointer->pointer) continue;
    if (wl_resource_get_client(relativePointer->pointer) != focusedClient) continue;
    zwp_relative_pointer_v1_send_relative_motion(relativePointer->resource,
                                                 timeHi,
                                                 timeLo,
                                                 fixedDx,
                                                 fixedDy,
                                                 fixedDx,
                                                 fixedDy);
  }
}

std::uint32_t keyboardModifierMask(WaylandServer::Impl* server);

void setKeyboardFocus(WaylandServer::Impl* server, WaylandServer::Impl::Surface* next) {
  if (server->keyboardFocus_ == next) return;
  std::uint32_t serial = server->nextInputSerial_++;
  if (server->keyboardFocus_) {
    for (wl_resource* keyboard : server->keyboardResources_) {
      wl_keyboard_send_leave(keyboard, serial, server->keyboardFocus_->resource);
    }
  }
  server->keyboardFocus_ = next;
  if (next) {
    wl_array keys;
    wl_array_init(&keys);
    std::uint32_t const modifiers = keyboardModifierMask(server);
    for (wl_resource* keyboard : server->keyboardResources_) {
      wl_keyboard_send_enter(keyboard, serial, next->resource, &keys);
      wl_keyboard_send_modifiers(keyboard, server->nextInputSerial_++, modifiers, 0, 0, 0);
    }
    wl_array_release(&keys);
  }
  sendSelectionForFocus(server);
  sendPrimarySelectionForFocus(server);
}

std::uint32_t modifierBit(std::uint32_t index, bool active) {
  if (!active || index == kInvalidModifierIndex || index >= 32u) return 0u;
  return 1u << index;
}

std::uint32_t keyboardModifierMask(WaylandServer::Impl* server) {
  return modifierBit(server->shiftModifierIndex_, server->shiftDown_) |
         modifierBit(server->ctrlModifierIndex_, server->ctrlDown_) |
         modifierBit(server->altModifierIndex_, server->altDown_) |
         modifierBit(server->logoModifierIndex_, server->metaDown_);
}

void sendKeyboardModifiers(WaylandServer::Impl* server) {
  std::uint32_t const depressed = keyboardModifierMask(server);
  for (wl_resource* keyboard : server->keyboardResources_) {
    wl_keyboard_send_modifiers(keyboard, server->nextInputSerial_++, depressed, 0, 0, 0);
  }
}

void focusSurface(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface, std::uint32_t timeMs) {
  if (!surface) return;
  raiseSurface(server, surface);
  setKeyboardFocus(server, surface);
  sendPointerFocus(server, surfaceAt(server, server->pointerX_, server->pointerY_), timeMs);
}

WaylandServer::Impl::Surface* previousToplevel(WaylandServer::Impl* server, WaylandServer::Impl::Surface* current) {
  WaylandServer::Impl::Surface* previous = nullptr;
  for (auto const& surface : server->surfaces_) {
    if (!surface || !surface->toplevel) continue;
    if (surface.get() == current) return previous;
    previous = surface.get();
  }
  return previous;
}

WaylandServer::Impl::XdgToplevel* focusedToplevel(WaylandServer::Impl* server) {
  return toplevelForSurface(server, server->keyboardFocus_);
}

bool closeFocusedToplevel(WaylandServer::Impl* server) {
  WaylandServer::Impl::XdgToplevel* toplevel = focusedToplevel(server);
  if (!toplevel || !toplevel->resource) return false;
  xdg_toplevel_send_close(toplevel->resource);
  return true;
}

bool cycleFocus(WaylandServer::Impl* server, std::uint32_t timeMs) {
  WaylandServer::Impl::Surface* target = previousToplevel(server, server->keyboardFocus_);
  if (!target) {
    for (auto const& surface : server->surfaces_) {
      if (surface && surface->toplevel) {
        target = surface.get();
      }
    }
  }
  if (!target || target == server->keyboardFocus_) return false;
  focusSurface(server, target, timeMs);
  return true;
}

std::optional<SnapPreviewSnapshot> snapPreviewForDrag(WaylandServer::Impl const* server) {
  WaylandServer::Impl::Surface const* surface = server->dragSurface_;
  if (!surface) return std::nullopt;
  auto preview = snapPreviewGeometry(windowGeometryFor(surface), outputGeometryFor(server));
  if (!preview) return std::nullopt;
  return SnapPreviewSnapshot{
      .x = preview->x,
      .y = 0,
      .width = preview->width,
      .height = std::max(kMinWindowHeight, server->output_.height),
  };
}

void startGeometryAnimation(WaylandServer::Impl* server,
                            WaylandServer::Impl::Surface* surface,
                            std::int32_t targetX,
                            std::int32_t targetY,
                            std::int32_t targetWidth,
                            std::int32_t targetHeight) {
  if (!surface) return;
  surface->geometryAnimationStartX = surface->windowX;
  surface->geometryAnimationStartY = surface->windowY;
  surface->geometryAnimationStartWidth = displayWidth(surface);
  surface->geometryAnimationStartHeight = displayHeight(surface);
  surface->geometryAnimationTargetX = targetX;
  surface->geometryAnimationTargetY = targetY;
  surface->geometryAnimationTargetWidth = targetWidth;
  surface->geometryAnimationTargetHeight = targetHeight;
  surface->geometryAnimationLastConfigureWidth = displayWidth(surface);
  surface->geometryAnimationLastConfigureHeight = displayHeight(surface);
  surface->geometryAnimationStartedAtMs = monotonicMilliseconds();
  surface->geometryAnimationActive = true;
  if (surface->geometryAnimationStartX == targetX && surface->geometryAnimationStartY == targetY &&
      surface->geometryAnimationStartWidth == targetWidth &&
      surface->geometryAnimationStartHeight == targetHeight) {
    surface->geometryAnimationActive = false;
    return;
  }
  server->flushClients();
}

void snapToplevel(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface, bool leftHalf) {
  if (!surface || !surface->toplevel) return;
  if (!surface->snapped && !surface->maximized) {
    surface->restoreX = surface->windowX;
    surface->restoreY = surface->windowY;
    surface->restoreWidth = displayWidth(surface);
    surface->restoreHeight = displayHeight(surface);
  }
  WindowGeometry const target = snappedWindowGeometry(outputGeometryFor(server), leftHalf);
  surface->snapped = true;
  surface->maximized = false;
  startGeometryAnimation(server,
                         surface,
                         target.x,
                         target.y,
                         target.width,
                         target.height);
}

void snapFocusedToplevel(WaylandServer::Impl* server, bool leftHalf) {
  snapToplevel(server, server->keyboardFocus_, leftHalf);
}

void restoreSnappedForDrag(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface) {
  if (!surface || (!surface->snapped && !surface->maximized)) return;
  WindowGeometry const restored = restoredDragGeometry({
      .pointerX = server->pointerX_,
      .pointerY = server->pointerY_,
      .dragOffsetY = server->dragOffsetY_,
      .snappedWindow = windowGeometryFor(surface),
      .restoreWindow = {
          .x = surface->restoreX,
          .y = surface->restoreY,
          .width = surface->restoreWidth > 0 ? surface->restoreWidth : surface->width,
          .height = surface->restoreHeight > 0 ? surface->restoreHeight : surface->height,
      },
      .output = outputGeometryFor(server),
  });
  surface->windowX = restored.x;
  surface->windowY = restored.y;
  setConfiguredFrameSize(surface, restored.width, restored.height);
  surface->snapped = false;
  surface->maximized = false;
  surface->geometryAnimationActive = false;
  server->dragOffsetX_ = server->pointerX_ - static_cast<float>(surface->windowX);
  server->dragOffsetY_ = server->pointerY_ - static_cast<float>(surface->windowY);
  sendToplevelConfigure(server, toplevelForSurface(server, surface), restored.width, restored.height);
}

void toggleMaximizedToplevel(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface) {
  if (!surface || !surface->toplevel) return;
  if (surface->maximized) {
    std::int32_t const restoreWidth =
        std::max(kMinWindowWidth, surface->restoreWidth > 0 ? surface->restoreWidth : surface->width);
    std::int32_t const restoreHeight =
        std::max(kMinWindowHeight, surface->restoreHeight > 0 ? surface->restoreHeight : surface->height);
    std::int32_t const restoreX = std::clamp(surface->restoreX, 0, std::max(0, server->output_.width - restoreWidth));
    std::int32_t const restoreY =
        std::clamp(surface->restoreY, kTitleBarHeight, std::max(kTitleBarHeight, server->output_.height - restoreHeight));
    surface->maximized = false;
    surface->snapped = false;
    startGeometryAnimation(server, surface, restoreX, restoreY, restoreWidth, restoreHeight);
    return;
  }

  if (!surface->snapped) {
    surface->restoreX = surface->windowX;
    surface->restoreY = surface->windowY;
    surface->restoreWidth = displayWidth(surface);
    surface->restoreHeight = displayHeight(surface);
  }
  std::int32_t const width = std::max(kMinWindowWidth, server->output_.width);
  std::int32_t const height = std::max(kMinWindowHeight, server->output_.height - kTitleBarHeight);
  surface->maximized = true;
  surface->snapped = false;
  startGeometryAnimation(server, surface, 0, kTitleBarHeight, width, height);
}

bool updateShortcutModifier(WaylandServer::Impl* server, std::uint32_t key, bool pressed) {
  bool changed = false;
  if (key == KEY_LEFTMETA || key == KEY_RIGHTMETA) {
    changed = server->metaDown_ != pressed;
    server->metaDown_ = pressed;
    if (changed) sendKeyboardModifiers(server);
    return true;
  }
  if (key == KEY_LEFTCTRL || key == KEY_RIGHTCTRL) {
    changed = server->ctrlDown_ != pressed;
    server->ctrlDown_ = pressed;
    if (changed) sendKeyboardModifiers(server);
    return false;
  }
  if (key == KEY_LEFTALT || key == KEY_RIGHTALT) {
    changed = server->altDown_ != pressed;
    server->altDown_ = pressed;
    if (changed) sendKeyboardModifiers(server);
    return false;
  }
  if (key == KEY_LEFTSHIFT || key == KEY_RIGHTSHIFT) {
    changed = server->shiftDown_ != pressed;
    server->shiftDown_ = pressed;
    if (changed) sendKeyboardModifiers(server);
    return false;
  }
  return false;
}

bool handleCompositorShortcut(WaylandServer::Impl* server, std::uint32_t key, bool pressed, std::uint32_t timeMs) {
  if (!pressed) return false;
  for (auto const& binding : server->shortcutBindings_) {
    if (binding.key != key) continue;
    if (binding.meta != server->metaDown_ || binding.ctrl != server->ctrlDown_ ||
        binding.alt != server->altDown_ || binding.shift != server->shiftDown_) {
      continue;
    }

    switch (binding.action) {
    case WaylandServer::ShortcutAction::CloseFocused:
      return closeFocusedToplevel(server);
    case WaylandServer::ShortcutAction::CycleFocus:
      return cycleFocus(server, timeMs);
    case WaylandServer::ShortcutAction::SnapLeft:
      snapFocusedToplevel(server, true);
      return true;
    case WaylandServer::ShortcutAction::SnapRight:
      snapFocusedToplevel(server, false);
      return true;
    case WaylandServer::ShortcutAction::Terminate:
      std::raise(SIGTERM);
      return true;
    }
  }
  return false;
}

void updateDrag(WaylandServer::Impl* server) {
  if (!server->dragSurface_) return;
  WaylandServer::Impl::Surface* surface = server->dragSurface_;
  restoreSnappedForDrag(server, surface);
  int const maxX = std::max(0, server->output_.width - displayWidth(surface));
  int const maxY = std::max(kTitleBarHeight, server->output_.height - displayHeight(surface));
  surface->windowX = std::clamp(static_cast<int>(server->pointerX_ - server->dragOffsetX_), 0, maxX);
  surface->windowY = std::clamp(static_cast<int>(server->pointerY_ - server->dragOffsetY_), kTitleBarHeight, maxY);
}

void updateResize(WaylandServer::Impl* server) {
  WaylandServer::Impl::Surface* surface = server->resizeSurface_;
  if (!surface) return;
  surface->geometryAnimationActive = false;

  float const dx = server->pointerX_ - server->resizeStartX_;
  float const dy = server->pointerY_ - server->resizeStartY_;
  ResizeEdge const edges = resizeEdgesFromXdg(server->resizeEdges_);
  bool const left = hasResizeEdge(edges, ResizeEdge::Left);
  bool const top = hasResizeEdge(edges, ResizeEdge::Top);
  WindowGeometry const next = resizedWindowGeometry({
      .startPointerX = server->resizeStartX_,
      .startPointerY = server->resizeStartY_,
      .pointerX = server->pointerX_,
      .pointerY = server->pointerY_,
      .startWindow = {
          .x = server->resizeStartWindowX_,
          .y = server->resizeStartWindowY_,
          .width = server->resizeStartWidth_,
          .height = server->resizeStartHeight_,
      },
      .edges = edges,
      .output = outputGeometryFor(server),
  });

  if (left) surface->windowX = next.x;
  if (top) surface->windowY = next.y;
  if (next.width == server->resizeLastWidth_ && next.height == server->resizeLastHeight_) return;
  server->resizeLastWidth_ = next.width;
  server->resizeLastHeight_ = next.height;
  setConfiguredFrameSize(surface, next.width, next.height);
  flux::detail::resizeTrace("compositor",
                            "update-resize surface=%llu pointer=%.1f,%.1f window=%d,%d size=%dx%d "
                            "delta=%.1f,%.1f edges=%u\n",
                            static_cast<unsigned long long>(surface->id),
                            server->pointerX_,
                            server->pointerY_,
                            surface->windowX,
                            surface->windowY,
                            next.width,
                            next.height,
                            dx,
                            dy,
                            server->resizeEdges_);
  sendToplevelConfigure(server, toplevelForSurface(server, surface), next.width, next.height);
}

void WaylandServer::Impl::handlePointerMotion(double dx, double dy, std::uint32_t timeMs) {
  if (auto* constraint = activePointerConstraint(this);
      constraint && constraint->kind == PointerConstraint::Kind::Lock) {
    sendRelativePointerMotion(this, dx, dy, timeMs);
    return;
  }
  pointerX_ = std::clamp(pointerX_ + static_cast<float>(dx), 0.f, std::max(0.f, static_cast<float>(output_.width - 1)));
  pointerY_ = std::clamp(pointerY_ + static_cast<float>(dy), 0.f, std::max(0.f, static_cast<float>(output_.height - 1)));
  if (auto* constraint = activePointerConstraint(this);
      constraint && constraint->kind == PointerConstraint::Kind::Confine && constraint->surface) {
    pointerX_ = std::clamp(pointerX_,
                           static_cast<float>(constraint->surface->windowX),
                           static_cast<float>(constraint->surface->windowX + std::max(0, displayWidth(constraint->surface) - 1)));
    pointerY_ = std::clamp(pointerY_,
                           static_cast<float>(constraint->surface->windowY),
                           static_cast<float>(constraint->surface->windowY + std::max(0, displayHeight(constraint->surface) - 1)));
  }
  if (resizeSurface_) {
    updateResize(this);
    return;
  }
  if (dragSurface_) {
    updateDrag(this);
    return;
  }
  if (dndSource_) {
    updateDndTarget(this, surfaceAt(this, pointerX_, pointerY_), timeMs);
    return;
  }
  sendPointerFocus(this, surfaceAt(this, pointerX_, pointerY_), timeMs);
  sendRelativePointerMotion(this, dx, dy, timeMs);
}

void WaylandServer::Impl::handlePointerPosition(double x, double y, std::uint32_t timeMs) {
  if (auto* constraint = activePointerConstraint(this);
      constraint && constraint->kind == PointerConstraint::Kind::Lock) {
    return;
  }
  pointerX_ = std::clamp(static_cast<float>(x), 0.f, std::max(0.f, static_cast<float>(output_.width - 1)));
  pointerY_ = std::clamp(static_cast<float>(y), 0.f, std::max(0.f, static_cast<float>(output_.height - 1)));
  if (auto* constraint = activePointerConstraint(this);
      constraint && constraint->kind == PointerConstraint::Kind::Confine && constraint->surface) {
    pointerX_ = std::clamp(pointerX_,
                           static_cast<float>(constraint->surface->windowX),
                           static_cast<float>(constraint->surface->windowX + std::max(0, displayWidth(constraint->surface) - 1)));
    pointerY_ = std::clamp(pointerY_,
                           static_cast<float>(constraint->surface->windowY),
                           static_cast<float>(constraint->surface->windowY + std::max(0, displayHeight(constraint->surface) - 1)));
  }
  if (resizeSurface_) {
    updateResize(this);
    return;
  }
  if (dragSurface_) {
    updateDrag(this);
    return;
  }
  if (dndSource_) {
    updateDndTarget(this, surfaceAt(this, pointerX_, pointerY_), timeMs);
    return;
  }
  sendPointerFocus(this, surfaceAt(this, pointerX_, pointerY_), timeMs);
}

void WaylandServer::Impl::handlePointerButton(std::uint32_t button, bool pressed, std::uint32_t timeMs) {
  Surface* target = surfaceAt(this, pointerX_, pointerY_);
  if (button == BTN_LEFT && !pressed && dndSource_) {
    updateDndTarget(this, target, timeMs);
    if (dndTarget_) {
      if (auto* device = dataDeviceForClient(this, wl_resource_get_client(dndTarget_->resource))) {
        wl_data_device_send_drop(device->resource);
      }
      if (dndSource_->resource && wl_resource_get_version(dndSource_->resource) >= WL_DATA_SOURCE_DND_DROP_PERFORMED_SINCE_VERSION) {
        wl_data_source_send_dnd_drop_performed(dndSource_->resource);
      }
    } else if (dndSource_->resource) {
      wl_data_source_send_cancelled(dndSource_->resource);
    }
    clearDnd(this);
    return;
  }
  if (button == BTN_LEFT) {
    if (pressed) {
      if (Surface* closeTarget = closeButtonAt(this, pointerX_, pointerY_)) {
        raiseSurface(this, closeTarget);
        setKeyboardFocus(this, closeTarget);
        closePressSurface_ = closeTarget;
        return;
      }
      std::uint32_t resizeEdges = XDG_TOPLEVEL_RESIZE_EDGE_NONE;
      Surface* resizeTarget = resizeGripAt(this, pointerX_, pointerY_, resizeEdges);
      if (resizeTarget) {
        raiseSurface(this, resizeTarget);
        setKeyboardFocus(this, resizeTarget);
        sendPointerFocus(this, nullptr, timeMs);
        resizeTarget->snapped = false;
        resizeTarget->maximized = false;
        resizeTarget->geometryAnimationActive = false;
        resizeSurface_ = resizeTarget;
        resizeStartX_ = pointerX_;
        resizeStartY_ = pointerY_;
        resizeStartWindowX_ = resizeTarget->windowX;
        resizeStartWindowY_ = resizeTarget->windowY;
        resizeStartWidth_ = displayWidth(resizeTarget);
        resizeStartHeight_ = displayHeight(resizeTarget);
        resizeLastWidth_ = resizeStartWidth_;
        resizeLastHeight_ = resizeStartHeight_;
        resizeEdges_ = resizeEdges;
        flux::detail::resizeTrace("compositor",
                                  "begin-resize surface=%llu pointer=%.1f,%.1f edges=%u startWindow=%d,%d "
                                  "startSize=%dx%d\n",
                                  static_cast<unsigned long long>(resizeTarget->id),
                                  pointerX_,
                                  pointerY_,
                                  resizeEdges_,
                                  resizeStartWindowX_,
                                  resizeStartWindowY_,
                                  resizeStartWidth_,
                                  resizeStartHeight_);
        return;
      }
      Surface* chromeTarget = titlebarAt(this, pointerX_, pointerY_);
      if (chromeTarget) {
        raiseSurface(this, chromeTarget);
        setKeyboardFocus(this, chromeTarget);
        sendPointerFocus(this, nullptr, timeMs);
        bool const doubleClick = lastTitleClickSurface_ == chromeTarget &&
                                 timeMs - lastTitleClickTimeMs_ <= 400u;
        lastTitleClickSurface_ = chromeTarget;
        lastTitleClickTimeMs_ = timeMs;
        if (doubleClick) {
          dragSurface_ = nullptr;
          toggleMaximizedToplevel(this, chromeTarget);
          return;
        }
        dragSurface_ = chromeTarget;
        dragOffsetX_ = pointerX_ - static_cast<float>(chromeTarget->windowX);
        dragOffsetY_ = pointerY_ - static_cast<float>(chromeTarget->windowY);
        return;
      }
    } else if (closePressSurface_) {
      Surface* closeTarget = closeButtonAt(this, pointerX_, pointerY_);
      if (closeTarget && closeTarget == closePressSurface_) {
        setKeyboardFocus(this, closePressSurface_);
        closeFocusedToplevel(this);
        flushClients();
      }
      closePressSurface_ = nullptr;
      return;
    } else if (resizeSurface_) {
      updateResize(this);
      traceResizeSurface("end-resize", resizeSurface_);
      resizeSurface_ = nullptr;
      resizeEdges_ = XDG_TOPLEVEL_RESIZE_EDGE_NONE;
      return;
    } else if (dragSurface_) {
      if (auto preview = snapPreviewForDrag(this)) {
        snapToplevel(this, dragSurface_, preview->x == 0);
      }
      dragSurface_ = nullptr;
      return;
    }
  }

  if (pressed && target) {
    raiseSurface(this, target);
    setKeyboardFocus(this, target);
    sendPointerFocus(this, target, timeMs);
  }
  if (!pointerFocus_) return;
  std::uint32_t serial = nextInputSerial_++;
  for (wl_resource* pointer : pointerResources_) {
    wl_pointer_send_button(pointer,
                           serial,
                           timeMs,
                           button,
                           pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
    if (wl_resource_get_version(pointer) >= WL_POINTER_FRAME_SINCE_VERSION) wl_pointer_send_frame(pointer);
  }
}

void WaylandServer::Impl::handlePointerAxis(double dx, double dy, std::uint32_t timeMs) {
  if (!pointerFocus_) return;
  for (wl_resource* pointer : pointerResources_) {
    if (dx != 0.0) {
      wl_pointer_send_axis(pointer, timeMs, WL_POINTER_AXIS_HORIZONTAL_SCROLL, wl_fixed_from_double(dx));
    }
    if (dy != 0.0) {
      wl_pointer_send_axis(pointer, timeMs, WL_POINTER_AXIS_VERTICAL_SCROLL, wl_fixed_from_double(dy));
    }
    if (wl_resource_get_version(pointer) >= WL_POINTER_FRAME_SINCE_VERSION) wl_pointer_send_frame(pointer);
  }
}

void WaylandServer::Impl::handleKeyboardKey(std::uint32_t key, bool pressed, std::uint32_t timeMs) {
  bool const consumeModifier = updateShortcutModifier(this, key, pressed);
  if (consumeModifier) return;
  if (handleCompositorShortcut(this, key, pressed, timeMs)) return;
  if (!keyboardFocus_) return;
  std::uint32_t serial = nextInputSerial_++;
  for (wl_resource* keyboard : keyboardResources_) {
    wl_keyboard_send_key(keyboard,
                         serial,
                         timeMs,
                         key,
                         pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
  }
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

wl_resource* WaylandServer::Impl::createSurface(wl_client* client, std::uint32_t version, std::uint32_t id) {
  auto surface = std::make_unique<Surface>();
  surface->server = this;
  surface->id = nextSurfaceId_++;
  wl_resource* resource = wl_resource_create(client, &wl_surface_interface, std::min(version, 5u), id);
  surface->resource = resource;
  auto* raw = surface.get();
  surfaces_.push_back(std::move(surface));
  wl_resource_set_implementation(resource, &surfaceImpl, raw, destroyResourceCallback<WaylandServer::Impl::Surface, WaylandServer::Impl, &WaylandServer::Impl::destroySurface>);
  return resource;
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
  if (dndOrigin_ == surface || dndTarget_ == surface) clearDnd(this);
  for (auto& device : cursorShapeDevices_) {
    if (device->pointer && wl_resource_get_client(device->pointer) == wl_resource_get_client(surface->resource)) {
      device->pointer = nullptr;
    }
  }
  if (surface->viewport) wl_resource_destroy(surface->viewport->resource);
  if (surface->fractionalScale) wl_resource_destroy(surface->fractionalScale->resource);
  if (surface->layerSurface) wl_resource_destroy(surface->layerSurface->resource);
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

void WaylandServer::Impl::destroyXdgSurface(XdgSurface* surface) {
  eraseResource(xdgSurfaces_, surface);
}

void WaylandServer::Impl::destroyXdgToplevel(XdgToplevel* toplevel) {
  while (auto* decoration = decorationFor(this, toplevel)) {
    wl_resource_destroy(decoration->resource);
  }
  eraseResource(toplevels_, toplevel);
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

WaylandServer::WaylandServer(WaylandOutputInfo output) : impl_(std::make_unique<Impl>(std::move(output))) {}

WaylandServer::~WaylandServer() = default;

char const* WaylandServer::socketName() const noexcept {
  return impl_->socketName();
}

int WaylandServer::eventFd() const noexcept {
  return impl_->eventFd();
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
