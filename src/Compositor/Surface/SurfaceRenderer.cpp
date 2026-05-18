#include "Compositor/Surface/SurfaceRenderer.hpp"

#include "Compositor/Chrome/WindowChromeRenderer.hpp"
#include "Detail/ResizeTrace.hpp"

#include <Flux/Core/Geometry.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <unistd.h>
#include <vector>

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

bool shouldTraceRenderSnapshot(CommittedSurfaceSnapshot const& current,
                               SurfaceVisualState const& visual) {
  if (!detail::resizeTraceEnabled()) return false;
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
    std::vector<std::uint8_t> fallbackPixels;
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
      }
      try {
        cached.image = Image::fromDmabuf({
            .width = static_cast<std::uint32_t>(bufferWidth),
            .height = static_cast<std::uint32_t>(bufferHeight),
            .drmFormat = surface.dmabufFormat,
            .planes = planes,
        });
        if (cached.image && !cached.logged) {
          std::fprintf(stderr, "flux-compositor: imported DMABUF as Vulkan image\n");
        }
      } catch (std::exception const& e) {
        std::fprintf(stderr, "flux-compositor: dmabuf import failed: %s\n", e.what());
      }
    } else {
      for (int fd : fds) close(fd);
    }
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
  drawWindowChrome(canvas, textSystem, surface);
  float const sourceWidth = surface.sourceWidth > 0.f
                                ? surface.sourceWidth
                                : static_cast<float>(cached.image->size().width);
  float const sourceHeight = surface.sourceHeight > 0.f
                                 ? surface.sourceHeight
                                 : static_cast<float>(cached.image->size().height);
  bool const staleResizeBuffer =
      surface.activeSizing &&
      (surface.destinationWidth != static_cast<int>(std::lround(windowWidth)) ||
       surface.destinationHeight != static_cast<int>(std::lround(windowHeight)));
  float const contentWidth = staleResizeBuffer
                                 ? static_cast<float>(surface.destinationWidth)
                                 : windowWidth;
  float const contentHeight = staleResizeBuffer
                                  ? static_cast<float>(surface.destinationHeight)
                                  : windowHeight;
  canvas.save();
  canvas.clipRect(Rect::sharp(windowX, windowY, windowWidth, windowHeight));
  canvas.drawImage(*cached.image,
                   Rect::sharp(surface.sourceX,
                               surface.sourceY,
                               sourceWidth,
                               sourceHeight),
                   Rect::sharp(windowX,
                               windowY,
                               contentWidth,
                               contentHeight));
  canvas.restore();
  canvas.restore();
}

} // namespace flux::compositor
