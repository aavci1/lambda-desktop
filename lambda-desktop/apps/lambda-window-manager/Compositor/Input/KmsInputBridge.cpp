#include "Compositor/Input/KmsInputBridge.hpp"

#include <Lambda/Debug/DebugFlags.hpp>

#include <cstdio>
#include <cstdlib>

namespace lambdaui::compositor {
namespace {

bool debugCompositorInput() {
  static bool const enabled = debug::envNonZero(std::getenv("LAMBDA_DEBUG_COMPOSITOR_INPUT"));
  return enabled;
}

} // namespace

void dispatchKmsInputEvent(WaylandServer& wayland, platform::KmsInputEvent const& event) {
  if (debugCompositorInput()) {
    std::fprintf(stderr,
                 "lambda-window-manager: input kind=%u dx=%.2f dy=%.2f x=%.1f y=%.1f button=%u pressed=%d key=%u\n",
                 static_cast<unsigned int>(event.kind),
                 event.dx,
                 event.dy,
                 event.x,
                 event.y,
                 event.button,
                 event.pressed,
                 event.key);
  }
  switch (event.kind) {
  case platform::KmsInputEvent::Kind::PointerMotion:
    wayland.handlePointerMotion(event.dx, event.dy, event.timeMs);
    break;
  case platform::KmsInputEvent::Kind::PointerPosition:
    wayland.handlePointerPosition(event.x, event.y, event.timeMs);
    break;
  case platform::KmsInputEvent::Kind::PointerButton:
    wayland.handlePointerButton(event.button, event.pressed, event.timeMs);
    break;
  case platform::KmsInputEvent::Kind::PointerAxis:
    wayland.handlePointerAxis(event.dx, event.dy, event.timeMs);
    break;
  case platform::KmsInputEvent::Kind::Key:
    wayland.handleKeyboardKey(event.key, event.pressed, event.timeMs);
    break;
  case platform::KmsInputEvent::Kind::KeyboardReset:
    wayland.resetKeyboardState(event.timeMs);
    break;
  }
}

} // namespace lambdaui::compositor
