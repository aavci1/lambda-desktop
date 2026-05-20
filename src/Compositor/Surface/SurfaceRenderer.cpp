#include "Compositor/Surface/SurfaceRenderer.hpp"

#include "Compositor/Chrome/WindowChromeRenderer.hpp"
#include "Detail/ResizeTrace.hpp"

#include <Flux/Core/Geometry.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <memory>
#include <vector>
#include <unistd.h>

namespace flux::compositor {
namespace {

float clamp01(float value) {
  return std::clamp(value, 0.f, 1.f);
}

float easeOutCubic(float value) {
  float const t = clamp01(value);
  float const inverse = 1.f - t;
  return 1.f - inverse * inverse * inverse;
}

bool renderSnapshotChanged(CommittedSurfaceSnapshot const& current,
                           SurfaceVisualState const& visual) {
  if (!visual.hasLastSnapshot) return true;
  auto const& previous = visual.lastSnapshot;
  return current.x != previous.x || current.y != previous.y ||
         current.width != previous.width || current.height != previous.height ||
         current.bufferWidth != previous.bufferWidth || current.bufferHeight != previous.bufferHeight ||
         current.activeSizing != previous.activeSizing ||
         current.serial != previous.serial ||
         current.sourceX != previous.sourceX || current.sourceY != previous.sourceY ||
         current.sourceWidth != previous.sourceWidth || current.sourceHeight != previous.sourceHeight ||
         current.destinationWidth != previous.destinationWidth ||
         current.destinationHeight != previous.destinationHeight;
}

} // namespace

bool shouldTraceRenderSnapshot(CommittedSurfaceSnapshot const& current,
                               SurfaceVisualState const& visual) {
  return detail::resizeTraceEnabled() && renderSnapshotChanged(current, visual);
}

void drawSurfaceImage(Canvas& canvas,
                      CommittedSurfaceSnapshot const& surface,
                      Image& image,
                      float opacity,
                      float scale) {
  float const windowX = static_cast<float>(surface.x);
  float const windowY = static_cast<float>(surface.y);
  float const windowWidth = static_cast<float>(surface.width);
  float const windowHeight = static_cast<float>(surface.height);
  float const titleBarHeight = static_cast<float>(surface.titleBarHeight);
  float const outerHeight = windowHeight + titleBarHeight;
  Point const pivot{windowX + windowWidth * 0.5f, windowY - titleBarHeight + outerHeight * 0.5f};
  canvas.save();
  canvas.setOpacity(canvas.opacity() * opacity);
  if (scale != 1.f) {
    canvas.translate(pivot.x, pivot.y);
    canvas.scale(scale);
    canvas.translate(-pivot.x, -pivot.y);
  }
  float const sourceWidth = surface.sourceWidth > 0.f
                                ? surface.sourceWidth
                                : static_cast<float>(image.size().width);
  float const sourceHeight = surface.sourceHeight > 0.f
                                 ? surface.sourceHeight
                                 : static_cast<float>(image.size().height);
  canvas.drawImage(image,
                   Rect::sharp(surface.sourceX,
                               surface.sourceY,
                               sourceWidth,
                               sourceHeight),
                   Rect::sharp(windowX,
                               windowY,
                               windowWidth,
                               windowHeight));
  canvas.restore();
}

void updateCachedImage(WaylandServer& wayland,
                       Canvas& canvas,
                       CommittedSurfaceSnapshot const& surface,
                       CachedClientImage& cached) {
  if (cached.image && cached.id == surface.id && cached.serial == surface.serial) return;

  cached.id = surface.id;
  cached.serial = surface.serial;
  cached.image.reset();
  cached.logged = false;
  std::int32_t const bufferWidth = surface.bufferWidth > 0 ? surface.bufferWidth : surface.width;
  std::int32_t const bufferHeight = surface.bufferHeight > 0 ? surface.bufferHeight : surface.height;
  if (!surface.rgbaPixels.empty()) {
    cached.image = Image::fromRgbaPixels(static_cast<std::uint32_t>(bufferWidth),
                                         static_cast<std::uint32_t>(bufferHeight),
                                         surface.rgbaPixels,
                                         canvas.gpuDevice());
  } else if (!surface.dmabufPlanes.empty()) {
    std::vector<int> fds = wayland.duplicateDmabufFds(surface.id);
    if (fds.size() == surface.dmabufPlanes.size()) {
      std::vector<Image::DmabufPlane> planes;
      planes.reserve(surface.dmabufPlanes.size());
      for (std::size_t i = 0; i < surface.dmabufPlanes.size(); ++i) {
        planes.push_back({
            .fd = fds[i],
            .offset = surface.dmabufPlanes[i].offset,
            .stride = surface.dmabufPlanes[i].stride,
            .modifier = surface.dmabufPlanes[i].modifier,
        });
        fds[i] = -1; // fromDmabuf consumes the fd.
      }
      try {
        cached.image = Image::fromDmabuf({
            .width = static_cast<std::uint32_t>(bufferWidth),
            .height = static_cast<std::uint32_t>(bufferHeight),
            .drmFormat = surface.dmabufFormat,
            .planes = planes,
        });
      } catch (std::exception const& error) {
        std::fprintf(stderr, "flux-compositor: dmabuf Vulkan import failed: %s\n", error.what());
      }
    }
    for (int fd : fds) {
      if (fd >= 0) close(fd);
    }
    if (cached.image && !cached.logged) {
      std::fprintf(stderr, "flux-compositor: displaying DMABUF via Vulkan import\n");
      cached.logged = true;
    }
    if (cached.image) return;

    std::vector<std::uint8_t> fallbackPixels;
    if (wayland.copyDmabufToRgba(surface.id, fallbackPixels)) {
      cached.image = Image::fromRgbaPixels(static_cast<std::uint32_t>(bufferWidth),
                                           static_cast<std::uint32_t>(bufferHeight),
                                           fallbackPixels,
                                           canvas.gpuDevice());
      if (cached.image && !cached.logged) {
        std::fprintf(stderr, "flux-compositor: displaying readable DMABUF contents\n");
        cached.logged = true;
      }
    }
  }
}

void drawCommittedSurface(WaylandServer& wayland,
                          Canvas& canvas,
                          TextSystem& textSystem,
                          CommittedSurfaceSnapshot const& surface,
                          SurfaceVisualState& visual,
                          CachedClientImage& cached,
                          std::chrono::steady_clock::time_point frameTime,
                          ChromeConfig const& chrome,
                          bool animationsEnabled) {
  if (visual.firstSeen.time_since_epoch().count() == 0) visual.firstSeen = frameTime;
  updateCachedImage(wayland, canvas, surface, cached);
  if (!cached.image) return;

  if (shouldTraceRenderSnapshot(surface, visual)) {
    auto const imageSize = cached.image->size();
    detail::resizeTrace(
        "compositor-render",
        "render-snapshot surface=%llu window=%d,%d frame=%dx%d buffer=%dx%d "
        "image=%dx%d source=%.1f,%.1f %.1fx%.1f dest=%dx%d serial=%llu\n",
        static_cast<unsigned long long>(surface.id),
        surface.x,
        surface.y,
        surface.width,
        surface.height,
        surface.bufferWidth,
        surface.bufferHeight,
        static_cast<int>(imageSize.width),
        static_cast<int>(imageSize.height),
        surface.sourceX,
        surface.sourceY,
        surface.sourceWidth,
        surface.sourceHeight,
        surface.destinationWidth,
        surface.destinationHeight,
        static_cast<unsigned long long>(surface.serial));
  }
  visual.lastSnapshot = surface;
  visual.hasLastSnapshot = true;

  float const windowX = static_cast<float>(surface.x);
  float const windowY = static_cast<float>(surface.y);
  float const windowWidth = static_cast<float>(surface.width);
  float const windowHeight = static_cast<float>(surface.height);
  float const titleBarHeight = static_cast<float>(surface.titleBarHeight);
  bool const cutoutChrome = surface.serverSideDecorated && surface.cutoutsBound && !surface.cutoutsRejected;
  CornerRadius const contentCorners = cutoutChrome
                                          ? CornerRadius{chrome.windowCornerRadius}
                                          : (titleBarHeight > 0.f
                                                 ? CornerRadius{0.f, 0.f, chrome.windowCornerRadius, chrome.windowCornerRadius}
                                                 : CornerRadius{});
  float const animationMs = static_cast<float>(
      std::chrono::duration_cast<std::chrono::milliseconds>(frameTime - visual.firstSeen).count());
  float const openProgress = animationsEnabled ? easeOutCubic(animationMs / 140.f) : 1.f;
  float const openScale = 0.965f + 0.035f * openProgress;
  float const openOpacity = openProgress;
  float const outerHeight = windowHeight + titleBarHeight;
  Point const pivot{windowX + windowWidth * 0.5f, windowY - titleBarHeight + outerHeight * 0.5f};
  canvas.save();
  canvas.setOpacity(canvas.opacity() * openOpacity);
  if (openScale < 1.f) {
    canvas.translate(pivot.x, pivot.y);
    canvas.scale(openScale);
    canvas.translate(-pivot.x, -pivot.y);
  }
  if (!cutoutChrome) drawWindowChrome(canvas, textSystem, surface, chrome);
  float const sourceWidth = surface.sourceWidth > 0.f
                                ? surface.sourceWidth
                                : static_cast<float>(cached.image->size().width);
  float const sourceHeight = surface.sourceHeight > 0.f
                                 ? surface.sourceHeight
                                 : static_cast<float>(cached.image->size().height);
  bool const clientContentSmallerThanFrame =
      surface.destinationWidth > 0 &&
      surface.destinationHeight > 0 &&
      (surface.destinationWidth != static_cast<int>(std::lround(windowWidth)) ||
       surface.destinationHeight != static_cast<int>(std::lround(windowHeight)));
  float const contentWidth = clientContentSmallerThanFrame
                                 ? static_cast<float>(surface.destinationWidth)
                                 : windowWidth;
  float const contentHeight = clientContentSmallerThanFrame
                                  ? static_cast<float>(surface.destinationHeight)
                                  : windowHeight;
  canvas.save();
  canvas.clipRect(Rect::sharp(windowX, windowY, windowWidth, windowHeight), contentCorners);
  canvas.drawImage(*cached.image,
                   Rect::sharp(surface.sourceX,
                               surface.sourceY,
                               sourceWidth,
                               sourceHeight),
                   Rect::sharp(windowX,
                               windowY,
                               contentWidth,
                               contentHeight),
                   clientContentSmallerThanFrame ? CornerRadius{} : contentCorners);
  if (clientContentSmallerThanFrame) {
    float const rightPad = std::max(0.f, windowWidth - contentWidth);
    float const bottomPad = std::max(0.f, windowHeight - contentHeight);
    float const edgeSourceWidth = std::max(1.f, sourceWidth);
    float const edgeSourceHeight = std::max(1.f, sourceHeight);
    if (rightPad > 0.f) {
      canvas.drawImage(*cached.image,
                       Rect::sharp(surface.sourceX + edgeSourceWidth - 1.f,
                                   surface.sourceY,
                                   1.f,
                                   edgeSourceHeight),
                       Rect::sharp(windowX + contentWidth,
                                   windowY,
                                   rightPad,
                                   contentHeight),
                       CornerRadius{});
    }
    if (bottomPad > 0.f) {
      canvas.drawImage(*cached.image,
                       Rect::sharp(surface.sourceX,
                                   surface.sourceY + edgeSourceHeight - 1.f,
                                   edgeSourceWidth,
                                   1.f),
                       Rect::sharp(windowX,
                                   windowY + contentHeight,
                                   contentWidth,
                                   bottomPad),
                       CornerRadius{});
    }
    if (rightPad > 0.f && bottomPad > 0.f) {
      canvas.drawImage(*cached.image,
                       Rect::sharp(surface.sourceX + edgeSourceWidth - 1.f,
                                   surface.sourceY + edgeSourceHeight - 1.f,
                                   1.f,
                                   1.f),
                       Rect::sharp(windowX + contentWidth,
                                   windowY + contentHeight,
                                   rightPad,
                                   bottomPad),
                       CornerRadius{});
    }
  }
  canvas.restore();
  if (cutoutChrome) drawWindowChrome(canvas, textSystem, surface, chrome);
  canvas.restore();
}

void captureClosingSurfaces(SurfaceRenderState& state,
                            std::unordered_set<std::uint64_t> const& liveSurfaceIds,
                            std::chrono::steady_clock::time_point frameTime,
                            bool animationsEnabled) {
  for (auto const& [surfaceId, visual] : state.surfaceVisuals) {
    if (liveSurfaceIds.contains(surfaceId)) continue;
    auto cached = state.clientImages.find(surfaceId);
    if (!animationsEnabled || !visual.hasLastSnapshot || cached == state.clientImages.end() ||
        !cached->second.image) {
      continue;
    }
    state.closingSurfaces[surfaceId] = ClosingSurfaceVisual{
        .snapshot = visual.lastSnapshot,
        .image = cached->second.image,
        .closedAt = frameTime,
    };
  }
}

void drawClosingSurfaces(Canvas& canvas,
                         SurfaceRenderState& state,
                         std::chrono::steady_clock::time_point frameTime) {
  for (auto it = state.closingSurfaces.begin(); it != state.closingSurfaces.end();) {
    float const closeMs = static_cast<float>(
        std::chrono::duration_cast<std::chrono::milliseconds>(frameTime - it->second.closedAt).count());
    float const progress = clamp01(closeMs / 120.f);
    if (progress >= 1.f || !it->second.image) {
      it = state.closingSurfaces.erase(it);
      continue;
    }
    float const eased = easeOutCubic(progress);
    drawSurfaceImage(canvas, it->second.snapshot, *it->second.image, 1.f - eased, 1.f - 0.025f * eased);
    ++it;
  }
}

void pruneSurfaceRenderState(SurfaceRenderState& state,
                             std::unordered_set<std::uint64_t> const& liveSurfaceIds) {
  for (auto it = state.clientImages.begin(); it != state.clientImages.end();) {
    if (liveSurfaceIds.contains(it->first)) {
      ++it;
    } else {
      it = state.clientImages.erase(it);
    }
  }
  for (auto it = state.surfaceVisuals.begin(); it != state.surfaceVisuals.end();) {
    if (liveSurfaceIds.contains(it->first)) {
      ++it;
    } else {
      it = state.surfaceVisuals.erase(it);
    }
  }
}

} // namespace flux::compositor
