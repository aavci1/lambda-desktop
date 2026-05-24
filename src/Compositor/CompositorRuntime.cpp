#include "Compositor/CompositorRuntime.hpp"

#include <Flux/Graphics/VulkanContext.hpp>
#include <Flux/Debug/DebugFlags.hpp>
#include <Flux/Platform/Linux/KmsOutput.hpp>

#include "Compositor/Chrome/CursorRenderer.hpp"
#include "Compositor/CompositorConfigWatch.hpp"
#include "Compositor/CompositorPresentation.hpp"
#include "Compositor/CompositorRenderFrame.hpp"
#include "Compositor/Config/AppliedCompositorConfig.hpp"
#include "Compositor/Config/WallpaperCache.hpp"
#include "Compositor/Config/WallpaperLoader.hpp"
#include "Compositor/Config/CompositorConfig.hpp"
#include "Compositor/Diagnostics/CrashLog.hpp"
#include "Compositor/Diagnostics/CpuTrace.hpp"
#include "Compositor/Input/KmsInputBridge.hpp"
#include "Compositor/Presenter.hpp"
#include "Compositor/PresentationLoop.hpp"
#include "Compositor/Surface/SurfaceRenderer.hpp"
#include "Compositor/Screenshot.hpp"
#include "Compositor/WaylandServer.hpp"
#include "Graphics/Linux/FreeTypeTextSystem.hpp"
#include "Graphics/Vulkan/VulkanCanvas.hpp"
#include "presentation-time-server-protocol.h"

#include <array>
#include <chrono>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <vulkan/vulkan.h>

namespace flux::compositor {
using namespace presentation;

int runKmsCompositor(std::atomic<bool>& running, KmsCompositorOptions options) {
  try {
    diagnostics::crashLog("runtime-start listOutputs=%d", options.listOutputs ? 1 : 0);
    if (diagnostics::cpuTraceEnabled()) {
      std::fprintf(stderr,
                   "lambda-window-manager: CPU trace logging to %s (set LAMBDA_WINDOW_MANAGER_CPU_TRACE=0 to disable)\n",
                   diagnostics::cpuTracePath());
    }
    auto device = flux::platform::KmsDevice::open();
    auto outputs = device->outputs();
    if (outputs.empty()) {
      std::fprintf(stderr, "lambda-window-manager: no connected KMS outputs\n");
      diagnostics::crashLog("runtime-abort no-connected-outputs");
      return 1;
    }
    presentation::printOutputs(outputs);
    if (options.listOutputs) return 0;

    LoadedCompositorConfig loadedConfig = loadConfigWithMetadata();
    auto outputIndex = presentation::selectOutputIndex(outputs, loadedConfig.config.outputSelector);
    if (!outputIndex) {
      std::fprintf(stderr,
                   "lambda-window-manager: output selector \"%s\" did not match any connected output\n",
                   loadedConfig.config.outputSelector ? loadedConfig.config.outputSelector->c_str() : "");
      diagnostics::crashLog("runtime-abort output-selector-miss selector=%s",
                            loadedConfig.config.outputSelector ? loadedConfig.config.outputSelector->c_str() : "");
      presentation::printOutputs(outputs);
      return 1;
    }

    flux::platform::KmsOutput const& output = outputs[*outputIndex];
    diagnostics::crashLog("runtime-output name=%s physical=%ux%u refresh_millihz=%u scale=%.3f",
                          output.name().c_str(),
                          output.width(),
                          output.height(),
                          output.refreshRateMilliHz(),
                          scaleForOutput(loadedConfig.config, output.name()));
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

    auto const canvasStart = SteadyClock::now();
    auto presenter = createPresenter(PresenterContext{
        .output = output,
        .textSystem = textSystem,
        .vulkanInstance = instance,
        .dpiScale = wayland.preferredScale(),
        .logicalWidth = wayland.logicalOutputWidth(),
        .logicalHeight = wayland.logicalOutputHeight(),
    });
    diagnostics::crashLog(presenter->kind() == PresenterKind::AtomicKms ? "presenter atomic-kms"
                                                                          : "presenter vulkan-display");
    flux::Canvas& canvasRef = presenter->canvas();
    flux::Canvas* canvas = &canvasRef;
    traceTiming("create-presenter", canvasStart);

    std::fprintf(stderr,
                 "lambda-window-manager: presenting %ux%u on %s\n",
                 output.width(),
                 output.height(),
                 output.name().c_str());
    diagnostics::crashLog("runtime-presenting output=%s socket=%s logical=%dx%d scale=%.3f",
                          output.name().c_str(),
                          wayland.socketName(),
                          wayland.logicalOutputWidth(),
                          wayland.logicalOutputHeight(),
                          wayland.preferredScale());

    AppliedCompositorConfig appliedConfig{};
    auto applyOutputScale = [&](bool force) {
      float const previousScale = wayland.preferredScale();
      wayland.setPreferredScale(appliedConfig.config.scale);
      if (force || std::abs(previousScale - wayland.preferredScale()) > 0.001f) {
        presenter->updateOutputGeometry(wayland.preferredScale(), wayland.logicalOutputWidth(), wayland.logicalOutputHeight());
        std::fprintf(stderr,
                     "lambda-window-manager: output physical=%ux%u logical=%dx%d scale=%.2f\n",
                     output.width(),
                     output.height(),
                     wayland.logicalOutputWidth(),
                     wayland.logicalOutputHeight(),
                     wayland.preferredScale());
        diagnostics::crashLog("output-scale physical=%ux%u logical=%dx%d scale=%.3f force=%d",
                              output.width(),
                              output.height(),
                              wayland.logicalOutputWidth(),
                              wayland.logicalOutputHeight(),
                              wayland.preferredScale(),
                              force ? 1 : 0);
      }
    };

    CompositorConfigWatchContext configWatch{
        .loadedConfig = loadedConfig,
        .appliedConfig = appliedConfig,
        .wayland = wayland,
        .presenter = *presenter,
        .canvas = *canvas,
        .effectiveConfig = effectiveConfig,
        .applyOutputScale = applyOutputScale,
    };
    AsyncWallpaperLoader wallpaperLoader;
    configWatch.wallpaperLoader = &wallpaperLoader;
    configWatch.wallpaperMaxLongEdge = std::max(output.width(), output.height());
    configWatch.wallpaperCacheDir = wallpaperCacheDirectory(device->cacheDir());
    applyCompositorRuntimeConfig(configWatch, true);

    SurfaceRenderState surfaceRenderState;
    CursorRenderState cursorState;
    LoopInstrumentation loopStats;
    CompositorFrameProfile frameProfile;
    bool const detailedFrameProfile =
        diagnostics::cpuTraceEnabled() || frameProfile.enabled || pacingTraceEnabled();
    std::uint32_t const hardwareCursorWidth = output.cursorWidth();
    std::uint32_t const hardwareCursorHeight = output.cursorHeight();
    bool const hardwareCursorAvailable = hardwareCursorWidth > 0 && hardwareCursorHeight > 0;
    if (!hardwareCursorAvailable && appliedConfig.config.hardwareCursorEnabled) {
      std::fprintf(stderr, "lambda-window-manager: hardware cursor unavailable; using software cursor\n");
    }

    bool forceRender = true;
    bool screenshotPending = false;
    bool skipNextVblank = true;
    bool wasVtForeground = device->isVtForeground();
    bool vtAcquireFramePending = false;
    std::uint64_t softwarePresentationSequence = 0;
    std::uint64_t lastAtomicFlipNsec = 0;
    std::uint64_t lastAtomicScheduledNsec = 0;
    double lastAtomicScheduledRenderMs = 0.0;
    auto nextConfigCheckAt = SteadyClock::now();
    struct AtomicFrameProfile {
      double backgroundMs = 0.0;
      double snapshotMs = 0.0;
      double surfaceMs = 0.0;
      double closingMs = 0.0;
      double cursorMs = 0.0;
      double presentMs = 0.0;
      double canvasPresentMs = 0.0;
      double kmsPresentMs = 0.0;
      double totalMs = 0.0;
      std::size_t activeSizingSurfaces = 0;
      std::int32_t maxBufferWidth = 0;
      std::int32_t maxBufferHeight = 0;
      std::int32_t maxFrameWidth = 0;
      std::int32_t maxFrameHeight = 0;
      std::uint32_t maxDmabufFormat = 0;
      std::uint64_t maxAgeSurfaceId = 0;
      double maxInputToRenderMs = 0.0;
      double maxConfigureToRenderMs = 0.0;
      double maxAckToRenderMs = 0.0;
      double maxCommitToRenderMs = 0.0;
      double maxConfigureToCommitMs = 0.0;
    };
    AtomicReadyFrame atomicReadyFrame{};
    bool atomicFrameDirty = true;
    std::uint64_t lastKnownContentSerial = wayland.contentSerial();
    presentation::AtomicFrameProfile lastAtomicScheduledProfile{};
    std::uint64_t atomicRenderAheadLeadEstimateNsec = initialRenderAheadLeadNsec(output.refreshRateMilliHz());
    auto lastCrashHeartbeat = SteadyClock::now();
    auto maybeCrashHeartbeat = [&](char const* reason) {
      if (!diagnostics::crashLogEnabled()) return;
      auto const now = SteadyClock::now();
      if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastCrashHeartbeat).count() < 2000) return;
      lastCrashHeartbeat = now;
      diagnostics::crashLog("heartbeat reason=%s loops=%llu surfaces=%llu toplevels=%zu serial=%llu "
                            "dirty=%d ready=%d pendingFlip=%d vt=%d idleBlanked=%d forceRender=%d",
                            reason ? reason : "loop",
                            static_cast<unsigned long long>(loopStats.loops),
                            static_cast<unsigned long long>(loopStats.lastSurfaceCount),
                            wayland.toplevelCount(),
                            static_cast<unsigned long long>(wayland.contentSerial()),
                            atomicFrameDirty ? 1 : 0,
                            atomicReadyFrame.ready ? 1 : 0,
                            presenter->atomicPresenter() && presenter->atomicPresenter()->hasPendingPageFlip() ? 1 : 0,
                            device->isVtForeground() ? 1 : 0,
                            idleBlanked ? 1 : 0,
                            forceRender ? 1 : 0);
    };
    constexpr int kIdlePollMs = 250;
    auto atomicRenderAheadDeadlineNsec = [&]() -> std::uint64_t {
      if (!presenter->atomicPresenter() || !presenter->atomicPresenter()->hasPendingPageFlip()) return 0;
      std::uint64_t const refresh = refreshNsec(output.refreshRateMilliHz());
      if (lastAtomicFlipNsec == 0 || refresh == 0) return 0;
      std::uint64_t const expectedFlipNsec = lastAtomicFlipNsec + refresh;
      std::uint64_t const lead = clampRenderAheadLeadNsec(atomicRenderAheadLeadEstimateNsec,
                                                          output.refreshRateMilliHz());
      if (expectedFlipNsec <= lead) return 0;
      return expectedFlipNsec - lead;
    };
    auto atomicRenderAheadDelayMs = [&]() -> int {
      if (!presenter->atomicPresenter() || !atomicFrameDirty || atomicReadyFrame.ready ||
          !presenter->atomicPresenter()->hasPendingPageFlip()) {
        return kIdlePollMs;
      }
      std::uint64_t const deadline = atomicRenderAheadDeadlineNsec();
      if (deadline == 0) return 0;
      std::uint64_t const now = monotonicNanoseconds();
      if (now >= deadline) return 0;
      std::uint64_t const remainingNsec = deadline - now;
      std::uint64_t const remainingMs = (remainingNsec + 999'999ull) / 1'000'000ull;
      return static_cast<int>(std::min<std::uint64_t>(remainingMs, kIdlePollMs));
    };
    auto atomicRenderAheadDue = [&]() {
      return presenter->atomicPresenter() && atomicFrameDirty && !atomicReadyFrame.ready &&
             presenter->atomicPresenter()->hasPendingPageFlip() && atomicRenderAheadDelayMs() == 0;
    };
    auto updateAtomicRenderAheadLead = [&](std::uint64_t renderToReadyNsec, bool usedRenderFence) {
      std::uint64_t const refresh = refreshNsec(output.refreshRateMilliHz());
      std::uint64_t const guard = refresh > 0 ? refresh / 8ull : 2'000'000ull;
      std::uint64_t const nominalLead = nominalRenderAheadLeadNsec(output.refreshRateMilliHz());
      std::uint64_t target = nominalLead;
      if (renderToReadyNsec > 0) {
        target = renderToReadyNsec + guard;
      } else if (!usedRenderFence) {
        target = guard * 2ull;
      }
      target = clampInteractiveRenderAheadLeadNsec(target, output.refreshRateMilliHz());
      if (target >= atomicRenderAheadLeadEstimateNsec) {
        atomicRenderAheadLeadEstimateNsec = (atomicRenderAheadLeadEstimateNsec * 3ull + target) / 4ull;
      } else {
        atomicRenderAheadLeadEstimateNsec = (atomicRenderAheadLeadEstimateNsec + target * 3ull) / 4ull;
      }
    };
    auto pollFds = [&](std::array<int, 4>& storage) {
      std::size_t count = 0;
      int const waylandEventFd = wayland.eventFd();
      if (waylandEventFd >= 0) storage[count++] = waylandEventFd;
      int const shellIpcFd = wayland.shellIpcFd();
      if (shellIpcFd >= 0) storage[count++] = shellIpcFd;
      if (presenter->atomicPresenter() && presenter->atomicPresenter()->hasPendingPageFlip()) {
        int const flipFd = presenter->atomicPresenter()->eventFd();
        if (flipFd >= 0) storage[count++] = flipFd;
      }
      if (presenter->atomicPresenter() && atomicReadyFrame.ready) {
        int const readyFd = presenter->atomicPresenter()->renderReadyFd();
        if (readyFd >= 0) storage[count++] = readyFd;
      }
      return std::span<int const>(storage.data(), count);
    };
    auto pollMaskHas = [](std::uint64_t mask, std::size_t index) {
      return index < 64 && (mask & (std::uint64_t{1} << index)) != 0;
    };
    auto handleVtResume = [&] {
      auto const resumeStart = SteadyClock::now();
      diagnostics::crashLog("vt-resume begin");
      applyOutputScale(true);
      traceTiming("vt-resume-total", resumeStart);
      vtAcquireFramePending = true;
      forceRender = true;
      skipNextVblank = true;
      atomicFrameDirty = true;
      diagnostics::crashLog("vt-resume end logical=%dx%d serial=%llu",
                            wayland.logicalOutputWidth(),
                            wayland.logicalOutputHeight(),
                            static_cast<unsigned long long>(wayland.contentSerial()));
    };
    auto scheduleAtomicFrame = [&](AtomicReadyFrame& frame) {
      if (!presenter->atomicPresenter() || !frame.ready || !presenter->atomicPresenter()->canSchedulePresent()) return false;
      std::uint64_t const frameContentSerial = frame.contentSerial;
      std::uint32_t const presentId = presenter->atomicPresenter()->schedulePresent();
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
                  "contentSerial=%llu sinceLastFlip=%.3fms gpuWait=%.3fms "
                  "ageSurface=%llu inputToRender=%.3fms configureToRender=%.3fms ackToRender=%.3fms "
                  "commitToRender=%.3fms configureToCommit=%.3fms "
                  "phaseBg=%.3fms phaseSnapshot=%.3fms phaseSurface=%.3fms "
                  "phaseClosing=%.3fms phaseCursor=%.3fms "
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
                  static_cast<unsigned long long>(frame.contentSerial),
                  sinceLastFlipMs,
                  gpuWaitMs,
                  static_cast<unsigned long long>(frame.profile.maxAgeSurfaceId),
                  frame.profile.maxInputToRenderMs,
                  frame.profile.maxConfigureToRenderMs,
                  frame.profile.maxAckToRenderMs,
                  frame.profile.maxCommitToRenderMs,
                  frame.profile.maxConfigureToCommitMs,
                  frame.profile.backgroundMs,
                  frame.profile.snapshotMs,
                  frame.profile.surfaceMs,
                  frame.profile.closingMs,
                  frame.profile.cursorMs,
                  frame.profile.presentMs,
                  frame.profile.totalMs);
      if (frame.profile.totalMs > 100.0 || frame.profile.presentMs > 50.0) {
        diagnostics::crashLog("slow-frame-scheduled presentId=%u surfaces=%zu total=%.3fms present=%.3fms "
                              "surface=%.3fms maxBuffer=%dx%d maxFrame=%dx%d",
                              presentId,
                              frame.surfaceCount,
                              frame.profile.totalMs,
                              frame.profile.presentMs,
                              frame.profile.surfaceMs,
                              frame.profile.maxBufferWidth,
                              frame.profile.maxBufferHeight,
                              frame.profile.maxFrameWidth,
                              frame.profile.maxFrameHeight);
      }
      lastAtomicScheduledNsec = scheduledNsec;
      lastAtomicScheduledRenderMs = frame.renderMs;
      lastAtomicScheduledProfile = frame.profile;
      frame.timing.backendPresentId = presentId;
      frame.timing.flags |= static_cast<std::uint32_t>(WP_PRESENTATION_FEEDBACK_KIND_VSYNC |
                                                       WP_PRESENTATION_FEEDBACK_KIND_HW_CLOCK);
      wayland.sendFrameCallbacksOnly(monotonicMilliseconds());
      wayland.sendPresentationFeedbacks(monotonicMilliseconds(), frame.timing);
      frame = {};
      if (wayland.contentSerial() != frameContentSerial) atomicFrameDirty = true;
      forceRender = false;
      return true;
    };
    auto dispatchAtomicPageFlip = [&] {
      if (!presenter->atomicPresenter()) return false;
      auto flip = presenter->atomicPresenter()->dispatchPageFlipEvents();
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
      presentation::AtomicFrameProfile const completedProfile = lastAtomicScheduledProfile;
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
      double const renderSubmitToFlipMs =
          flip->renderSubmittedMonotonicNsec > 0 && completionNsec >= flip->renderSubmittedMonotonicNsec
              ? static_cast<double>(completionNsec - flip->renderSubmittedMonotonicNsec) / 1'000'000.0
              : 0.0;
      double const commitToFlipMs =
          completedProfile.maxAgeSurfaceId != 0 ? completedProfile.maxCommitToRenderMs + renderSubmitToFlipMs : 0.0;
      double const inputToFlipMs =
          completedProfile.maxAgeSurfaceId != 0 ? completedProfile.maxInputToRenderMs + renderSubmitToFlipMs : 0.0;
      double const configureToFlipMs =
          completedProfile.maxAgeSurfaceId != 0 ? completedProfile.maxConfigureToRenderMs + renderSubmitToFlipMs
                                                : 0.0;
      double const scheduledToCommitStartMs =
          flip->scheduledMonotonicNsec > 0 && flip->commitStartMonotonicNsec >= flip->scheduledMonotonicNsec
              ? static_cast<double>(flip->commitStartMonotonicNsec - flip->scheduledMonotonicNsec) / 1'000'000.0
              : 0.0;
      double const commitReturnToFlipMs =
          flip->commitReturnMonotonicNsec > 0 && completionNsec >= flip->commitReturnMonotonicNsec
              ? static_cast<double>(completionNsec - flip->commitReturnMonotonicNsec) / 1'000'000.0
              : 0.0;
      double const flipEventDispatchDelayMs =
          flip->eventDispatchStartMonotonicNsec > 0 && flip->eventDispatchStartMonotonicNsec >= completionNsec
              ? static_cast<double>(flip->eventDispatchStartMonotonicNsec - completionNsec) / 1'000'000.0
              : 0.0;
      double const flipEventHandleMs =
          flip->eventDispatchEndMonotonicNsec > 0 &&
                  flip->eventDispatchEndMonotonicNsec >= flip->eventDispatchStartMonotonicNsec
              ? static_cast<double>(flip->eventDispatchEndMonotonicNsec - flip->eventDispatchStartMonotonicNsec) /
                    1'000'000.0
              : 0.0;
      std::uint64_t const renderToReadyNsec =
          flip->renderSubmittedMonotonicNsec > 0 &&
                  flip->renderReadyMonotonicNsec >= flip->renderSubmittedMonotonicNsec
              ? flip->renderReadyMonotonicNsec - flip->renderSubmittedMonotonicNsec
              : 0;
      updateAtomicRenderAheadLead(renderToReadyNsec, flip->usedRenderFence);
      tracePacing("flip-complete id=%u hw=%d seq=%llu interval=%.3fms expected=%.3fms error=%+.3fms "
                  "queue=%.3fms render=%.3fms renderToReady=%.3fms readyToCommit=%.3fms "
                  "commit=%.3fms scheduledToCommit=%.3fms commitReturnToFlip=%.3fms "
                  "eventDelay=%.3fms eventHandle=%.3fms scheduledDelta=%.3fms renderFence=%d "
                  "ageSurface=%llu commitToFlip=%.3fms inputToFlip=%.3fms configureToFlip=%.3fms\n",
                  flip->presentId,
                  flip->hardware ? 1 : 0,
                  static_cast<unsigned long long>(flip->sequence),
                  static_cast<double>(intervalNsec) / 1'000'000.0,
                  static_cast<double>(expectedNsec) / 1'000'000.0,
                  intervalErrorMs,
                  static_cast<double>(queueNsec) / 1'000'000.0,
                  completedRenderMs,
                  static_cast<double>(renderToReadyNsec) / 1'000'000.0,
                  flip->renderReadyMonotonicNsec > 0 &&
                          flip->scheduledMonotonicNsec >= flip->renderReadyMonotonicNsec
                      ? static_cast<double>(flip->scheduledMonotonicNsec - flip->renderReadyMonotonicNsec) /
                            1'000'000.0
                      : 0.0,
                  static_cast<double>(flip->commitDurationNsec) / 1'000'000.0,
                  scheduledToCommitStartMs,
                  commitReturnToFlipMs,
                  flipEventDispatchDelayMs,
                  flipEventHandleMs,
                  completedScheduledNsec > 0 && flip->scheduledMonotonicNsec >= completedScheduledNsec
                      ? static_cast<double>(flip->scheduledMonotonicNsec - completedScheduledNsec) / 1'000'000.0
                      : 0.0,
                  flip->usedRenderFence ? 1 : 0,
                  static_cast<unsigned long long>(completedProfile.maxAgeSurfaceId),
                  commitToFlipMs,
                  inputToFlipMs,
                  configureToFlipMs);
      if (queueNsec > 100'000'000ull || flip->commitDurationNsec > 50'000'000ull ||
          flipEventDispatchDelayMs > 100.0) {
        diagnostics::crashLog("slow-flip-complete id=%u seq=%llu queue=%.3fms commit=%.3fms "
                              "eventDelay=%.3fms interval=%.3fms",
                              flip->presentId,
                              static_cast<unsigned long long>(flip->sequence),
                              static_cast<double>(queueNsec) / 1'000'000.0,
                              static_cast<double>(flip->commitDurationNsec) / 1'000'000.0,
                              flipEventDispatchDelayMs,
                              static_cast<double>(intervalNsec) / 1'000'000.0);
      }
      return true;
    };
    CompositorRenderFrameContext renderFrameCtx{
        .wayland = wayland,
        .output = output,
        .presenter = *presenter,
        .canvas = *canvas,
        .textSystem = textSystem,
        .appliedConfig = appliedConfig,
        .surfaceRenderState = surfaceRenderState,
        .cursorState = cursorState,
        .frameProfile = frameProfile,
        .loopStats = loopStats,
        .idleBlanked = idleBlanked,
        .hardwareCursorAvailable = hardwareCursorAvailable,
        .detailedFrameProfile = detailedFrameProfile,
        .atomicReadyFrame = &atomicReadyFrame,
        .atomicFrameDirty = &atomicFrameDirty,
        .lastKnownContentSerial = &lastKnownContentSerial,
        .vulkanDisplayTimingSupportLogged = displayTimingSupportLogged,
        .useVulkanPresentationCompletion = useVulkanPresentationCompletion,
    };
    auto renderCompositorFrame = [&](std::chrono::steady_clock::time_point frameTime,
                                     LoopInstrumentation::Clock::time_point renderStart,
                                     PresentationTiming presentationTiming,
                                     bool renderAheadFrame) {
      if (screenshotPending && !flux::requestNextFrameCaptureForCanvas(canvas)) {
        screenshotPending = false;
        std::fprintf(stderr, "lambda-window-manager: screenshots are not supported by this presenter\n");
      }
      renderFrameCtx.idleBlanked = idleBlanked;
      renderFrameCtx.vulkanDisplayTimingSupportLogged = displayTimingSupportLogged;
      renderFrameCtx.useVulkanPresentationCompletion = useVulkanPresentationCompletion;
      flux::compositor::renderCompositorFrame(renderFrameCtx, frameTime, renderStart, presentationTiming, renderAheadFrame);
      displayTimingSupportLogged = renderFrameCtx.vulkanDisplayTimingSupportLogged;
      useVulkanPresentationCompletion = renderFrameCtx.useVulkanPresentationCompletion;
      if (screenshotPending) {
        std::vector<std::uint8_t> pixels;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        if (flux::takeCapturedFrameForCanvas(canvas, pixels, width, height)) {
          auto saved = saveScreenshotPng(pixels, width, height);
          if (saved.error.empty()) {
            std::fprintf(stderr, "lambda-window-manager: saved screenshot to %s\n",
                         saved.path.string().c_str());
          } else {
            std::fprintf(stderr, "lambda-window-manager: failed to save screenshot to %s: %s\n",
                         saved.path.string().c_str(),
                         saved.error.c_str());
          }
        } else {
          std::fprintf(stderr, "lambda-window-manager: screenshot capture did not produce a frame\n");
        }
        screenshotPending = false;
      }
    };
    auto queueScreenshotIfRequested = [&] {
      if (!wayland.consumeScreenshotRequest()) {
        return;
      }
      screenshotPending = true;
      forceRender = true;
      skipNextVblank = true;
      if (idleBlanked) {
        idleBlanked = false;
      }
    };
    auto noteContentSerialChange = [&] {
      std::uint64_t const contentSerial = wayland.contentSerial();
      if (contentSerial == lastKnownContentSerial) return false;
      atomicFrameDirty = true;
      lastKnownContentSerial = contentSerial;
      return true;
    };
    auto acknowledgeVtAcquireAfterFrame = [&] {
      if (!vtAcquireFramePending) return;
      vtAcquireFramePending = false;
      device->acknowledgeVtAcquire();
    };
    auto renderAtomicFrame = [&](bool renderAheadFrame) {
      PresentationTiming presentationTiming{
          .monotonicNsec = monotonicNanoseconds(),
          .sequence = softwarePresentationSequence,
          .refreshNsec = refreshNsec(output.refreshRateMilliHz()),
          .flags = 0u,
      };
      auto const frameTime = std::chrono::steady_clock::now();
      wayland.updateAnimations(monotonicMilliseconds(), appliedConfig.config.animationsEnabled);
      auto timingStart = LoopInstrumentation::Clock::now();
      renderCompositorFrame(frameTime, timingStart, presentationTiming, renderAheadFrame);
      acknowledgeVtAcquireAfterFrame();
      forceRender = false;
    };

    while (running.load(std::memory_order_relaxed) && !device->shouldTerminate()) {
      ++loopStats.loops;
      diagnostics::recordCpuLoop();
      wayland.dispatchShellIpc();
      auto const animationCheckTime = std::chrono::steady_clock::now();
      bool const animationFrameNeeded =
          wayland.hasActiveAnimations() ||
          hasActiveSurfaceAnimations(surfaceRenderState,
                                     animationCheckTime,
                                     appliedConfig.config.animationsEnabled);
      std::array<int, 4> eventFdStorage{};
      std::span<int const> const eventFds = pollFds(eventFdStorage);
      bool const atomicPageFlipPendingBeforePoll =
          presenter->atomicPresenter() && presenter->atomicPresenter()->hasPendingPageFlip();
      bool const atomicFrameAwaitingRender =
          presenter->atomicPresenter() && atomicReadyFrame.ready &&
          presenter->atomicPresenter()->renderReadyFd() >= 0;
      bool const atomicFrameBlockedBeforePoll =
          presenter->atomicPresenter() && (atomicPageFlipPendingBeforePoll || atomicFrameAwaitingRender ||
                              atomicReadyFrame.ready);
      bool const renderAheadNeededBeforePoll =
          presenter->atomicPresenter() && atomicFrameDirty && atomicPageFlipPendingBeforePoll && !atomicReadyFrame.ready &&
          atomicRenderAheadDue();
      std::optional<int> const snapPreviewDelayBeforePoll = wayland.snapPreviewWakeDelayMs();
      bool const snapPreviewFrameNeededBeforePoll =
          snapPreviewDelayBeforePoll && *snapPreviewDelayBeforePoll <= 0;
      bool const animationCanRenderBeforePoll =
          (animationFrameNeeded || snapPreviewFrameNeededBeforePoll || (presenter->atomicPresenter() && atomicFrameDirty)) &&
          !atomicFrameBlockedBeforePoll;
      int const renderAheadDelayBeforePoll = atomicRenderAheadDelayMs();
      int pollTimeoutMs = forceRender || animationCanRenderBeforePoll || renderAheadNeededBeforePoll
                              ? 0
                              : std::min(kIdlePollMs, renderAheadDelayBeforePoll);
      if (snapPreviewDelayBeforePoll) {
        pollTimeoutMs = std::min(pollTimeoutMs, std::max(0, *snapPreviewDelayBeforePoll));
      }
      auto timingStart = LoopInstrumentation::Clock::now();
      auto const pollResult = device->pollEventDetails(pollTimeoutMs, eventFds);
      bool const pollWoke = pollResult.woke;
      loopStats.recordPoll(timingStart, pollWoke, pollTimeoutMs);
      bool const waylandFdPolled = wayland.eventFd() >= 0;
      bool const waylandWoke = waylandFdPolled && pollMaskHas(pollResult.extraReadableMask, 0);
      std::size_t const shellFdIndex = waylandFdPolled ? 1u : 0u;
      bool const shellFdPolled = wayland.shellIpcFd() >= 0;
      bool const shellWoke = shellFdPolled && pollMaskHas(pollResult.extraReadableMask, shellFdIndex);
      std::size_t const pageFlipFdIndex = shellFdIndex + (shellFdPolled ? 1u : 0u);
      bool const pageFlipFdPolled = presenter->atomicPresenter() && presenter->atomicPresenter()->hasPendingPageFlip();
      bool const pageFlipWoke = pageFlipFdPolled && pollMaskHas(pollResult.extraReadableMask, pageFlipFdIndex);
      std::size_t const renderReadyFdIndex = pageFlipFdIndex + (pageFlipFdPolled ? 1u : 0u);
      bool const renderReadyFdPolled =
          presenter->atomicPresenter() && atomicReadyFrame.ready && presenter->atomicPresenter()->renderReadyFd() >= 0;
      bool const renderReadyWoke =
          renderReadyFdPolled && pollMaskHas(pollResult.extraReadableMask, renderReadyFdIndex);
      bool renderReadyUpdated =
          renderReadyWoke && presenter->atomicPresenter() ? presenter->atomicPresenter()->updateRenderReady() : false;
      bool const pageFlipCompleted = dispatchAtomicPageFlip();
      if (pollWoke) {
        diagnostics::recordCpuWakeSources(pollResult.inputOrSystem,
                                          waylandWoke || shellWoke,
                                          pageFlipWoke,
                                          renderReadyWoke);
      }
      if (shellWoke) {
        wayland.dispatchShellIpc();
      }
      if (waylandWoke) {
        timingStart = LoopInstrumentation::Clock::now();
        wayland.dispatch();
        loopStats.recordDispatch(timingStart);
        bool const contentChanged = noteContentSerialChange();
        if (contentChanged) wayland.notifyShellStateChanged();
        diagnostics::recordWaylandDispatch(contentChanged);
      }
      wayland.dispatchShellIpc();
      queueScreenshotIfRequested();
      maybeCrashHeartbeat("main-loop");
      bool const hadInputActivity = inputActivityThisLoop;
      if (hadInputActivity) {
        lastInputActivity = SteadyClock::now();
        inputActivityThisLoop = false;
      }
      ++loopStats.configChecks;
      bool configReloaded = false;
      auto const nowForConfig = SteadyClock::now();
      bool const shouldCheckConfig = nowForConfig >= nextConfigCheckAt;
      if (shouldCheckConfig) {
        nextConfigCheckAt = nowForConfig + std::chrono::milliseconds(500);
      }
      if (shouldCheckConfig && maybeReloadCompositorConfig(configWatch)) {
        configReloaded = true;
      }
      if (pollWallpaperPreview(configWatch)) {
        forceRender = true;
      }
      if (pollWallpaperLoad(configWatch)) {
        configReloaded = true;
        forceRender = true;
      } else if (configWatch.appliedConfig.wallpaperPreviewRevealStart ||
                 configWatch.appliedConfig.wallpaperRevealStart) {
        forceRender = true;
      }
      bool const vtForeground = device->isVtForeground();
      bool const wasForegroundBeforeCheck = wasVtForeground;
      if (vtForeground && !wasForegroundBeforeCheck) {
        diagnostics::crashLog("vt-state foreground=1");
        atomicReadyFrame = {};
        atomicFrameDirty = true;
        handleVtResume();
      }
      wasVtForeground = vtForeground;
      if (!vtForeground) {
        if (wasForegroundBeforeCheck) diagnostics::crashLog("vt-state foreground=0");
        ++loopStats.vtSleeps;
        std::array<int, 4> vtEventFdStorage{};
        std::span<int const> const vtEventFds = pollFds(vtEventFdStorage);
        timingStart = LoopInstrumentation::Clock::now();
        bool const vtPollWoke = device->pollEvents(kIdlePollMs, vtEventFds);
        loopStats.recordPoll(timingStart, vtPollWoke, kIdlePollMs);
        dispatchAtomicPageFlip();
        timingStart = LoopInstrumentation::Clock::now();
        wayland.dispatch();
        wayland.dispatchShellIpc();
        loopStats.recordDispatch(timingStart);
        bool const contentChanged = noteContentSerialChange();
        if (contentChanged) wayland.notifyShellStateChanged();
        diagnostics::recordWaylandDispatch(contentChanged);
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

      if (!renderReadyUpdated && presenter->atomicPresenter() && atomicReadyFrame.ready) {
        renderReadyUpdated = presenter->atomicPresenter()->updateRenderReady();
      }

      bool const genericRenderWake =
          !presenter->atomicPresenter() &&
          (pollResult.inputOrSystem || waylandWoke || (pollWoke && (!pageFlipWoke || !pageFlipCompleted)));
      std::optional<int> const snapPreviewDelay = wayland.snapPreviewWakeDelayMs();
      bool const snapPreviewFrameNeeded = snapPreviewDelay && *snapPreviewDelay <= 0;
      if (forceRender || pollResult.inputOrSystem || waylandWoke || hadInputActivity || configReloaded ||
          animationFrameNeeded || snapPreviewFrameNeeded) {
        if (!presenter->atomicPresenter() || forceRender || waylandWoke || hadInputActivity || configReloaded ||
            animationFrameNeeded || snapPreviewFrameNeeded) {
          atomicFrameDirty = true;
        }
      }
      bool const atomicPageFlipPending = presenter->atomicPresenter() && presenter->atomicPresenter()->hasPendingPageFlip();
      bool const atomicReadyFrameAwaitingRender =
          presenter->atomicPresenter() && atomicReadyFrame.ready &&
          presenter->atomicPresenter()->renderReadyFd() >= 0;
      bool const atomicFrameBlocked =
          presenter->atomicPresenter() && (atomicPageFlipPending || atomicReadyFrameAwaitingRender || atomicReadyFrame.ready);
      bool const renderAheadNeeded =
          presenter->atomicPresenter() && atomicFrameDirty && atomicPageFlipPending && !atomicReadyFrame.ready &&
          atomicRenderAheadDue();
      bool const animationCanRenderNow = animationFrameNeeded && !atomicFrameBlocked;
      bool const renderNeeded =
          forceRender || animationCanRenderNow || snapPreviewFrameNeeded || renderAheadNeeded || genericRenderWake ||
          hadInputActivity || configReloaded ||
          (presenter->atomicPresenter() && atomicFrameDirty && !atomicReadyFrame.ready);
      if (pollWoke || renderNeeded) {
        tracePacing("loop woke=%d system=%d extra=0x%llx waylandWake=%d pageFlipWake=%d "
                    "pageFlipDone=%d input=%d nonFlipWake=%d force=%d anim=%d config=%d render=%d "
                    "ready=%d pendingFlip=%d renderReadyFd=%d renderReadyWake=%d renderAheadNeed=%d "
                    "renderAheadDelay=%d renderAheadLead=%.3fms dirty=%d activeSizing=%zu "
                    "contentSerial=%llu readySerial=%llu renderReadyNow=%d\n",
                    pollWoke ? 1 : 0,
                    pollResult.inputOrSystem ? 1 : 0,
                    static_cast<unsigned long long>(pollResult.extraReadableMask),
                    waylandWoke ? 1 : 0,
                    pageFlipWoke ? 1 : 0,
                    pageFlipCompleted ? 1 : 0,
                    hadInputActivity ? 1 : 0,
                    genericRenderWake ? 1 : 0,
                    forceRender ? 1 : 0,
                    animationFrameNeeded ? 1 : 0,
                    configReloaded ? 1 : 0,
                    renderNeeded ? 1 : 0,
                    atomicReadyFrame.ready ? 1 : 0,
                    presenter->atomicPresenter() && presenter->atomicPresenter()->hasPendingPageFlip() ? 1 : 0,
                    renderReadyFdPolled ? 1 : 0,
                    renderReadyWoke ? 1 : 0,
                    renderAheadNeeded ? 1 : 0,
                    atomicRenderAheadDelayMs(),
                    static_cast<double>(atomicRenderAheadLeadEstimateNsec) / 1'000'000.0,
                    atomicFrameDirty ? 1 : 0,
                    atomicReadyFrame.profile.activeSizingSurfaces,
                    static_cast<unsigned long long>(wayland.contentSerial()),
                    static_cast<unsigned long long>(atomicReadyFrame.contentSerial),
                    renderReadyUpdated ? 1 : 0);
      }
      if (presenter->atomicPresenter()) {
        if (atomicReadyFrame.ready && !presenter->atomicPresenter()->hasPendingPageFlip()) {
          if (device->isVtForeground() && scheduleAtomicFrame(atomicReadyFrame)) {
            if (atomicFrameDirty && !atomicReadyFrame.ready && presenter->atomicPresenter()->hasPendingPageFlip() &&
                atomicRenderAheadDue()) {
              renderAtomicFrame(true);
            }
          }
          loopStats.maybeLog();
          continue;
        }

        if (presenter->atomicPresenter()->hasPendingPageFlip()) {
          if (atomicFrameDirty && !atomicReadyFrame.ready && device->isVtForeground() &&
              atomicRenderAheadDue()) {
            renderAtomicFrame(true);
          }
          loopStats.maybeLog();
          continue;
        }

        if (atomicFrameDirty && device->isVtForeground()) {
          renderAtomicFrame(false);
          if (atomicReadyFrame.ready && !presenter->atomicPresenter()->hasPendingPageFlip()) {
            scheduleAtomicFrame(atomicReadyFrame);
          }
          loopStats.maybeLog();
          continue;
        }

        ++loopStats.idleSkips;
        diagnostics::recordCpuIdleSkip();
        loopStats.maybeLog();
        continue;
      }

      if (!renderNeeded) {
        ++loopStats.idleSkips;
        diagnostics::recordCpuIdleSkip();
        loopStats.maybeLog();
        continue;
      }

      timingStart = LoopInstrumentation::Clock::now();
      PresentationTiming presentationTiming{};
      if (presenter->atomicPresenter()) {
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
            presentation::presentationTimingFromVblank(vblank, output.refreshRateMilliHz(), softwarePresentationSequence);
        loopStats.recordVblank(timingStart);
      }
      if (!device->isVtForeground()) continue;
      auto const frameTime = std::chrono::steady_clock::now();
      wayland.updateAnimations(monotonicMilliseconds(), appliedConfig.config.animationsEnabled);
      timingStart = LoopInstrumentation::Clock::now();
      wayland.dispatch();
      wayland.dispatchShellIpc();
      loopStats.recordDispatch(timingStart);
      bool const contentChanged = noteContentSerialChange();
      if (contentChanged) wayland.notifyShellStateChanged();
      diagnostics::recordWaylandDispatch(contentChanged);

      timingStart = LoopInstrumentation::Clock::now();
      renderCompositorFrame(frameTime, timingStart, presentationTiming, false);
      if (presenter->atomicPresenter() && atomicReadyFrame.ready && !presenter->atomicPresenter()->hasPendingPageFlip()) {
        timingStart = LoopInstrumentation::Clock::now();
        wayland.dispatch();
        wayland.dispatchShellIpc();
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
    diagnostics::crashLog("runtime-stop shouldTerminate=%d running=%d",
                          device->shouldTerminate() ? 1 : 0,
                          running.load(std::memory_order_relaxed) ? 1 : 0);
    return 0;
  } catch (std::exception const& e) {
    std::fprintf(stderr, "lambda-window-manager: %s\n", e.what());
    diagnostics::crashLog("runtime-exception what=%s", e.what());
    return 1;
  } catch (...) {
    diagnostics::crashLog("runtime-exception unknown");
    return 1;
  }
}

} // namespace flux::compositor
