#include "Compositor/Wayland/WaylandServerImpl.hpp"

#include "Compositor/Window/WindowGeometry.hpp"
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
constexpr std::uint32_t kFallbackConfigureLeadMs = 16;
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

std::uint32_t refreshIntervalMs(std::uint32_t refreshMilliHz) {
  if (refreshMilliHz == 0) return kFallbackConfigureLeadMs;
  std::uint64_t const interval = 1'000'000ull / static_cast<std::uint64_t>(refreshMilliHz);
  return static_cast<std::uint32_t>(std::clamp<std::uint64_t>(interval, 1ull, 33ull));
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
  bool sentConfigure = false;
  std::uint32_t const configureLeadMs = refreshIntervalMs(output_.refreshMilliHz);
  for (auto const& surface : surfaces_) {
    if (!surface->geometryAnimationActive) continue;

    float const linearProgress =
        animationsEnabled
            ? static_cast<float>(timeMs - surface->geometryAnimationStartedAtMs) /
                  static_cast<float>(kGeometryAnimationMs)
            : 1.f;
    float const progress = easeInOutCubic(linearProgress);
    float const configureProgress =
        animationsEnabled
            ? easeInOutCubic(static_cast<float>(timeMs + configureLeadMs - surface->geometryAnimationStartedAtMs) /
                             static_cast<float>(kGeometryAnimationMs))
            : 1.f;
    std::int32_t const nextX = lerpInt(surface->geometryAnimationStartX, surface->geometryAnimationTargetX, progress);
    std::int32_t const nextY = lerpInt(surface->geometryAnimationStartY, surface->geometryAnimationTargetY, progress);
    std::int32_t const nextWidth =
        std::max(kMinWindowWidth,
                 lerpInt(surface->geometryAnimationStartWidth, surface->geometryAnimationTargetWidth, progress));
    std::int32_t const nextHeight =
        std::max(kMinWindowHeight,
                 lerpInt(surface->geometryAnimationStartHeight, surface->geometryAnimationTargetHeight, progress));
    std::int32_t const configureWidth =
        std::max(kMinWindowWidth,
                 lerpInt(surface->geometryAnimationStartWidth,
                         surface->geometryAnimationTargetWidth,
                         configureProgress));
    std::int32_t const configureHeight =
        std::max(kMinWindowHeight,
                 lerpInt(surface->geometryAnimationStartHeight,
                         surface->geometryAnimationTargetHeight,
                         configureProgress));

    surface->windowX = nextX;
    surface->windowY = nextY;
    setConfiguredFrameSize(surface.get(), nextWidth, nextHeight);
    traceResizeSurface("animation-frame", surface.get());
    if (configureWidth != surface->geometryAnimationLastConfigureWidth ||
        configureHeight != surface->geometryAnimationLastConfigureHeight) {
      surface->geometryAnimationLastConfigureWidth = configureWidth;
      surface->geometryAnimationLastConfigureHeight = configureHeight;
      sendToplevelConfigure(this, toplevelForSurface(this, surface.get()), configureWidth, configureHeight);
      sentConfigure = true;
    }

    if (linearProgress >= 1.f) {
      surface->windowX = surface->geometryAnimationTargetX;
      surface->windowY = surface->geometryAnimationTargetY;
      setConfiguredFrameSize(surface.get(),
                             surface->geometryAnimationTargetWidth,
                             surface->geometryAnimationTargetHeight);
      surface->geometryAnimationActive = false;
      if (surface->frameWidth != surface->geometryAnimationLastConfigureWidth ||
          surface->frameHeight != surface->geometryAnimationLastConfigureHeight) {
        surface->geometryAnimationLastConfigureWidth = surface->frameWidth;
        surface->geometryAnimationLastConfigureHeight = surface->frameHeight;
        sendToplevelConfigure(this, toplevelForSurface(this, surface.get()), surface->frameWidth, surface->frameHeight);
        sentConfigure = true;
      }
    }
  }
  if (sentConfigure) flushClients();
}

bool WaylandServer::Impl::hasActiveAnimations() const noexcept {
  return std::any_of(surfaces_.begin(), surfaces_.end(), [](auto const& surface) {
    return surface && surface->geometryAnimationActive;
  });
}

bool WaylandServer::Impl::hasIdleInhibitors() const noexcept {
  return std::any_of(idleInhibitors_.begin(), idleInhibitors_.end(), [](auto const& inhibitor) {
    auto const* surface = inhibitor ? inhibitor->surface : nullptr;
    return surface && surface->currentBuffer && !surface->minimized && surface->width > 0 && surface->height > 0;
  });
}

void WaylandServer::Impl::sendFrameCallbacks(std::uint32_t timeMs, PresentationTiming timing) {
  sendPresentationFeedbacks(timeMs, timing);
  sendFrameCallbacksOnly(timeMs);
}

void WaylandServer::Impl::sendFrameCallbacksOnly(std::uint32_t timeMs) {
  for (auto const& surface : surfaces_) {
    std::vector<wl_resource*> callbacks = std::move(surface->frameCallbacks);
    surface->frameCallbacks.clear();
    for (wl_resource* callback : callbacks) {
      wl_callback_send_done(callback, timeMs);
      wl_resource_destroy(callback);
    }
  }
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
