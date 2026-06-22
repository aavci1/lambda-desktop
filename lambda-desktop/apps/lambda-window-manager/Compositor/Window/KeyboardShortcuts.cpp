#include "Compositor/Window/WindowManagerInternal.hpp"

#include "Compositor/Wayland/WaylandServerImpl.hpp"
#include "Compositor/Chrome/ChromeMetrics.hpp"
#include "Compositor/Window/WindowGeometry.hpp"
#include "Detail/ResizeTrace.hpp"
#include "pointer-constraints-unstable-v1-server-protocol.h"
#include "relative-pointer-unstable-v1-server-protocol.h"
#include "wlr-layer-shell-unstable-v1-server-protocol.h"
#include "xdg-shell-server-protocol.h"

#include <linux/input-event-codes.h>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <chrono>
#include <optional>
#include <vector>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <xkbcommon/xkbcommon.h>

namespace lambda::compositor::wm {

bool updateShortcutModifier(WaylandServer::Impl* server, std::uint32_t key, bool pressed) {
  bool changed = false;
  if (key == KEY_LEFTMETA || key == KEY_RIGHTMETA) {
    changed = server->metaDown_ != pressed;
    server->metaDown_ = pressed;
    if (!pressed) clearFocusCycle(server);
    if (changed && !server->xkbState_) sendKeyboardModifiers(server);
    return true;
  }
  if (key == KEY_LEFTCTRL || key == KEY_RIGHTCTRL) {
    changed = server->ctrlDown_ != pressed;
    server->ctrlDown_ = pressed;
    if (!pressed) clearFocusCycle(server);
    if (changed && !server->xkbState_) sendKeyboardModifiers(server);
    return false;
  }
  if (key == KEY_LEFTALT) {
    changed = server->altDown_ != pressed;
    server->altDown_ = pressed;
    if (!pressed) clearFocusCycle(server);
    if (changed && !server->xkbState_) sendKeyboardModifiers(server);
    return false;
  }
  if (key == KEY_LEFTSHIFT || key == KEY_RIGHTSHIFT) {
    changed = server->shiftDown_ != pressed;
    server->shiftDown_ = pressed;
    if (changed && !server->xkbState_) sendKeyboardModifiers(server);
    return false;
  }
  return false;
}

} // namespace lambda::compositor::wm

namespace lambda::compositor::wm {

bool handleCompositorShortcut(WaylandServer::Impl* server, std::uint32_t key, bool pressed, std::uint32_t timeMs) {
  if (!pressed) return focusCycleActive(server) && key == KEY_TAB;
  for (auto const& binding : server->shortcutBindings_) {
    if (binding.key != key) continue;
    if (!shortcutBindingMatches(binding, server->metaDown_, server->ctrlDown_, server->altDown_, server->shiftDown_)) {
      continue;
    }

    switch (binding.action) {
    case WaylandServer::ShortcutAction::CloseFocused:
      return closeFocusedToplevel(server);
    case WaylandServer::ShortcutAction::CycleFocus:
      return cycleFocus(server, timeMs, !server->shiftDown_);
    case WaylandServer::ShortcutAction::SnapLeft:
      snapFocusedToplevel(server, true);
      return true;
    case WaylandServer::ShortcutAction::SnapRight:
      snapFocusedToplevel(server, false);
      return true;
    case WaylandServer::ShortcutAction::Maximize:
      maximizeFocusedToplevel(server);
      return true;
    case WaylandServer::ShortcutAction::Restore:
      restoreFocusedToplevel(server);
      return true;
    case WaylandServer::ShortcutAction::LaunchCommand:
      server->requestShellOpenCommandLauncher();
      return true;
    case WaylandServer::ShortcutAction::Screenshot:
      server->requestScreenshot(ScreenshotMode::FullOutput, timeMs);
      return true;
    case WaylandServer::ShortcutAction::ScreenshotRegion:
      server->requestScreenshot(ScreenshotMode::Region, timeMs);
      return true;
    case WaylandServer::ShortcutAction::ScreenshotActiveWindow:
      server->requestScreenshot(ScreenshotMode::ActiveWindow, timeMs);
      return true;
    case WaylandServer::ShortcutAction::Terminate:
      std::raise(SIGTERM);
      return true;
    }
  }
  return false;
}

} // namespace lambda::compositor::wm
