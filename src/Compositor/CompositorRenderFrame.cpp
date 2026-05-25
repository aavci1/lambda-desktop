#include "Compositor/CompositorRenderFrame.hpp"

#include "Compositor/Chrome/CursorRenderer.hpp"
#include "Compositor/Chrome/WindowChromeRenderer.hpp"
#include "Compositor/CompositorPresentation.hpp"
#include "Compositor/Diagnostics/CpuTrace.hpp"
#include "Compositor/Surface/SurfaceRenderer.hpp"
#include "Graphics/Linux/FreeTypeTextSystem.hpp"

#include "presentation-time-server-protocol.h"

#include <algorithm>
#include <unordered_set>
#include <vector>

namespace flux::compositor {
namespace {

void drawScreenshotSelectionOverlay(Canvas& canvas,
                                    WaylandServer const& wayland,
                                    ScreenshotSelectionOverlay const& overlay) {
  float const outputWidth = static_cast<float>(wayland.logicalOutputWidth());
  float const outputHeight = static_cast<float>(wayland.logicalOutputHeight());
  if (outputWidth <= 0.f || outputHeight <= 0.f) return;

  Color const scrim{0.f, 0.f, 0.f, 0.36f};
  Color const guide{0.38f, 0.68f, 1.f, 0.72f};
  Color const fill{0.38f, 0.68f, 1.f, 0.12f};
  Color const border{0.86f, 0.95f, 1.f, 0.96f};
  Rect const outputRect = Rect::sharp(0.f, 0.f, outputWidth, outputHeight);

  if (!overlay.region) {
    canvas.drawRect(outputRect,
                    CornerRadius{},
                    FillStyle::solid(Color{0.f, 0.f, 0.f, 0.12f}),
                    StrokeStyle::none(),
                    ShadowStyle::none());
    float const x = std::clamp(overlay.currentX, 0.f, outputWidth);
    float const y = std::clamp(overlay.currentY, 0.f, outputHeight);
    canvas.drawLine(Point{x, 0.f}, Point{x, outputHeight}, StrokeStyle::solid(guide, 1.f));
    canvas.drawLine(Point{0.f, y}, Point{outputWidth, y}, StrokeStyle::solid(guide, 1.f));
    return;
  }

  Rect const selected = Rect::sharp(static_cast<float>(overlay.region->x),
                                   static_cast<float>(overlay.region->y),
                                   static_cast<float>(overlay.region->width),
                                   static_cast<float>(overlay.region->height));
  canvas.drawRect(Rect::sharp(0.f, 0.f, outputWidth, selected.y),
                  CornerRadius{},
                  FillStyle::solid(scrim),
                  StrokeStyle::none(),
                  ShadowStyle::none());
  canvas.drawRect(Rect::sharp(0.f, selected.y, selected.x, selected.height),
                  CornerRadius{},
                  FillStyle::solid(scrim),
                  StrokeStyle::none(),
                  ShadowStyle::none());
  canvas.drawRect(Rect::sharp(selected.x + selected.width,
                              selected.y,
                              std::max(0.f, outputWidth - selected.x - selected.width),
                              selected.height),
                  CornerRadius{},
                  FillStyle::solid(scrim),
                  StrokeStyle::none(),
                  ShadowStyle::none());
  canvas.drawRect(Rect::sharp(0.f,
                              selected.y + selected.height,
                              outputWidth,
                              std::max(0.f, outputHeight - selected.y - selected.height)),
                  CornerRadius{},
                  FillStyle::solid(scrim),
                  StrokeStyle::none(),
                  ShadowStyle::none());
  canvas.drawRect(selected,
                  CornerRadius{},
                  FillStyle::solid(fill),
                  StrokeStyle::solid(border, 1.5f),
                  ShadowStyle::none());
}

void drawScreenshotFlash(Canvas& canvas, WaylandServer const& wayland, float opacity) {
  if (opacity <= 0.f) return;
  float const outputWidth = static_cast<float>(wayland.logicalOutputWidth());
  float const outputHeight = static_cast<float>(wayland.logicalOutputHeight());
  if (outputWidth <= 0.f || outputHeight <= 0.f) return;
  canvas.drawRect(Rect::sharp(0.f, 0.f, outputWidth, outputHeight),
                  CornerRadius{},
                  FillStyle::solid(Color{1.f, 1.f, 1.f, std::clamp(opacity, 0.f, 1.f)}),
                  StrokeStyle::none(),
                  ShadowStyle::none());
}

} // namespace

void renderCompositorFrame(CompositorRenderFrameContext& ctx,
                           std::chrono::steady_clock::time_point frameTime,
                           std::chrono::steady_clock::time_point renderStart,
                           PresentationTiming presentationTiming,
                           bool renderAheadFrame) {
  auto const profileNow = [&] {
    return ctx.detailedFrameProfile ? presentation::CompositorFrameProfile::Clock::now()
                                    : presentation::CompositorFrameProfile::Clock::time_point{};
  };
  auto const profileMs = [&](presentation::CompositorFrameProfile::Clock::time_point start) {
    return ctx.detailedFrameProfile ? presentation::CompositorFrameProfile::milliseconds(start) : 0.0;
  };
  auto const frameProfileStart = profileNow();
  auto phaseStart = frameProfileStart;
  presentation::AtomicFrameProfile atomicFrameProfile{};
  platform::KmsAtomicPresenter* atomicPresenter = ctx.presenter.atomicPresenter();
  if (atomicPresenter) atomicPresenter->prepareFrame();
  std::size_t committedSurfaceCount = 0;
  ctx.canvas.beginFrame();
  if (ctx.idleBlanked) {
    ctx.canvas.clear(Color{0.f, 0.f, 0.f, 1.f});
    ctx.output.hideCursor();
    atomicFrameProfile.backgroundMs = profileMs(phaseStart);
    ctx.frameProfile.backgroundMs += atomicFrameProfile.backgroundMs;
    phaseStart = profileNow();
    auto const canvasPresentStart = profileNow();
    ctx.canvas.present();
    atomicFrameProfile.canvasPresentMs = profileMs(canvasPresentStart);
    if (atomicPresenter) {
      auto const kmsPresentStart = profileNow();
      atomicPresenter->markFrameRendered();
      atomicFrameProfile.kmsPresentMs = profileMs(kmsPresentStart);
    }
    atomicFrameProfile.presentMs = profileMs(phaseStart);
    atomicFrameProfile.totalMs = profileMs(frameProfileStart);
    ctx.frameProfile.presentMs += atomicFrameProfile.presentMs;
    ++ctx.frameProfile.frames;
    ctx.frameProfile.totalMs += atomicFrameProfile.totalMs;
    diagnostics::recordCpuFrame({
        .surfaces = committedSurfaceCount,
        .backgroundMs = atomicFrameProfile.backgroundMs,
        .snapshotMs = atomicFrameProfile.snapshotMs,
        .surfaceMs = atomicFrameProfile.surfaceMs,
        .closingMs = atomicFrameProfile.closingMs,
        .cursorMs = atomicFrameProfile.cursorMs,
        .presentMs = atomicFrameProfile.presentMs,
        .canvasPresentMs = atomicFrameProfile.canvasPresentMs,
        .kmsPresentMs = atomicFrameProfile.kmsPresentMs,
        .totalMs = atomicFrameProfile.totalMs,
    });
    ctx.frameProfile.maybeLog();
    ctx.loopStats.recordRender(renderStart);
    if (atomicPresenter && ctx.atomicReadyFrame && ctx.atomicFrameDirty && ctx.lastKnownContentSerial) {
      double const renderMs =
          ctx.detailedFrameProfile
              ? presentation::LoopInstrumentation::milliseconds(renderStart, presentation::LoopInstrumentation::Clock::now())
              : 0.0;
      *ctx.atomicReadyFrame = AtomicReadyFrame{
          .ready = true,
          .timing = presentationTiming,
          .surfaceCount = committedSurfaceCount,
          .frameTime = frameTime,
          .renderMs = renderMs,
          .renderedAhead = renderAheadFrame,
          .contentSerial = ctx.wayland.contentSerial(),
          .profile = atomicFrameProfile,
      };
      *ctx.atomicFrameDirty = false;
      *ctx.lastKnownContentSerial = ctx.atomicReadyFrame->contentSerial;
    }
    return;
  }
  drawCompositorBackground(ctx.canvas,
                           ctx.appliedConfig,
                           static_cast<std::uint32_t>(ctx.wayland.logicalOutputWidth()),
                           static_cast<std::uint32_t>(ctx.wayland.logicalOutputHeight()));
  atomicFrameProfile.backgroundMs = profileMs(phaseStart);
  ctx.frameProfile.backgroundMs += atomicFrameProfile.backgroundMs;
  phaseStart = profileNow();
  auto snapPreview = ctx.wayland.snapPreview();
  bool snapPreviewDrawn = false;
  auto committedSurfaces = ctx.wayland.committedSurfaces();
  committedSurfaceCount = committedSurfaces.size();
  std::uint64_t const renderSnapshotNsec = presentation::monotonicNanoseconds();
  auto ageMs = [renderSnapshotNsec](std::uint64_t thenNsec) {
    return thenNsec > 0 && renderSnapshotNsec >= thenNsec
               ? static_cast<double>(renderSnapshotNsec - thenNsec) / 1'000'000.0
               : 0.0;
  };
  for (auto const& surface : committedSurfaces) {
    if (surface.pacingSizing) ++atomicFrameProfile.activeSizingSurfaces;
    if (surface.bufferWidth * surface.bufferHeight >
        atomicFrameProfile.maxBufferWidth * atomicFrameProfile.maxBufferHeight) {
      atomicFrameProfile.maxBufferWidth = surface.bufferWidth;
      atomicFrameProfile.maxBufferHeight = surface.bufferHeight;
      atomicFrameProfile.maxDmabufFormat = surface.dmabufFormat;
    }
    if (surface.width * surface.height > atomicFrameProfile.maxFrameWidth * atomicFrameProfile.maxFrameHeight) {
      atomicFrameProfile.maxFrameWidth = surface.width;
      atomicFrameProfile.maxFrameHeight = surface.height;
    }
    if (surface.pacingSizing) {
      double const commitToRenderMs = ageMs(surface.lastCommitNsec);
      double const inputToRenderMs = ageMs(surface.lastResizeInputNsec);
      double const configureToRenderMs = ageMs(surface.lastConfigureSentNsec);
      double const ackToRenderMs = ageMs(surface.lastConfigureAckNsec);
      double const configureToCommitMs =
          surface.lastConfigureSentNsec > 0 && surface.lastCommitNsec >= surface.lastConfigureSentNsec
              ? static_cast<double>(surface.lastCommitNsec - surface.lastConfigureSentNsec) / 1'000'000.0
              : 0.0;
      if (commitToRenderMs >= atomicFrameProfile.maxCommitToRenderMs) {
        atomicFrameProfile.maxAgeSurfaceId = surface.id;
        atomicFrameProfile.maxCommitToRenderMs = commitToRenderMs;
      }
      atomicFrameProfile.maxInputToRenderMs =
          std::max(atomicFrameProfile.maxInputToRenderMs, inputToRenderMs);
      atomicFrameProfile.maxConfigureToRenderMs =
          std::max(atomicFrameProfile.maxConfigureToRenderMs, configureToRenderMs);
      atomicFrameProfile.maxAckToRenderMs = std::max(atomicFrameProfile.maxAckToRenderMs, ackToRenderMs);
      atomicFrameProfile.maxConfigureToCommitMs =
          std::max(atomicFrameProfile.maxConfigureToCommitMs, configureToCommitMs);
      if (presentation::pacingTraceEnabled()) {
        presentation::tracePacing("surface-age surface=%llu frame=%dx%d buffer=%dx%d serial=%llu "
                                  "configureSerial=%u configure=%dx%d inputToRender=%.3fms "
                                  "configureToRender=%.3fms ackToRender=%.3fms commitToRender=%.3fms "
                                  "configureToCommit=%.3fms activeSizing=%d pacingSizing=%d\n",
                                  static_cast<unsigned long long>(surface.id),
                                  surface.width,
                                  surface.height,
                                  surface.bufferWidth,
                                  surface.bufferHeight,
                                  static_cast<unsigned long long>(surface.serial),
                                  surface.lastConfigureSerial,
                                  surface.lastConfigureWidth,
                                  surface.lastConfigureHeight,
                                  inputToRenderMs,
                                  configureToRenderMs,
                                  ackToRenderMs,
                                  commitToRenderMs,
                                  configureToCommitMs,
                                  surface.activeSizing ? 1 : 0,
                                  surface.pacingSizing ? 1 : 0);
      }
    }
  }
  ctx.loopStats.lastSurfaceCount = committedSurfaces.size();
  atomicFrameProfile.snapshotMs = profileMs(phaseStart);
  ctx.frameProfile.snapshotMs += atomicFrameProfile.snapshotMs;
  ctx.frameProfile.surfaces += committedSurfaces.size();
  std::unordered_set<std::uint64_t> liveSurfaceIds;
  liveSurfaceIds.reserve(committedSurfaces.size());
  phaseStart = profileNow();
  for (auto const& clientSurface : committedSurfaces) {
    if (snapPreview && !snapPreviewDrawn && snapPreview->surfaceId == clientSurface.id) {
      drawSnapPreview(ctx.canvas, *snapPreview, ctx.appliedConfig.config.chrome);
      snapPreviewDrawn = true;
    }
    liveSurfaceIds.insert(clientSurface.id);
    auto& visual = ctx.surfaceRenderState.surfaceVisuals[clientSurface.id];
    auto& cached = ctx.surfaceRenderState.clientImages[clientSurface.id];
    drawCommittedSurface(ctx.wayland,
                         ctx.canvas,
                         ctx.textSystem,
                         clientSurface,
                         visual,
                         cached,
                         frameTime,
                         ctx.appliedConfig.config.chrome,
                         ctx.appliedConfig.config.animationsEnabled);
  }
  if (snapPreview && !snapPreviewDrawn) {
    drawSnapPreview(ctx.canvas, *snapPreview, ctx.appliedConfig.config.chrome);
  }
  atomicFrameProfile.surfaceMs = profileMs(phaseStart);
  ctx.frameProfile.surfaceMs += atomicFrameProfile.surfaceMs;
  phaseStart = profileNow();
  captureClosingSurfaces(ctx.surfaceRenderState,
                         liveSurfaceIds,
                         frameTime,
                         ctx.appliedConfig.config.animationsEnabled);
  drawClosingSurfaces(ctx.canvas, ctx.surfaceRenderState, frameTime);
  if (auto overlay = ctx.wayland.screenshotSelectionOverlay()) {
    drawScreenshotSelectionOverlay(ctx.canvas, ctx.wayland, *overlay);
  }
  atomicFrameProfile.closingMs = profileMs(phaseStart);
  ctx.frameProfile.closingMs += atomicFrameProfile.closingMs;
  drawCompositorCursor(ctx.wayland,
                       ctx.canvas,
                       ctx.output,
                       ctx.cursorState,
                       ctx.appliedConfig.config.cursorTheme,
                       ctx.appliedConfig.config.cursorSize,
                       ctx.appliedConfig.config.hardwareCursorEnabled && ctx.hardwareCursorAvailable);
  drawScreenshotFlash(ctx.canvas, ctx.wayland, ctx.screenshotFlashOpacity);
  atomicFrameProfile.cursorMs = profileMs(phaseStart);
  ctx.frameProfile.cursorMs += atomicFrameProfile.cursorMs;
  pruneSurfaceRenderState(ctx.surfaceRenderState, liveSurfaceIds);
  phaseStart = profileNow();
  std::vector<PresentationCompletion> presentationCompletions;
  auto const canvasPresentStart = profileNow();
  ctx.canvas.present();
  atomicFrameProfile.canvasPresentMs = profileMs(canvasPresentStart);
  if (atomicPresenter) {
    auto const kmsPresentStart = profileNow();
    atomicPresenter->markFrameRendered();
    atomicFrameProfile.kmsPresentMs = profileMs(kmsPresentStart);
  }
  atomicFrameProfile.presentMs = profileMs(phaseStart);
  atomicFrameProfile.totalMs = profileMs(frameProfileStart);
  if (atomicPresenter && ctx.atomicReadyFrame && ctx.atomicFrameDirty && ctx.lastKnownContentSerial) {
    double const renderMs =
        ctx.detailedFrameProfile
            ? presentation::LoopInstrumentation::milliseconds(renderStart, presentation::LoopInstrumentation::Clock::now())
            : 0.0;
    *ctx.atomicReadyFrame = AtomicReadyFrame{
        .ready = true,
        .timing = presentationTiming,
        .surfaceCount = committedSurfaceCount,
        .frameTime = frameTime,
        .renderMs = renderMs,
        .renderedAhead = renderAheadFrame,
        .contentSerial = ctx.wayland.contentSerial(),
        .profile = atomicFrameProfile,
    };
    *ctx.atomicFrameDirty = false;
    *ctx.lastKnownContentSerial = ctx.atomicReadyFrame->contentSerial;
  } else if (!atomicPresenter) {
    if (!ctx.vulkanDisplayTimingSupportLogged && ctx.presenter.vulkanDisplayTimingAvailable()) {
      std::fprintf(stderr, "lambda-window-manager: Vulkan display timing available\n");
      ctx.vulkanDisplayTimingSupportLogged = true;
    }
    auto pastPresentationTimings = ctx.presenter.pollVulkanPresentationTimings();
    if (!pastPresentationTimings.empty()) {
      ctx.useVulkanPresentationCompletion = true;
      presentationCompletions.reserve(pastPresentationTimings.size());
      for (auto const& timing : pastPresentationTimings) {
        presentationCompletions.push_back(PresentationCompletion{
            .backendPresentId = timing.presentId,
            .monotonicNsec = timing.actualPresentTime,
        });
      }
    }
    if (ctx.useVulkanPresentationCompletion) {
      presentationTiming.backendPresentId = ctx.presenter.lastVulkanPresentId();
    }
  }
  ctx.frameProfile.presentMs += atomicFrameProfile.presentMs;
  ++ctx.frameProfile.frames;
  ctx.frameProfile.totalMs += atomicFrameProfile.totalMs;
  diagnostics::recordCpuFrame({
      .surfaces = committedSurfaceCount,
      .backgroundMs = atomicFrameProfile.backgroundMs,
      .snapshotMs = atomicFrameProfile.snapshotMs,
      .surfaceMs = atomicFrameProfile.surfaceMs,
      .closingMs = atomicFrameProfile.closingMs,
      .cursorMs = atomicFrameProfile.cursorMs,
      .presentMs = atomicFrameProfile.presentMs,
      .canvasPresentMs = atomicFrameProfile.canvasPresentMs,
      .kmsPresentMs = atomicFrameProfile.kmsPresentMs,
      .totalMs = atomicFrameProfile.totalMs,
  });
  ctx.frameProfile.maybeLog();
  ctx.loopStats.recordRender(renderStart);
  if (!atomicPresenter) {
    ctx.wayland.completePresentationFeedbacks(presentationCompletions, presentation::monotonicMilliseconds());
    ctx.wayland.sendFrameCallbacks(presentation::monotonicMilliseconds(), presentationTiming);
  }
}

} // namespace flux::compositor
