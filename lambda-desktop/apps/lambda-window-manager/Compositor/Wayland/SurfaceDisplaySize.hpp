#pragma once

#include "Compositor/Wayland/WaylandServerImpl.hpp"

#include <algorithm>
#include <cstdint>

namespace lambda::compositor {

struct SurfaceDisplaySize {
  std::int32_t width = 0;
  std::int32_t height = 0;
};

inline bool surfaceHasPendingUncommittedFrame(WaylandServer::Impl::Surface const* surface) {
  return surfaceIsXdgToplevel(surface) &&
         (surface->awaitingConfigureCommit ||
          surface->resizeConfigureInFlight ||
          surface->pendingResizeConfigure);
}

inline SurfaceDisplaySize surfaceLiveDisplaySize(WaylandServer::Impl::Surface const* surface) {
  if (!surface) return {};
  return {
      .width = surface->frameWidth > 0
                   ? surface->frameWidth
                   : std::max(0, surfaceTransformedBufferWidth(surface) / std::max(1, surface->bufferState.scale)),
      .height = surface->frameHeight > 0
                    ? surface->frameHeight
                    : std::max(0, surfaceTransformedBufferHeight(surface) / std::max(1, surface->bufferState.scale)),
  };
}

inline bool surfaceHasCommittedDisplaySize(WaylandServer::Impl::Surface const* surface) {
  if (!surface) return false;
  if (surface->viewportState.destinationSet) {
    return surface->viewportState.destinationWidth > 0 && surface->viewportState.destinationHeight > 0;
  }
  if (surface->viewportState.sourceSet) {
    return surface->viewportState.sourceWidth > 0.f && surface->viewportState.sourceHeight > 0.f;
  }
  return surfaceTransformedBufferWidth(surface) > 0 && surfaceTransformedBufferHeight(surface) > 0;
}

inline SurfaceDisplaySize surfaceInteractiveDisplaySize(WaylandServer::Impl::Surface const* surface) {
  SurfaceDisplaySize size = surfaceLiveDisplaySize(surface);
  if (!surfaceHasPendingUncommittedFrame(surface) || !surfaceHasCommittedDisplaySize(surface)) return size;

  std::int32_t const committedWidth = surfaceCommittedDisplayWidth(surface);
  std::int32_t const committedHeight = surfaceCommittedDisplayHeight(surface);
  if (committedWidth > 0 && committedHeight > 0) {
    size.width = committedWidth;
    size.height = committedHeight;
  }
  return size;
}

} // namespace lambda::compositor
