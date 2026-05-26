#include "Compositor/Wayland/WaylandServerImpl.hpp"

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

namespace flux::compositor {
namespace {

constexpr std::int32_t kMinWindowWidth = kCompositorMinWindowWidth;
constexpr std::int32_t kMinWindowHeight = kCompositorMinWindowHeight;
constexpr std::uint32_t kGeometryAnimationMs = 180;
constexpr std::uint32_t kShellPanelAnimationMs = 180;
constexpr std::uint32_t kPresentationCompletionFallbackMs = 500;

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
  std::uint64_t const seconds = timing.monotonicNsec / 1'000'000'000ull;
  std::uint32_t const nsec = static_cast<std::uint32_t>(timing.monotonicNsec % 1'000'000'000ull);
  std::uint32_t const tvSecHi = static_cast<std::uint32_t>(seconds >> 32u);
  std::uint32_t const tvSecLo = static_cast<std::uint32_t>(seconds & 0xffffffffu);
  std::uint32_t const seqHi = static_cast<std::uint32_t>(timing.sequence >> 32u);
  std::uint32_t const seqLo = static_cast<std::uint32_t>(timing.sequence & 0xffffffffu);
  if (wl_resource* output = outputResourceForClient(server, wl_resource_get_client(feedback->resource))) {
    wp_presentation_feedback_send_sync_output(feedback->resource, output);
  }
  wp_presentation_feedback_send_presented(feedback->resource,
                                          tvSecHi,
                                          tvSecLo,
                                          nsec,
                                          timing.refreshNsec,
                                          seqHi,
                                          seqLo,
                                          timing.flags);
  wl_resource_destroy(feedback->resource);
}

} // namespace

void WaylandServer::Impl::updateAnimations(std::uint32_t timeMs, bool animationsEnabled) {
  updateShellPanelAnimation(*this, timeMs, animationsEnabled);

  bool sentConfigure = false;
  for (auto const& surface : surfaces_) {
    if (!surface->geometryAnimationActive) continue;
    std::int32_t const oldX = surface->windowX;
    std::int32_t const oldY = surface->windowY;
    std::int32_t const oldWidth = displayWidth(surface.get());
    std::int32_t const oldHeight = displayHeight(surface.get());

    float const linearProgress =
        animationsEnabled
            ? static_cast<float>(timeMs - surface->geometryAnimationStartedAtMs) /
                  static_cast<float>(kGeometryAnimationMs)
            : 1.f;
    float const progress = easeInOutCubic(linearProgress);
    std::int32_t const nextX = lerpInt(surface->geometryAnimationStartX, surface->geometryAnimationTargetX, progress);
    std::int32_t const nextY = lerpInt(surface->geometryAnimationStartY, surface->geometryAnimationTargetY, progress);
    std::int32_t const nextWidth =
        std::max(kMinWindowWidth,
                 lerpInt(surface->geometryAnimationStartWidth, surface->geometryAnimationTargetWidth, progress));
    std::int32_t const nextHeight =
        std::max(kMinWindowHeight,
                 lerpInt(surface->geometryAnimationStartHeight, surface->geometryAnimationTargetHeight, progress));
    surface->windowX = nextX;
    surface->windowY = nextY;
    setConfiguredFrameSize(surface.get(), nextWidth, nextHeight);
    traceResizeSurface("animation-frame", surface.get());

    if (linearProgress >= 1.f) {
      surface->windowX = surface->geometryAnimationTargetX;
      surface->windowY = surface->geometryAnimationTargetY;
      setConfiguredFrameSize(surface.get(),
                             surface->geometryAnimationTargetWidth,
                             surface->geometryAnimationTargetHeight);
      surface->geometryAnimationActive = false;
      if (!surface->geometryAnimationConfigureSent) {
        sendToplevelConfigure(this,
                              toplevelForSurface(this, surface.get()),
                              surface->geometryAnimationTargetWidth,
                              surface->geometryAnimationTargetHeight);
        surface->geometryAnimationConfigureSent = true;
        sentConfigure = true;
      }
    }
    if (surface->windowX != oldX ||
        surface->windowY != oldY ||
        displayWidth(surface.get()) != oldWidth ||
        displayHeight(surface.get()) != oldHeight) {
      ++contentSerial_;
    }
  }
  if (sentConfigure) flushClients();
}

bool WaylandServer::Impl::hasActiveAnimations() const noexcept {
  return shellPanelHideAnimationActive_ || std::any_of(surfaces_.begin(), surfaces_.end(), [](auto const& surface) {
    return surface && surface->geometryAnimationActive;
  });
}

bool WaylandServer::Impl::hasIdleInhibitors() const noexcept {
  return std::any_of(idleInhibitors_.begin(), idleInhibitors_.end(), [](auto const& inhibitor) {
    auto const* surface = inhibitor ? inhibitor->surface : nullptr;
    return surface && surface->currentBuffer && !surface->minimized && surface->width > 0 && surface->height > 0;
  });
}

void WaylandServer::Impl::releasePendingBuffers() {
  std::uint64_t releaseCount = 0;
  for (auto const& surface : surfaces_) {
    if (!surface || surface->pendingBufferReleases.empty()) continue;
    std::vector<wl_resource*> releases = std::move(surface->pendingBufferReleases);
    surface->pendingBufferReleases.clear();
    for (wl_resource* buffer : releases) {
      if (!buffer) continue;
      wl_buffer_send_release(buffer);
      ++releaseCount;
    }
  }
  if (releaseCount > 0) {
    flux::detail::resizeTrace("compositor",
                              "buffer-releases count=%llu\n",
                              static_cast<unsigned long long>(releaseCount));
  }
}

void WaylandServer::Impl::sendFrameCallbacks(std::uint32_t timeMs, PresentationTiming timing) {
  sendPresentationFeedbacks(timeMs, timing);
  sendFrameCallbacksOnly(timeMs);
}

void WaylandServer::Impl::sendFrameCallbacksOnly(std::uint32_t timeMs) {
  releasePendingBuffers();
  std::uint64_t callbackCount = 0;
  for (auto const& surface : surfaces_) {
    std::vector<wl_resource*> callbacks = std::move(surface->frameCallbacks);
    surface->frameCallbacks.clear();
    callbackCount += callbacks.size();
    for (wl_resource* callback : callbacks) {
      wl_callback_send_done(callback, timeMs);
      wl_resource_destroy(callback);
    }
  }
  flux::detail::resizeTrace("compositor",
                            "frame-callbacks time=%u callbacks=%llu pendingPresentationBatches=%zu\n",
                            timeMs,
                            static_cast<unsigned long long>(callbackCount),
                            pendingPresentationBatches_.size());
  flushClients();
}

void WaylandServer::Impl::sendPresentationFeedbacks(std::uint32_t timeMs, PresentationTiming timing) {
  normalizePresentationTiming(output_, timing);
  std::vector<PresentationFeedback*> delayedFeedbacks;
  for (auto const& surface : surfaces_) {
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
    bool const expired = static_cast<std::uint32_t>(timeMs - it->queuedAtMs) >= kPresentationCompletionFallbackMs;
    if (completion == completions.end() && !expired) {
      ++it;
      continue;
    }
    PresentationTiming timing = it->fallbackTiming;
    if (completion != completions.end()) {
      timing.monotonicNsec = completion->monotonicNsec;
      if (completion->sequence != 0) timing.sequence = completion->sequence;
      timing.flags |= completion->flags | static_cast<std::uint32_t>(WP_PRESENTATION_FEEDBACK_KIND_HW_COMPLETION);
    }
    std::vector<PresentationFeedback*> feedbacks = std::move(it->feedbacks);
    it = pendingPresentationBatches_.erase(it);
    for (auto* feedback : feedbacks) {
      sendPresentationFeedback(*this, feedback, timing);
      sent = true;
    }
  }
  if (sent) flushClients();
}

} // namespace flux::compositor
