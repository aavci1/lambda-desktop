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
#include <cstring>
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

using SteadyClock = std::chrono::steady_clock;

double elapsedMilliseconds(SteadyClock::time_point start) {
  return std::chrono::duration<double, std::milli>(SteadyClock::now() - start).count();
}

bool timingTraceEnabled() {
  char const* value = std::getenv("FLUX_COMPOSITOR_TIMING");
  return value && *value && std::strcmp(value, "0") != 0;
}

void traceTiming(char const* label, SteadyClock::time_point start) {
  if (!timingTraceEnabled()) return;
  std::fprintf(stderr, "flux-compositor: timing %s %.3fms\n", label, elapsedMilliseconds(start));
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

struct CompositorFrameProfile {
  using Clock = std::chrono::steady_clock;

  bool enabled = std::getenv("FLUX_COMPOSITOR_PROFILE") != nullptr;
  Clock::time_point windowStart = Clock::now();
  std::uint64_t frames = 0;
  std::uint64_t surfaces = 0;
  double backgroundMs = 0.0;
  double snapshotMs = 0.0;
  double surfaceMs = 0.0;
  double closingMs = 0.0;
  double launcherMs = 0.0;
  double cursorMs = 0.0;
  double presentMs = 0.0;
  double totalMs = 0.0;

  static double milliseconds(Clock::time_point begin, Clock::time_point end = Clock::now()) {
    return std::chrono::duration<double, std::milli>(end - begin).count();
  }

  void maybeLog() {
    if (!enabled) return;
    auto const now = Clock::now();
    double const elapsedMs = milliseconds(windowStart, now);
    if (elapsedMs < 2000.0 || frames == 0) return;

    double const invFrames = 1.0 / static_cast<double>(frames);
    std::fprintf(stderr,
                 "flux-compositor: frame-profile %.1fs frames=%llu fps=%.1f surfaces=%.2f/f "
                 "total=%.3fms bg=%.3fms snapshots=%.3fms surfaces=%.3fms closing=%.3fms "
                 "launcher=%.3fms cursor=%.3fms present=%.3fms\n",
                 elapsedMs / 1000.0,
                 static_cast<unsigned long long>(frames),
                 frames / (elapsedMs / 1000.0),
                 static_cast<double>(surfaces) * invFrames,
                 totalMs * invFrames,
                 backgroundMs * invFrames,
                 snapshotMs * invFrames,
                 surfaceMs * invFrames,
                 closingMs * invFrames,
                 launcherMs * invFrames,
                 cursorMs * invFrames,
                 presentMs * invFrames);

    windowStart = now;
    frames = 0;
    surfaces = 0;
    backgroundMs = 0.0;
    snapshotMs = 0.0;
    surfaceMs = 0.0;
    closingMs = 0.0;
    launcherMs = 0.0;
    cursorMs = 0.0;
    presentMs = 0.0;
    totalMs = 0.0;
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
    auto timingStart = SteadyClock::now();
    VkInstance instance = flux::ensureSharedVulkanInstance();
    traceTiming("ensure-vulkan-instance", timingStart);

    static flux::FreeTypeTextSystem textSystem;

    auto effectiveConfig = [&] {
      CompositorConfig config = loadedConfig.config;
      config.scale = scaleForOutput(config, output.name());
      return config;
    };
    wayland.setPreferredScale(effectiveConfig().scale);

    auto createCanvas = [&] {
      auto const surfaceStart = SteadyClock::now();
      VkSurfaceKHR surface = output.createVulkanSurface(instance);
      traceTiming("create-vulkan-surface", surfaceStart);

      auto const canvasStart = SteadyClock::now();
      auto nextCanvas = flux::createVulkanCanvas(surface, 1u, textSystem);
      nextCanvas->updateDpiScale(wayland.preferredScale(), wayland.preferredScale());
      nextCanvas->resize(wayland.logicalOutputWidth(), wayland.logicalOutputHeight());
      traceTiming("create-vulkan-canvas", canvasStart);
      return nextCanvas;
    };

    std::unique_ptr<flux::Canvas> canvas = createCanvas();

    std::fprintf(stderr,
                 "flux-compositor: presenting %ux%u on %s\n",
                 output.width(),
                 output.height(),
                 output.name().c_str());

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
      if (!canvas) return;
      auto const configStart = SteadyClock::now();
      appliedConfig = applyCompositorConfig(effectiveConfig(), *canvas);
      traceTiming("apply-config", configStart);
      wayland.setShortcutBindings(appliedConfig.config.shortcutBindings);
      wayland.setChromeConfig(appliedConfig.config.chrome);
      applyOutputScale(forceOutputScale);
    };
    applyConfig(true);

    SurfaceRenderState surfaceRenderState;
    CursorRenderState cursorState;
    LoopInstrumentation loopStats;
    CompositorFrameProfile frameProfile;
    std::uint32_t const hardwareCursorWidth = output.cursorWidth();
    std::uint32_t const hardwareCursorHeight = output.cursorHeight();
    bool const hardwareCursorAvailable = hardwareCursorWidth > 0 && hardwareCursorHeight > 0;
    if (!hardwareCursorAvailable && appliedConfig.config.hardwareCursorEnabled) {
      std::fprintf(stderr, "flux-compositor: hardware cursor unavailable; using software cursor\n");
    }

    bool forceRender = true;
    bool skipNextVblank = true;
    bool wasVtForeground = device->isVtForeground();
    bool vtAcquireFramePending = false;
    constexpr int kIdlePollMs = 250;
    auto handleVtResume = [&] {
      auto const resumeStart = SteadyClock::now();
      applyOutputScale(true);
      traceTiming("vt-resume-total", resumeStart);
      vtAcquireFramePending = true;
      forceRender = true;
      skipNextVblank = true;
    };
    auto renderCompositorFrame = [&](std::chrono::steady_clock::time_point frameTime,
                                     LoopInstrumentation::Clock::time_point renderStart) {
      auto const frameProfileStart = CompositorFrameProfile::Clock::now();
      auto phaseStart = frameProfileStart;
      canvas->beginFrame();
      drawCompositorBackground(*canvas,
                               appliedConfig,
                               static_cast<std::uint32_t>(wayland.logicalOutputWidth()),
                               static_cast<std::uint32_t>(wayland.logicalOutputHeight()));
      frameProfile.backgroundMs += CompositorFrameProfile::milliseconds(phaseStart);
      phaseStart = CompositorFrameProfile::Clock::now();
      auto snapPreview = wayland.snapPreview();
      bool snapPreviewDrawn = false;
      auto committedSurfaces = wayland.committedSurfaces();
      loopStats.lastSurfaceCount = committedSurfaces.size();
      frameProfile.snapshotMs += CompositorFrameProfile::milliseconds(phaseStart);
      frameProfile.surfaces += committedSurfaces.size();
      std::unordered_set<std::uint64_t> liveSurfaceIds;
      liveSurfaceIds.reserve(committedSurfaces.size());
      phaseStart = CompositorFrameProfile::Clock::now();
      for (auto const& clientSurface : committedSurfaces) {
        if (snapPreview && !snapPreviewDrawn && snapPreview->surfaceId == clientSurface.id) {
          drawSnapPreview(*canvas, *snapPreview, appliedConfig.config.chrome);
          snapPreviewDrawn = true;
        }
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
                             appliedConfig.config.chrome,
                             appliedConfig.config.animationsEnabled);
      }
      if (snapPreview && !snapPreviewDrawn) {
        drawSnapPreview(*canvas, *snapPreview, appliedConfig.config.chrome);
      }
      frameProfile.surfaceMs += CompositorFrameProfile::milliseconds(phaseStart);
      phaseStart = CompositorFrameProfile::Clock::now();
      captureClosingSurfaces(surfaceRenderState,
                             liveSurfaceIds,
                             frameTime,
                             appliedConfig.config.animationsEnabled);
      drawClosingSurfaces(*canvas, surfaceRenderState, frameTime);
      frameProfile.closingMs += CompositorFrameProfile::milliseconds(phaseStart);
      phaseStart = CompositorFrameProfile::Clock::now();
      drawCommandLauncher(*canvas,
                          textSystem,
                          wayland.commandLauncher(),
                          appliedConfig.config.chrome,
                          wayland.logicalOutputWidth(),
                          wayland.logicalOutputHeight());
      frameProfile.launcherMs += CompositorFrameProfile::milliseconds(phaseStart);
      phaseStart = CompositorFrameProfile::Clock::now();
      drawCompositorCursor(wayland,
                           *canvas,
                           output,
                           cursorState,
                           appliedConfig.config.cursorTheme,
                           appliedConfig.config.cursorSize,
                           appliedConfig.config.hardwareCursorEnabled && hardwareCursorAvailable);
      frameProfile.cursorMs += CompositorFrameProfile::milliseconds(phaseStart);
      pruneSurfaceRenderState(surfaceRenderState, liveSurfaceIds);
      phaseStart = CompositorFrameProfile::Clock::now();
      canvas->present();
      frameProfile.presentMs += CompositorFrameProfile::milliseconds(phaseStart);
      ++frameProfile.frames;
      frameProfile.totalMs += CompositorFrameProfile::milliseconds(frameProfileStart);
      frameProfile.maybeLog();
      loopStats.recordRender(renderStart);
      wayland.sendFrameCallbacks(monotonicMilliseconds());
    };

    while (running.load(std::memory_order_relaxed) && !device->shouldTerminate()) {
      ++loopStats.loops;
      auto const animationCheckTime = std::chrono::steady_clock::now();
      bool const animationFrameNeeded =
          wayland.hasActiveAnimations() ||
          hasActiveSurfaceAnimations(surfaceRenderState,
                                     animationCheckTime,
                                     appliedConfig.config.animationsEnabled);
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
        handleVtResume();
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
        if (!device->isVtForeground()) {
          loopStats.maybeLog();
          continue;
        }
        handleVtResume();
        wasVtForeground = true;
      }

      bool const renderNeeded = forceRender || animationFrameNeeded || pollWoke || configReloaded;
      if (!renderNeeded) {
        ++loopStats.idleSkips;
        loopStats.maybeLog();
        continue;
      }

      timingStart = LoopInstrumentation::Clock::now();
      if (skipNextVblank) {
        skipNextVblank = false;
      } else {
        output.waitForVblank();
        loopStats.recordVblank(timingStart);
      }
      if (!device->isVtForeground()) continue;
      auto const frameTime = std::chrono::steady_clock::now();
      wayland.updateAnimations(monotonicMilliseconds(), appliedConfig.config.animationsEnabled);

      timingStart = LoopInstrumentation::Clock::now();
      renderCompositorFrame(frameTime, timingStart);
      if (vtAcquireFramePending) {
        vtAcquireFramePending = false;
        device->acknowledgeVtAcquire();
      }
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
