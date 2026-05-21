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
#include "presentation-time-server-protocol.h"

#include <array>
#include <chrono>
#include <charconv>
#include <cctype>
#include <cstdarg>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
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

std::uint64_t monotonicNanoseconds() {
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<std::uint64_t>(now.tv_sec) * 1'000'000'000ull +
         static_cast<std::uint64_t>(now.tv_nsec);
}

using SteadyClock = std::chrono::steady_clock;

double elapsedMilliseconds(SteadyClock::time_point start) {
  return std::chrono::duration<double, std::milli>(SteadyClock::now() - start).count();
}

bool timingTraceEnabled() {
  char const* value = std::getenv("FLUX_COMPOSITOR_TIMING");
  return value && *value && std::strcmp(value, "0") != 0;
}

bool pacingTraceEnabled() {
  char const* value = std::getenv("FLUX_COMPOSITOR_PACING_TRACE");
  return value && *value && std::strcmp(value, "0") != 0;
}

bool forceVulkanDisplayPresenter() {
  char const* value = std::getenv("FLUX_COMPOSITOR_PRESENT");
  return value && (std::strcmp(value, "vulkan") == 0 || std::strcmp(value, "vulkan-display") == 0);
}

void traceTiming(char const* label, SteadyClock::time_point start) {
  if (!timingTraceEnabled()) return;
  std::fprintf(stderr, "flux-compositor: timing %s %.3fms\n", label, elapsedMilliseconds(start));
}

void tracePacing(char const* format, ...) {
  if (!pacingTraceEnabled()) return;
  std::uint64_t const now = monotonicNanoseconds();

  auto write = [&](FILE* file, va_list args) {
    std::fprintf(file, "pacing-trace: %.3fms ", static_cast<double>(now) / 1'000'000.0);
    std::vfprintf(file, format, args);
  };

  va_list args;
  va_start(args, format);
  va_list stderrArgs;
  va_copy(stderrArgs, args);
  write(stderr, stderrArgs);
  va_end(stderrArgs);

  char const* path = std::getenv("FLUX_COMPOSITOR_PACING_TRACE_LOG");
  if (!path || !*path) path = "/tmp/flux-compositor-pacing.log";
  if (FILE* file = std::fopen(path, "a")) {
    write(file, args);
    std::fclose(file);
  }
  va_end(args);
}

std::uint32_t refreshNsec(std::uint32_t refreshMilliHz) {
  return refreshMilliHz > 0
             ? static_cast<std::uint32_t>(1'000'000'000'000ull / static_cast<std::uint64_t>(refreshMilliHz))
             : 0u;
}

PresentationTiming presentationTimingFromVblank(platform::KmsOutput::VblankTiming const& vblank,
                                                std::uint32_t refreshMilliHz,
                                                std::uint64_t fallbackSequence) {
  return PresentationTiming{
      .monotonicNsec = vblank.monotonicNsec != 0 ? vblank.monotonicNsec : monotonicNanoseconds(),
      .sequence = vblank.hardware ? vblank.sequence : fallbackSequence,
      .refreshNsec = refreshNsec(refreshMilliHz),
      .flags = vblank.hardware
                   ? static_cast<std::uint32_t>(WP_PRESENTATION_FEEDBACK_KIND_VSYNC |
                                                WP_PRESENTATION_FEEDBACK_KIND_HW_CLOCK)
                   : 0u,
  };
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
    auto lastInputActivity = SteadyClock::now();
    bool inputActivityThisLoop = false;
    bool idleBlanked = false;
    bool displayTimingSupportLogged = false;
    bool useVulkanPresentationCompletion = false;
    device->setInputHandler([&](flux::platform::KmsInputEvent const& event) {
      inputActivityThisLoop = true;
      if (idleBlanked) return;
      dispatchKmsInputEvent(wayland, event);
    });

    flux::configureVulkanCanvasRuntime(device->requiredVulkanInstanceExtensions(), device->cacheDir());
    auto& vulkan = flux::VulkanContext::instance();
    vulkan.addRequiredDeviceExtension(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
    vulkan.addRequiredDeviceExtension(VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME);
    vulkan.addRequiredDeviceExtension(VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME);
    vulkan.addRequiredDeviceExtension(VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME);
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

    std::unique_ptr<flux::platform::KmsAtomicPresenter> atomicPresenter;
    std::unique_ptr<flux::Canvas> vulkanDisplayCanvas;
    flux::Canvas* canvas = nullptr;
    auto createCanvas = [&] {
      auto const canvasStart = SteadyClock::now();
      atomicPresenter = forceVulkanDisplayPresenter() ? nullptr : output.createAtomicPresenter(textSystem);
      if (atomicPresenter) {
        vulkanDisplayCanvas.reset();
        canvas = &atomicPresenter->canvas();
        std::fprintf(stderr, "flux-compositor: using GBM/atomic-KMS presenter\n");
      } else {
        auto const surfaceStart = SteadyClock::now();
        VkSurfaceKHR surface = output.createVulkanSurface(instance);
        traceTiming("create-vulkan-surface", surfaceStart);
        vulkanDisplayCanvas = flux::createVulkanCanvas(surface, 1u, textSystem);
        canvas = vulkanDisplayCanvas.get();
        std::fprintf(stderr, "flux-compositor: using Vulkan display presenter\n");
      }
      canvas->updateDpiScale(wayland.preferredScale(), wayland.preferredScale());
      canvas->resize(wayland.logicalOutputWidth(), wayland.logicalOutputHeight());
      traceTiming("create-vulkan-canvas", canvasStart);
    };

    createCanvas();

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
    std::uint64_t softwarePresentationSequence = 0;
    std::uint64_t lastAtomicFlipNsec = 0;
    std::uint64_t lastAtomicScheduledNsec = 0;
    double lastAtomicScheduledRenderMs = 0.0;
    struct AtomicFrameProfile {
      double backgroundMs = 0.0;
      double snapshotMs = 0.0;
      double surfaceMs = 0.0;
      double closingMs = 0.0;
      double launcherMs = 0.0;
      double cursorMs = 0.0;
      double presentMs = 0.0;
      double totalMs = 0.0;
      std::size_t activeSizingSurfaces = 0;
      std::int32_t maxBufferWidth = 0;
      std::int32_t maxBufferHeight = 0;
      std::int32_t maxFrameWidth = 0;
      std::int32_t maxFrameHeight = 0;
      std::uint32_t maxDmabufFormat = 0;
    };
    struct AtomicReadyFrame {
      bool ready = false;
      PresentationTiming timing{};
      std::size_t surfaceCount = 0;
      std::chrono::steady_clock::time_point frameTime{};
      double renderMs = 0.0;
      bool renderedAhead = false;
      AtomicFrameProfile profile{};
    };
    AtomicReadyFrame atomicReadyFrame{};
    constexpr int kIdlePollMs = 250;
    auto pollFds = [&](std::array<int, 3>& storage) {
      std::size_t count = 0;
      int const waylandEventFd = wayland.eventFd();
      if (waylandEventFd >= 0) storage[count++] = waylandEventFd;
      if (atomicPresenter && atomicPresenter->hasPendingPageFlip()) {
        int const flipFd = atomicPresenter->eventFd();
        if (flipFd >= 0) storage[count++] = flipFd;
      }
      if (atomicPresenter && atomicReadyFrame.ready && !atomicPresenter->hasPendingPageFlip()) {
        int const readyFd = atomicPresenter->renderReadyFd();
        if (readyFd >= 0) storage[count++] = readyFd;
      }
      return std::span<int const>(storage.data(), count);
    };
    auto pollMaskHas = [](std::uint64_t mask, std::size_t index) {
      return index < 64 && (mask & (std::uint64_t{1} << index)) != 0;
    };
    auto handleVtResume = [&] {
      auto const resumeStart = SteadyClock::now();
      applyOutputScale(true);
      traceTiming("vt-resume-total", resumeStart);
      vtAcquireFramePending = true;
      forceRender = true;
      skipNextVblank = true;
    };
    auto scheduleAtomicFrame = [&](AtomicReadyFrame& frame) {
      if (!atomicPresenter || !frame.ready || !atomicPresenter->canSchedulePresent()) return false;
      std::uint32_t const presentId = atomicPresenter->schedulePresent();
      std::uint64_t const scheduledNsec = monotonicNanoseconds();
      double const sinceLastFlipMs =
          lastAtomicFlipNsec > 0 && scheduledNsec >= lastAtomicFlipNsec
              ? static_cast<double>(scheduledNsec - lastAtomicFlipNsec) / 1'000'000.0
              : 0.0;
      double const gpuWaitMs =
          frame.timing.monotonicNsec > 0 && scheduledNsec >= frame.timing.monotonicNsec
              ? static_cast<double>(scheduledNsec - frame.timing.monotonicNsec) / 1'000'000.0
              : 0.0;
      tracePacing("flip-scheduled id=%u surfaces=%zu activeSizing=%zu maxBuffer=%dx%d "
                  "maxFrame=%dx%d maxDmabuf=0x%08x snapshotFrameTime=%.3fms renderAhead=%d "
                  "sinceLastFlip=%.3fms gpuWait=%.3fms "
                  "phaseBg=%.3fms phaseSnapshot=%.3fms phaseSurface=%.3fms "
                  "phaseClosing=%.3fms phaseLauncher=%.3fms phaseCursor=%.3fms "
                  "phasePresent=%.3fms phaseTotal=%.3fms\n",
                  presentId,
                  frame.surfaceCount,
                  frame.profile.activeSizingSurfaces,
                  frame.profile.maxBufferWidth,
                  frame.profile.maxBufferHeight,
                  frame.profile.maxFrameWidth,
                  frame.profile.maxFrameHeight,
                  frame.profile.maxDmabufFormat,
                  std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frame.frameTime)
                      .count(),
                  frame.renderedAhead ? 1 : 0,
                  sinceLastFlipMs,
                  gpuWaitMs,
                  frame.profile.backgroundMs,
                  frame.profile.snapshotMs,
                  frame.profile.surfaceMs,
                  frame.profile.closingMs,
                  frame.profile.launcherMs,
                  frame.profile.cursorMs,
                  frame.profile.presentMs,
                  frame.profile.totalMs);
      lastAtomicScheduledNsec = scheduledNsec;
      lastAtomicScheduledRenderMs = frame.renderMs;
      frame.timing.backendPresentId = presentId;
      frame.timing.flags |= static_cast<std::uint32_t>(WP_PRESENTATION_FEEDBACK_KIND_VSYNC |
                                                       WP_PRESENTATION_FEEDBACK_KIND_HW_CLOCK);
      wayland.sendFrameCallbacksOnly(monotonicMilliseconds());
      wayland.sendPresentationFeedbacks(monotonicMilliseconds(), frame.timing);
      frame = {};
      forceRender = false;
      return true;
    };
    auto dispatchAtomicPageFlip = [&] {
      if (!atomicPresenter) return false;
      auto flip = atomicPresenter->dispatchPageFlipEvents();
      if (!flip) return false;
      std::uint64_t const completionNsec = flip->monotonicNsec != 0 ? flip->monotonicNsec : monotonicNanoseconds();
      std::uint64_t const intervalNsec = lastAtomicFlipNsec > 0 && completionNsec >= lastAtomicFlipNsec
                                             ? completionNsec - lastAtomicFlipNsec
                                             : 0;
      std::uint64_t const queueNsec =
          flip->scheduledMonotonicNsec > 0 && completionNsec >= flip->scheduledMonotonicNsec
              ? completionNsec - flip->scheduledMonotonicNsec
              : 0;
      std::uint64_t const expectedNsec = refreshNsec(output.refreshRateMilliHz());
      double const intervalErrorMs = intervalNsec > 0 && expectedNsec > 0
                                         ? (static_cast<double>(intervalNsec) -
                                            static_cast<double>(expectedNsec)) /
                                               1'000'000.0
                                         : 0.0;
      double const completedRenderMs = lastAtomicScheduledRenderMs;
      std::uint64_t const completedScheduledNsec = lastAtomicScheduledNsec;
      lastAtomicFlipNsec = completionNsec;
      PresentationCompletion completion{
          .backendPresentId = flip->presentId,
          .monotonicNsec = completionNsec,
          .sequence = flip->sequence,
          .flags = flip->hardware
                       ? static_cast<std::uint32_t>(WP_PRESENTATION_FEEDBACK_KIND_VSYNC |
                                                    WP_PRESENTATION_FEEDBACK_KIND_HW_CLOCK)
                       : 0u,
      };
      wayland.completePresentationFeedbacks({completion}, monotonicMilliseconds());
      tracePacing("flip-complete id=%u hw=%d seq=%llu interval=%.3fms expected=%.3fms error=%+.3fms "
                  "queue=%.3fms render=%.3fms renderToReady=%.3fms readyToCommit=%.3fms "
                  "commit=%.3fms scheduledDelta=%.3fms\n",
                  flip->presentId,
                  flip->hardware ? 1 : 0,
                  static_cast<unsigned long long>(flip->sequence),
                  static_cast<double>(intervalNsec) / 1'000'000.0,
                  static_cast<double>(expectedNsec) / 1'000'000.0,
                  intervalErrorMs,
                  static_cast<double>(queueNsec) / 1'000'000.0,
                  completedRenderMs,
                  flip->renderSubmittedMonotonicNsec > 0 &&
                          flip->renderReadyMonotonicNsec >= flip->renderSubmittedMonotonicNsec
                      ? static_cast<double>(flip->renderReadyMonotonicNsec -
                                            flip->renderSubmittedMonotonicNsec) /
                            1'000'000.0
                      : 0.0,
                  flip->renderReadyMonotonicNsec > 0 &&
                          flip->scheduledMonotonicNsec >= flip->renderReadyMonotonicNsec
                      ? static_cast<double>(flip->scheduledMonotonicNsec - flip->renderReadyMonotonicNsec) /
                            1'000'000.0
                      : 0.0,
                  static_cast<double>(flip->commitDurationNsec) / 1'000'000.0,
                  completedScheduledNsec > 0 && flip->scheduledMonotonicNsec >= completedScheduledNsec
                      ? static_cast<double>(flip->scheduledMonotonicNsec - completedScheduledNsec) / 1'000'000.0
                      : 0.0);
      return true;
    };
    auto renderCompositorFrame = [&](std::chrono::steady_clock::time_point frameTime,
                                     LoopInstrumentation::Clock::time_point renderStart,
                                     PresentationTiming presentationTiming,
                                     bool renderAheadFrame) {
      auto const frameProfileStart = CompositorFrameProfile::Clock::now();
      auto phaseStart = frameProfileStart;
      AtomicFrameProfile atomicFrameProfile{};
      if (atomicPresenter) atomicPresenter->prepareFrame();
      std::size_t committedSurfaceCount = 0;
      canvas->beginFrame();
      if (idleBlanked) {
        canvas->clear(Color{0.f, 0.f, 0.f, 1.f});
        output.hideCursor();
        atomicFrameProfile.backgroundMs = CompositorFrameProfile::milliseconds(phaseStart);
        frameProfile.backgroundMs += atomicFrameProfile.backgroundMs;
        phaseStart = CompositorFrameProfile::Clock::now();
        canvas->present();
        if (atomicPresenter) atomicPresenter->markFrameRendered();
        atomicFrameProfile.presentMs = CompositorFrameProfile::milliseconds(phaseStart);
        atomicFrameProfile.totalMs = CompositorFrameProfile::milliseconds(frameProfileStart);
        frameProfile.presentMs += atomicFrameProfile.presentMs;
        ++frameProfile.frames;
        frameProfile.totalMs += atomicFrameProfile.totalMs;
        frameProfile.maybeLog();
        loopStats.recordRender(renderStart);
        if (atomicPresenter) {
          double const renderMs = LoopInstrumentation::milliseconds(renderStart, LoopInstrumentation::Clock::now());
          atomicReadyFrame = AtomicReadyFrame{
              .ready = true,
              .timing = presentationTiming,
              .surfaceCount = committedSurfaceCount,
              .frameTime = frameTime,
              .renderMs = renderMs,
              .renderedAhead = renderAheadFrame,
              .profile = atomicFrameProfile,
          };
        }
        return;
      }
      drawCompositorBackground(*canvas,
                               appliedConfig,
                               static_cast<std::uint32_t>(wayland.logicalOutputWidth()),
                               static_cast<std::uint32_t>(wayland.logicalOutputHeight()));
      atomicFrameProfile.backgroundMs = CompositorFrameProfile::milliseconds(phaseStart);
      frameProfile.backgroundMs += atomicFrameProfile.backgroundMs;
      phaseStart = CompositorFrameProfile::Clock::now();
      auto snapPreview = wayland.snapPreview();
      bool snapPreviewDrawn = false;
      auto committedSurfaces = wayland.committedSurfaces();
      committedSurfaceCount = committedSurfaces.size();
      for (auto const& surface : committedSurfaces) {
        if (surface.activeSizing) ++atomicFrameProfile.activeSizingSurfaces;
        if (surface.bufferWidth * surface.bufferHeight >
            atomicFrameProfile.maxBufferWidth * atomicFrameProfile.maxBufferHeight) {
          atomicFrameProfile.maxBufferWidth = surface.bufferWidth;
          atomicFrameProfile.maxBufferHeight = surface.bufferHeight;
          atomicFrameProfile.maxDmabufFormat = surface.dmabufFormat;
        }
        if (surface.width * surface.height >
            atomicFrameProfile.maxFrameWidth * atomicFrameProfile.maxFrameHeight) {
          atomicFrameProfile.maxFrameWidth = surface.width;
          atomicFrameProfile.maxFrameHeight = surface.height;
        }
      }
      loopStats.lastSurfaceCount = committedSurfaces.size();
      atomicFrameProfile.snapshotMs = CompositorFrameProfile::milliseconds(phaseStart);
      frameProfile.snapshotMs += atomicFrameProfile.snapshotMs;
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
      atomicFrameProfile.surfaceMs = CompositorFrameProfile::milliseconds(phaseStart);
      frameProfile.surfaceMs += atomicFrameProfile.surfaceMs;
      phaseStart = CompositorFrameProfile::Clock::now();
      captureClosingSurfaces(surfaceRenderState,
                             liveSurfaceIds,
                             frameTime,
                             appliedConfig.config.animationsEnabled);
      drawClosingSurfaces(*canvas, surfaceRenderState, frameTime);
      atomicFrameProfile.closingMs = CompositorFrameProfile::milliseconds(phaseStart);
      frameProfile.closingMs += atomicFrameProfile.closingMs;
      phaseStart = CompositorFrameProfile::Clock::now();
      drawCommandLauncher(*canvas,
                          textSystem,
                          wayland.commandLauncher(),
                          appliedConfig.config.chrome,
                          wayland.logicalOutputWidth(),
                          wayland.logicalOutputHeight());
      atomicFrameProfile.launcherMs = CompositorFrameProfile::milliseconds(phaseStart);
      frameProfile.launcherMs += atomicFrameProfile.launcherMs;
      phaseStart = CompositorFrameProfile::Clock::now();
      drawCompositorCursor(wayland,
                           *canvas,
                           output,
                           cursorState,
                           appliedConfig.config.cursorTheme,
                           appliedConfig.config.cursorSize,
                           appliedConfig.config.hardwareCursorEnabled && hardwareCursorAvailable);
      atomicFrameProfile.cursorMs = CompositorFrameProfile::milliseconds(phaseStart);
      frameProfile.cursorMs += atomicFrameProfile.cursorMs;
      pruneSurfaceRenderState(surfaceRenderState, liveSurfaceIds);
      phaseStart = CompositorFrameProfile::Clock::now();
      std::vector<PresentationCompletion> presentationCompletions;
      canvas->present();
      if (atomicPresenter) atomicPresenter->markFrameRendered();
      atomicFrameProfile.presentMs = CompositorFrameProfile::milliseconds(phaseStart);
      atomicFrameProfile.totalMs = CompositorFrameProfile::milliseconds(frameProfileStart);
      if (atomicPresenter) {
        double const renderMs = LoopInstrumentation::milliseconds(renderStart, LoopInstrumentation::Clock::now());
        atomicReadyFrame = AtomicReadyFrame{
            .ready = true,
            .timing = presentationTiming,
            .surfaceCount = committedSurfaceCount,
            .frameTime = frameTime,
            .renderMs = renderMs,
            .renderedAhead = renderAheadFrame,
            .profile = atomicFrameProfile,
        };
      } else {
        if (!displayTimingSupportLogged && flux::vulkanCanvasSupportsDisplayTiming(canvas)) {
          std::fprintf(stderr, "flux-compositor: Vulkan display timing available\n");
          displayTimingSupportLogged = true;
        }
        auto pastPresentationTimings = flux::pollVulkanCanvasPastPresentationTimings(canvas);
        if (!pastPresentationTimings.empty()) {
          useVulkanPresentationCompletion = true;
          presentationCompletions.reserve(pastPresentationTimings.size());
          for (auto const& timing : pastPresentationTimings) {
            presentationCompletions.push_back(PresentationCompletion{
                .backendPresentId = timing.presentId,
                .monotonicNsec = timing.actualPresentTime,
            });
          }
        }
        if (useVulkanPresentationCompletion) {
          presentationTiming.backendPresentId = flux::lastVulkanCanvasPresentId(canvas);
        }
      }
      frameProfile.presentMs += atomicFrameProfile.presentMs;
      ++frameProfile.frames;
      frameProfile.totalMs += atomicFrameProfile.totalMs;
      frameProfile.maybeLog();
      loopStats.recordRender(renderStart);
      if (!atomicPresenter) {
        wayland.completePresentationFeedbacks(presentationCompletions, monotonicMilliseconds());
        wayland.sendFrameCallbacks(monotonicMilliseconds(), presentationTiming);
      }
    };

    while (running.load(std::memory_order_relaxed) && !device->shouldTerminate()) {
      ++loopStats.loops;
      auto const animationCheckTime = std::chrono::steady_clock::now();
      bool const animationFrameNeeded =
          wayland.hasActiveAnimations() ||
          hasActiveSurfaceAnimations(surfaceRenderState,
                                     animationCheckTime,
                                     appliedConfig.config.animationsEnabled);
      std::array<int, 3> eventFdStorage{};
      std::span<int const> const eventFds = pollFds(eventFdStorage);
      bool const atomicFrameAlreadyQueued =
          atomicPresenter && atomicPresenter->hasPendingPageFlip() && atomicReadyFrame.ready;
      int const pollTimeoutMs = forceRender || (animationFrameNeeded && !atomicFrameAlreadyQueued) ? 0 : kIdlePollMs;
      auto timingStart = LoopInstrumentation::Clock::now();
      auto const pollResult = device->pollEventDetails(pollTimeoutMs, eventFds);
      bool const pollWoke = pollResult.woke;
      loopStats.recordPoll(timingStart, pollWoke);
      bool const waylandFdPolled = wayland.eventFd() >= 0;
      bool const waylandWoke = waylandFdPolled && pollMaskHas(pollResult.extraReadableMask, 0);
      std::size_t const pageFlipFdIndex = waylandFdPolled ? 1u : 0u;
      bool const pageFlipFdPolled = atomicPresenter && atomicPresenter->hasPendingPageFlip();
      bool const pageFlipWoke = pageFlipFdPolled && pollMaskHas(pollResult.extraReadableMask, pageFlipFdIndex);
      std::size_t const renderReadyFdIndex = pageFlipFdIndex + (pageFlipFdPolled ? 1u : 0u);
      bool const renderReadyFdPolled =
          atomicPresenter && atomicReadyFrame.ready && !atomicPresenter->hasPendingPageFlip() &&
          atomicPresenter->renderReadyFd() >= 0;
      bool const renderReadyWoke =
          renderReadyFdPolled && pollMaskHas(pollResult.extraReadableMask, renderReadyFdIndex);
      bool const pageFlipCompleted = dispatchAtomicPageFlip();
      timingStart = LoopInstrumentation::Clock::now();
      wayland.dispatch();
      loopStats.recordDispatch(timingStart);
      bool const hadInputActivity = inputActivityThisLoop;
      if (hadInputActivity) {
        lastInputActivity = SteadyClock::now();
        inputActivityThisLoop = false;
      }
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
        atomicReadyFrame = {};
        handleVtResume();
      }
      wasVtForeground = vtForeground;
      if (!vtForeground) {
        ++loopStats.vtSleeps;
        std::array<int, 3> vtEventFdStorage{};
        std::span<int const> const vtEventFds = pollFds(vtEventFdStorage);
        timingStart = LoopInstrumentation::Clock::now();
        bool const vtPollWoke = device->pollEvents(kIdlePollMs, vtEventFds);
        loopStats.recordPoll(timingStart, vtPollWoke);
        dispatchAtomicPageFlip();
        timingStart = LoopInstrumentation::Clock::now();
        wayland.dispatch();
        loopStats.recordDispatch(timingStart);
        if (inputActivityThisLoop) {
          lastInputActivity = SteadyClock::now();
          inputActivityThisLoop = false;
        }
        if (!device->isVtForeground()) {
          loopStats.maybeLog();
          continue;
        }
        handleVtResume();
        wasVtForeground = true;
      }

      bool const shouldIdleBlank =
          appliedConfig.config.idleBlankTimeoutSeconds > 0 && !wayland.hasIdleInhibitors() &&
          std::chrono::duration_cast<std::chrono::seconds>(SteadyClock::now() - lastInputActivity).count() >=
              appliedConfig.config.idleBlankTimeoutSeconds;
      if (shouldIdleBlank != idleBlanked) {
        idleBlanked = shouldIdleBlank;
        forceRender = true;
      }

      bool const nonPageFlipWake =
          pollResult.inputOrSystem || waylandWoke || (pollWoke && (!pageFlipWoke || !pageFlipCompleted));
      bool const renderNeeded = forceRender || animationFrameNeeded || nonPageFlipWake || hadInputActivity ||
                                configReloaded;
      if (pollWoke || renderNeeded) {
        tracePacing("loop woke=%d system=%d extra=0x%llx waylandWake=%d pageFlipWake=%d "
                    "pageFlipDone=%d input=%d nonFlipWake=%d force=%d anim=%d config=%d render=%d "
                    "ready=%d pendingFlip=%d renderReadyFd=%d renderReadyWake=%d\n",
                    pollWoke ? 1 : 0,
                    pollResult.inputOrSystem ? 1 : 0,
                    static_cast<unsigned long long>(pollResult.extraReadableMask),
                    waylandWoke ? 1 : 0,
                    pageFlipWoke ? 1 : 0,
                    pageFlipCompleted ? 1 : 0,
                    hadInputActivity ? 1 : 0,
                    nonPageFlipWake ? 1 : 0,
                    forceRender ? 1 : 0,
                    animationFrameNeeded ? 1 : 0,
                    configReloaded ? 1 : 0,
                    renderNeeded ? 1 : 0,
                    atomicReadyFrame.ready ? 1 : 0,
                    atomicPresenter && atomicPresenter->hasPendingPageFlip() ? 1 : 0,
                    renderReadyFdPolled ? 1 : 0,
                    renderReadyWoke ? 1 : 0);
      }
      if (!renderNeeded) {
        if (atomicPresenter && atomicReadyFrame.ready && !atomicPresenter->hasPendingPageFlip() &&
            device->isVtForeground()) {
          scheduleAtomicFrame(atomicReadyFrame);
          forceRender = false;
        }
        ++loopStats.idleSkips;
        loopStats.maybeLog();
        continue;
      }
      if (atomicPresenter && atomicReadyFrame.ready && !atomicPresenter->hasPendingPageFlip()) {
        if (device->isVtForeground() && scheduleAtomicFrame(atomicReadyFrame)) {
          loopStats.maybeLog();
          continue;
        }
        loopStats.maybeLog();
        continue;
      }
      if (atomicPresenter && atomicPresenter->hasPendingPageFlip()) {
        if (!atomicReadyFrame.ready && device->isVtForeground()) {
          timingStart = LoopInstrumentation::Clock::now();
          PresentationTiming presentationTiming{
              .monotonicNsec = monotonicNanoseconds(),
              .sequence = softwarePresentationSequence,
              .refreshNsec = refreshNsec(output.refreshRateMilliHz()),
              .flags = 0u,
          };
          auto const frameTime = std::chrono::steady_clock::now();
          wayland.updateAnimations(monotonicMilliseconds(), appliedConfig.config.animationsEnabled);
          timingStart = LoopInstrumentation::Clock::now();
          wayland.dispatch();
          loopStats.recordDispatch(timingStart);
          timingStart = LoopInstrumentation::Clock::now();
          renderCompositorFrame(frameTime, timingStart, presentationTiming, true);
          forceRender = false;
        }
        loopStats.maybeLog();
        continue;
      }
      if (atomicPresenter && atomicReadyFrame.ready) {
        scheduleAtomicFrame(atomicReadyFrame);
        forceRender = false;
        loopStats.maybeLog();
        continue;
      }

      timingStart = LoopInstrumentation::Clock::now();
      PresentationTiming presentationTiming{};
      if (atomicPresenter) {
        presentationTiming = PresentationTiming{
            .monotonicNsec = monotonicNanoseconds(),
            .sequence = softwarePresentationSequence,
            .refreshNsec = refreshNsec(output.refreshRateMilliHz()),
            .flags = 0u,
        };
      } else if (skipNextVblank) {
        skipNextVblank = false;
        presentationTiming = PresentationTiming{
            .monotonicNsec = monotonicNanoseconds(),
            .sequence = ++softwarePresentationSequence,
            .refreshNsec = refreshNsec(output.refreshRateMilliHz()),
            .flags = 0u,
        };
      } else {
        auto const vblank = output.waitForVblank();
        if (!vblank.hardware) ++softwarePresentationSequence;
        presentationTiming =
            presentationTimingFromVblank(vblank, output.refreshRateMilliHz(), softwarePresentationSequence);
        loopStats.recordVblank(timingStart);
      }
      if (!device->isVtForeground()) continue;
      auto const frameTime = std::chrono::steady_clock::now();
      wayland.updateAnimations(monotonicMilliseconds(), appliedConfig.config.animationsEnabled);
      timingStart = LoopInstrumentation::Clock::now();
      wayland.dispatch();
      loopStats.recordDispatch(timingStart);

      timingStart = LoopInstrumentation::Clock::now();
      renderCompositorFrame(frameTime, timingStart, presentationTiming, false);
      if (atomicPresenter && atomicReadyFrame.ready && !atomicPresenter->hasPendingPageFlip()) {
        timingStart = LoopInstrumentation::Clock::now();
        wayland.dispatch();
        loopStats.recordDispatch(timingStart);
        scheduleAtomicFrame(atomicReadyFrame);
      }
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
