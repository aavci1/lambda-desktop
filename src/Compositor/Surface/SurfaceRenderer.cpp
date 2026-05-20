#include "Compositor/Surface/SurfaceRenderer.hpp"

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

} // namespace

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
  updateCachedImage(wayland, canvas, surface, cached);
  if (!cached.image) return;
  drawCommittedSurfaceSnapshot(canvas,
                               textSystem,
                               surface,
                               visual,
                               *cached.image,
                               frameTime,
                               chrome,
                               animationsEnabled);
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
