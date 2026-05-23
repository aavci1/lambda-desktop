#include "Compositor/Window/WindowManagerInternal.hpp"

#include "Compositor/Wayland/WaylandServerImpl.hpp"
#include "wlr-layer-shell-unstable-v1-server-protocol.h"

namespace flux::compositor {

using wm::previousFocusedToplevel;
using wm::raiseSurface;
using wm::sendPointerFocus;
using wm::setKeyboardFocus;

bool WaylandServer::Impl::claimCommandLauncherModal(std::uint32_t timeMs) {
  for (auto const& layerSurface : layerSurfaces_) {
    if (!layerSurface || !layerSurface->surface) continue;
    if (layerSurface->nameSpace != "lambda.command-launcher") continue;
    if (layerSurface->keyboardInteractivity != ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE) continue;
    commandLauncherModalSurface_ = layerSurface->surface;
    raiseSurface(this, layerSurface->surface);
    setKeyboardFocus(this, layerSurface->surface);
    sendPointerFocus(this, surfaceAt(this, pointerX_, pointerY_), timeMs);
    return true;
  }
  return false;
}

void WaylandServer::Impl::releaseCommandLauncherModal(std::uint32_t timeMs) {
  if (keyboardFocus_ == commandLauncherModalSurface_) {
    Surface* next = previousFocusedToplevel(this, keyboardFocus_);
    setKeyboardFocus(this, next);
    sendPointerFocus(this, surfaceAt(this, pointerX_, pointerY_), timeMs);
  }
  commandLauncherModalSurface_ = nullptr;
}

} // namespace flux::compositor
