#include "Compositor/CompositorRenderFrame.hpp"

#include "Compositor/Chrome/CursorRenderer.hpp"
#include "Compositor/Chrome/WindowChromeRenderer.hpp"
#include "Compositor/CompositorPresentation.hpp"
#include "Compositor/Diagnostics/CpuTrace.hpp"
#include "Compositor/SceneGraph/CompositorSceneGraph.hpp"
#include "Compositor/Surface/CommittedSurfacePainter.hpp"
#include "Compositor/Surface/SurfaceRenderer.hpp"
#include "Graphics/Linux/FreeTypeTextSystem.hpp"

#include "presentation-time-server-protocol.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <optional>
#include <unordered_set>
#include <vector>

namespace lambda::compositor {
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

bool surfaceEligibleForFullscreenDirectScanout(CommittedSurfaceSnapshot const& surface,
                                               platform::KmsAtomicPresenter::OverlayCandidate const& candidate,
                                               WaylandServer const& wayland,
                                               platform::KmsOutput const& output,
                                               CursorRenderState const& cursorState,
                                               bool hardwareCursorEnabled,
                                               bool hardwareCursorAvailable,
                                               float screenshotFlashOpacity) {
  if (std::getenv("LAMBDA_COMPOSITOR_DISABLE_FULLSCREEN_DIRECT_SCANOUT")) return false;
  if (!surface.fullscreen) return false;
  if (screenshotFlashOpacity > 0.001f) return false;
  if (surface.x != 0 || surface.y != 0 ||
      surface.width != wayland.logicalOutputWidth() ||
      surface.height != wayland.logicalOutputHeight()) {
    return false;
  }
  if (candidate.crtcX != 0 || candidate.crtcY != 0 ||
      candidate.crtcWidth != output.width() || candidate.crtcHeight != output.height()) {
    return false;
  }
  if (!wayland.cursorSurface()) return true;
  return hardwareCursorEnabled && hardwareCursorAvailable && cursorState.hardwareVisible;
}

bool overlayPrimaryReuseCursorReady(WaylandServer& wayland,
                                    platform::KmsOutput const& output,
                                    CursorRenderState const& cursorState,
                                    bool hardwareCursorEnabled) {
  if (!hardwareCursorEnabled || !cursorState.hardwareVisible) return true;
  return moveCurrentHardwareCursor(wayland, output, cursorState, true);
}

double renderMilliseconds(CompositorRenderFrameContext const& ctx,
                          std::chrono::steady_clock::time_point renderStart) {
  return ctx.detailedFrameProfile
             ? presentation::LoopInstrumentation::milliseconds(renderStart, presentation::LoopInstrumentation::Clock::now())
             : 0.0;
}

using RegionRect = CommittedSurfaceSnapshot::RegionRect;

Rect logicalRect(RegionRect const& rect) {
  return Rect::sharp(static_cast<float>(rect.x),
                     static_cast<float>(rect.y),
                     static_cast<float>(rect.width),
                     static_cast<float>(rect.height));
}

std::uint64_t damageArea(SceneDamageResult const& damage) {
  std::uint64_t area = 0;
  for (RegionRect const& rect : damage.rects) {
    if (rect.width <= 0 || rect.height <= 0) continue;
    area += static_cast<std::uint64_t>(rect.width) * static_cast<std::uint64_t>(rect.height);
  }
  return area;
}

std::vector<platform::KmsAtomicPresenter::DamageRect>
physicalDamageRects(SceneDamageResult const& damage,
                    platform::KmsOutput const& output,
                    std::int32_t logicalWidth,
                    std::int32_t logicalHeight) {
  std::vector<platform::KmsAtomicPresenter::DamageRect> rects;
  std::int32_t const outputWidth = static_cast<std::int32_t>(output.width());
  std::int32_t const outputHeight = static_cast<std::int32_t>(output.height());
  if (damage.fullOutput || damage.empty() || logicalWidth <= 0 || logicalHeight <= 0 ||
      outputWidth <= 0 || outputHeight <= 0) {
    return rects;
  }
  double const scaleX = static_cast<double>(outputWidth) / static_cast<double>(logicalWidth);
  double const scaleY = static_cast<double>(outputHeight) / static_cast<double>(logicalHeight);
  rects.reserve(damage.rects.size());
  for (RegionRect const& rect : damage.rects) {
    if (rect.width <= 0 || rect.height <= 0) continue;
    std::int32_t const x1 = std::clamp(static_cast<std::int32_t>(std::floor(rect.x * scaleX)), 0, outputWidth);
    std::int32_t const y1 = std::clamp(static_cast<std::int32_t>(std::floor(rect.y * scaleY)), 0, outputHeight);
    std::int32_t const x2 =
        std::clamp(static_cast<std::int32_t>(std::ceil((rect.x + rect.width) * scaleX)), 0, outputWidth);
    std::int32_t const y2 =
        std::clamp(static_cast<std::int32_t>(std::ceil((rect.y + rect.height) * scaleY)), 0, outputHeight);
    if (x2 <= x1 || y2 <= y1) continue;
    rects.push_back(platform::KmsAtomicPresenter::DamageRect{
        .x = x1,
        .y = y1,
        .width = static_cast<std::uint32_t>(x2 - x1),
        .height = static_cast<std::uint32_t>(y2 - y1),
    });
  }
  return rects;
}

struct AtomicReadyFrameOptions {
  bool overlayOnly = false;
  bool directScanout = false;
  std::shared_ptr<platform::KmsAtomicPresenter::OverlayCandidate> scanoutCandidate;
};

void storeAtomicReadyFrame(
    CompositorRenderFrameContext& ctx,
    std::uint32_t presentToken,
    PresentationTiming presentationTiming,
    std::size_t surfaceCount,
    std::chrono::steady_clock::time_point frameTime,
    std::chrono::steady_clock::time_point renderStart,
    bool renderAheadFrame,
    presentation::AtomicFrameProfile const& profile,
    CompositorSceneGraphState&& sceneGraphState,
    std::vector<std::uint64_t> const& frameCallbackSurfaceIds,
    AtomicReadyFrameOptions options = {}) {
  if (!ctx.atomicReadyFrame || !ctx.atomicFrameDirty || !ctx.lastKnownContentSerial) return;
  *ctx.atomicReadyFrame = AtomicReadyFrame{
      .ready = true,
      .presentToken = presentToken,
      .timing = presentationTiming,
      .surfaceCount = surfaceCount,
      .frameTime = frameTime,
      .renderMs = renderMilliseconds(ctx, renderStart),
      .renderedAhead = renderAheadFrame,
      .overlayOnly = options.overlayOnly,
      .directScanout = options.directScanout,
      .contentSerial = ctx.wayland.contentSerial(),
      .profile = profile,
      .sceneGraphState = std::move(sceneGraphState),
      .sceneGraphStateValid = true,
      .frameCallbackSurfaceIds = frameCallbackSurfaceIds,
      .scanoutCandidate = std::move(options.scanoutCandidate),
  };
  *ctx.atomicFrameDirty = false;
  *ctx.lastKnownContentSerial = ctx.atomicReadyFrame->contentSerial;
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
  std::size_t committedSurfaceCount = 0;
  if (ctx.idleBlanked) {
    std::vector<CommittedSurfaceSnapshot> emptySurfaces;
    std::optional<CommittedSurfaceSnapshot> emptyCursor;
    CompositorSceneFramePlan scenePlan =
        buildCompositorSceneFrame(ctx.surfaceRenderState.sceneGraph,
                                  CompositorSceneFrameInput{
                                      .wayland = ctx.wayland,
                                      .output = ctx.output,
                                      .atomicPresenter = atomicPresenter,
                                      .chrome = ctx.appliedConfig.config.chrome,
                                      .surfaceVisuals = ctx.surfaceRenderState.surfaceVisuals,
                                      .surfaces = emptySurfaces,
                                      .softwareCursor = emptyCursor,
                                      .frameTime = frameTime,
                                      .logicalOutputWidth = ctx.wayland.logicalOutputWidth(),
                                      .logicalOutputHeight = ctx.wayland.logicalOutputHeight(),
                                      .dpiScale = ctx.canvas.dpiScale(),
                                      .animationsEnabled = ctx.appliedConfig.config.animationsEnabled,
                                      .forceFullDamage = true,
                                      .selectScanout = false,
                                  });
    if (atomicPresenter) (void)atomicPresenter->prepareFrame();
    ctx.canvas.beginFrame();
    ctx.canvas.clear(Color{0.f, 0.f, 0.f, 1.f});
    ctx.output.hideCursor();
    atomicFrameProfile.backgroundMs = profileMs(phaseStart);
    ctx.frameProfile.backgroundMs += atomicFrameProfile.backgroundMs;
    phaseStart = profileNow();
    auto const canvasPresentStart = profileNow();
    ctx.canvas.present();
    atomicFrameProfile.canvasPresentMs = profileMs(canvasPresentStart);
    std::uint32_t presentToken = 0;
    if (atomicPresenter) {
      auto const kmsPresentStart = profileNow();
      presentToken = atomicPresenter->markFrameRendered();
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
    if (atomicPresenter) {
      storeAtomicReadyFrame(ctx,
                            presentToken,
                            presentationTiming,
                            committedSurfaceCount,
                            frameTime,
                            renderStart,
                            renderAheadFrame,
                            atomicFrameProfile,
                            std::move(scenePlan.nextState),
                            {});
    }
    if (!atomicPresenter) {
      ctx.surfaceRenderState.sceneGraph = std::move(scenePlan.nextState);
    }
    return;
  }
  auto snapPreview = ctx.wayland.snapPreview();
  bool snapPreviewDrawn = false;
  auto committedSurfaces = ctx.wayland.committedSurfaces();
  committedSurfaceCount = committedSurfaces.size();
  auto screenshotOverlay = ctx.wayland.screenshotSelectionOverlay();
  std::optional<CommittedSurfaceSnapshot> softwareCursorSnapshot;
  if (!ctx.appliedConfig.config.hardwareCursorEnabled || !ctx.hardwareCursorAvailable) {
    softwareCursorSnapshot = ctx.wayland.cursorSurface();
  }
  bool const forceFullSceneDamage =
      snapPreview.has_value() ||
      screenshotOverlay.has_value() ||
      ctx.screenshotFlashOpacity > 0.001f ||
      !ctx.surfaceRenderState.closingSurfaces.empty();
  CompositorSceneFramePlan scenePlan =
      buildCompositorSceneFrame(ctx.surfaceRenderState.sceneGraph,
                                CompositorSceneFrameInput{
                                    .wayland = ctx.wayland,
                                    .output = ctx.output,
                                    .atomicPresenter = atomicPresenter,
                                    .chrome = ctx.appliedConfig.config.chrome,
                                    .surfaceVisuals = ctx.surfaceRenderState.surfaceVisuals,
                                    .surfaces = committedSurfaces,
                                    .softwareCursor = softwareCursorSnapshot,
                                    .frameTime = frameTime,
                                    .logicalOutputWidth = ctx.wayland.logicalOutputWidth(),
                                    .logicalOutputHeight = ctx.wayland.logicalOutputHeight(),
                                    .dpiScale = ctx.canvas.dpiScale(),
                                    .animationsEnabled = ctx.appliedConfig.config.animationsEnabled,
                                    .forceFullDamage = forceFullSceneDamage,
                                    .selectScanout = atomicPresenter && !snapPreview && !screenshotOverlay &&
                                                     ctx.surfaceRenderState.closingSurfaces.empty(),
                                });
  std::vector<std::uint64_t> const& frameCallbackSurfaceIds = scenePlan.frameCallbackSurfaceIds;
  SceneDamageResult const& sceneDamage = scenePlan.damage;
  LAMBDA_WINDOW_MANAGER_TRACE_PACING("scene-damage full=%d rects=%zu area=%llu empty=%d surfaces=%zu\n",
                            sceneDamage.fullOutput ? 1 : 0,
                            sceneDamage.rectCount(),
                            static_cast<unsigned long long>(damageArea(sceneDamage)),
                            sceneDamage.empty() ? 1 : 0,
                            committedSurfaces.size());
  bool const collectAgeProfile = ctx.detailedFrameProfile;
  std::uint64_t const renderSnapshotNsec = collectAgeProfile ? presentation::monotonicNanoseconds() : 0;
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
      atomicFrameProfile.maxDmabufModifier =
          surface.dmabufPlanes.empty() ? 0 : surface.dmabufPlanes.front().modifier;
    }
    if (surface.width * surface.height > atomicFrameProfile.maxFrameWidth * atomicFrameProfile.maxFrameHeight) {
      atomicFrameProfile.maxFrameWidth = surface.width;
      atomicFrameProfile.maxFrameHeight = surface.height;
    }
    if (surface.pacingSizing && collectAgeProfile) {
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
      LAMBDA_WINDOW_MANAGER_TRACE_PACING("surface-age surface=%llu frame=%dx%d buffer=%dx%d serial=%llu "
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
  ctx.loopStats.lastSurfaceCount = committedSurfaces.size();
  atomicFrameProfile.snapshotMs = profileMs(phaseStart);
  ctx.frameProfile.snapshotMs += atomicFrameProfile.snapshotMs;
  ctx.frameProfile.surfaces += committedSurfaces.size();
  std::unordered_set<std::uint64_t> liveSurfaceIds;
  liveSurfaceIds.reserve(committedSurfaces.size());
  std::uint64_t overlaySurfaceId = 0;
  std::optional<CompositorHardwareScanoutSelection> pendingOverlay = std::move(scenePlan.scanout);
  if (atomicPresenter && !snapPreview && !screenshotOverlay && ctx.surfaceRenderState.closingSurfaces.empty()) {
    overlaySurfaceId = pendingOverlay ? pendingOverlay->candidate.surfaceId : 0;
    if (!pendingOverlay) {
      atomicPresenter->clearPreparedOverlayCandidate();
      atomicPresenter->clearPreparedDirectScanout();
      ctx.wayland.setRetainedDmabufBufferIds(atomicPresenter->overlayBufferIdsInUse());
    }
  } else if (atomicPresenter) {
    atomicPresenter->clearPreparedOverlayCandidate();
    atomicPresenter->clearPreparedDirectScanout();
    ctx.wayland.setRetainedDmabufBufferIds(atomicPresenter->overlayBufferIdsInUse());
  }
  if (atomicPresenter && pendingOverlay && pendingOverlay->surfaceIndex < committedSurfaces.size() &&
      compositorSceneScanoutCoversOutput(*pendingOverlay, ctx.output) &&
      surfaceEligibleForFullscreenDirectScanout(committedSurfaces[pendingOverlay->surfaceIndex],
                                                pendingOverlay->candidate,
                                                ctx.wayland,
                                                ctx.output,
                                                ctx.cursorState,
                                                ctx.appliedConfig.config.hardwareCursorEnabled,
                                                ctx.hardwareCursorAvailable,
                                                ctx.screenshotFlashOpacity)) {
    std::uint64_t const candidateSurfaceId = pendingOverlay->candidate.surfaceId;
    std::uint64_t const candidateBufferId = pendingOverlay->candidate.bufferId;
    bool const cursorMoved =
        moveCurrentHardwareCursor(ctx.wayland,
                                  ctx.output,
                                  ctx.cursorState,
                                  ctx.appliedConfig.config.hardwareCursorEnabled && ctx.hardwareCursorAvailable);
    platform::KmsAtomicPresenter::OverlayCandidate directCandidate =
        cursorMoved ? duplicateCompositorSceneOverlayCandidate(pendingOverlay->candidate)
                    : platform::KmsAtomicPresenter::OverlayCandidate{};
    auto const directPresentStart = profileNow();
    bool directPrepared = false;
    bool directDeferred = false;
    if (cursorMoved && !directCandidate.planes.empty()) {
      if (renderAheadFrame) {
        directDeferred = true;
        (void)atomicPresenter->primeDirectScanoutCandidate(directCandidate);
      } else {
        directPrepared = atomicPresenter->prepareDirectScanoutCandidate(std::move(directCandidate));
      }
    }
    if (directPrepared || directDeferred) {
      closeCompositorSceneOverlayCandidate(pendingOverlay->candidate);
      ctx.surfaceRenderState.clientImages.erase(candidateSurfaceId);
      atomicFrameProfile.presentMs = profileMs(directPresentStart);
      atomicFrameProfile.totalMs = profileMs(frameProfileStart);
      diagnostics::recordCpuFrame({
          .surfaces = committedSurfaceCount,
          .snapshotMs = atomicFrameProfile.snapshotMs,
          .presentMs = atomicFrameProfile.presentMs,
          .totalMs = atomicFrameProfile.totalMs,
      });
      ctx.loopStats.recordRender(renderStart);
      ctx.wayland.setRetainedDmabufBufferIds(atomicPresenter->overlayBufferIdsInUse());
      clearCompositorScenePrimaryReuse(scenePlan.nextState);
      storeAtomicReadyFrame(ctx,
                            0,
                            presentationTiming,
                            committedSurfaceCount,
                            frameTime,
                            renderStart,
                            renderAheadFrame,
                            atomicFrameProfile,
                            std::move(scenePlan.nextState),
                            frameCallbackSurfaceIds,
                            AtomicReadyFrameOptions{
                                .directScanout = true,
                                .scanoutCandidate = directDeferred
                                                        ? ownCompositorSceneOverlayCandidate(std::move(directCandidate))
                                                        : nullptr,
                            });
      LAMBDA_WINDOW_MANAGER_TRACE_PACING("direct-scanout surface=%llu buffer=%llu prepared=1 fullscreen=1\n",
                                static_cast<unsigned long long>(candidateSurfaceId),
                                static_cast<unsigned long long>(candidateBufferId));
      return;
    }
    LAMBDA_WINDOW_MANAGER_TRACE_PACING("direct-scanout surface=%llu buffer=%llu prepared=0 fullscreen=1\n",
                              static_cast<unsigned long long>(candidateSurfaceId),
                              static_cast<unsigned long long>(candidateBufferId));
  }
  bool overlayPreparedForFrame = false;
  if (atomicPresenter && pendingOverlay && atomicPresenter->canPrepareOverlayOnly() &&
      ctx.screenshotFlashOpacity <= 0.001f &&
      scenePlan.primaryReuseMatchesScanout) {
    bool const cursorReady =
        overlayPrimaryReuseCursorReady(ctx.wayland,
                                       ctx.output,
                                       ctx.cursorState,
                                       ctx.appliedConfig.config.hardwareCursorEnabled && ctx.hardwareCursorAvailable);
    if (cursorReady) {
      std::uint64_t const candidateSurfaceId = pendingOverlay->candidate.surfaceId;
      std::uint64_t const candidateBufferId = pendingOverlay->candidate.bufferId;
      auto const overlayPresentStart = profileNow();
      bool overlayPrepared = false;
      bool overlayDeferred = false;
      if (renderAheadFrame) {
        overlayDeferred = true;
      } else {
        overlayPrepared =
            atomicPresenter->prepareOverlayCandidateForDisplayedFrame(std::move(pendingOverlay->candidate));
      }
      if (overlayPrepared || overlayDeferred) {
        atomicFrameProfile.presentMs = profileMs(overlayPresentStart);
        atomicFrameProfile.totalMs = profileMs(frameProfileStart);
        diagnostics::recordCpuFrame({
            .surfaces = committedSurfaceCount,
            .snapshotMs = atomicFrameProfile.snapshotMs,
            .presentMs = atomicFrameProfile.presentMs,
            .totalMs = atomicFrameProfile.totalMs,
        });
        ctx.loopStats.recordRender(renderStart);
        ctx.wayland.setRetainedDmabufBufferIds(atomicPresenter->overlayBufferIdsInUse());
        storeAtomicReadyFrame(ctx,
                              0,
                              presentationTiming,
                              committedSurfaceCount,
                              frameTime,
                              renderStart,
                              renderAheadFrame,
                              atomicFrameProfile,
                              std::move(scenePlan.nextState),
                              frameCallbackSurfaceIds,
                              AtomicReadyFrameOptions{
                                  .overlayOnly = true,
                                  .scanoutCandidate = overlayDeferred
                                                          ? ownCompositorSceneOverlayCandidate(std::move(pendingOverlay->candidate))
                                                          : nullptr,
                              });
        LAMBDA_WINDOW_MANAGER_TRACE_PACING("hardware-overlay surface=%llu buffer=%llu prepared=1 overlayOnly=1\n",
                                  static_cast<unsigned long long>(candidateSurfaceId),
                                  static_cast<unsigned long long>(candidateBufferId));
        return;
      }
      rejectCompositorSceneScanout(ctx.surfaceRenderState.sceneGraph, *pendingOverlay);
      rejectCompositorSceneScanout(scenePlan.nextState, *pendingOverlay);
      overlaySurfaceId = 0;
      LAMBDA_WINDOW_MANAGER_TRACE_PACING("hardware-overlay surface=%llu buffer=%llu prepared=0 overlayOnly=1\n",
                                static_cast<unsigned long long>(candidateSurfaceId),
                                static_cast<unsigned long long>(candidateBufferId));
    }
  }

  bool const partialDamageCandidate =
      atomicPresenter &&
      !renderAheadFrame &&
      !pendingOverlay &&
      !sceneDamage.fullOutput &&
      !sceneDamage.empty();
  std::vector<platform::KmsAtomicPresenter::DamageRect> physicalDamage =
      partialDamageCandidate
          ? physicalDamageRects(sceneDamage,
                                ctx.output,
                                ctx.wayland.logicalOutputWidth(),
                                ctx.wayland.logicalOutputHeight())
          : std::vector<platform::KmsAtomicPresenter::DamageRect>{};
  bool partialDamageFrame = false;
  if (atomicPresenter) {
    if (!physicalDamage.empty()) {
      partialDamageFrame = atomicPresenter->prepareFrame(physicalDamage);
    } else {
      (void)atomicPresenter->prepareFrame();
    }
    if (!partialDamageFrame) {
      physicalDamage.clear();
    }
  }
  LAMBDA_WINDOW_MANAGER_TRACE_PACING("scene-damage-render partial=%d logicalRects=%zu kmsRects=%zu\n",
                            partialDamageFrame ? 1 : 0,
                            partialDamageFrame ? sceneDamage.rects.size() : 0u,
                            physicalDamage.size());
  ctx.canvas.beginFrame();
  auto drawFrameContent = [&] {
    phaseStart = profileNow();
    drawCompositorBackground(ctx.canvas,
                             ctx.appliedConfig,
                             static_cast<std::uint32_t>(ctx.wayland.logicalOutputWidth()),
                             static_cast<std::uint32_t>(ctx.wayland.logicalOutputHeight()));
    double const backgroundMs = profileMs(phaseStart);
    atomicFrameProfile.backgroundMs += backgroundMs;
    ctx.frameProfile.backgroundMs += backgroundMs;
    phaseStart = profileNow();
    for (auto const& clientSurface : committedSurfaces) {
      if (snapPreview && !snapPreviewDrawn && snapPreview->surfaceId == clientSurface.id) {
        drawSnapPreview(ctx.canvas, *snapPreview, ctx.appliedConfig.config.chrome);
        snapPreviewDrawn = true;
      }
      liveSurfaceIds.insert(clientSurface.id);
      if (clientSurface.id == overlaySurfaceId) {
        ctx.surfaceRenderState.clientImages.erase(clientSurface.id);
        drawWindowFrameShadow(ctx.canvas, clientSurface, ctx.appliedConfig.config.chrome);
        drawWindowChrome(ctx.canvas, ctx.textSystem, clientSurface, ctx.appliedConfig.config.chrome);
        drawWindowFrameBorder(ctx.canvas, clientSurface, ctx.appliedConfig.config.chrome);
        continue;
      }
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
    double const surfaceMs = profileMs(phaseStart);
    atomicFrameProfile.surfaceMs += surfaceMs;
    ctx.frameProfile.surfaceMs += surfaceMs;
    phaseStart = profileNow();
    captureClosingSurfaces(ctx.surfaceRenderState,
                           liveSurfaceIds,
                           frameTime,
                           ctx.appliedConfig.config.animationsEnabled);
    drawClosingSurfaces(ctx.canvas, ctx.surfaceRenderState, frameTime);
    if (screenshotOverlay) {
      drawScreenshotSelectionOverlay(ctx.canvas, ctx.wayland, *screenshotOverlay);
    }
    double const closingMs = profileMs(phaseStart);
    atomicFrameProfile.closingMs += closingMs;
    ctx.frameProfile.closingMs += closingMs;
    phaseStart = profileNow();
    drawCompositorCursor(ctx.wayland,
                         ctx.canvas,
                         ctx.output,
                         ctx.cursorState,
                         ctx.appliedConfig.config.cursorTheme,
                         ctx.appliedConfig.config.cursorSize,
                         ctx.appliedConfig.config.hardwareCursorEnabled && ctx.hardwareCursorAvailable);
    drawScreenshotFlash(ctx.canvas, ctx.wayland, ctx.screenshotFlashOpacity);
    double const cursorMs = profileMs(phaseStart);
    atomicFrameProfile.cursorMs += cursorMs;
    ctx.frameProfile.cursorMs += cursorMs;
  };
  if (partialDamageFrame) {
    for (RegionRect const& rect : sceneDamage.rects) {
      ctx.canvas.save();
      ctx.canvas.clipRect(logicalRect(rect));
      drawFrameContent();
      ctx.canvas.restore();
    }
  } else {
    drawFrameContent();
  }
  pruneSurfaceRenderState(ctx.surfaceRenderState, liveSurfaceIds);
  phaseStart = profileNow();
  std::vector<PresentationCompletion> presentationCompletions;
  auto const canvasPresentStart = profileNow();
  ctx.canvas.present();
  atomicFrameProfile.canvasPresentMs = profileMs(canvasPresentStart);
  std::uint32_t presentToken = 0;
  if (atomicPresenter) {
    auto const kmsPresentStart = profileNow();
    presentToken = atomicPresenter->markFrameRendered();
    if (pendingOverlay) {
      std::uint64_t const candidateSurfaceId = pendingOverlay->candidate.surfaceId;
      std::uint64_t const candidateBufferId = pendingOverlay->candidate.bufferId;
      if (atomicPresenter->prepareOverlayCandidate(presentToken, std::move(pendingOverlay->candidate))) {
        overlaySurfaceId = atomicPresenter->preparedOverlaySurfaceId();
        overlayPreparedForFrame = true;
        LAMBDA_WINDOW_MANAGER_TRACE_PACING("hardware-overlay surface=%llu buffer=%llu prepared=1\n",
                                  static_cast<unsigned long long>(overlaySurfaceId),
                                  static_cast<unsigned long long>(candidateBufferId));
      } else {
        rejectCompositorSceneScanout(ctx.surfaceRenderState.sceneGraph, *pendingOverlay);
        rejectCompositorSceneScanout(scenePlan.nextState, *pendingOverlay);
        overlaySurfaceId = 0;
        LAMBDA_WINDOW_MANAGER_TRACE_PACING("hardware-overlay surface=%llu buffer=%llu prepared=0\n",
                                  static_cast<unsigned long long>(candidateSurfaceId),
                                  static_cast<unsigned long long>(candidateBufferId));
      }
      ctx.wayland.setRetainedDmabufBufferIds(atomicPresenter->overlayBufferIdsInUse());
    }
    atomicFrameProfile.kmsPresentMs = profileMs(kmsPresentStart);
  }
  if (overlayPreparedForFrame && overlaySurfaceId != 0) {
    rememberCompositorScenePrimaryReuse(scenePlan.nextState,
                                        overlaySurfaceId,
                                        scenePlan.primaryReuseSignatureForScanout);
  } else {
    clearCompositorScenePrimaryReuse(scenePlan.nextState);
  }
  atomicFrameProfile.presentMs = profileMs(phaseStart);
  atomicFrameProfile.totalMs = profileMs(frameProfileStart);
  if (atomicPresenter) {
    storeAtomicReadyFrame(ctx,
                          presentToken,
                          presentationTiming,
                          committedSurfaceCount,
                          frameTime,
                          renderStart,
                          renderAheadFrame,
                          atomicFrameProfile,
                          std::move(scenePlan.nextState),
                          frameCallbackSurfaceIds);
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
    ctx.surfaceRenderState.sceneGraph = std::move(scenePlan.nextState);
    ctx.wayland.completePresentationFeedbacks(presentationCompletions, presentation::monotonicMilliseconds());
    ctx.wayland.sendFrameCallbacks(presentation::monotonicMilliseconds(),
                                   presentationTiming,
                                   frameCallbackSurfaceIds);
  }
}

} // namespace lambda::compositor
