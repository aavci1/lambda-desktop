#include <Flux/Graphics/VulkanContext.hpp>
#include <Flux/Platform/Linux/KmsOutput.hpp>

#include "Compositor/Chrome/CursorRenderer.hpp"
#include "Compositor/Chrome/WindowChromeRenderer.hpp"
#include "Compositor/Config/AppliedCompositorConfig.hpp"
#include "Compositor/Config/CompositorConfig.hpp"
#include "Compositor/Input/KmsInputBridge.hpp"
#include "Compositor/Surface/SurfaceRenderer.hpp"
#include "Compositor/WaylandServer.hpp"
#include "Graphics/Linux/FreeTypeTextSystem.hpp"
#include "Graphics/Vulkan/VulkanCanvas.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <exception>
#include <memory>
#include <unordered_set>
#include <vector>

#include <vulkan/vulkan.h>

namespace {

std::atomic<bool> gRunning{true};

void onSignal(int) {
  gRunning.store(false, std::memory_order_relaxed);
}

std::uint32_t monotonicMilliseconds() {
  using Clock = std::chrono::steady_clock;
  auto const now = std::chrono::duration_cast<std::chrono::milliseconds>(
      Clock::now().time_since_epoch());
  return static_cast<std::uint32_t>(now.count());
}

} // namespace

int main(int, char**) {
  std::signal(SIGTERM, onSignal);

  try {
    auto device = flux::platform::KmsDevice::open();
    // Ctrl+C belongs to the focused Wayland client. The compositor exits via
    // SIGTERM or its configured terminate shortcut.
    std::signal(SIGINT, SIG_IGN);
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
    flux::compositor::SurfaceRenderState surfaceRenderState;
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
      flux::compositor::drawCompositorBackground(*canvas, appliedConfig, output.width(), output.height());
      auto committedSurfaces = wayland.committedSurfaces();
      std::unordered_set<std::uint64_t> liveSurfaceIds;
      liveSurfaceIds.reserve(committedSurfaces.size());
      for (auto const& clientSurface : committedSurfaces) {
        liveSurfaceIds.insert(clientSurface.id);
        auto& visual = surfaceRenderState.surfaceVisuals[clientSurface.id];
        auto& cached = surfaceRenderState.clientImages[clientSurface.id];
        flux::compositor::drawCommittedSurface(wayland,
                                               *canvas,
                                               textSystem,
                                               clientSurface,
                                               visual,
                                               cached,
                                               frameTime,
                                               appliedConfig.config.animationsEnabled);
      }
      flux::compositor::captureClosingSurfaces(surfaceRenderState,
                                               liveSurfaceIds,
                                               frameTime,
                                               appliedConfig.config.animationsEnabled);
      flux::compositor::drawClosingSurfaces(*canvas, surfaceRenderState, frameTime);
      if (auto snapPreview = wayland.snapPreview()) {
        flux::compositor::drawSnapPreview(*canvas, *snapPreview);
      }
      flux::compositor::drawCompositorCursor(wayland,
                                             *canvas,
                                             output,
                                             cursorImage,
                                             hardwareArrowCursor,
                                             hardwareCursorPixels,
                                             hardwareCursorWidth,
                                             hardwareCursorHeight);
      flux::compositor::pruneSurfaceRenderState(surfaceRenderState, liveSurfaceIds);
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
