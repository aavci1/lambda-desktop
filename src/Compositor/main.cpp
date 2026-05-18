#include <Flux/Core/Geometry.hpp>
#include <Flux/Graphics/Canvas.hpp>
#include <Flux/Graphics/VulkanContext.hpp>
#include <Flux/Platform/Linux/KmsOutput.hpp>

#include "Compositor/Chrome/CursorRenderer.hpp"
#include "Compositor/Chrome/WindowChromeRenderer.hpp"
#include "Compositor/Config/AppliedCompositorConfig.hpp"
#include "Compositor/Config/CompositorConfig.hpp"
#include "Compositor/Input/KmsInputBridge.hpp"
#include "Compositor/Surface/SurfaceRenderer.hpp"
#include "Compositor/WaylandServer.hpp"
#include "Detail/ResizeTrace.hpp"
#include "Graphics/Linux/FreeTypeTextSystem.hpp"
#include "Graphics/Vulkan/VulkanCanvas.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <exception>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <vulkan/vulkan.h>

namespace {

std::atomic<bool> gRunning{true};

void onSignal(int) {
  gRunning.store(false, std::memory_order_relaxed);
}

float clamp01(float value) {
  return std::clamp(value, 0.f, 1.f);
}

float easeOutCubic(float value) {
  float const t = clamp01(value);
  float const inverse = 1.f - t;
  return 1.f - inverse * inverse * inverse;
}

std::uint32_t monotonicMilliseconds() {
  using Clock = std::chrono::steady_clock;
  auto const now = std::chrono::duration_cast<std::chrono::milliseconds>(
      Clock::now().time_since_epoch());
  return static_cast<std::uint32_t>(now.count());
}

} // namespace

int main(int, char**) {
  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);

  try {
    auto device = flux::platform::KmsDevice::open();
    auto outputs = device->outputs();
    if (outputs.empty()) {
      std::fprintf(stderr, "flux-compositor: no connected KMS outputs\n");
      return 1;
    }

    flux::platform::KmsOutput const& output = outputs.front();
    flux::compositor::WaylandServer wayland({
        .name = output.name(),
        .width = static_cast<std::int32_t>(output.width()),
        .height = static_cast<std::int32_t>(output.height()),
        .refreshMilliHz = static_cast<std::int32_t>(output.refreshRateMilliHz()),
    });
    device->setInputHandler([&wayland](flux::platform::KmsInputEvent const& event) {
      flux::compositor::dispatchKmsInputEvent(wayland, event);
    });

    flux::configureVulkanCanvasRuntime(device->requiredVulkanInstanceExtensions(), device->cacheDir());
    auto& vulkan = flux::VulkanContext::instance();
    vulkan.addRequiredDeviceExtension(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
    vulkan.addRequiredDeviceExtension(VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME);
    vulkan.addRequiredDeviceExtension(VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME);
    VkInstance instance = flux::ensureSharedVulkanInstance();
    VkSurfaceKHR surface = output.createVulkanSurface(instance);

    static flux::FreeTypeTextSystem textSystem;
    std::unique_ptr<flux::Canvas> canvas = flux::createVulkanCanvas(surface, 1u, textSystem);
    canvas->updateDpiScale(1.f, 1.f);
    canvas->resize(static_cast<int>(output.width()), static_cast<int>(output.height()));

    std::fprintf(stderr,
                 "flux-compositor: presenting %ux%u on %s\n",
                 output.width(),
                 output.height(),
                 output.name().c_str());

    flux::compositor::LoadedCompositorConfig loadedConfig = flux::compositor::loadConfigWithMetadata();
    flux::compositor::AppliedCompositorConfig appliedConfig =
        flux::compositor::applyCompositorConfig(loadedConfig.config, *canvas);
    wayland.setShortcutBindings(appliedConfig.config.shortcutBindings);
    auto applyConfig = [&] {
      appliedConfig = flux::compositor::applyCompositorConfig(loadedConfig.config, *canvas);
      wayland.setShortcutBindings(appliedConfig.config.shortcutBindings);
    };
    std::unordered_map<std::uint64_t, flux::compositor::CachedClientImage> clientImages;
    std::unordered_map<std::uint64_t, flux::compositor::SurfaceVisualState> surfaceVisuals;
    std::unordered_map<std::uint64_t, flux::compositor::ClosingSurfaceVisual> closingSurfaces;
    flux::compositor::CachedClientImage cursorImage;
    bool hardwareArrowCursor = false;
    std::uint32_t const hardwareCursorWidth = output.cursorWidth();
    std::uint32_t const hardwareCursorHeight = output.cursorHeight();
    std::vector<std::uint32_t> hardwareCursorPixels;
    if (appliedConfig.config.hardwareCursorEnabled && hardwareCursorWidth > 0 && hardwareCursorHeight > 0) {
      hardwareCursorPixels = flux::compositor::makeHardwareArrowCursor(hardwareCursorWidth, hardwareCursorHeight);
      hardwareArrowCursor = output.setCursorImage(hardwareCursorPixels, hardwareCursorWidth, hardwareCursorHeight);
      if (!hardwareArrowCursor) {
        std::fprintf(stderr, "flux-compositor: hardware cursor unavailable; using software cursor\n");
      }
    }
    while (gRunning.load(std::memory_order_relaxed) && !device->shouldTerminate()) {
      device->pollEvents(0);
      wayland.dispatch();
      if (flux::compositor::configChanged(loadedConfig)) {
        loadedConfig = flux::compositor::loadConfigWithMetadata();
        applyConfig();
      }
      if (!device->isVtForeground()) {
        device->pollEvents(250);
        wayland.dispatch();
        continue;
      }

      output.waitForVblank();
      device->pollEvents(0);
      wayland.dispatch();
      if (!device->isVtForeground()) continue;
      auto const frameTime = std::chrono::steady_clock::now();
      wayland.updateAnimations(monotonicMilliseconds(), appliedConfig.config.animationsEnabled);

      canvas->beginFrame();
      canvas->clear(appliedConfig.config.backgroundColor);
      if (appliedConfig.config.backgroundGradientEnd) {
        canvas->drawRect(flux::Rect::sharp(0.f, 0.f, static_cast<float>(output.width()), static_cast<float>(output.height())),
                         flux::CornerRadius{0.f},
                         appliedConfig.backgroundFill,
                         flux::StrokeStyle::none(),
                         flux::ShadowStyle::none());
      }
      if (appliedConfig.wallpaperImage) {
        canvas->drawImage(*appliedConfig.wallpaperImage,
                          flux::Rect::sharp(0.f,
                                            0.f,
                                            static_cast<float>(output.width()),
                                            static_cast<float>(output.height())),
                          appliedConfig.config.wallpaperMode);
      }
      auto committedSurfaces = wayland.committedSurfaces();
      std::unordered_set<std::uint64_t> liveSurfaceIds;
      liveSurfaceIds.reserve(committedSurfaces.size());
      for (auto const& clientSurface : committedSurfaces) {
        liveSurfaceIds.insert(clientSurface.id);
        auto& visual = surfaceVisuals[clientSurface.id];
        if (visual.firstSeen.time_since_epoch().count() == 0) visual.firstSeen = frameTime;
        auto& cached = clientImages[clientSurface.id];
        flux::compositor::updateCachedImage(wayland, *canvas, clientSurface, cached);
        if (!cached.image) continue;
        bool const traceSnapshot = flux::compositor::shouldTraceRenderSnapshot(clientSurface, visual);
        if (traceSnapshot) {
          auto const imageSize = cached.image->size();
          flux::detail::resizeTrace(
              "compositor-render",
              "render-snapshot surface=%llu window=%d,%d frame=%dx%d buffer=%dx%d "
              "image=%dx%d source=%.1f,%.1f %.1fx%.1f dest=%dx%d serial=%llu\n",
              static_cast<unsigned long long>(clientSurface.id),
              clientSurface.x,
              clientSurface.y,
              clientSurface.width,
              clientSurface.height,
              clientSurface.bufferWidth,
              clientSurface.bufferHeight,
              static_cast<int>(imageSize.width),
              static_cast<int>(imageSize.height),
              clientSurface.sourceX,
              clientSurface.sourceY,
              clientSurface.sourceWidth,
              clientSurface.sourceHeight,
              clientSurface.destinationWidth,
              clientSurface.destinationHeight,
              static_cast<unsigned long long>(clientSurface.serial));
        }
        visual.lastSnapshot = clientSurface;
        visual.hasLastSnapshot = true;

        float const windowX = static_cast<float>(clientSurface.x);
        float const windowY = static_cast<float>(clientSurface.y);
        float const windowWidth = static_cast<float>(clientSurface.width);
        float const windowHeight = static_cast<float>(clientSurface.height);
        float const titleBarHeight = static_cast<float>(clientSurface.titleBarHeight);
        float const animationMs = static_cast<float>(
            std::chrono::duration_cast<std::chrono::milliseconds>(frameTime - visual.firstSeen).count());
        float const openProgress = appliedConfig.config.animationsEnabled ? easeOutCubic(animationMs / 140.f) : 1.f;
        float const openScale = 0.965f + 0.035f * openProgress;
        float const openOpacity = openProgress;
        float const outerHeight = windowHeight + titleBarHeight;
        flux::Point const pivot{windowX + windowWidth * 0.5f, windowY - titleBarHeight + outerHeight * 0.5f};
        canvas->save();
        canvas->setOpacity(canvas->opacity() * openOpacity);
        if (openScale < 1.f) {
          canvas->translate(pivot.x, pivot.y);
          canvas->scale(openScale);
          canvas->translate(-pivot.x, -pivot.y);
        }
        flux::compositor::drawWindowChrome(*canvas, textSystem, clientSurface);
        float const sourceWidth = clientSurface.sourceWidth > 0.f
                                      ? clientSurface.sourceWidth
                                      : static_cast<float>(cached.image->size().width);
        float const sourceHeight = clientSurface.sourceHeight > 0.f
                                       ? clientSurface.sourceHeight
                                       : static_cast<float>(cached.image->size().height);
        bool const staleResizeBuffer =
            clientSurface.activeSizing &&
            (clientSurface.destinationWidth != static_cast<int>(std::lround(windowWidth)) ||
             clientSurface.destinationHeight != static_cast<int>(std::lround(windowHeight)));
        float const contentWidth = staleResizeBuffer
                                       ? static_cast<float>(clientSurface.destinationWidth)
                                       : windowWidth;
        float const contentHeight = staleResizeBuffer
                                        ? static_cast<float>(clientSurface.destinationHeight)
                                        : windowHeight;
        canvas->save();
        canvas->clipRect(flux::Rect::sharp(windowX, windowY, windowWidth, windowHeight));
        canvas->drawImage(*cached.image,
                          flux::Rect::sharp(clientSurface.sourceX,
                                            clientSurface.sourceY,
                                            sourceWidth,
                                            sourceHeight),
                          flux::Rect::sharp(windowX,
                                            windowY,
                                            contentWidth,
                                            contentHeight));
        canvas->restore();
        canvas->restore();
      }
      for (auto const& [surfaceId, visual] : surfaceVisuals) {
        if (liveSurfaceIds.contains(surfaceId)) continue;
        auto cached = clientImages.find(surfaceId);
        if (!appliedConfig.config.animationsEnabled || !visual.hasLastSnapshot || cached == clientImages.end() ||
            !cached->second.image) {
          continue;
        }
        closingSurfaces[surfaceId] = flux::compositor::ClosingSurfaceVisual{
            .snapshot = visual.lastSnapshot,
            .image = cached->second.image,
            .closedAt = frameTime,
        };
      }
      for (auto it = closingSurfaces.begin(); it != closingSurfaces.end();) {
        float const closeMs = static_cast<float>(
            std::chrono::duration_cast<std::chrono::milliseconds>(frameTime - it->second.closedAt).count());
        float const progress = clamp01(closeMs / 120.f);
        if (progress >= 1.f || !it->second.image) {
          it = closingSurfaces.erase(it);
          continue;
        }
        float const eased = easeOutCubic(progress);
        flux::compositor::drawSurfaceImage(*canvas, it->second.snapshot, *it->second.image, 1.f - eased, 1.f - 0.025f * eased);
        ++it;
      }
      if (auto snapPreview = wayland.snapPreview()) {
        flux::compositor::drawSnapPreview(*canvas, *snapPreview);
      }
      if (auto cursorSurface = wayland.cursorSurface()) {
        if (hardwareArrowCursor) output.hideCursor();
        flux::compositor::updateCachedImage(wayland, *canvas, *cursorSurface, cursorImage);
        if (cursorImage.image) {
          float const cursorSourceWidth = cursorSurface->sourceWidth > 0.f
                                              ? cursorSurface->sourceWidth
                                              : static_cast<float>(cursorImage.image->size().width);
          float const cursorSourceHeight = cursorSurface->sourceHeight > 0.f
                                               ? cursorSurface->sourceHeight
                                               : static_cast<float>(cursorImage.image->size().height);
          canvas->drawImage(*cursorImage.image,
                            flux::Rect::sharp(cursorSurface->sourceX,
                                              cursorSurface->sourceY,
                                              cursorSourceWidth,
                                              cursorSourceHeight),
                            flux::Rect::sharp(static_cast<float>(cursorSurface->x),
                                              static_cast<float>(cursorSurface->y),
                                              static_cast<float>(cursorSurface->width),
                                              static_cast<float>(cursorSurface->height)));
        }
      } else {
        cursorImage = {};
        float const cursorX = wayland.pointerX();
        float const cursorY = wayland.pointerY();
        if (hardwareArrowCursor && wayland.cursorShape() == flux::compositor::CursorShape::Arrow) {
          std::int32_t const cursorXi = static_cast<std::int32_t>(std::lround(cursorX));
          std::int32_t const cursorYi = static_cast<std::int32_t>(std::lround(cursorY));
          if (!output.moveCursor(cursorXi, cursorYi)) {
            (void)output.setCursorImage(hardwareCursorPixels, hardwareCursorWidth, hardwareCursorHeight);
            (void)output.moveCursor(cursorXi, cursorYi);
          }
        } else {
          if (hardwareArrowCursor) output.hideCursor();
          flux::compositor::drawFallbackCursor(*canvas, wayland.cursorShape(), cursorX, cursorY);
        }
      }
      for (auto it = clientImages.begin(); it != clientImages.end();) {
        if (liveSurfaceIds.contains(it->first)) {
          ++it;
        } else {
          it = clientImages.erase(it);
        }
      }
      for (auto it = surfaceVisuals.begin(); it != surfaceVisuals.end();) {
        if (liveSurfaceIds.contains(it->first)) {
          ++it;
        } else {
          it = surfaceVisuals.erase(it);
        }
      }
      canvas->present();
      wayland.sendFrameCallbacks(monotonicMilliseconds());
    }

    output.hideCursor();
    return 0;
  } catch (std::exception const& e) {
    std::fprintf(stderr, "flux-compositor: %s\n", e.what());
    return 1;
  }
}
