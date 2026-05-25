#include "Compositor/Wayland/WaylandServerImpl.hpp"

#include <algorithm>
#include <vector>

#include "Compositor/Chrome/ChromeMetrics.hpp"
#include "Compositor/Wayland/Globals/LinuxDmabuf.hpp"
#include "Compositor/Window/WindowManagerInternal.hpp"
#include "Compositor/Window/WindowGeometry.hpp"

#include <drm_fourcc.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <optional>
#include <vector>

namespace flux::compositor {
namespace {

bool surfaceIsRenderable(WaylandServer::Impl::Surface const* surface) {
  return surface && surface->width > 0 && surface->height > 0 &&
         ((surface->rgbaPixels && !surface->rgbaPixels->empty()) || surface->dmabufBuffer);
}

bool dmabufFormatFullyOpaque(std::uint32_t format) {
  return format == DRM_FORMAT_XRGB8888 || format == DRM_FORMAT_XBGR8888;
}

using RegionRect = CommittedSurfaceSnapshot::RegionRect;

bool emptyRegionRect(RegionRect const& rect) {
  return rect.width <= 0 || rect.height <= 0;
}

void appendRegionRect(std::vector<RegionRect>& rects, RegionRect rect) {
  if (!emptyRegionRect(rect)) rects.push_back(rect);
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

bool regionCoversRect(std::vector<RegionRect> const& rects,
                      std::int32_t x,
                      std::int32_t y,
                      std::int32_t width,
                      std::int32_t height) {
  if (width <= 0 || height <= 0) return false;
  std::vector<RegionRect> remaining{RegionRect{x, y, width, height}};
  for (RegionRect const& rect : rects) {
    subtractRegionRect(remaining, rect);
    if (remaining.empty()) return true;
  }
  return remaining.empty();
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

std::int32_t committedDisplayWidthForSurface(WaylandServer::Impl::Surface const* surface);
std::int32_t committedDisplayHeightForSurface(WaylandServer::Impl::Surface const* surface);

bool surfaceContentFullyOpaque(WaylandServer::Impl::Surface const* surface) {
  if (!surface) return false;
  if (regionCoversRect(surface->opaqueRegionRects, 0, 0, committedDisplayWidthForSurface(surface),
                       committedDisplayHeightForSurface(surface))) {
    return true;
  }
  if (surface->rgbaPixels && !surface->rgbaPixels->empty()) return surface->rgbaFullyOpaque;
  if (surface->dmabufBuffer) return dmabufFormatFullyOpaque(surface->dmabufBuffer->format);
  return false;
}

std::int32_t committedDisplayWidthForSurface(WaylandServer::Impl::Surface const* surface) {
  if (!surface) return 0;
  if (surface->destinationSet) return surface->destinationWidth;
  if (surface->sourceSet) return static_cast<std::int32_t>(surface->sourceWidth);
  return std::max(1, transformedBufferWidth(surface) / std::max(1, surface->scale));
}

std::int32_t committedDisplayHeightForSurface(WaylandServer::Impl::Surface const* surface) {
  if (!surface) return 0;
  if (surface->destinationSet) return surface->destinationHeight;
  if (surface->sourceSet) return static_cast<std::int32_t>(surface->sourceHeight);
  return std::max(1, transformedBufferHeight(surface) / std::max(1, surface->scale));
}

WaylandServer::Impl::XdgToplevel const* toplevelForSurface(WaylandServer::Impl const* server,
                                                           WaylandServer::Impl::Surface const* surface) {
  auto found = std::find_if(server->toplevels_.begin(), server->toplevels_.end(),
                            [surface](auto const& toplevel) {
                              return toplevel->xdgSurface && toplevel->xdgSurface->surface == surface;
                            });
  return found == server->toplevels_.end() ? nullptr : found->get();
}

bool serverSideDecorated(WaylandServer::Impl const* server, WaylandServer::Impl::Surface const* surface) {
  auto* toplevel = toplevelForSurface(server, surface);
  if (!toplevel) return false;
  auto found = std::find_if(server->toplevelDecorations_.begin(), server->toplevelDecorations_.end(),
                            [toplevel](auto const& decoration) { return decoration->toplevel == toplevel; });
  return found != server->toplevelDecorations_.end() &&
         (*found)->mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
}

bool cutoutsBound(WaylandServer::Impl const* server, WaylandServer::Impl::Surface const* surface) {
  auto* toplevel = toplevelForSurface(server, surface);
  return toplevel && toplevel->cutouts;
}

bool cutoutsRejected(WaylandServer::Impl const* server, WaylandServer::Impl::Surface const* surface) {
  auto* toplevel = toplevelForSurface(server, surface);
  return toplevel && toplevel->cutoutsRejected;
}

enum class ChromeButton {
  None,
  Close,
  Maximize,
  Minimize,
};

ChromeButton chromeButtonAt(WaylandServer::Impl const* server,
                            WaylandServer::Impl::Surface const* surface,
                            float x,
                            float y,
                            bool cutoutMode) {
  if (!surface || !serverSideDecorated(server, surface)) return ChromeButton::None;
  auto const& chrome = server->chromeConfig_;
  float const windowX = static_cast<float>(surface->windowX);
  float const windowY = static_cast<float>(surface->windowY);
  float const width = static_cast<float>(displayWidth(surface));
  float const titleBarHeight = static_cast<float>(chrome.titleBarHeight);
  float const top = windowY - (cutoutMode ? 0.f : titleBarHeight);
  ChromeControlRects const rects = chromeControlRects(chrome, windowX, top, width, titleBarHeight);
  auto contains = [&](Rect const& rect) {
    return x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height;
  };
  if (contains(rects.closeButton)) return ChromeButton::Close;
  if (contains(rects.maximizeButton)) return ChromeButton::Maximize;
  if (contains(rects.minimizeButton)) return ChromeButton::Minimize;
  return ChromeButton::None;
}

CommittedSurfaceSnapshot snapshotForSurface(WaylandServer::Impl const* server,
                                            WaylandServer::Impl::Surface const* surface,
                                            std::int32_t x,
                                            std::int32_t y,
                                            bool withChrome) {
  bool const decorated = withChrome && surfaceIsXdgToplevel(surface) && serverSideDecorated(server, surface);
  bool const bound = withChrome && surfaceIsXdgToplevel(surface) && cutoutsBound(server, surface);
  bool const rejected = withChrome && surfaceIsXdgToplevel(surface) && cutoutsRejected(server, surface);
  bool const cutoutMode = decorated && bound && !rejected;
  ChromeButton const hovered = chromeButtonAt(server, surface, server->pointerX_, server->pointerY_, cutoutMode);
  bool const xdgToplevel = surfaceIsXdgToplevel(surface);
  std::int32_t const width = displayWidth(surface);
  std::int32_t const height = displayHeight(surface);
  std::int32_t const committedWidth = committedDisplayWidthForSurface(surface);
  std::int32_t const committedHeight = committedDisplayHeightForSurface(surface);
  std::int32_t const titleBarHeight = decorated && !cutoutMode ? server->chromeConfig_.titleBarHeight : 0;
  std::int32_t const workTop = server ? server->topBarExclusiveZone_ : 0;
  std::int32_t const workBottom = server
                                      ? std::max(0, server->logicalOutputHeight() - server->dockReservedZone_)
                                      : 0;
  std::int32_t const frameTop = y - titleBarHeight;
  std::int32_t const frameBottom = y + height;
  std::int32_t const windowClipTop = xdgToplevel && workTop > frameTop ? workTop : 0;
  std::int32_t const windowClipBottom = xdgToplevel && workBottom > 0 && workBottom < frameBottom ? workBottom : 0;
  bool const geometryAnimationGrowing =
      surface->geometryAnimationActive &&
      (surface->geometryAnimationTargetWidth > surface->geometryAnimationStartWidth ||
       surface->geometryAnimationTargetHeight > surface->geometryAnimationStartHeight);
  CommittedSurfaceSnapshot snapshot{
      .id = surface->id,
      .x = x,
      .y = y,
      .width = width,
      .height = height,
      .committedWidth = committedWidth,
      .committedHeight = committedHeight,
      .bufferWidth = surface->width,
      .bufferHeight = surface->height,
      .bufferTransform = surface->bufferTransform,
      .sourceX = surface->sourceSet ? surface->sourceX : 0.f,
      .sourceY = surface->sourceSet ? surface->sourceY : 0.f,
      .sourceWidth = surface->sourceSet ? surface->sourceWidth : static_cast<float>(surface->width),
      .sourceHeight = surface->sourceSet ? surface->sourceHeight : static_cast<float>(surface->height),
      .destinationWidth = surface->destinationSet ? surface->destinationWidth : width,
      .destinationHeight = surface->destinationSet ? surface->destinationHeight : height,
      .titleBarHeight = titleBarHeight,
      .title = decorated && !cutoutMode ? titleForSurface(server, surface) : std::string{},
      .serverSideDecorated = decorated,
      .cutoutsBound = bound,
      .cutoutsRejected = rejected,
      .closeButtonHovered = hovered == ChromeButton::Close,
      .closeButtonPressed = server->closePressSurface_ == surface,
      .maximizeButtonHovered = hovered == ChromeButton::Maximize,
      .maximizeButtonPressed = server->maximizePressSurface_ == surface,
      .minimizeButtonHovered = hovered == ChromeButton::Minimize,
      .minimizeButtonPressed = server->minimizePressSurface_ == surface,
	      .focused = server->keyboardFocus_ == surface,
	      .activeSizing = server->resizeSurface_ == surface ||
	                      surface->geometryAnimationActive ||
	                      surface->awaitingConfigureCommit,
	      .pacingSizing = server->resizeSurface_ == surface ||
	                      surface->geometryAnimationActive,
	      .geometryAnimationGrowing = geometryAnimationGrowing,
	      .defaultGlassEligible = withChrome && surfaceIsXdgToplevel(surface) && !surfaceContentFullyOpaque(surface),
	      .shadowClipTop = server ? server->topBarExclusiveZone_ : 0,
	      .shadowClipBottom = server
	                              ? std::max(0, server->logicalOutputHeight() - server->dockReservedZone_)
	                              : 0,
	      .windowClipTop = windowClipTop,
	      .windowClipBottom = windowClipBottom,
	      .backgroundEffect = surface->backgroundEffectState,
	      .serial = surface->serial,
	      .lastConfigureSerial = surface->lastConfigureSerial,
	      .lastConfigureWidth = surface->lastConfigureWidth,
	      .lastConfigureHeight = surface->lastConfigureHeight,
	      .lastResizeInputNsec = surface->lastResizeInputNsec,
	      .lastConfigureSentNsec = surface->lastConfigureSentNsec,
	      .lastConfigureAckNsec = surface->lastConfigureAckNsec,
	      .lastCommitNsec = surface->lastCommitNsec,
	      .backgroundBlurRects = surface->backgroundBlurRects,
	      .opaqueRegionRects = surface->opaqueRegionRects,
	      .rgbaPixels = surface->rgbaPixels,
	      .pixelFormat = surface->pixelFormat,
	      .dmabufBufferId = surface->dmabufBuffer ? surface->dmabufBuffer->id : 0,
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

bool layerSurfaceAboveWindows(WaylandServer::Impl::Surface const* surface) {
  return surfaceIsLayerSurface(surface) && surface->layerSurface &&
         surface->layerSurface->layer >= ZWLR_LAYER_SHELL_V1_LAYER_TOP;
}

bool renderInPass(WaylandServer::Impl::Surface const* surface, bool aboveWindowLayers) {
  if (!surface || !surfaceIsTopLevelRenderable(surface)) return false;
  return layerSurfaceAboveWindows(surface) == aboveWindowLayers;
}

void appendRenderableSurface(WaylandServer::Impl const* server,
                             std::vector<CommittedSurfaceSnapshot>& snapshots,
                             WaylandServer::Impl::Surface const* surface) {
  if (!wm::surfaceEligibleForPresentation(surface)) return;
  if (surfaceIsRenderable(surface)) {
    snapshots.push_back(snapshotForSurface(server, surface, surface->windowX, surface->windowY, true));
  }
  appendSubsurfaceSnapshots(server, snapshots, surface, surface->windowX, surface->windowY);
}

} // namespace

std::vector<CommittedSurfaceSnapshot> WaylandServer::Impl::committedSurfaces() const {
  std::vector<CommittedSurfaceSnapshot> snapshots;
  snapshots.reserve(surfaces_.size());
  for (bool aboveWindowLayers : {false, true}) {
    std::vector<WaylandServer::Impl::Surface const*> passSurfaces;
    passSurfaces.reserve(surfaces_.size());
    for (auto const& surface : surfaces_) {
      if (renderInPass(surface.get(), aboveWindowLayers)) passSurfaces.push_back(surface.get());
    }
    if (aboveWindowLayers) {
      std::stable_sort(passSurfaces.begin(), passSurfaces.end(),
                       [](WaylandServer::Impl::Surface const* a, WaylandServer::Impl::Surface const* b) {
                         std::uint32_t const layerA =
                             a->layerSurface ? a->layerSurface->layer : ZWLR_LAYER_SHELL_V1_LAYER_TOP;
                         std::uint32_t const layerB =
                             b->layerSurface ? b->layerSurface->layer : ZWLR_LAYER_SHELL_V1_LAYER_TOP;
                         return layerA < layerB;
                       });
    }
    for (WaylandServer::Impl::Surface const* surface : passSurfaces) {
      appendRenderableSurface(this, snapshots, surface);
    }
  }
  return snapshots;
}

std::optional<CommittedSurfaceSnapshot> WaylandServer::Impl::cursorSurface() const {
  if (compositorCursorOverride_) return std::nullopt;
  Surface* surface = cursorSurface_;
  if (!surface || surface->width <= 0 || surface->height <= 0) return std::nullopt;
  if ((!surface->rgbaPixels || surface->rgbaPixels->empty()) && !surface->dmabufBuffer) return std::nullopt;

  CommittedSurfaceSnapshot snapshot{
      .id = surface->id,
      .x = static_cast<std::int32_t>(pointerX_) - cursorHotspotX_,
      .y = static_cast<std::int32_t>(pointerY_) - cursorHotspotY_,
      .width = displayWidth(surface),
      .height = displayHeight(surface),
      .committedWidth = committedDisplayWidthForSurface(surface),
      .committedHeight = committedDisplayHeightForSurface(surface),
      .bufferWidth = surface->width,
      .bufferHeight = surface->height,
      .bufferTransform = surface->bufferTransform,
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
	      .defaultGlassEligible = false,
	      .serial = surface->serial,
	      .backgroundBlurRects = {},
	      .rgbaPixels = surface->rgbaPixels,
	      .pixelFormat = surface->pixelFormat,
	      .dmabufBufferId = surface->dmabufBuffer ? surface->dmabufBuffer->id : 0,
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
    std::fprintf(stderr, "lambda-window-manager: dmabuf CPU fallback mmap failed: %s\n", std::strerror(errno));
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

} // namespace flux::compositor
