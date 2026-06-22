#pragma once

#include <algorithm>
#include <cstdint>

namespace lambda::compositor {

struct CutoutBox {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t width = 0;
  std::int32_t height = 0;
  std::uint32_t id = 1;
};

struct CutoutSendState {
  bool lastSent = false;
  std::int32_t lastX = 0;
  std::int32_t lastY = 0;
  std::int32_t lastWidth = 0;
  std::int32_t lastHeight = 0;
};

inline constexpr std::uint32_t kCompositorControlsCutoutId = 1;

inline std::uint32_t xdgTitlebarModeForClientRequest(std::uint32_t requestedMode,
                                                     std::uint32_t clientSideMode,
                                                     std::uint32_t serverSideMode) {
  return requestedMode == clientSideMode ? clientSideMode : serverSideMode;
}

inline std::uint32_t defaultDecorationMode(std::uint32_t serverSideMode) {
  return serverSideMode;
}

inline CutoutBox compositorControlsCutout(std::int32_t surfaceWidth,
                                          std::int32_t surfaceHeight,
                                          std::int32_t controlsWidth,
                                          std::int32_t titleBarHeight) {
  std::int32_t const width = std::min(std::max(0, controlsWidth), std::max(0, surfaceWidth));
  std::int32_t const height = std::min(std::max(0, titleBarHeight), std::max(0, surfaceHeight));
  return {
      .x = std::max(0, surfaceWidth - width),
      .y = 0,
      .width = width,
      .height = height,
      .id = kCompositorControlsCutoutId,
  };
}

inline bool hasUsableCutoutSize(std::int32_t surfaceWidth, std::int32_t surfaceHeight) {
  return surfaceWidth > 0 && surfaceHeight > 0;
}

inline bool shouldSendCutoutConfigure(CutoutSendState const& state,
                                      CutoutBox const& box,
                                      bool force = false) {
  if (box.width <= 0 || box.height <= 0) return false;
  if (force) return true;
  return !state.lastSent ||
         state.lastX != box.x ||
         state.lastY != box.y ||
         state.lastWidth != box.width ||
         state.lastHeight != box.height;
}

inline bool shouldSendEmptyCutoutConfigure(bool cutoutsBound,
                                           bool previouslySentCutouts,
                                           bool usesCutouts) {
  return cutoutsBound && previouslySentCutouts && !usesCutouts;
}

inline bool shouldSendInitialCutoutConfigure(bool cutoutsBound,
                                             bool cutoutsRejected,
                                             bool cutoutLastSent,
                                             std::int32_t surfaceWidth,
                                             std::int32_t surfaceHeight) {
  return cutoutsBound &&
         !cutoutsRejected &&
         !cutoutLastSent &&
         hasUsableCutoutSize(surfaceWidth, surfaceHeight);
}

inline bool shouldReportDefunctCutoutsOnToplevelDestroy(bool cutoutsAlive) {
  return cutoutsAlive;
}

} // namespace lambda::compositor
