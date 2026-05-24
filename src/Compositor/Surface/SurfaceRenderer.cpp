#include "Compositor/Surface/SurfaceRenderer.hpp"

#include "Compositor/Diagnostics/CpuTrace.hpp"
#include "Compositor/Diagnostics/CrashLog.hpp"
#include "Detail/ResizeTrace.hpp"
#include "Graphics/Vulkan/VulkanCanvas.hpp"
#include "Graphics/Vulkan/VulkanFrameRecorder.hpp"

#include <Flux/Core/Geometry.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <memory>
#include <unistd.h>
#include <vector>

namespace flux::compositor {
namespace {

float clamp01(float value) { return std::clamp(value, 0.f, 1.f); }

float easeOutCubic(float value) {
  float const t = clamp01(value);
  float const inverse = 1.f - t;
  return 1.f - inverse * inverse * inverse;
}

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

std::uint64_t surfaceDrawSignature(CommittedSurfaceSnapshot const &surface, CachedClientImage const &cached,
                                   Image const &image, ChromeConfig const &chrome) {
  std::uint64_t hash = 1469598103934665603ull;
  hashValue(hash, surface.id);
  hashValue(hash, surface.serial);
  hashValue(hash, surface.dmabufBufferId);
  hashValue(hash, surface.x);
  hashValue(hash, surface.y);
  hashValue(hash, surface.width);
  hashValue(hash, surface.height);
  hashValue(hash, surface.bufferWidth);
  hashValue(hash, surface.bufferHeight);
  hashValue(hash, surface.sourceX);
  hashValue(hash, surface.sourceY);
  hashValue(hash, surface.sourceWidth);
  hashValue(hash, surface.sourceHeight);
  hashValue(hash, surface.destinationWidth);
  hashValue(hash, surface.destinationHeight);
  hashValue(hash, surface.titleBarHeight);
  hashValue(hash, surface.serverSideDecorated);
  hashValue(hash, surface.cutoutsBound);
  hashValue(hash, surface.cutoutsRejected);
  hashValue(hash, surface.closeButtonHovered);
  hashValue(hash, surface.closeButtonPressed);
  hashValue(hash, surface.maximizeButtonHovered);
  hashValue(hash, surface.maximizeButtonPressed);
  hashValue(hash, surface.minimizeButtonHovered);
  hashValue(hash, surface.minimizeButtonPressed);
  hashValue(hash, surface.focused);
  hashValue(hash, surface.activeSizing);
  hashValue(hash, surface.defaultGlassEligible);
  hashValue(hash, surface.backgroundEffect.blurRadius);
  hashColor(hash, surface.backgroundEffect.baseColor);
  hashColor(hash, surface.backgroundEffect.tint);
  hashColor(hash, surface.backgroundEffect.borderColor);
  hashValue(hash, surface.backgroundEffect.usesDefaultMaterial);
  hashValue(hash, surface.backgroundEffect.cornerRadiusSet);
  hashCornerRadius(hash, surface.backgroundEffect.cornerRadius);
  hashValue(hash, surface.title.size());
  hashCombine(hash, surface.title.data(), surface.title.size());
  hashValue(hash, surface.backgroundBlurRects.size());
  for (auto const &rect : surface.backgroundBlurRects) {
    hashValue(hash, rect.x);
    hashValue(hash, rect.y);
    hashValue(hash, rect.width);
    hashValue(hash, rect.height);
  }
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
  hashValue(hash, chrome.windowGlassEnabled);
  hashValue(hash, chrome.glass.opacity);
  hashValue(hash, chrome.glass.blurRadius);
  hashColor(hash, chrome.glass.baseColor);
  hashColor(hash, chrome.glass.tintColor);
  hashColor(hash, chrome.glass.borderColor);
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

} // namespace

void drawSurfaceImage(Canvas &canvas, CommittedSurfaceSnapshot const &surface, Image &image, float opacity,
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
  float const sourceWidth = surface.sourceWidth > 0.f ? surface.sourceWidth : static_cast<float>(image.size().width);
  float const sourceHeight =
      surface.sourceHeight > 0.f ? surface.sourceHeight : static_cast<float>(image.size().height);
  canvas.drawImage(image, Rect::sharp(surface.sourceX, surface.sourceY, sourceWidth, sourceHeight),
                   Rect::sharp(windowX, windowY, windowWidth, windowHeight));
  canvas.restore();
}

void updateCachedImage(WaylandServer &wayland, Canvas &canvas, CommittedSurfaceSnapshot const &surface,
                       CachedClientImage &cached) {
  bool const hasDmabuf = !surface.dmabufPlanes.empty();
  if (cached.id != 0 && cached.id != surface.id) {
    cached = {};
  }
  if (cached.image && cached.id == surface.id) {
    if (hasDmabuf && cached.dmabufBufferId == surface.dmabufBufferId)
      return;
    if (!hasDmabuf && cached.serial == surface.serial)
      return;
  }

  cached.id = surface.id;
  cached.serial = surface.serial;
  cached.dmabufBufferId = surface.dmabufBufferId;
  std::int32_t const bufferWidth = surface.bufferWidth > 0 ? surface.bufferWidth : surface.width;
  std::int32_t const bufferHeight = surface.bufferHeight > 0 ? surface.bufferHeight : surface.height;
  if (surface.rgbaPixels && !surface.rgbaPixels->empty()) {
    cached.dmabufBufferId = 0;
    cached.dmabufImported = false;
    std::size_t const uploadBytes = surface.rgbaPixels->size();
    if (cached.image && cached.shmBufferWidth == bufferWidth && cached.shmBufferHeight == bufferHeight && [&] {
          auto const updateStart = diagnostics::cpuTraceNow();
          bool const updated = cached.image->updatePixels(*surface.rgbaPixels, surface.pixelFormat, canvas.gpuDevice());
          diagnostics::recordSurfaceImageUpload(uploadBytes, diagnostics::cpuTraceElapsedMilliseconds(updateStart),
                                                false);
          return updated;
        }()) {
      return;
    }
    cached.dmabufImages.clear();
    cached.logged = false;
    auto const createStart = diagnostics::cpuTraceNow();
    cached.image = Image::fromPixels(static_cast<std::uint32_t>(bufferWidth), static_cast<std::uint32_t>(bufferHeight),
                                     *surface.rgbaPixels, surface.pixelFormat, canvas.gpuDevice());
    diagnostics::recordSurfaceImageUpload(uploadBytes, diagnostics::cpuTraceElapsedMilliseconds(createStart), true);
    cached.shmBufferWidth = bufferWidth;
    cached.shmBufferHeight = bufferHeight;
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
      cached.image = cachedDmabuf->image;
      cached.dmabufImported = cachedDmabuf->imported;
      CachedClientImage::DmabufEntry entry = std::move(*cachedDmabuf);
      cached.dmabufImages.erase(cachedDmabuf);
      cached.dmabufImages.push_back(std::move(entry));
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
          detail::resizeTrace(
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
          .image = cached.image,
          .imported = cached.dmabufImported,
      });
      while (cached.dmabufImages.size() > kMaxCachedDmabufImagesPerSurface) {
        cached.dmabufImages.erase(cached.dmabufImages.begin());
      }
      return;
    }

    std::vector<std::uint8_t> fallbackPixels;
    auto const fallbackStart = diagnostics::cpuTraceNow();
    if (wayland.copyDmabufToRgba(surface.id, fallbackPixels)) {
      std::size_t const fallbackBytes = fallbackPixels.size();
      cached.image =
          Image::fromRgbaPixels(static_cast<std::uint32_t>(bufferWidth), static_cast<std::uint32_t>(bufferHeight),
                                fallbackPixels, canvas.gpuDevice());
      diagnostics::recordDmabufFallbackCopy(fallbackBytes, diagnostics::cpuTraceElapsedMilliseconds(fallbackStart),
                                            cached.image != nullptr);
      if (cached.image && !cached.logged) {
        std::fprintf(stderr, "lambda-window-manager: displaying readable DMABUF contents\n");
        cached.logged = true;
      }
      if (cached.image) {
        detail::resizeTrace("compositor-render",
                            "dmabuf-cache-import surface=%llu buffer=%llu imported=0 elapsed=%.3fms\n",
                            static_cast<unsigned long long>(surface.id),
                            static_cast<unsigned long long>(surface.dmabufBufferId), elapsedMilliseconds(importStart));
        cached.dmabufImages.push_back(CachedClientImage::DmabufEntry{
            .bufferId = surface.dmabufBufferId,
            .image = cached.image,
            .imported = false,
        });
        while (cached.dmabufImages.size() > kMaxCachedDmabufImagesPerSurface) {
          cached.dmabufImages.erase(cached.dmabufImages.begin());
        }
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
  bool const canRecordSurface =
      canvas.backend() == Backend::Vulkan &&
      !hasTransientChromeState(surface) &&
      surfaceOpenAnimationComplete(visual, frameTime, animationsEnabled);
  std::uint64_t const signature = canRecordSurface ? surfaceDrawSignature(surface, cached, *cached.image, chrome) : 0;
  if (canRecordSurface && cached.recordedOps && cached.recordedSignature == signature &&
      replayRecordedOpsForCanvas(&canvas, *cached.recordedOps)) {
    diagnostics::recordSurfaceDrawCache(true, 0.0);
    visual.lastSnapshot = surface;
    visual.hasLastSnapshot = true;
    return;
  }

  bool const signatureStable = canRecordSurface && cached.lastDrawSignature == signature;
  if (canRecordSurface) {
    cached.stableDrawSignatureFrames = signatureStable ? cached.stableDrawSignatureFrames + 1u : 1u;
    cached.lastDrawSignature = signature;
  }
  bool const shouldRecordSurface = canRecordSurface && cached.stableDrawSignatureFrames >= 2u;

  if (shouldRecordSurface) {
    auto recorder = std::make_unique<VulkanFrameRecorder>();
    if (beginRecordedOpsCaptureForCanvas(&canvas, recorder.get())) {
      auto const recordStart = diagnostics::cpuTraceNow();
      drawCommittedSurfaceSnapshot(canvas, textSystem, surface, visual, *cached.image, frameTime, chrome,
                                   animationsEnabled);
      endRecordedOpsCaptureForCanvas(&canvas);
      double const recordMs = diagnostics::cpuTraceElapsedMilliseconds(recordStart);
      diagnostics::recordSurfaceDrawCache(false, recordMs);
      if (replayRecordedOpsForCanvas(&canvas, *recorder)) {
        cached.recordedSignature = signature;
        cached.recordedOps = std::move(recorder);
        return;
      }
      cached.recordedOps.reset();
      cached.recordedSignature = 0;
    }
  } else {
    cached.recordedOps.reset();
    cached.recordedSignature = 0;
    if (!canRecordSurface) {
      cached.lastDrawSignature = 0;
      cached.stableDrawSignatureFrames = 0;
    }
  }

  drawCommittedSurfaceSnapshot(canvas, textSystem, surface, visual, *cached.image, frameTime, chrome,
                               animationsEnabled);
}

void captureClosingSurfaces(SurfaceRenderState &state, std::unordered_set<std::uint64_t> const &liveSurfaceIds,
                            std::chrono::steady_clock::time_point frameTime, bool animationsEnabled) {
  for (auto const &[surfaceId, visual] : state.surfaceVisuals) {
    if (liveSurfaceIds.contains(surfaceId))
      continue;
    auto cached = state.clientImages.find(surfaceId);
    if (!animationsEnabled || !visual.hasLastSnapshot || cached == state.clientImages.end() || !cached->second.image) {
      continue;
    }
    state.closingSurfaces[surfaceId] = ClosingSurfaceVisual{
        .snapshot = visual.lastSnapshot,
        .image = cached->second.image,
        .closedAt = frameTime,
    };
  }
}

void drawClosingSurfaces(Canvas &canvas, SurfaceRenderState &state, std::chrono::steady_clock::time_point frameTime) {
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

bool hasActiveSurfaceAnimations(SurfaceRenderState const &state, std::chrono::steady_clock::time_point frameTime,
                                bool animationsEnabled) {
  if (!state.closingSurfaces.empty())
    return true;
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

} // namespace flux::compositor
