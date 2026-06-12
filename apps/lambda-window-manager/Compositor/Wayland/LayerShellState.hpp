#pragma once

#include "wlr-layer-shell-unstable-v1-server-protocol.h"

#include <cstdint>
#include <string_view>

namespace lambda::compositor {

inline bool layerShellSupportsOnDemandKeyboardInteractivity(std::uint32_t resourceVersion) {
  return resourceVersion >= ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND_SINCE_VERSION;
}

inline std::uint32_t normalizeLayerShellKeyboardInteractivity(std::uint32_t resourceVersion,
                                                              std::uint32_t interactivity) {
  if (layerShellSupportsOnDemandKeyboardInteractivity(resourceVersion)) {
    return interactivity;
  }
  return interactivity != 0 ? ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE
                            : ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;
}

inline bool validLayerShellKeyboardInteractivity(std::uint32_t interactivity) {
  return interactivity <= ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND;
}

inline bool layerShellNamespaceHidesForFullscreen(std::string_view nameSpace) {
  return nameSpace == "lambda.dock";
}

inline bool layerShellFrameCallbacksHiddenForFullscreen(std::string_view nameSpace, float hideProgress) {
  return layerShellNamespaceHidesForFullscreen(nameSpace) && hideProgress >= 0.999f;
}

inline bool layerShellClaimsCommandLauncherModal(std::string_view nameSpace,
                                                 bool mapped,
                                                 std::uint32_t keyboardInteractivity) {
  return mapped &&
         nameSpace == "lambda.command-launcher" &&
         keyboardInteractivity == ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE;
}

} // namespace lambda::compositor
