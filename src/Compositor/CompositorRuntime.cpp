#include "Compositor/CompositorRuntime.hpp"

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

#include <chrono>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <vulkan/vulkan.h>

namespace flux::compositor {
namespace {

std::uint32_t monotonicMilliseconds() {
  using Clock = std::chrono::steady_clock;
  auto const now = std::chrono::duration_cast<std::chrono::milliseconds>(
      Clock::now().time_since_epoch());
  return static_cast<std::uint32_t>(now.count());
}

struct LoopInstrumentation {
  using Clock = std::chrono::steady_clock;

  bool enabled = std::getenv("FLUX_COMPOSITOR_IDLE_PROFILE") != nullptr;
  Clock::time_point windowStart = Clock::now();
  std::uint64_t loops = 0;
  std::uint64_t frames = 0;
  std::uint64_t idleSkips = 0;
  std::uint64_t polls = 0;
  std::uint64_t pollWakeups = 0;
  std::uint64_t dispatches = 0;
  std::uint64_t vtSleeps = 0;
  std::uint64_t configChecks = 0;
  std::uint64_t lastSurfaceCount = 0;
  double pollMs = 0.0;
  double dispatchMs = 0.0;
  double vblankMs = 0.0;
  double renderMs = 0.0;

  static double milliseconds(Clock::time_point begin, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - begin).count();
  }

  void recordPoll(Clock::time_point begin, bool woke) {
    ++polls;
    if (woke) ++pollWakeups;
    pollMs += milliseconds(begin, Clock::now());
  }

  void recordDispatch(Clock::time_point begin) {
    ++dispatches;
    dispatchMs += milliseconds(begin, Clock::now());
  }

  void recordVblank(Clock::time_point begin) {
    vblankMs += milliseconds(begin, Clock::now());
  }

  void recordRender(Clock::time_point begin) {
    ++frames;
    renderMs += milliseconds(begin, Clock::now());
  }

  void maybeLog() {
    if (!enabled) return;
    auto const now = Clock::now();
    double const elapsedMs = milliseconds(windowStart, now);
    if (elapsedMs < 2000.0) return;

    double const seconds = elapsedMs / 1000.0;
    std::fprintf(stderr,
                 "flux-compositor: idle-profile %.1fs loops=%llu frames=%llu fps=%.1f polls=%llu poll_wakeups=%llu "
                 "idle_skips=%llu dispatches=%llu vt_sleeps=%llu config_checks=%llu surfaces=%llu poll_ms=%.2f "
                 "dispatch_ms=%.2f vblank_ms=%.2f render_ms=%.2f\n",
                 seconds,
                 static_cast<unsigned long long>(loops),
                 static_cast<unsigned long long>(frames),
                 frames / seconds,
                 static_cast<unsigned long long>(polls),
                 static_cast<unsigned long long>(pollWakeups),
                 static_cast<unsigned long long>(idleSkips),
                 static_cast<unsigned long long>(dispatches),
                 static_cast<unsigned long long>(vtSleeps),
                 static_cast<unsigned long long>(configChecks),
                 static_cast<unsigned long long>(lastSurfaceCount),
                 pollMs,
                 dispatchMs,
                 vblankMs,
                 renderMs);

    windowStart = now;
    loops = 0;
    frames = 0;
    idleSkips = 0;
    polls = 0;
    pollWakeups = 0;
    dispatches = 0;
    vtSleeps = 0;
    configChecks = 0;
    pollMs = 0.0;
    dispatchMs = 0.0;
    vblankMs = 0.0;
    renderMs = 0.0;
  }
};

std::string lowerAscii(std::string_view value) {
  std::string result(value);
  for (char& c : result) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return result;
}

std::optional<std::size_t> parseOutputIndex(std::string_view selector, std::size_t count) {
  std::size_t index = 0;
  auto const* begin = selector.data();
  auto const* end = selector.data() + selector.size();
  auto [ptr, error] = std::from_chars(begin, end, index);
  if (error != std::errc{} || ptr != end || index >= count) return std::nullopt;
  return index;
}

void printOutputs(std::vector<flux::platform::KmsOutput> const& outputs) {
  std::fprintf(stderr, "flux-compositor: connected KMS outputs:\n");
  for (std::size_t i = 0; i < outputs.size(); ++i) {
    auto const& output = outputs[i];
    std::fprintf(stderr,
                 "flux-compositor:   [%zu] %s %ux%u @ %.3f Hz\n",
                 i,
                 output.name().c_str(),
                 output.width(),
                 output.height(),
                 static_cast<double>(output.refreshRateMilliHz()) / 1000.0);
  }
}

std::optional<std::size_t> selectOutputIndex(std::vector<flux::platform::KmsOutput> const& outputs,
                                             std::optional<std::string> const& selector) {
  if (outputs.empty()) return std::nullopt;
  if (!selector || selector->empty()) return 0u;

  for (std::size_t i = 0; i < outputs.size(); ++i) {
    if (outputs[i].name() == *selector) return i;
  }

  std::string const normalized = lowerAscii(*selector);
  for (std::size_t i = 0; i < outputs.size(); ++i) {
    if (lowerAscii(outputs[i].name()) == normalized) return i;
  }
  if (normalized == "primary" || normalized == "first") return 0u;
  if ((normalized == "secondary" || normalized == "second") && outputs.size() > 1u) return 1u;
  return parseOutputIndex(*selector, outputs.size());
}

} // namespace

int runKmsCompositor(std::atomic<bool>& running, KmsCompositorOptions options) {
  try {
    auto device = flux::platform::KmsDevice::open();
    auto outputs = device->outputs();
    if (outputs.empty()) {
      std::fprintf(stderr, "flux-compositor: no connected KMS outputs\n");
      return 1;
    }
    printOutputs(outputs);
    if (options.listOutputs) return 0;

    LoadedCompositorConfig loadedConfig = loadConfigWithMetadata();
    auto outputIndex = selectOutputIndex(outputs, loadedConfig.config.outputSelector);
    if (!outputIndex) {
      std::fprintf(stderr,
                   "flux-compositor: output selector \"%s\" did not match any connected output\n",
                   loadedConfig.config.outputSelector ? loadedConfig.config.outputSelector->c_str() : "");
      printOutputs(outputs);
      return 1;
    }

    flux::platform::KmsOutput const& output = outputs[*outputIndex];
    WaylandServer wayland({
        .name = output.name(),
        .width = static_cast<std::int32_t>(output.width()),
        .height = static_cast<std::int32_t>(output.height()),
        .refreshMilliHz = static_cast<std::int32_t>(output.refreshRateMilliHz()),
    });
    device->setInputHandler([&wayland](flux::platform::KmsInputEvent const& event) {
      dispatchKmsInputEvent(wayland, event);
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

    auto effectiveConfig = [&] {
      CompositorConfig config = loadedConfig.config;
      config.scale = scaleForOutput(config, output.name());
      return config;
    };

    AppliedCompositorConfig appliedConfig{};
    auto applyOutputScale = [&](bool force) {
      float const previousScale = wayland.preferredScale();
      wayland.setPreferredScale(appliedConfig.config.scale);
      if (force || std::abs(previousScale - wayland.preferredScale()) > 0.001f) {
        canvas->updateDpiScale(wayland.preferredScale(), wayland.preferredScale());
        canvas->resize(wayland.logicalOutputWidth(), wayland.logicalOutputHeight());
        std::fprintf(stderr,
                     "flux-compositor: output physical=%ux%u logical=%dx%d scale=%.2f\n",
                     output.width(),
                     output.height(),
                     wayland.logicalOutputWidth(),
                     wayland.logicalOutputHeight(),
                     wayland.preferredScale());
      }
    };

    auto applyConfig = [&](bool forceOutputScale = false) {
      appliedConfig = applyCompositorConfig(effectiveConfig(), *canvas);
      wayland.setShortcutBindings(appliedConfig.config.shortcutBindings);
      applyOutputScale(forceOutputScale);
    };
    applyConfig(true);

    SurfaceRenderState surfaceRenderState;
    CursorRenderState cursorState;
    LoopInstrumentation loopStats;
    std::uint32_t const hardwareCursorWidth = output.cursorWidth();
    std::uint32_t const hardwareCursorHeight = output.cursorHeight();
    bool const hardwareCursorAvailable = hardwareCursorWidth > 0 && hardwareCursorHeight > 0;
    if (!hardwareCursorAvailable && appliedConfig.config.hardwareCursorEnabled) {
      std::fprintf(stderr, "flux-compositor: hardware cursor unavailable; using software cursor\n");
    }

    bool forceRender = true;
    bool wasVtForeground = device->isVtForeground();
    constexpr int kIdlePollMs = 250;

    while (running.load(std::memory_order_relaxed) && !device->shouldTerminate()) {
      ++loopStats.loops;
      bool const animationFrameNeeded =
          wayland.hasActiveAnimations() || !surfaceRenderState.closingSurfaces.empty();
      int const waylandEventFd = wayland.eventFd();
      std::span<int const> const waylandEventFds(&waylandEventFd, waylandEventFd >= 0 ? 1u : 0u);
      int const pollTimeoutMs = forceRender || animationFrameNeeded ? 0 : kIdlePollMs;
      auto timingStart = LoopInstrumentation::Clock::now();
      bool const pollWoke = device->pollEvents(pollTimeoutMs, waylandEventFds);
      loopStats.recordPoll(timingStart, pollWoke);
      timingStart = LoopInstrumentation::Clock::now();
      wayland.dispatch();
      loopStats.recordDispatch(timingStart);
      ++loopStats.configChecks;
      bool configReloaded = false;
      if (configChanged(loadedConfig)) {
        auto const previousOutputSelector = loadedConfig.config.outputSelector;
        loadedConfig = loadConfigWithMetadata();
        if (loadedConfig.config.outputSelector != previousOutputSelector) {
          std::fprintf(stderr,
                       "flux-compositor: output selector changed; restart required to move compositor outputs\n");
        }
        applyConfig();
        configReloaded = true;
      }
      bool const vtForeground = device->isVtForeground();
      if (vtForeground && !wasVtForeground) {
        applyConfig(true);
        forceRender = true;
      }
      wasVtForeground = vtForeground;
      if (!vtForeground) {
        ++loopStats.vtSleeps;
        timingStart = LoopInstrumentation::Clock::now();
        bool const vtPollWoke = device->pollEvents(kIdlePollMs, waylandEventFds);
        loopStats.recordPoll(timingStart, vtPollWoke);
        timingStart = LoopInstrumentation::Clock::now();
        wayland.dispatch();
        loopStats.recordDispatch(timingStart);
        loopStats.maybeLog();
        continue;
      }

      bool const renderNeeded = forceRender || animationFrameNeeded || pollWoke || configReloaded;
      if (!renderNeeded) {
        ++loopStats.idleSkips;
        loopStats.maybeLog();
        continue;
      }

      timingStart = LoopInstrumentation::Clock::now();
      output.waitForVblank();
      loopStats.recordVblank(timingStart);
      if (!device->isVtForeground()) continue;
      auto const frameTime = std::chrono::steady_clock::now();
      wayland.updateAnimations(monotonicMilliseconds(), appliedConfig.config.animationsEnabled);

      timingStart = LoopInstrumentation::Clock::now();
      canvas->beginFrame();
      drawCompositorBackground(*canvas,
                               appliedConfig,
                               static_cast<std::uint32_t>(wayland.logicalOutputWidth()),
                               static_cast<std::uint32_t>(wayland.logicalOutputHeight()));
      auto committedSurfaces = wayland.committedSurfaces();
      loopStats.lastSurfaceCount = committedSurfaces.size();
      std::unordered_set<std::uint64_t> liveSurfaceIds;
      liveSurfaceIds.reserve(committedSurfaces.size());
      for (auto const& clientSurface : committedSurfaces) {
        liveSurfaceIds.insert(clientSurface.id);
        auto& visual = surfaceRenderState.surfaceVisuals[clientSurface.id];
        auto& cached = surfaceRenderState.clientImages[clientSurface.id];
        drawCommittedSurface(wayland,
                             *canvas,
                             textSystem,
                             clientSurface,
                             visual,
                             cached,
                             frameTime,
                             appliedConfig.config.animationsEnabled);
      }
      captureClosingSurfaces(surfaceRenderState,
                             liveSurfaceIds,
                             frameTime,
                             appliedConfig.config.animationsEnabled);
      drawClosingSurfaces(*canvas, surfaceRenderState, frameTime);
      if (auto snapPreview = wayland.snapPreview()) {
        drawSnapPreview(*canvas, *snapPreview);
      }
      drawCommandLauncher(*canvas,
                          textSystem,
                          wayland.commandLauncher(),
                          wayland.logicalOutputWidth(),
                          wayland.logicalOutputHeight());
      drawCompositorCursor(wayland,
                           *canvas,
                           output,
                           cursorState,
                           appliedConfig.config.cursorTheme,
                           appliedConfig.config.cursorSize,
                           appliedConfig.config.hardwareCursorEnabled && hardwareCursorAvailable);
      pruneSurfaceRenderState(surfaceRenderState, liveSurfaceIds);
      canvas->present();
      loopStats.recordRender(timingStart);
      wayland.sendFrameCallbacks(monotonicMilliseconds());
      forceRender = false;
      loopStats.maybeLog();
    }

    output.hideCursor();
    return 0;
  } catch (std::exception const& e) {
    std::fprintf(stderr, "flux-compositor: %s\n", e.what());
    return 1;
  }
}

} // namespace flux::compositor
