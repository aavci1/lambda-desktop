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
  if (timing.monotonicNsec == 0) {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    timing.monotonicNsec = static_cast<std::uint64_t>(now.tv_sec) * 1'000'000'000ull +
                           static_cast<std::uint64_t>(now.tv_nsec);
  }
  if (timing.refreshNsec == 0 && output_.refreshMilliHz > 0) {
    timing.refreshNsec =
        static_cast<std::uint32_t>(1'000'000'000'000ull / static_cast<std::uint64_t>(output_.refreshMilliHz));
  }
  std::uint64_t const seconds = timing.monotonicNsec / 1'000'000'000ull;
  std::uint32_t const nsec = static_cast<std::uint32_t>(timing.monotonicNsec % 1'000'000'000ull);
  std::uint32_t const tvSecHi = static_cast<std::uint32_t>(seconds >> 32u);
  std::uint32_t const tvSecLo = static_cast<std::uint32_t>(seconds & 0xffffffffu);
  std::uint32_t const seqHi = static_cast<std::uint32_t>(timing.sequence >> 32u);
  std::uint32_t const seqLo = static_cast<std::uint32_t>(timing.sequence & 0xffffffffu);

  for (auto const& surface : surfaces_) {
    std::vector<PresentationFeedback*> feedbacks = std::move(surface->presentationFeedbacks);
    surface->presentationFeedbacks.clear();
    for (auto* feedback : feedbacks) {
      if (!feedback || !feedback->resource) continue;
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
    std::vector<wl_resource*> callbacks = std::move(surface->frameCallbacks);
    surface->frameCallbacks.clear();
    for (wl_resource* callback : callbacks) {
      wl_callback_send_done(callback, timeMs);
      wl_resource_destroy(callback);
    }
  }
  flushClients();
}

} // namespace flux::compositor
