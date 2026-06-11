#include "Compositor/Surface/SurfaceRenderer.hpp"

#include "Compositor/Diagnostics/CpuTrace.hpp"
#include "Compositor/Diagnostics/CrashLog.hpp"
#include "Compositor/Chrome/WindowChromeRenderer.hpp"
#include "Compositor/Surface/CommittedSurfaceSnapshotState.hpp"
#include "Compositor/Surface/SurfaceUploadDamage.hpp"
#include "Detail/ResizeTrace.hpp"
#include "Graphics/Vulkan/VulkanCanvas.hpp"
#include "Graphics/Vulkan/VulkanFrameRecorder.hpp"

#include <Lambda/Core/Geometry.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <unistd.h>
#include <vector>

namespace lambda::compositor {
namespace {

using TraceClock = std::chrono::steady_clock;

double elapsedMilliseconds(TraceClock::time_point start) {
  return std::chrono::duration<double, std::milli>(TraceClock::now() - start).count();
}

constexpr std::size_t kMaxCachedDmabufImagesPerSurface = 8;

void hashCombine(std::uint64_t &hash, void const *data, std::size_t size) {
  auto const *bytes = static_cast<std::uint8_t const *>(data);
  for (std::size_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= 1099511628211ull;
  }
}

template <typename T> void hashValue(std::uint64_t &hash, T const &value) { hashCombine(hash, &value, sizeof(value)); }

void hashColor(std::uint64_t &hash, Color color) {
  hashValue(hash, color.r);
  hashValue(hash, color.g);
  hashValue(hash, color.b);
  hashValue(hash, color.a);
}

void hashCornerRadius(std::uint64_t &hash, CornerRadius radius) {
  hashValue(hash, radius.topLeft);
  hashValue(hash, radius.topRight);
  hashValue(hash, radius.bottomRight);
  hashValue(hash, radius.bottomLeft);
}

struct SurfaceDrawSignatureHasher {
  std::uint64_t& hash;

  template <typename T>
  void operator()(T const& value) const {
    hashValue(hash, value);
  }

  void operator()(std::string const& value) const {
    hashValue(hash, value.size());
    hashCombine(hash, value.data(), value.size());
  }
};

std::uint64_t surfaceDrawSignature(CommittedSurfaceSnapshot const &surface, CachedClientImage const &cached,
                                   Image const &image, ChromeConfig const &chrome) {
  std::uint64_t hash = 1469598103934665603ull;
  SurfaceDrawSignatureHasher const hashSurfaceValue{hash};
  hashValue(hash, surface.id);
  hashValue(hash, surface.serial);
  hashValue(hash, surface.dmabufBufferId);
  visitCommittedSurfaceContentShape(surface, hashSurfaceValue);
  visitCommittedSurfaceFrameVisualState(surface, hashSurfaceValue);
  hashValue(hash, reinterpret_cast<std::uintptr_t>(&image));
  hashValue(hash, cached.serial);
  hashValue(hash, cached.dmabufBufferId);
  hashValue(hash, chrome.titleBarHeight);
  hashValue(hash, chrome.controlsWidth);
  hashValue(hash, chrome.controlsInsetRight);
  hashValue(hash, chrome.controlsInsetTop);
  hashValue(hash, chrome.buttonSize);
  hashValue(hash, chrome.buttonRadius);
  hashValue(hash, chrome.buttonGap);
  hashValue(hash, chrome.glass.opacity);
  hashColor(hash, chrome.glass.baseColor);
  hashColor(hash, chrome.glass.tintColor);
  hashColor(hash, chrome.glass.borderColor);
  hashColor(hash, chrome.glass.contrastColor);
  hashValue(hash, chrome.glass.focusedContrastOpacity);
  hashValue(hash, chrome.glass.unfocusedContrastOpacity);
  hashColor(hash, chrome.closeGlyphColor);
  hashColor(hash, chrome.closeGlyphHoverColor);
  hashColor(hash, chrome.closeHoverBackground);
  hashColor(hash, chrome.minimizeGlyphColor);
  hashColor(hash, chrome.minimizeGlyphHoverColor);
  hashColor(hash, chrome.minimizeHoverBackground);
  hashColor(hash, chrome.titleTextColor);
  hashValue(hash, chrome.titleTextFontSize);
  hashValue(hash, chrome.titleTextFontWeight);
  hashColor(hash, chrome.windowBorderColor);
  hashValue(hash, chrome.windowBorderWidth);
  hashColor(hash, chrome.borderLineColor);
  hashColor(hash, chrome.insetHighlightColor);
  hashColor(hash, chrome.focusedShadowColor);
  hashColor(hash, chrome.unfocusedShadowColor);
  hashCornerRadius(hash, chrome.windowCornerRadius);
  return hash;
}

bool surfaceOpenAnimationComplete(SurfaceVisualState const &visual, std::chrono::steady_clock::time_point frameTime,
                                  bool animationsEnabled) {
  if (!animationsEnabled)
    return true;
  if (visual.firstSeen.time_since_epoch().count() == 0)
    return false;
  return frameTime - visual.firstSeen >= kSurfaceOpenAnimationDuration;
}

bool hasTransientChromeState(CommittedSurfaceSnapshot const &surface) {
  return surface.closeButtonHovered ||
         surface.closeButtonPressed ||
         surface.maximizeButtonHovered ||
         surface.maximizeButtonPressed ||
         surface.minimizeButtonHovered ||
         surface.minimizeButtonPressed;
}

CommittedSurfaceSnapshot stableChromeSnapshot(CommittedSurfaceSnapshot surface) {
  surface.closeButtonHovered = false;
  surface.closeButtonPressed = false;
  surface.maximizeButtonHovered = false;
  surface.maximizeButtonPressed = false;
  surface.minimizeButtonHovered = false;
  surface.minimizeButtonPressed = false;
  return surface;
}

void drawTransientChromeControls(Canvas& canvas,
                                 CommittedSurfaceSnapshot const& surface,
                                 ChromeConfig const& chrome) {
  if (!hasTransientChromeState(surface)) return;
  drawWindowChromeActiveControls(canvas, surface, chrome);
}

std::span<std::uint8_t const> surfacePixelBytes(CommittedSurfaceSnapshot const &surface) {
  if (surface.shmPixels && surface.shmPixelBytes > 0) {
    return {surface.shmPixels, surface.shmPixelBytes};
  }
  if (surface.rgbaPixels && !surface.rgbaPixels->empty()) {
    return *surface.rgbaPixels;
  }
  return {};
}

bool setCanvasImagePremultipliedAlpha(Canvas* canvas, bool enabled) {
#if LAMBDA_VULKAN
  return setVulkanCanvasImagePremultipliedAlpha(canvas, enabled);
#else
  (void)canvas;
  return enabled;
#endif
}

void markCachedImageContentsChanged(Image* image) {
#if LAMBDA_VULKAN
  (void)markVulkanImageContentsChanged(image);
#else
  (void)image;
#endif
}

bool destinationMatchesWindow(CommittedSurfaceSnapshot const &surface) {
  return (surface.destinationWidth <= 0 || surface.destinationWidth == surface.width) &&
         (surface.destinationHeight <= 0 || surface.destinationHeight == surface.height);
}

bool rectCoversBuffer(CommittedSurfaceSnapshot::RegionRect const& rect,
                      std::int32_t bufferWidth,
                      std::int32_t bufferHeight) {
  return rect.x <= 0 && rect.y <= 0 &&
         rect.width >= bufferWidth &&
         rect.height >= bufferHeight;
}

bool damageCoversFullBuffer(std::vector<CommittedSurfaceSnapshot::RegionRect> const& damage,
                            std::int32_t bufferWidth,
                            std::int32_t bufferHeight) {
  return std::any_of(damage.begin(), damage.end(), [&](auto const& rect) {
    return rectCoversBuffer(rect, bufferWidth, bufferHeight);
  });
}

bool updateDamagedImageRegions(Canvas& canvas,
                               Image& image,
                               std::span<std::uint8_t const> pixels,
                               Image::PixelFormat pixelFormat,
                               std::int32_t bufferWidth,
                               std::int32_t bufferHeight,
                               std::vector<CommittedSurfaceSnapshot::RegionRect> const& damage,
                               std::vector<CommittedSurfaceSnapshot::RegionRect>& uploadDamage) {
  if (pixels.empty() || damage.empty() || bufferWidth <= 0 || bufferHeight <= 0 ||
      damageCoversFullBuffer(damage, bufferWidth, bufferHeight)) {
    return false;
  }
  std::size_t const fullSize = static_cast<std::size_t>(bufferWidth) * static_cast<std::size_t>(bufferHeight) * 4u;
  if (pixels.size() != fullSize) return false;
  buildSurfaceUploadDamageRects(damage, uploadDamage, bufferWidth, bufferHeight);
  if (uploadDamage.empty() || damageCoversFullBuffer(uploadDamage, bufferWidth, bufferHeight)) return false;

  auto const updateStart = diagnostics::cpuTraceNow();
  std::size_t uploadedBytes = 0;
  std::size_t const sourceStride = static_cast<std::size_t>(bufferWidth) * 4u;
  if (sourceStride > std::numeric_limits<std::uint32_t>::max()) return false;
  for (auto const& damageRect : uploadDamage) {
    std::int32_t const left = damageRect.x;
    std::int32_t const top = damageRect.y;
    std::int32_t const width = damageRect.width;
    std::int32_t const height = damageRect.height;

    std::size_t const rowBytes = static_cast<std::size_t>(width) * 4u;
    std::size_t const sourceOffset =
        static_cast<std::size_t>(top) * sourceStride + static_cast<std::size_t>(left) * 4u;
    std::size_t const sourceBytes = sourceStride * static_cast<std::size_t>(height - 1) + rowBytes;
    if (sourceOffset > pixels.size() || sourceBytes > pixels.size() - sourceOffset) {
      return false;
    }
    std::span<std::uint8_t const> regionPixels{pixels.data() + sourceOffset, sourceBytes};
    if (!image.updatePixelsRegion(regionPixels,
                                  pixelFormat,
                                  static_cast<std::uint32_t>(left),
                                  static_cast<std::uint32_t>(top),
                                  static_cast<std::uint32_t>(width),
                                  static_cast<std::uint32_t>(height),
                                  canvas.gpuDevice(),
                                  static_cast<std::uint32_t>(sourceStride))) {
      return false;
    }
    uploadedBytes += rowBytes * static_cast<std::size_t>(height);
  }
  if (uploadedBytes == 0) return false;
  diagnostics::recordSurfaceImageUpload(uploadedBytes,
                                        diagnostics::cpuTraceElapsedMilliseconds(updateStart),
                                        false);
  return true;
}

bool hasCompositorMaterial(CommittedSurfaceSnapshot const &surface, ChromeConfig const &chrome) {
  (void)chrome;
  return !surface.backgroundBlurRects.empty();
}

bool canDrawPlainClientSurfaceDirectly(CommittedSurfaceSnapshot const &surface,
                                       ChromeConfig const &chrome,
                                       SurfaceVisualState const &visual,
                                       std::chrono::steady_clock::time_point frameTime,
                                       bool animationsEnabled) {
  return !surface.serverSideDecorated &&
         surface.windowClipTop <= 0 &&
         surface.windowClipBottom <= 0 &&
         !surface.activeSizing &&
         !surface.pacingSizing &&
         !surface.geometryAnimationGrowing &&
         !hasTransientChromeState(surface) &&
         !hasCompositorMaterial(surface, chrome) &&
         destinationMatchesWindow(surface) &&
         surfaceOpenAnimationComplete(visual, frameTime, animationsEnabled);
}

void drawPlainClientSurface(Canvas &canvas, CommittedSurfaceSnapshot const &surface, Image &image) {
  float const sourceWidth = surface.sourceWidth > 0.f ? surface.sourceWidth : static_cast<float>(image.size().width);
  float const sourceHeight = surface.sourceHeight > 0.f ? surface.sourceHeight : static_cast<float>(image.size().height);
  bool const previousPremultiplied = setCanvasImagePremultipliedAlpha(&canvas, true);
  canvas.drawImage(image,
                   Rect::sharp(surface.sourceX, surface.sourceY, sourceWidth, sourceHeight),
                   Rect::sharp(static_cast<float>(surface.x),
                               static_cast<float>(surface.y),
                               static_cast<float>(surface.width),
                               static_cast<float>(surface.height)));
  setCanvasImagePremultipliedAlpha(&canvas, previousPremultiplied);
}

} // namespace

void updateCachedImage(WaylandServer &wayland, Canvas &canvas, CommittedSurfaceSnapshot const &surface,
                       CachedClientImage &cached) {
  bool const hasDmabuf = !surface.dmabufPlanes.empty();
  std::span<std::uint8_t const> pixels = surfacePixelBytes(surface);
  if (cached.id != 0 && cached.id != surface.id) {
    cached = {};
  }
  if (cached.image && cached.id == surface.id) {
    if (hasDmabuf && cached.dmabufBufferId == surface.dmabufBufferId) {
      if (cached.serial != surface.serial) {
        markCachedImageContentsChanged(cached.image.get());
        for (CachedClientImage::DmabufEntry& entry : cached.dmabufImages) {
          if (entry.bufferId == surface.dmabufBufferId) {
            entry.serial = surface.serial;
          }
        }
      }
      cached.serial = surface.serial;
      wayland.consumeSurfaceDamage(surface.id, surface.serial);
      return;
    }
    if (!hasDmabuf && cached.serial == surface.serial) {
      wayland.consumeSurfaceDamage(surface.id, surface.serial);
      return;
    }
  }

  cached.id = surface.id;
  cached.serial = surface.serial;
  cached.dmabufBufferId = surface.dmabufBufferId;
  std::int32_t const bufferWidth = surface.bufferWidth > 0 ? surface.bufferWidth : surface.width;
  std::int32_t const bufferHeight = surface.bufferHeight > 0 ? surface.bufferHeight : surface.height;
  if (!pixels.empty()) {
    cached.dmabufBufferId = 0;
    cached.dmabufImported = false;
    std::size_t const uploadBytes = pixels.size();
    if (cached.image && cached.shmBufferWidth == bufferWidth && cached.shmBufferHeight == bufferHeight &&
        updateDamagedImageRegions(canvas,
                                  *cached.image,
                                  pixels,
                                  surface.pixelFormat,
                                  bufferWidth,
                                  bufferHeight,
                                  surface.bufferDamageRects,
                                  cached.uploadDamageRects)) {
      wayland.consumeSurfaceDamage(surface.id, surface.serial);
      return;
    }
    if (cached.image && cached.shmBufferWidth == bufferWidth && cached.shmBufferHeight == bufferHeight && [&] {
          auto const updateStart = diagnostics::cpuTraceNow();
          bool const updated = cached.image->updatePixels(pixels, surface.pixelFormat, canvas.gpuDevice());
          diagnostics::recordSurfaceImageUpload(uploadBytes, diagnostics::cpuTraceElapsedMilliseconds(updateStart),
                                                false);
          return updated;
        }()) {
      wayland.consumeSurfaceDamage(surface.id, surface.serial);
      return;
    }
    cached.dmabufImages.clear();
    cached.logged = false;
    auto const createStart = diagnostics::cpuTraceNow();
    cached.image = Image::fromPixels(static_cast<std::uint32_t>(bufferWidth), static_cast<std::uint32_t>(bufferHeight),
                                     pixels, surface.pixelFormat, canvas.gpuDevice());
    diagnostics::recordSurfaceImageUpload(uploadBytes, diagnostics::cpuTraceElapsedMilliseconds(createStart), true);
    cached.shmBufferWidth = bufferWidth;
    cached.shmBufferHeight = bufferHeight;
    if (cached.image) wayland.consumeSurfaceDamage(surface.id, surface.serial);
  } else if (!surface.dmabufPlanes.empty()) {
    cached.image.reset();
    cached.dmabufImported = false;
    cached.shmBufferWidth = 0;
    cached.shmBufferHeight = 0;
    auto cachedDmabuf = std::find_if(cached.dmabufImages.begin(), cached.dmabufImages.end(),
                                     [&](CachedClientImage::DmabufEntry const &entry) {
                                       return entry.bufferId == surface.dmabufBufferId && entry.image;
                                     });
    if (cachedDmabuf != cached.dmabufImages.end()) {
      if (cachedDmabuf->serial != surface.serial) {
        markCachedImageContentsChanged(cachedDmabuf->image.get());
        cachedDmabuf->serial = surface.serial;
      }
      cached.image = cachedDmabuf->image;
      cached.dmabufImported = cachedDmabuf->imported;
      CachedClientImage::DmabufEntry entry = std::move(*cachedDmabuf);
      cached.dmabufImages.erase(cachedDmabuf);
      cached.dmabufImages.push_back(std::move(entry));
      wayland.consumeSurfaceDamage(surface.id, surface.serial);
      return;
    }

    auto const importStart = TraceClock::now();
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
        cached.dmabufImported = cached.image != nullptr;
        diagnostics::recordDmabufImport(elapsedMilliseconds(importStart), cached.dmabufImported);
        if (cached.image) {
          LAMBDA_RESIZE_TRACE(
              "compositor-render", "dmabuf-cache-import surface=%llu buffer=%llu imported=1 elapsed=%.3fms\n",
              static_cast<unsigned long long>(surface.id), static_cast<unsigned long long>(surface.dmabufBufferId),
              elapsedMilliseconds(importStart));
        }
      } catch (std::exception const &error) {
        diagnostics::recordDmabufImport(elapsedMilliseconds(importStart), false);
        std::fprintf(stderr, "lambda-window-manager: dmabuf Vulkan import failed: %s\n", error.what());
        diagnostics::crashLog("dmabuf-import-failed surface=%llu buffer=%llu size=%dx%d format=0x%08x error=%s",
                              static_cast<unsigned long long>(surface.id),
                              static_cast<unsigned long long>(surface.dmabufBufferId), bufferWidth, bufferHeight,
                              surface.dmabufFormat, error.what());
      }
    }
    for (int fd : fds) {
      if (fd >= 0)
        close(fd);
    }
    if (cached.image && !cached.logged) {
      std::fprintf(stderr, "lambda-window-manager: displaying DMABUF via Vulkan import\n");
      cached.logged = true;
    }
    if (cached.image) {
      cached.dmabufImages.push_back(CachedClientImage::DmabufEntry{
          .bufferId = surface.dmabufBufferId,
          .serial = surface.serial,
          .image = cached.image,
          .imported = cached.dmabufImported,
      });
      while (cached.dmabufImages.size() > kMaxCachedDmabufImagesPerSurface) {
        cached.dmabufImages.erase(cached.dmabufImages.begin());
      }
      wayland.consumeSurfaceDamage(surface.id, surface.serial);
      return;
    }

    std::vector<std::uint8_t> fallbackPixels;
    auto const fallbackStart = diagnostics::cpuTraceNow();
    if (wayland.copyDmabufToRgba(surface.id, fallbackPixels)) {
      std::size_t const fallbackBytes = fallbackPixels.size();
      cached.image =
          Image::fromRgbaPixels(static_cast<std::uint32_t>(bufferWidth), static_cast<std::uint32_t>(bufferHeight),
                                fallbackPixels, canvas.gpuDevice());
      double const fallbackMs = diagnostics::cpuTraceElapsedMilliseconds(fallbackStart);
      diagnostics::recordDmabufFallbackCopy(fallbackBytes, fallbackMs, cached.image != nullptr);
      if (cached.image && !cached.dmabufFallbackLogged) {
        cached.dmabufFallbackLogged = true;
        cached.logged = true;
        std::fprintf(stderr,
                     "lambda-window-manager: dmabuf CPU fallback active surface=%llu buffer=%llu "
                     "size=%dx%d format=0x%08x bytes=%zu elapsed=%.3fms\n",
                     static_cast<unsigned long long>(surface.id),
                     static_cast<unsigned long long>(surface.dmabufBufferId),
                     bufferWidth,
                     bufferHeight,
                     surface.dmabufFormat,
                     fallbackBytes,
                     fallbackMs);
        diagnostics::crashLog("dmabuf-cpu-fallback surface=%llu buffer=%llu size=%dx%d format=0x%08x bytes=%zu elapsed=%.3fms",
                              static_cast<unsigned long long>(surface.id),
                              static_cast<unsigned long long>(surface.dmabufBufferId),
                              bufferWidth,
                              bufferHeight,
                              surface.dmabufFormat,
                              fallbackBytes,
                              fallbackMs);
      }
      if (cached.image) {
        LAMBDA_RESIZE_TRACE("compositor-render",
                            "dmabuf-cache-import surface=%llu buffer=%llu imported=0 elapsed=%.3fms\n",
                            static_cast<unsigned long long>(surface.id),
                            static_cast<unsigned long long>(surface.dmabufBufferId), elapsedMilliseconds(importStart));
        cached.dmabufImages.push_back(CachedClientImage::DmabufEntry{
            .bufferId = surface.dmabufBufferId,
            .serial = surface.serial,
            .image = cached.image,
            .imported = false,
        });
        while (cached.dmabufImages.size() > kMaxCachedDmabufImagesPerSurface) {
          cached.dmabufImages.erase(cached.dmabufImages.begin());
        }
        wayland.consumeSurfaceDamage(surface.id, surface.serial);
      }
    } else {
      diagnostics::recordDmabufFallbackCopy(0, diagnostics::cpuTraceElapsedMilliseconds(fallbackStart), false);
      diagnostics::crashLog("dmabuf-fallback-failed surface=%llu buffer=%llu size=%dx%d format=0x%08x",
                            static_cast<unsigned long long>(surface.id),
                            static_cast<unsigned long long>(surface.dmabufBufferId), bufferWidth, bufferHeight,
                            surface.dmabufFormat);
    }
  } else {
    cached.image.reset();
    cached.dmabufImages.clear();
    cached.dmabufBufferId = 0;
    cached.shmBufferWidth = 0;
    cached.shmBufferHeight = 0;
    cached.dmabufImported = false;
  }
}

void drawCommittedSurface(WaylandServer &wayland, Canvas &canvas, TextSystem &textSystem,
                          CommittedSurfaceSnapshot const &surface, SurfaceVisualState &visual,
                          CachedClientImage &cached, std::chrono::steady_clock::time_point frameTime,
                          ChromeConfig const &chrome, bool animationsEnabled) {
  updateCachedImage(wayland, canvas, surface, cached);
  if (!cached.image)
    return;

  if (visual.firstSeen.time_since_epoch().count() == 0) {
    visual.firstSeen = frameTime;
  }
  if (canDrawPlainClientSurfaceDirectly(surface, chrome, visual, frameTime, animationsEnabled)) {
    drawPlainClientSurface(canvas, surface, *cached.image);
    visual.lastSnapshot = retainedSurfaceRenderSnapshot(surface);
    visual.hasLastSnapshot = true;
    return;
  }
  bool const cacheBackendOk = canvas.backend() == Backend::Vulkan;
  bool const cacheClipOk = surface.windowClipTop <= 0 && surface.windowClipBottom <= 0;
  bool const cacheCalloutOk = surface.backgroundEffect.shape != BackgroundEffectShape::Callout;
  bool const cacheSizingOk = !surface.activeSizing && !surface.pacingSizing && !surface.geometryAnimationGrowing;
  bool const cacheOpeningOk = surfaceOpenAnimationComplete(visual, frameTime, animationsEnabled);
  bool const canRecordSurface =
      cacheBackendOk &&
      cacheClipOk &&
      cacheCalloutOk &&
      cacheSizingOk &&
      cacheOpeningOk;
  if (!canRecordSurface) {
    if (!cacheBackendOk) diagnostics::recordSurfaceDrawCacheBlock(diagnostics::CpuSurfaceDrawCacheBlockReason::Backend);
    if (!cacheClipOk) diagnostics::recordSurfaceDrawCacheBlock(diagnostics::CpuSurfaceDrawCacheBlockReason::Clip);
    if (!cacheCalloutOk) diagnostics::recordSurfaceDrawCacheBlock(diagnostics::CpuSurfaceDrawCacheBlockReason::Callout);
    if (!cacheSizingOk) diagnostics::recordSurfaceDrawCacheBlock(diagnostics::CpuSurfaceDrawCacheBlockReason::Sizing);
    if (!cacheOpeningOk) {
      diagnostics::recordSurfaceDrawCacheBlock(diagnostics::CpuSurfaceDrawCacheBlockReason::OpeningAnimation);
    }
  }
  CommittedSurfaceSnapshot const recordedSurface = stableChromeSnapshot(surface);
  std::uint64_t const signature = canRecordSurface ? surfaceDrawSignature(recordedSurface, cached, *cached.image, chrome) : 0;
  if (canRecordSurface && cached.recordedOps && cached.recordedSignature == signature) {
    canvas.save();
    canvas.translate(static_cast<float>(surface.x - cached.recordedX),
                     static_cast<float>(surface.y - cached.recordedY));
    bool const replayed = replayRecordedLocalOpsForCanvas(&canvas, *cached.recordedOps);
    canvas.restore();
    if (replayed) {
      drawTransientChromeControls(canvas, surface, chrome);
      diagnostics::recordSurfaceDrawCache(true, 0.0);
      visual.lastSnapshot = retainedSurfaceRenderSnapshot(surface);
      visual.hasLastSnapshot = true;
      return;
    }
  }

  if (canRecordSurface) {
    auto recorder = std::make_unique<VulkanFrameRecorder>();
    if (beginRecordedOpsCaptureForCanvas(&canvas, recorder.get())) {
      auto const recordStart = diagnostics::cpuTraceNow();
      drawCommittedSurfaceSnapshot(canvas, textSystem, recordedSurface, visual, *cached.image, frameTime, chrome,
                                   animationsEnabled);
      endRecordedOpsCaptureForCanvas(&canvas);
      prepareRecordedOpsForCanvas(&canvas, recorder.get());
      double const recordMs = diagnostics::cpuTraceElapsedMilliseconds(recordStart);
      diagnostics::recordSurfaceDrawCache(false, recordMs);
      if (replayRecordedOpsForCanvas(&canvas, *recorder)) {
        drawTransientChromeControls(canvas, surface, chrome);
        visual.lastSnapshot = retainedSurfaceRenderSnapshot(surface);
        visual.hasLastSnapshot = true;
        cached.recordedSignature = signature;
        cached.recordedX = recordedSurface.x;
        cached.recordedY = recordedSurface.y;
        cached.recordedOps = std::move(recorder);
        return;
      }
      cached.recordedOps.reset();
      cached.recordedSignature = 0;
      cached.recordedX = 0;
      cached.recordedY = 0;
    }
  } else {
    cached.recordedOps.reset();
    cached.recordedSignature = 0;
    cached.recordedX = 0;
    cached.recordedY = 0;
  }

  drawCommittedSurfaceSnapshot(canvas, textSystem, surface, visual, *cached.image, frameTime, chrome,
                               animationsEnabled);
}

bool hasActiveSurfaceAnimations(SurfaceRenderState const &state, std::chrono::steady_clock::time_point frameTime,
                                bool animationsEnabled) {
  if (!animationsEnabled)
    return false;

  return std::any_of(state.surfaceVisuals.begin(), state.surfaceVisuals.end(), [&](auto const &entry) {
    auto const firstSeen = entry.second.firstSeen;
    if (firstSeen.time_since_epoch().count() == 0)
      return false;
    return frameTime - firstSeen < kSurfaceOpenAnimationDuration;
  });
}

void pruneSurfaceRenderState(SurfaceRenderState &state, std::unordered_set<std::uint64_t> const &liveSurfaceIds) {
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

} // namespace lambda::compositor
