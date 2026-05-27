#include "Compositor/CompositorRenderFrame.hpp"

#include "Compositor/Chrome/CursorRenderer.hpp"
#include "Compositor/Chrome/WindowChromeRenderer.hpp"
#include "Compositor/CompositorPresentation.hpp"
#include "Compositor/Diagnostics/CpuTrace.hpp"
#include "Compositor/Surface/CommittedSurfacePainter.hpp"
#include "Compositor/Surface/SurfaceRenderer.hpp"
#include "Graphics/Linux/FreeTypeTextSystem.hpp"

#include "presentation-time-server-protocol.h"

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_set>
#include <vector>
#include <unistd.h>

#include <drm_fourcc.h>

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

struct OverlaySurfaceSelection {
  std::size_t index = 0;
  platform::KmsAtomicPresenter::OverlayCandidate candidate{};
};

void hashCombine(std::uint64_t& seed, std::uint64_t value) {
  seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u);
}

void hashCombineFloat(std::uint64_t& seed, float value) {
  hashCombine(seed, static_cast<std::uint64_t>(std::llround(static_cast<double>(value) * 1024.0)));
}

void hashCombineString(std::uint64_t& seed, std::string const& value) {
  hashCombine(seed, static_cast<std::uint64_t>(std::hash<std::string>{}(value)));
}

std::uint64_t primaryReuseSignature(std::vector<CommittedSurfaceSnapshot> const& surfaces,
                                    std::uint64_t overlaySurfaceId,
                                    std::int32_t logicalWidth,
                                    std::int32_t logicalHeight,
                                    float pointerX,
                                    float pointerY,
                                    CursorShape cursorShape) {
  std::uint64_t seed = 0xcbf29ce484222325ull;
  hashCombine(seed, static_cast<std::uint64_t>(logicalWidth));
  hashCombine(seed, static_cast<std::uint64_t>(logicalHeight));
  hashCombineFloat(seed, pointerX);
  hashCombineFloat(seed, pointerY);
  hashCombine(seed, static_cast<std::uint64_t>(cursorShape));
  hashCombine(seed, static_cast<std::uint64_t>(surfaces.size()));
  for (auto const& surface : surfaces) {
    bool const overlaySurface = surface.id == overlaySurfaceId;
    hashCombine(seed, surface.id);
    hashCombine(seed, static_cast<std::uint64_t>(surface.x));
    hashCombine(seed, static_cast<std::uint64_t>(surface.y));
    hashCombine(seed, static_cast<std::uint64_t>(surface.width));
    hashCombine(seed, static_cast<std::uint64_t>(surface.height));
    hashCombine(seed, static_cast<std::uint64_t>(surface.committedWidth));
    hashCombine(seed, static_cast<std::uint64_t>(surface.committedHeight));
    hashCombine(seed, static_cast<std::uint64_t>(surface.bufferWidth));
    hashCombine(seed, static_cast<std::uint64_t>(surface.bufferHeight));
    hashCombine(seed, static_cast<std::uint64_t>(surface.bufferTransform));
    hashCombineFloat(seed, surface.sourceX);
    hashCombineFloat(seed, surface.sourceY);
    hashCombineFloat(seed, surface.sourceWidth);
    hashCombineFloat(seed, surface.sourceHeight);
    hashCombine(seed, static_cast<std::uint64_t>(surface.destinationWidth));
    hashCombine(seed, static_cast<std::uint64_t>(surface.destinationHeight));
    hashCombine(seed, static_cast<std::uint64_t>(surface.titleBarHeight));
    hashCombineString(seed, surface.title);
    hashCombine(seed, surface.serverSideDecorated ? 1u : 0u);
    hashCombine(seed, surface.cutoutsBound ? 1u : 0u);
    hashCombine(seed, surface.cutoutsRejected ? 1u : 0u);
    hashCombine(seed, surface.closeButtonHovered ? 1u : 0u);
    hashCombine(seed, surface.closeButtonPressed ? 1u : 0u);
    hashCombine(seed, surface.maximizeButtonHovered ? 1u : 0u);
    hashCombine(seed, surface.maximizeButtonPressed ? 1u : 0u);
    hashCombine(seed, surface.minimizeButtonHovered ? 1u : 0u);
    hashCombine(seed, surface.minimizeButtonPressed ? 1u : 0u);
    hashCombine(seed, surface.focused ? 1u : 0u);
    hashCombine(seed, surface.activeSizing ? 1u : 0u);
    hashCombine(seed, surface.pacingSizing ? 1u : 0u);
    hashCombine(seed, surface.geometryAnimationGrowing ? 1u : 0u);
    hashCombine(seed, static_cast<std::uint64_t>(surface.shadowClipTop));
    hashCombine(seed, static_cast<std::uint64_t>(surface.shadowClipBottom));
    hashCombine(seed, static_cast<std::uint64_t>(surface.windowClipTop));
    hashCombine(seed, static_cast<std::uint64_t>(surface.windowClipBottom));
    hashCombine(seed, static_cast<std::uint64_t>(surface.backgroundBlurRects.size()));
    for (auto const& rect : surface.backgroundBlurRects) {
      hashCombine(seed, static_cast<std::uint64_t>(rect.x));
      hashCombine(seed, static_cast<std::uint64_t>(rect.y));
      hashCombine(seed, static_cast<std::uint64_t>(rect.width));
      hashCombine(seed, static_cast<std::uint64_t>(rect.height));
    }
    if (!overlaySurface) {
      hashCombine(seed, surface.serial);
      hashCombine(seed, surface.dmabufBufferId);
      hashCombine(seed, surface.dmabufFormat);
      hashCombine(seed, static_cast<std::uint64_t>(surface.dmabufPlanes.size()));
      for (auto const& plane : surface.dmabufPlanes) {
        hashCombine(seed, plane.offset);
        hashCombine(seed, plane.stride);
        hashCombine(seed, plane.modifier);
      }
      hashCombine(seed, reinterpret_cast<std::uintptr_t>(surface.shmPixels));
      hashCombine(seed, static_cast<std::uint64_t>(surface.shmPixelBytes));
      hashCombine(seed, static_cast<std::uint64_t>(surface.pixelFormat));
    }
  }
  return seed;
}

bool rectsOverlap(CommittedSurfaceSnapshot const& a, CommittedSurfaceSnapshot const& b) {
  int const ax2 = a.x + std::max(0, a.width);
  int const ay2 = a.y + std::max(0, a.height);
  int const bx2 = b.x + std::max(0, b.width);
  int const by2 = b.y + std::max(0, b.height);
  return a.x < bx2 && ax2 > b.x && a.y < by2 && ay2 > b.y;
}

bool surfaceOpenAnimationComplete(SurfaceRenderState const& state,
                                  CommittedSurfaceSnapshot const& surface,
                                  std::chrono::steady_clock::time_point frameTime,
                                  bool animationsEnabled) {
  if (!animationsEnabled) return true;
  auto visual = state.surfaceVisuals.find(surface.id);
  if (visual == state.surfaceVisuals.end() || visual->second.firstSeen.time_since_epoch().count() == 0) {
    return false;
  }
  return frameTime - visual->second.firstSeen >= kSurfaceOpenAnimationDuration;
}

bool surfaceGeometryEligibleForOverlay(CommittedSurfaceSnapshot const& surface,
                                       WaylandServer const& wayland,
                                       SurfaceRenderState const& state,
                                       std::chrono::steady_clock::time_point frameTime,
                                       bool animationsEnabled) {
  if (surface.id == 0 || surface.dmabufBufferId == 0 || surface.dmabufFormat == 0 ||
      surface.dmabufPlanes.empty() || surface.dmabufPlanes.size() > 4) {
    return false;
  }
  auto reject = [](char const*) {
    return false;
  };
  bool const usesCutoutChrome = surface.serverSideDecorated && surface.cutoutsBound && !surface.cutoutsRejected;
  if (usesCutoutChrome || surface.activeSizing ||
      surface.pacingSizing || surface.geometryAnimationGrowing || surface.windowClipTop > 0 ||
      surface.windowClipBottom > 0 || !surface.backgroundBlurRects.empty()) {
    return reject("composited-frame");
  }
  if (surface.bufferTransform != 0 || surface.bufferWidth <= 0 || surface.bufferHeight <= 0 ||
      surface.width <= 0 || surface.height <= 0) {
    return reject("geometry");
  }
  if (surface.x < 0 || surface.y < 0 ||
      surface.x + surface.width > wayland.logicalOutputWidth() ||
      surface.y + surface.height > wayland.logicalOutputHeight()) {
    return reject("output-bounds");
  }
  double const sourceWidth = surface.sourceWidth > 0.f ? surface.sourceWidth : static_cast<double>(surface.bufferWidth);
  double const sourceHeight = surface.sourceHeight > 0.f ? surface.sourceHeight : static_cast<double>(surface.bufferHeight);
  if (!std::isfinite(sourceWidth) || !std::isfinite(sourceHeight) ||
      surface.sourceX < 0.f || surface.sourceY < 0.f ||
      sourceWidth <= 0.0 || sourceHeight <= 0.0 ||
      surface.sourceX + sourceWidth > static_cast<double>(surface.bufferWidth) + 0.5 ||
      surface.sourceY + sourceHeight > static_cast<double>(surface.bufferHeight) + 0.5) {
    return reject("source");
  }
  if (!surfaceOpenAnimationComplete(state, surface, frameTime, animationsEnabled)) return reject("open-animation");
  return true;
}

std::uint64_t overlayCandidateSignature(CommittedSurfaceSnapshot const& surface,
                                        double outputScaleX,
                                        double outputScaleY) {
  std::uint64_t seed = 0x84222325cbf29ce4ull;
  double const sourceWidth = surface.sourceWidth > 0.f ? surface.sourceWidth : static_cast<double>(surface.bufferWidth);
  double const sourceHeight = surface.sourceHeight > 0.f ? surface.sourceHeight : static_cast<double>(surface.bufferHeight);
  std::int32_t const crtcX = static_cast<std::int32_t>(std::llround(static_cast<double>(surface.x) * outputScaleX));
  std::int32_t const crtcY = static_cast<std::int32_t>(std::llround(static_cast<double>(surface.y) * outputScaleY));
  std::uint32_t const crtcWidth =
      static_cast<std::uint32_t>(std::max(1ll, std::llround(static_cast<double>(surface.width) * outputScaleX)));
  std::uint32_t const crtcHeight =
      static_cast<std::uint32_t>(std::max(1ll, std::llround(static_cast<double>(surface.height) * outputScaleY)));

  hashCombine(seed, surface.dmabufFormat);
  hashCombine(seed, static_cast<std::uint64_t>(surface.bufferWidth));
  hashCombine(seed, static_cast<std::uint64_t>(surface.bufferHeight));
  hashCombineFloat(seed, surface.sourceX);
  hashCombineFloat(seed, surface.sourceY);
  hashCombineFloat(seed, static_cast<float>(sourceWidth));
  hashCombineFloat(seed, static_cast<float>(sourceHeight));
  hashCombine(seed, static_cast<std::uint64_t>(crtcX));
  hashCombine(seed, static_cast<std::uint64_t>(crtcY));
  hashCombine(seed, crtcWidth);
  hashCombine(seed, crtcHeight);
  hashCombine(seed, static_cast<std::uint64_t>(surface.dmabufPlanes.size()));
  for (auto const& plane : surface.dmabufPlanes) {
    hashCombine(seed, plane.offset);
    hashCombine(seed, plane.stride);
    hashCombine(seed, plane.modifier);
  }
  return seed;
}

void rememberRejectedOverlayCandidate(SurfaceRenderState& state,
                                      std::vector<CommittedSurfaceSnapshot> const& surfaces,
                                      std::optional<OverlaySurfaceSelection> const& selection,
                                      double outputScaleX,
                                      double outputScaleY) {
  if (!selection || selection->index >= surfaces.size()) return;
  CommittedSurfaceSnapshot const& surface = surfaces[selection->index];
  state.rejectedOverlaySignaturesBySurface[surface.id] =
      overlayCandidateSignature(surface, outputScaleX, outputScaleY);
}

std::optional<OverlaySurfaceSelection>
selectOverlaySurface(WaylandServer& wayland,
                     std::vector<CommittedSurfaceSnapshot> const& surfaces,
                     SurfaceRenderState const& state,
                     platform::KmsAtomicPresenter const& atomicPresenter,
                     std::chrono::steady_clock::time_point frameTime,
                     bool animationsEnabled,
                     double outputScaleX,
                     double outputScaleY) {
  std::optional<std::size_t> selectedIndex;
  std::int64_t selectedArea = 0;
  for (std::size_t i = 0; i < surfaces.size(); ++i) {
    CommittedSurfaceSnapshot const& surface = surfaces[i];
    if (!surfaceGeometryEligibleForOverlay(surface, wayland, state, frameTime, animationsEnabled)) continue;
    auto const rejected = state.rejectedOverlaySignaturesBySurface.find(surface.id);
    if (rejected != state.rejectedOverlaySignaturesBySurface.end() &&
        rejected->second == overlayCandidateSignature(surface, outputScaleX, outputScaleY)) {
      continue;
    }
    std::uint64_t const modifier = surface.dmabufPlanes.empty() || surface.dmabufPlanes.front().modifier == DRM_FORMAT_MOD_INVALID
                                       ? DRM_FORMAT_MOD_LINEAR
                                       : surface.dmabufPlanes.front().modifier;
    if (!atomicPresenter.canUseOverlayFormatModifier(surface.dmabufFormat, modifier)) continue;
    bool coveredByLaterSurface = false;
    for (std::size_t j = i + 1; j < surfaces.size(); ++j) {
      if (rectsOverlap(surface, surfaces[j])) {
        coveredByLaterSurface = true;
        break;
      }
    }
    if (coveredByLaterSurface) continue;

    std::int64_t const area = static_cast<std::int64_t>(surface.width) * static_cast<std::int64_t>(surface.height);
    if (!selectedIndex || area > selectedArea) {
      selectedIndex = i;
      selectedArea = area;
    }
  }
  if (!selectedIndex) return std::nullopt;

  CommittedSurfaceSnapshot const& surface = surfaces[*selectedIndex];
  std::vector<int> fds = wayland.duplicateDmabufFds(surface.id);
  if (fds.size() != surface.dmabufPlanes.size()) {
    for (int fd : fds) {
      if (fd >= 0) close(fd);
    }
    return std::nullopt;
  }

  platform::KmsAtomicPresenter::OverlayCandidate candidate{
      .surfaceId = surface.id,
      .bufferId = surface.dmabufBufferId,
      .drmFormat = surface.dmabufFormat,
      .bufferWidth = static_cast<std::uint32_t>(surface.bufferWidth),
      .bufferHeight = static_cast<std::uint32_t>(surface.bufferHeight),
      .sourceX = surface.sourceX,
      .sourceY = surface.sourceY,
      .sourceWidth = surface.sourceWidth > 0.f ? surface.sourceWidth : static_cast<double>(surface.bufferWidth),
      .sourceHeight = surface.sourceHeight > 0.f ? surface.sourceHeight : static_cast<double>(surface.bufferHeight),
      .crtcX = static_cast<std::int32_t>(std::llround(static_cast<double>(surface.x) * outputScaleX)),
      .crtcY = static_cast<std::int32_t>(std::llround(static_cast<double>(surface.y) * outputScaleY)),
      .crtcWidth = static_cast<std::uint32_t>(std::max(1ll, std::llround(static_cast<double>(surface.width) * outputScaleX))),
      .crtcHeight = static_cast<std::uint32_t>(std::max(1ll, std::llround(static_cast<double>(surface.height) * outputScaleY))),
      .acquireFenceFd = -1,
      .planes = {},
  };
  candidate.planes.reserve(surface.dmabufPlanes.size());
  for (std::size_t planeIndex = 0; planeIndex < surface.dmabufPlanes.size(); ++planeIndex) {
    candidate.planes.push_back({
        .fd = fds[planeIndex],
        .offset = surface.dmabufPlanes[planeIndex].offset,
        .stride = surface.dmabufPlanes[planeIndex].stride,
        .modifier = surface.dmabufPlanes[planeIndex].modifier,
    });
    fds[planeIndex] = -1;
  }
  return OverlaySurfaceSelection{.index = *selectedIndex, .candidate = std::move(candidate)};
}

platform::KmsAtomicPresenter::OverlayCandidate duplicateOverlayCandidate(
    platform::KmsAtomicPresenter::OverlayCandidate const& candidate) {
  platform::KmsAtomicPresenter::OverlayCandidate copy = candidate;
  copy.acquireFenceFd = candidate.acquireFenceFd >= 0 ? dup(candidate.acquireFenceFd) : -1;
  if (candidate.acquireFenceFd >= 0 && copy.acquireFenceFd < 0) {
    return {};
  }
  copy.planes.clear();
  copy.planes.reserve(candidate.planes.size());
  for (auto const& plane : candidate.planes) {
    int fd = plane.fd >= 0 ? dup(plane.fd) : -1;
    if (fd < 0) {
      for (auto& copiedPlane : copy.planes) {
        if (copiedPlane.fd >= 0) close(copiedPlane.fd);
        copiedPlane.fd = -1;
      }
      if (copy.acquireFenceFd >= 0) close(copy.acquireFenceFd);
      copy.acquireFenceFd = -1;
      copy.planes.clear();
      return {};
    }
    copy.planes.push_back({
        .fd = fd,
        .offset = plane.offset,
        .stride = plane.stride,
        .modifier = plane.modifier,
    });
  }
  return copy;
}

void closeOverlayCandidate(platform::KmsAtomicPresenter::OverlayCandidate& candidate) noexcept {
  if (candidate.acquireFenceFd >= 0) {
    close(candidate.acquireFenceFd);
    candidate.acquireFenceFd = -1;
  }
  for (auto& plane : candidate.planes) {
    if (plane.fd >= 0) {
      close(plane.fd);
      plane.fd = -1;
    }
  }
}

std::shared_ptr<platform::KmsAtomicPresenter::OverlayCandidate> ownOverlayCandidate(
    platform::KmsAtomicPresenter::OverlayCandidate candidate) {
  return std::shared_ptr<platform::KmsAtomicPresenter::OverlayCandidate>(
      new platform::KmsAtomicPresenter::OverlayCandidate(std::move(candidate)),
      [](platform::KmsAtomicPresenter::OverlayCandidate* owned) {
        if (owned) {
          closeOverlayCandidate(*owned);
          delete owned;
        }
      });
}

bool surfaceEligibleForFullscreenDirectScanout(CommittedSurfaceSnapshot const& surface,
                                               platform::KmsAtomicPresenter::OverlayCandidate const& candidate,
                                               WaylandServer const& wayland,
                                               platform::KmsOutput const& output,
                                               CursorRenderState const& cursorState,
                                               bool hardwareCursorEnabled,
                                               bool hardwareCursorAvailable,
                                               float screenshotFlashOpacity) {
  if (std::getenv("FLUX_COMPOSITOR_DISABLE_FULLSCREEN_DIRECT_SCANOUT")) return false;
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
    if (atomicPresenter) atomicPresenter->prepareFrame();
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
    if (atomicPresenter && ctx.atomicReadyFrame && ctx.atomicFrameDirty && ctx.lastKnownContentSerial) {
      double const renderMs =
          ctx.detailedFrameProfile
              ? presentation::LoopInstrumentation::milliseconds(renderStart, presentation::LoopInstrumentation::Clock::now())
              : 0.0;
      *ctx.atomicReadyFrame = AtomicReadyFrame{
          .ready = true,
          .presentToken = presentToken,
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
  auto snapPreview = ctx.wayland.snapPreview();
  bool snapPreviewDrawn = false;
  auto committedSurfaces = ctx.wayland.committedSurfaces();
  committedSurfaceCount = committedSurfaces.size();
  auto screenshotOverlay = ctx.wayland.screenshotSelectionOverlay();
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
      atomicFrameProfile.maxDmabufModifier =
          surface.dmabufPlanes.empty() ? 0 : surface.dmabufPlanes.front().modifier;
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
  std::uint64_t overlaySurfaceId = 0;
  std::optional<OverlaySurfaceSelection> pendingOverlay;
  double outputScaleX = 1.0;
  double outputScaleY = 1.0;
  if (atomicPresenter && !snapPreview && !screenshotOverlay && ctx.surfaceRenderState.closingSurfaces.empty()) {
    outputScaleX = ctx.wayland.logicalOutputWidth() > 0
                       ? static_cast<double>(ctx.output.width()) /
                             static_cast<double>(ctx.wayland.logicalOutputWidth())
                       : 1.0;
    outputScaleY = ctx.wayland.logicalOutputHeight() > 0
                       ? static_cast<double>(ctx.output.height()) /
                             static_cast<double>(ctx.wayland.logicalOutputHeight())
                       : 1.0;
    pendingOverlay = selectOverlaySurface(ctx.wayland,
                                          committedSurfaces,
                                          ctx.surfaceRenderState,
                                          *atomicPresenter,
                                          frameTime,
                                          ctx.appliedConfig.config.animationsEnabled,
                                          outputScaleX,
                                          outputScaleY);
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
  if (atomicPresenter && pendingOverlay && pendingOverlay->index < committedSurfaces.size() &&
      surfaceEligibleForFullscreenDirectScanout(committedSurfaces[pendingOverlay->index],
                                                pendingOverlay->candidate,
                                                ctx.wayland,
                                                ctx.output,
                                                ctx.cursorState,
                                                ctx.appliedConfig.config.hardwareCursorEnabled,
                                                ctx.hardwareCursorAvailable,
                                                ctx.screenshotFlashOpacity)) {
    std::uint64_t const candidateSurfaceId = pendingOverlay->candidate.surfaceId;
    std::uint64_t const candidateBufferId = pendingOverlay->candidate.bufferId;
    bool const cursorRequiresMove = ctx.wayland.cursorSurface().has_value() && ctx.cursorState.hardwareVisible;
    bool const cursorMoved =
        !cursorRequiresMove ||
        moveCurrentHardwareCursor(ctx.wayland,
                                  ctx.output,
                                  ctx.cursorState,
                                  ctx.appliedConfig.config.hardwareCursorEnabled && ctx.hardwareCursorAvailable);
	    platform::KmsAtomicPresenter::OverlayCandidate directCandidate =
	        cursorMoved ? duplicateOverlayCandidate(pendingOverlay->candidate)
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
      if (ctx.atomicReadyFrame && ctx.atomicFrameDirty && ctx.lastKnownContentSerial) {
        double const renderMs =
            ctx.detailedFrameProfile
                ? presentation::LoopInstrumentation::milliseconds(renderStart, presentation::LoopInstrumentation::Clock::now())
                : 0.0;
        *ctx.atomicReadyFrame = AtomicReadyFrame{
            .ready = true,
            .presentToken = 0,
            .timing = presentationTiming,
            .surfaceCount = committedSurfaceCount,
            .frameTime = frameTime,
            .renderMs = renderMs,
	            .renderedAhead = renderAheadFrame,
	            .directScanout = true,
	            .contentSerial = ctx.wayland.contentSerial(),
	            .profile = atomicFrameProfile,
	            .scanoutCandidate = directDeferred ? ownOverlayCandidate(std::move(directCandidate)) : nullptr,
	        };
        *ctx.atomicFrameDirty = false;
        *ctx.lastKnownContentSerial = ctx.atomicReadyFrame->contentSerial;
      }
      presentation::tracePacing("direct-scanout surface=%llu buffer=%llu prepared=1 fullscreen=1\n",
                                static_cast<unsigned long long>(candidateSurfaceId),
                                static_cast<unsigned long long>(candidateBufferId));
      return;
    }
    presentation::tracePacing("direct-scanout surface=%llu buffer=%llu prepared=0 fullscreen=1\n",
                              static_cast<unsigned long long>(candidateSurfaceId),
                              static_cast<unsigned long long>(candidateBufferId));
  }
  bool overlayPreparedForFrame = false;
  if (atomicPresenter && pendingOverlay && atomicPresenter->canPrepareOverlayOnly() &&
      ctx.screenshotFlashOpacity <= 0.001f &&
      ctx.surfaceRenderState.primaryReuseSignatureValid &&
      ctx.surfaceRenderState.primaryReuseOverlaySurfaceId == pendingOverlay->candidate.surfaceId) {
    std::uint64_t const signature = primaryReuseSignature(committedSurfaces,
                                                          pendingOverlay->candidate.surfaceId,
                                                          ctx.wayland.logicalOutputWidth(),
                                                          ctx.wayland.logicalOutputHeight(),
                                                          ctx.wayland.pointerX(),
                                                          ctx.wayland.pointerY(),
                                                          ctx.wayland.cursorShape());
	    if (signature == ctx.surfaceRenderState.primaryReuseSignature) {
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
        if (ctx.atomicReadyFrame && ctx.atomicFrameDirty && ctx.lastKnownContentSerial) {
          double const renderMs =
              ctx.detailedFrameProfile
                  ? presentation::LoopInstrumentation::milliseconds(renderStart, presentation::LoopInstrumentation::Clock::now())
                  : 0.0;
          *ctx.atomicReadyFrame = AtomicReadyFrame{
              .ready = true,
              .presentToken = 0,
              .timing = presentationTiming,
              .surfaceCount = committedSurfaceCount,
              .frameTime = frameTime,
              .renderMs = renderMs,
              .renderedAhead = renderAheadFrame,
	              .overlayOnly = true,
	              .contentSerial = ctx.wayland.contentSerial(),
	              .profile = atomicFrameProfile,
	              .scanoutCandidate = overlayDeferred ? ownOverlayCandidate(std::move(pendingOverlay->candidate)) : nullptr,
	          };
          *ctx.atomicFrameDirty = false;
          *ctx.lastKnownContentSerial = ctx.atomicReadyFrame->contentSerial;
        }
        presentation::tracePacing("hardware-overlay surface=%llu buffer=%llu prepared=1 overlayOnly=1\n",
                                  static_cast<unsigned long long>(candidateSurfaceId),
                                  static_cast<unsigned long long>(candidateBufferId));
        return;
      }
      rememberRejectedOverlayCandidate(ctx.surfaceRenderState,
                                       committedSurfaces,
                                       pendingOverlay,
                                       outputScaleX,
                                       outputScaleY);
      overlaySurfaceId = 0;
      presentation::tracePacing("hardware-overlay surface=%llu buffer=%llu prepared=0 overlayOnly=1\n",
                                static_cast<unsigned long long>(candidateSurfaceId),
                                static_cast<unsigned long long>(candidateBufferId));
    }
  }

  if (atomicPresenter) atomicPresenter->prepareFrame();
  ctx.canvas.beginFrame();
  phaseStart = profileNow();
  drawCompositorBackground(ctx.canvas,
                           ctx.appliedConfig,
                           static_cast<std::uint32_t>(ctx.wayland.logicalOutputWidth()),
                           static_cast<std::uint32_t>(ctx.wayland.logicalOutputHeight()));
  atomicFrameProfile.backgroundMs = profileMs(phaseStart);
  ctx.frameProfile.backgroundMs += atomicFrameProfile.backgroundMs;
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
  atomicFrameProfile.surfaceMs = profileMs(phaseStart);
  ctx.frameProfile.surfaceMs += atomicFrameProfile.surfaceMs;
  phaseStart = profileNow();
  captureClosingSurfaces(ctx.surfaceRenderState,
                         liveSurfaceIds,
                         frameTime,
                         ctx.appliedConfig.config.animationsEnabled);
  drawClosingSurfaces(ctx.canvas, ctx.surfaceRenderState, frameTime);
  if (screenshotOverlay) {
    drawScreenshotSelectionOverlay(ctx.canvas, ctx.wayland, *screenshotOverlay);
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
        presentation::tracePacing("hardware-overlay surface=%llu buffer=%llu prepared=1\n",
                                  static_cast<unsigned long long>(overlaySurfaceId),
                                  static_cast<unsigned long long>(candidateBufferId));
      } else {
        rememberRejectedOverlayCandidate(ctx.surfaceRenderState,
                                         committedSurfaces,
                                         pendingOverlay,
                                         outputScaleX,
                                         outputScaleY);
        overlaySurfaceId = 0;
        presentation::tracePacing("hardware-overlay surface=%llu buffer=%llu prepared=0\n",
                                  static_cast<unsigned long long>(candidateSurfaceId),
                                  static_cast<unsigned long long>(candidateBufferId));
      }
      ctx.wayland.setRetainedDmabufBufferIds(atomicPresenter->overlayBufferIdsInUse());
    }
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
        .presentToken = presentToken,
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
  if (overlayPreparedForFrame && overlaySurfaceId != 0) {
    ctx.surfaceRenderState.primaryReuseSignature =
        primaryReuseSignature(committedSurfaces,
                              overlaySurfaceId,
                              ctx.wayland.logicalOutputWidth(),
                              ctx.wayland.logicalOutputHeight(),
                              ctx.wayland.pointerX(),
                              ctx.wayland.pointerY(),
                              ctx.wayland.cursorShape());
    ctx.surfaceRenderState.primaryReuseOverlaySurfaceId = overlaySurfaceId;
    ctx.surfaceRenderState.primaryReuseSignatureValid = true;
  } else {
    ctx.surfaceRenderState.primaryReuseSignatureValid = false;
    ctx.surfaceRenderState.primaryReuseOverlaySurfaceId = 0;
    ctx.surfaceRenderState.primaryReuseSignature = 0;
  }
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
