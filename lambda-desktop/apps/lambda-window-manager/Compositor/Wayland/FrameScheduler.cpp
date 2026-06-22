#include "Compositor/Wayland/WaylandServerImpl.hpp"

#include "Compositor/CompositorPresentation.hpp"
#include "Compositor/Wayland/BufferRelease.hpp"
#include "Compositor/Wayland/IdleInhibitState.hpp"
#include "Compositor/Wayland/LayerShellState.hpp"
#include "Compositor/Window/WindowGeometry.hpp"
#include "Detail/ResizeTrace.hpp"
#include "presentation-time-server-protocol.h"

#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <cstdint>
#include <vector>

namespace lambda::compositor {
namespace {

constexpr std::int32_t kMinWindowWidth = kCompositorMinWindowWidth;
constexpr std::int32_t kMinWindowHeight = kCompositorMinWindowHeight;
constexpr std::uint32_t kGeometryAnimationMs = 180;
constexpr std::uint32_t kShellPanelAnimationMs = 180;

std::uint64_t monotonicNanoseconds() noexcept {
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<std::uint64_t>(now.tv_sec) * 1'000'000'000ull +
         static_cast<std::uint64_t>(now.tv_nsec);
}

float clamp01(float value) {
  return std::clamp(value, 0.f, 1.f);
}

float easeInOutCubic(float value) {
  float const t = clamp01(value);
  if (t < 0.5f) return 4.f * t * t * t;
  float const inverse = -2.f * t + 2.f;
  return 1.f - inverse * inverse * inverse * 0.5f;
}

std::int32_t lerpInt(std::int32_t from, std::int32_t to, float t) {
  return static_cast<std::int32_t>(std::lround(static_cast<float>(from) +
                                               static_cast<float>(to - from) * t));
}

bool hasVisibleFullscreenToplevel(WaylandServer::Impl const& server) {
  return std::any_of(server.surfaces_.begin(), server.surfaces_.end(), [](auto const& surface) {
    return surface && surfaceIsXdgToplevel(surface.get()) && surface->fullscreen && !surface->minimized;
  });
}

bool hiddenFullscreenShellPanel(WaylandServer::Impl const& server, WaylandServer::Impl::Surface const* surface) {
  return surfaceIsLayerSurface(surface) && surface->layerSurface &&
         layerShellFrameCallbacksHiddenForFullscreen(surface->layerSurface->nameSpace,
                                                     server.shellPanelHideProgress_);
}

void updateShellPanelAnimation(WaylandServer::Impl& server, std::uint32_t timeMs, bool animationsEnabled) {
  float const target = hasVisibleFullscreenToplevel(server) ? 1.f : 0.f;
  if (std::fabs(target - server.shellPanelHideTargetProgress_) > 0.001f) {
    server.shellPanelHideStartProgress_ = server.shellPanelHideProgress_;
    server.shellPanelHideTargetProgress_ = target;
    server.shellPanelHideAnimationStartedAtMs_ = timeMs;
    server.shellPanelHideAnimationActive_ =
        animationsEnabled && std::fabs(target - server.shellPanelHideProgress_) > 0.001f;
    if (!server.shellPanelHideAnimationActive_) server.shellPanelHideProgress_ = target;
    ++server.contentSerial_;
  }

  if (!server.shellPanelHideAnimationActive_) return;
  float const linearProgress =
      animationsEnabled
          ? static_cast<float>(timeMs - server.shellPanelHideAnimationStartedAtMs_) /
                static_cast<float>(kShellPanelAnimationMs)
          : 1.f;
  float const progress = easeInOutCubic(linearProgress);
  float const oldProgress = server.shellPanelHideProgress_;
  server.shellPanelHideProgress_ =
      server.shellPanelHideStartProgress_ +
      (server.shellPanelHideTargetProgress_ - server.shellPanelHideStartProgress_) * progress;
  if (linearProgress >= 1.f) {
    server.shellPanelHideProgress_ = server.shellPanelHideTargetProgress_;
    server.shellPanelHideAnimationActive_ = false;
  }
  if (std::fabs(server.shellPanelHideProgress_ - oldProgress) > 0.001f) ++server.contentSerial_;
}

wl_resource* outputResourceForClient(WaylandServer::Impl const& server, wl_client* client) {
  if (!client) return nullptr;
  auto const found =
      std::find_if(server.outputResources_.begin(), server.outputResources_.end(), [client](wl_resource* resource) {
        return resource && wl_resource_get_client(resource) == client;
      });
  return found == server.outputResources_.end() ? nullptr : *found;
}

void normalizePresentationTiming(WaylandOutputInfo const& output, PresentationTiming& timing) {
  if (timing.monotonicNsec == 0) {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    timing.monotonicNsec = static_cast<std::uint64_t>(now.tv_sec) * 1'000'000'000ull +
                           static_cast<std::uint64_t>(now.tv_nsec);
  }
  if (timing.refreshNsec == 0 && output.refreshMilliHz > 0) {
    timing.refreshNsec =
        static_cast<std::uint32_t>(1'000'000'000'000ull / static_cast<std::uint64_t>(output.refreshMilliHz));
  }
}

void sendPresentationFeedback(WaylandServer::Impl& server,
                              WaylandServer::Impl::PresentationFeedback* feedback,
                              PresentationTiming timing) {
  if (!feedback || !feedback->resource) return;
  normalizePresentationTiming(server.output_, timing);
  auto const fields = presentation::presentedFeedbackFields(timing);
  if (wl_resource* output = outputResourceForClient(server, wl_resource_get_client(feedback->resource))) {
    wp_presentation_feedback_send_sync_output(feedback->resource, output);
  }
  wp_presentation_feedback_send_presented(feedback->resource,
                                          fields.tvSecHi,
                                          fields.tvSecLo,
                                          fields.tvNsec,
                                          fields.refresh,
                                          fields.seqHi,
                                          fields.seqLo,
                                          fields.flags);
  wl_resource_destroy(feedback->resource);
}

void discardPresentationFeedback(WaylandServer::Impl::PresentationFeedback* feedback) {
  if (!feedback || !feedback->resource) return;
  wp_presentation_feedback_send_discarded(feedback->resource);
  wl_resource_destroy(feedback->resource);
}

} // namespace

void WaylandServer::Impl::updateAnimations(std::uint32_t timeMs, bool animationsEnabled) {
  updateShellPanelAnimation(*this, timeMs, animationsEnabled);

  bool sentConfigure = false;
  for (auto const& surface : surfaces_) {
    if (!surface->geometryAnimationActive) continue;
    float const linearProgress =
        animationsEnabled
            ? static_cast<float>(timeMs - surface->geometryAnimationStartedAtMs) /
                  static_cast<float>(kGeometryAnimationMs)
            : 1.f;
    float const progress = easeInOutCubic(linearProgress);
    bool const finished = linearProgress >= 1.f;
    std::int32_t const nextX = finished
                                   ? surface->geometryAnimationTargetX
                                   : lerpInt(surface->geometryAnimationStartX, surface->geometryAnimationTargetX, progress);
    std::int32_t const nextY = finished
                                   ? surface->geometryAnimationTargetY
                                   : lerpInt(surface->geometryAnimationStartY, surface->geometryAnimationTargetY, progress);
    std::int32_t const nextWidth =
        finished
            ? surface->geometryAnimationTargetWidth
            : std::max(kMinWindowWidth,
                       lerpInt(surface->geometryAnimationStartWidth, surface->geometryAnimationTargetWidth, progress));
    std::int32_t const nextHeight =
        finished
            ? surface->geometryAnimationTargetHeight
            : std::max(kMinWindowHeight,
                       lerpInt(surface->geometryAnimationStartHeight, surface->geometryAnimationTargetHeight, progress));

    bool const configureSent =
        requestToplevelResizeConfigure(this, surface.get(), nextX, nextY, nextWidth, nextHeight);
    LAMBDA_RESIZE_TRACE("compositor",
                                "animation-frame surface=%llu linear=%.3f eased=%.3f desired=%d,%d %dx%d "
                                "sent=%d inFlight=%d acked=%d pending=%d %d,%d %dx%d\n",
                                static_cast<unsigned long long>(surface->id),
                                linearProgress,
                                progress,
                                nextX,
                                nextY,
                                nextWidth,
                                nextHeight,
                                configureSent ? 1 : 0,
                                surface->resizeConfigureInFlight ? 1 : 0,
                                surface->resizeConfigureAcked ? 1 : 0,
                                surface->pendingResizeConfigure ? 1 : 0,
                                surface->pendingResizeConfigureX,
                                surface->pendingResizeConfigureY,
                                surface->pendingResizeConfigureWidth,
                                surface->pendingResizeConfigureHeight);
    traceResizeSurface("animation-surface", surface.get());

    if (configureSent) {
      sentConfigure = true;
    }

    if (finished) {
      surface->geometryAnimationActive = false;
    }
  }
  if (sentConfigure) flushClients();
}

bool WaylandServer::Impl::hasActiveAnimations() const noexcept {
  return shellPanelHideAnimationActive_ || std::any_of(surfaces_.begin(), surfaces_.end(), [](auto const& surface) {
    return surface && surface->geometryAnimationActive;
  });
}

bool WaylandServer::Impl::hasActiveResizePacing() const noexcept {
  if (resizeSurface_ != nullptr || std::any_of(surfaces_.begin(), surfaces_.end(), [](auto const& surface) {
    return surface && (surface->geometryAnimationActive || surface->resizeConfigureInFlight ||
                       surface->pendingResizeConfigure);
  })) {
    return true;
  }
  return hasRecentResizePacing();
}

bool WaylandServer::Impl::hasRecentResizePacing(std::uint64_t nowNsec) const noexcept {
  if (nowNsec == 0) nowNsec = monotonicNanoseconds();
  return resizePacingGraceActive(lastResizePacingActivityNsec_, nowNsec, output_.refreshMilliHz);
}

void WaylandServer::Impl::noteResizePacingActivity(std::uint64_t nowNsec) noexcept {
  lastResizePacingActivityNsec_ = nowNsec == 0 ? monotonicNanoseconds() : nowNsec;
}

bool WaylandServer::Impl::hasIdleInhibitors() const noexcept {
  return std::any_of(idleInhibitors_.begin(), idleInhibitors_.end(), [](auto const& inhibitor) {
    return idleInhibitorSurfaceActive(inhibitor ? inhibitor->surface : nullptr);
  });
}

bool WaylandServer::Impl::hasPendingFrameCallbacks() const noexcept {
  return std::any_of(surfaces_.begin(), surfaces_.end(), [this](auto const& surface) {
    return surface &&
           !hiddenFullscreenShellPanel(*this, surface.get()) &&
           (!surface->frameCallbacks.empty() || !surface->pendingFrameCallbacks.empty());
  });
}

std::uint64_t dmabufBufferIdForResource(WaylandServer::Impl const& server, wl_resource* resource) {
  auto const found = std::find_if(server.dmabufBuffers_.begin(),
                                  server.dmabufBuffers_.end(),
                                  [resource](auto const& buffer) {
                                    return buffer && buffer->resource == resource;
                                  });
  return found == server.dmabufBuffers_.end() ? 0 : (*found)->id;
}

bool isRetainedDmabufBuffer(WaylandServer::Impl const& server, wl_resource* resource) {
  std::uint64_t const id = dmabufBufferIdForResource(server, resource);
  return id != 0 && std::find(server.retainedDmabufBufferIds_.begin(),
                              server.retainedDmabufBufferIds_.end(),
                              id) != server.retainedDmabufBufferIds_.end();
}

std::vector<PendingBufferReleaseRecord> bufferReleaseRecordsFor(WaylandServer::Impl const& server,
                                                                std::vector<wl_resource*> const& queue) {
  std::vector<PendingBufferReleaseRecord> records;
  records.reserve(queue.size());
  for (wl_resource* buffer : queue) {
    if (!buffer) continue;
    records.push_back({
        .token = reinterpret_cast<std::uintptr_t>(buffer),
        .dmabufBufferId = dmabufBufferIdForResource(server, buffer),
    });
  }
  return records;
}

std::uint64_t releaseBufferQueue(WaylandServer::Impl const& server, std::vector<wl_resource*>& queue) {
  std::vector<PendingBufferReleaseRecord> const records = bufferReleaseRecordsFor(server, queue);
  BufferReleasePlan const plan = planBufferReleases(records, server.retainedDmabufBufferIds_);
  queue.clear();
  queue.reserve(plan.retained.size());
  for (PendingBufferReleaseRecord const& record : plan.retained) {
    queue.push_back(reinterpret_cast<wl_resource*>(record.token));
  }
  for (PendingBufferReleaseRecord const& record : plan.releasable) {
    wl_buffer_send_release(reinterpret_cast<wl_resource*>(record.token));
  }
  return plan.releasable.size();
}

bool WaylandServer::Impl::bufferReleaseIsRetained(wl_resource* buffer) const {
  return isRetainedDmabufBuffer(*this, buffer);
}

void WaylandServer::Impl::queueOrphanedBufferRelease(wl_resource* buffer) {
  if (!buffer) return;
  if (std::find(orphanedBufferReleases_.begin(), orphanedBufferReleases_.end(), buffer) ==
      orphanedBufferReleases_.end()) {
    orphanedBufferReleases_.push_back(buffer);
  }
}

void WaylandServer::Impl::releasePendingBuffers() {
  std::uint64_t releaseCount = releaseBufferQueue(*this, orphanedBufferReleases_);
  for (auto const& surface : surfaces_) {
    if (!surface || surface->pendingBufferReleases.empty()) continue;
    releaseCount += releaseBufferQueue(*this, surface->pendingBufferReleases);
  }
  if (releaseCount > 0) {
    LAMBDA_RESIZE_TRACE("compositor",
                              "buffer-releases count=%llu\n",
                              static_cast<unsigned long long>(releaseCount));
  }
}

void WaylandServer::Impl::sendFrameCallbacks(std::uint32_t timeMs, PresentationTiming timing) {
  std::vector<std::uint64_t> frameSurfaceIds;
  frameSurfaceIds.reserve(surfaces_.size());
  for (auto const& surface : surfaces_) {
    if (surface) frameSurfaceIds.push_back(surface->id);
  }
  sendFrameCallbacks(timeMs, timing, frameSurfaceIds);
}

void WaylandServer::Impl::sendFrameCallbacks(std::uint32_t timeMs,
                                             PresentationTiming timing,
                                             std::span<std::uint64_t const> frameSurfaceIds) {
  sendPresentationFeedbacks(timeMs, timing, frameSurfaceIds);
  sendFrameCallbacksOnly(timeMs, frameSurfaceIds);
}

void WaylandServer::Impl::sendFrameCallbacksOnly(std::uint32_t timeMs) {
  std::vector<std::uint64_t> frameSurfaceIds;
  frameSurfaceIds.reserve(surfaces_.size());
  for (auto const& surface : surfaces_) {
    if (surface) frameSurfaceIds.push_back(surface->id);
  }
  sendFrameCallbacksOnly(timeMs, frameSurfaceIds);
}

void WaylandServer::Impl::sendFrameCallbacksOnly(std::uint32_t timeMs,
                                                 std::span<std::uint64_t const> frameSurfaceIds) {
  releasePendingBuffers();
  std::uint64_t callbackCount = 0;
  for (auto const& surface : surfaces_) {
    if (surface && hiddenFullscreenShellPanel(*this, surface.get())) continue;
    if (!surfaceParticipatesInPresentedFrame(surface.get(), frameSurfaceIds)) continue;
    std::vector<wl_resource*> callbacks = std::move(surface->frameCallbacks);
    surface->frameCallbacks.clear();
    callbackCount += callbacks.size();
    for (wl_resource* callback : callbacks) {
      wl_callback_send_done(callback, timeMs);
      wl_resource_destroy(callback);
    }
  }
  LAMBDA_RESIZE_TRACE("compositor",
                            "frame-callbacks time=%u callbacks=%llu pendingPresentationBatches=%zu\n",
                            timeMs,
                            static_cast<unsigned long long>(callbackCount),
                            pendingPresentationBatches_.size());
  flushClients();
}

void WaylandServer::Impl::sendPresentationFeedbacks(std::uint32_t timeMs, PresentationTiming timing) {
  std::vector<std::uint64_t> frameSurfaceIds;
  frameSurfaceIds.reserve(surfaces_.size());
  for (auto const& surface : surfaces_) {
    if (surface) frameSurfaceIds.push_back(surface->id);
  }
  sendPresentationFeedbacks(timeMs, timing, frameSurfaceIds);
}

void WaylandServer::Impl::sendPresentationFeedbacks(std::uint32_t timeMs,
                                                    PresentationTiming timing,
                                                    std::span<std::uint64_t const> frameSurfaceIds) {
  normalizePresentationTiming(output_, timing);
  std::vector<PresentationFeedback*> delayedFeedbacks;
  for (auto const& surface : surfaces_) {
    if (!surfaceParticipatesInPresentedFrame(surface.get(), frameSurfaceIds)) continue;
    std::vector<PresentationFeedback*> feedbacks = std::move(surface->presentationFeedbacks);
    surface->presentationFeedbacks.clear();
    for (auto* feedback : feedbacks) {
      if (!feedback || !feedback->resource) continue;
      if (timing.backendPresentId != 0) {
        delayedFeedbacks.push_back(feedback);
      } else {
        sendPresentationFeedback(*this, feedback, timing);
      }
    }
  }
  if (!delayedFeedbacks.empty()) {
    pendingPresentationBatches_.push_back(PendingPresentationBatch{
        .backendPresentId = timing.backendPresentId,
        .queuedAtMs = timeMs,
        .fallbackTiming = timing,
        .feedbacks = std::move(delayedFeedbacks),
    });
  }
  completePresentationFeedbacks({}, timeMs);
  flushClients();
}

void WaylandServer::Impl::completePresentationFeedbacks(std::vector<PresentationCompletion> const& completions,
                                                        std::uint32_t timeMs) {
  bool sent = false;
  for (auto it = pendingPresentationBatches_.begin(); it != pendingPresentationBatches_.end();) {
    auto completion = std::find_if(completions.begin(), completions.end(), [&](PresentationCompletion const& item) {
      return item.backendPresentId == it->backendPresentId;
    });
    std::uint32_t const refreshMilliHz =
        output_.refreshMilliHz > 0 ? static_cast<std::uint32_t>(output_.refreshMilliHz) : 0u;
    bool const hasCompletion = completion != completions.end();
    auto const decision = presentation::resolvePresentationFeedbackCompletion(
        it->fallbackTiming,
        hasCompletion,
        hasCompletion ? *completion : PresentationCompletion{},
        it->queuedAtMs,
        timeMs,
        refreshMilliHz,
        static_cast<std::uint32_t>(WP_PRESENTATION_FEEDBACK_KIND_HW_COMPLETION));
    if (!decision.ready) {
      ++it;
      continue;
    }
    std::vector<PresentationFeedback*> feedbacks = std::move(it->feedbacks);
    it = pendingPresentationBatches_.erase(it);
    for (auto* feedback : feedbacks) {
      if (decision.presented) {
        sendPresentationFeedback(*this, feedback, decision.timing);
      } else {
        discardPresentationFeedback(feedback);
      }
      sent = true;
    }
  }
  if (sent) flushClients();
}

} // namespace lambda::compositor
