#include "Platform/Linux/KmsPlatform.hpp"

#include <Lambda/Debug/DebugFlags.hpp>
#include <Lambda/UI/Application.hpp>
#include <Lambda/UI/EventQueue.hpp>

#include "Platform/Linux/Common/XkbState.hpp"

#include <libinput.h>
#include <linux/input-event-codes.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

namespace lambdaui {
namespace {

linux_platform::XkbState& xkbState() {
  static linux_platform::XkbState state;
  static bool initialized = state.createDefaultKeymap();
  (void)initialized;
  return state;
}

MouseButton mouseButtonFromLinux(std::uint32_t button) {
  if (button == BTN_LEFT) return MouseButton::Left;
  if (button == BTN_RIGHT) return MouseButton::Right;
  if (button == BTN_MIDDLE) return MouseButton::Middle;
  return MouseButton::Other;
}

std::uint8_t buttonMaskBit(std::uint32_t button) {
  if (button == BTN_LEFT) return 1u;
  if (button == BTN_RIGHT) return 2u;
  if (button == BTN_MIDDLE) return 4u;
  return 0u;
}

bool debugKmsInput() {
  static bool const enabled = debug::envNonZero(std::getenv("LAMBDA_DEBUG_KMS"));
  return enabled;
}

std::uint32_t inputEventTimeMs() {
  using namespace std::chrono;
  auto const now = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
  return static_cast<std::uint32_t>(std::max<std::int64_t>(0, now));
}

} // namespace

void KmsApplication::handleInputDeviceAdded(libinput_device* device) {
  if (!device) return;
  ++inputDeviceCount_;
  if (libinput_device_config_tap_get_finger_count(device) > 0) {
    libinput_device_config_tap_set_enabled(device, LIBINPUT_CONFIG_TAP_ENABLED);
  }
  if (debugKmsInput()) {
    std::fprintf(stderr, "[lambda:kms:input] device added: %s [keyboard=%d pointer=%d touch=%d tap_fingers=%d]\n",
                 libinput_device_get_name(device),
                 libinput_device_has_capability(device, LIBINPUT_DEVICE_CAP_KEYBOARD),
                 libinput_device_has_capability(device, LIBINPUT_DEVICE_CAP_POINTER),
                 libinput_device_has_capability(device, LIBINPUT_DEVICE_CAP_TOUCH),
                 libinput_device_config_tap_get_finger_count(device));
    std::fflush(stderr);
  }
}

void KmsApplication::setPointerPosition(KmsWindow* window, Point localPosition) {
  if (!window) {
    pointerPos_ = localPosition;
    return;
  }
  Point const local = window->clampPointer(localPosition);
  pointerPos_ = windowOrigin(window);
  pointerPos_.x += local.x;
  pointerPos_.y += local.y;
  focusPointerWindow(window);
  window->moveCursor(local);
}

void KmsApplication::routePointer(Point position, InputEvent::Kind kind, MouseButton button,
                                  Vec2 scrollDelta, bool preciseScrollDelta) {
  pointerPos_ = clampGlobalPointer(position);
  Point localPosition{};
  KmsWindow* window = windowAtGlobalPoint(pointerPos_, localPosition);
  if (!window) return;
  focusPointerWindow(window);
  window->moveCursor(localPosition);
  if (debugKmsInput()) {
    char const* kindName = kind == InputEvent::Kind::PointerMove ? "move" :
                           kind == InputEvent::Kind::PointerDown ? "down" :
                           kind == InputEvent::Kind::PointerUp ? "up" :
                           kind == InputEvent::Kind::Scroll ? "scroll" : "pointer";
    std::fprintf(stderr, "[lambda:kms:input] pointer %s on %s at %.1f,%.1f global %.1f,%.1f button=%u mask=%u\n",
                 kindName, window->outputName().c_str(), localPosition.x, localPosition.y,
                 pointerPos_.x, pointerPos_.y, static_cast<unsigned int>(button),
                 static_cast<unsigned int>(pressedButtons_));
    std::fflush(stderr);
  }
  ::lambdaui::Application::instance().eventQueue().post(InputEvent{.kind = kind,
                                                               .handle = window->handle(),
                                                               .position = localPosition,
                                                               .scrollDelta = scrollDelta,
                                                               .preciseScrollDelta = preciseScrollDelta,
                                                               .button = button,
                                                               .pressedButtons = pressedButtons_});
}

void KmsApplication::routeKey(std::uint32_t evdevKey, bool pressed) {
  KmsWindow* window = focusedWindow();
  if (!window) return;
  auto& xkb = xkbState();
  xkb.updateKey(evdevKey, pressed);
  KeyCode const key = xkb.keyCodeForEvdevKey(evdevKey);
  Modifiers const modifiers = xkb.modifiers();
  ::lambdaui::Application::instance().eventQueue().post(InputEvent{.kind = pressed ? InputEvent::Kind::KeyDown
                                                                                : InputEvent::Kind::KeyUp,
                                                               .handle = window->handle(),
                                                               .key = key,
                                                               .modifiers = modifiers});
  if (pressed && linux_platform::shouldEmitTextInputForModifiers(modifiers)) {
    std::string text = xkb.utf8ForEvdevKey(evdevKey);
    if (!text.empty()) {
      ::lambdaui::Application::instance().eventQueue().post(InputEvent{.kind = InputEvent::Kind::TextInput,
                                                                   .handle = window->handle(),
                                                                   .text = std::move(text)});
    }
  }
}

void KmsApplication::emitRawInput(platform::KmsInputEvent const& event) {
  if (event.kind == platform::KmsInputEvent::Kind::PointerButton) {
    if (event.pressed) rawPressedButtons_.insert(event.button);
    else rawPressedButtons_.erase(event.button);
  } else if (event.kind == platform::KmsInputEvent::Kind::Key) {
    if (event.pressed) rawPressedKeys_.insert(event.key);
    else rawPressedKeys_.erase(event.key);
  }
  if (rawInputHandler_) rawInputHandler_(event);
}

void KmsApplication::releaseRawInputState(std::uint32_t timeMs) {
  std::vector<std::uint32_t> buttons(rawPressedButtons_.begin(), rawPressedButtons_.end());
  std::vector<std::uint32_t> keys(rawPressedKeys_.begin(), rawPressedKeys_.end());
  for (std::uint32_t button : buttons) {
    emitRawInput({.kind = platform::KmsInputEvent::Kind::PointerButton,
                  .button = button,
                  .pressed = false,
                  .timeMs = timeMs});
  }
  for (std::uint32_t key : keys) {
    emitRawInput({.kind = platform::KmsInputEvent::Kind::Key,
                  .pressed = false,
                  .key = key,
                  .timeMs = timeMs});
  }
  rawPressedButtons_.clear();
  rawPressedKeys_.clear();
  pressedButtons_ = 0;
  xkbState().resetState();
  emitRawInput({.kind = platform::KmsInputEvent::Kind::KeyboardReset, .timeMs = timeMs});
}

void KmsApplication::discardPendingInputEvents(bool handleDeviceEvents) {
  if (!input_) return;

  for (;;) {
    int const dispatchResult = libinput_dispatch(input_);
    if (dispatchResult != 0) {
      if (debugKmsInput()) {
        std::fprintf(stderr, "[lambda:kms:input] libinput_dispatch while draining returned %d\n",
                     dispatchResult);
      }
      break;
    }

    bool sawEvent = false;
    while (libinput_event* event = libinput_get_event(input_)) {
      sawEvent = true;
      libinput_event_type const type = libinput_event_get_type(event);
      if (handleDeviceEvents && type == LIBINPUT_EVENT_DEVICE_ADDED) {
        handleInputDeviceAdded(libinput_event_get_device(event));
      } else if (handleDeviceEvents && type == LIBINPUT_EVENT_DEVICE_REMOVED && debugKmsInput()) {
        libinput_device* device = libinput_event_get_device(event);
        std::fprintf(stderr, "[lambda:kms:input] device removed while draining: %s\n",
                     device ? libinput_device_get_name(device) : "(unknown)");
      } else if (debugKmsInput()) {
        std::fprintf(stderr, "[lambda:kms:input] discarded libinput event type %d during VT transition\n",
                     static_cast<int>(type));
      }
      libinput_event_destroy(event);
    }

    if (!sawEvent) break;
  }
}

void KmsApplication::suspendInputForVtSwitch() {
  if (!input_ || inputSuspendedForVt_) return;
  releaseRawInputState(inputEventTimeMs());
  discardPendingInputEvents(false);
  libinput_suspend(input_);
  inputSuspendedForVt_ = true;
  if (debugKmsInput()) {
    std::fprintf(stderr, "[lambda:kms:input] suspended libinput for inactive VT\n");
  }
}

void KmsApplication::resumeInputAfterVtSwitch() {
  if (!input_ || !inputSuspendedForVt_) return;
  int const resumeResult = libinput_resume(input_);
  if (resumeResult != 0 && debugKmsInput()) {
    std::fprintf(stderr, "[lambda:kms:input] libinput_resume returned %d\n", resumeResult);
  }
  inputSuspendedForVt_ = false;
  releaseRawInputState(inputEventTimeMs());
  discardPendingInputEvents(true);
  if (debugKmsInput()) {
    std::fprintf(stderr, "[lambda:kms:input] resumed libinput for active VT\n");
  }
}

void KmsApplication::dispatchPendingInput() {
  handlePendingVtSignal();
  pollActiveVt();
  handlePendingTerminateSignal();
  if (!isVtForeground()) return;
  if (!input_) return;
  int const dispatchResult = libinput_dispatch(input_);
  if (dispatchResult != 0 && debugKmsInput()) {
    std::fprintf(stderr, "[lambda:kms:input] libinput_dispatch returned %d\n", dispatchResult);
  }
  while (libinput_event* event = libinput_get_event(input_)) {
    switch (libinput_event_get_type(event)) {
    case LIBINPUT_EVENT_DEVICE_ADDED:
      handleInputDeviceAdded(libinput_event_get_device(event));
      break;
    case LIBINPUT_EVENT_DEVICE_REMOVED:
      if (debugKmsInput()) {
        libinput_device* device = libinput_event_get_device(event);
        std::fprintf(stderr, "[lambda:kms:input] device removed: %s\n",
                     device ? libinput_device_get_name(device) : "(unknown)");
      }
      break;
    case LIBINPUT_EVENT_POINTER_MOTION: {
      auto* pointer = libinput_event_get_pointer_event(event);
      double const dx = libinput_event_pointer_get_dx(pointer);
      double const dy = libinput_event_pointer_get_dy(pointer);
      if (debugKmsInput()) {
        std::fprintf(stderr, "[lambda:kms:input] raw pointer motion dx=%.2f dy=%.2f\n", dx, dy);
        std::fflush(stderr);
      }
      emitRawInput({.kind = platform::KmsInputEvent::Kind::PointerMotion,
                    .dx = dx,
                    .dy = dy,
                    .timeMs = libinput_event_pointer_get_time(pointer)});
      pointerPos_.x += static_cast<float>(dx);
      pointerPos_.y += static_cast<float>(dy);
      routePointer(pointerPos_, InputEvent::Kind::PointerMove);
      break;
    }
    case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE: {
      auto* pointer = libinput_event_get_pointer_event(event);
      Size rawSize(1.f, 1.f);
      if (!connectors_.empty()) {
        drmModeModeInfo const& mode = connectors_.front().mode;
        rawSize = Size(static_cast<float>(std::max(1, static_cast<int>(mode.hdisplay))),
                       static_cast<float>(std::max(1, static_cast<int>(mode.vdisplay))));
      }
      double const x = libinput_event_pointer_get_absolute_x_transformed(
          pointer, static_cast<std::uint32_t>(rawSize.width));
      double const y = libinput_event_pointer_get_absolute_y_transformed(
          pointer, static_cast<std::uint32_t>(rawSize.height));
      if (debugKmsInput()) {
        std::fprintf(stderr, "[lambda:kms:input] raw pointer absolute x=%.1f y=%.1f\n", x, y);
        std::fflush(stderr);
      }
      emitRawInput({.kind = platform::KmsInputEvent::Kind::PointerPosition,
                    .x = x,
                    .y = y,
                    .timeMs = libinput_event_pointer_get_time(pointer)});
      KmsWindow* window = focusedWindow();
      if (!window) break;
      Size size = window->currentSize();
      Point p{static_cast<float>(libinput_event_pointer_get_absolute_x_transformed(pointer,
                                                                                  static_cast<std::uint32_t>(size.width))),
              static_cast<float>(libinput_event_pointer_get_absolute_y_transformed(pointer,
                                                                                  static_cast<std::uint32_t>(size.height)))};
      Point global = windowOrigin(window);
      global.x += p.x;
      global.y += p.y;
      routePointer(global, InputEvent::Kind::PointerMove);
      break;
    }
    case LIBINPUT_EVENT_POINTER_BUTTON: {
      auto* pointer = libinput_event_get_pointer_event(event);
      std::uint32_t button = libinput_event_pointer_get_button(pointer);
      bool pressed = libinput_event_pointer_get_button_state(pointer) == LIBINPUT_BUTTON_STATE_PRESSED;
      emitRawInput({.kind = platform::KmsInputEvent::Kind::PointerButton,
                    .button = button,
                    .pressed = pressed,
                    .timeMs = libinput_event_pointer_get_time(pointer)});
      std::uint8_t bit = buttonMaskBit(button);
      if (pressed) pressedButtons_ |= bit;
      else pressedButtons_ &= static_cast<std::uint8_t>(~bit);
      routePointer(pointerPos_, pressed ? InputEvent::Kind::PointerDown : InputEvent::Kind::PointerUp,
                   mouseButtonFromLinux(button));
      break;
    }
    case LIBINPUT_EVENT_POINTER_AXIS: {
      auto* pointer = libinput_event_get_pointer_event(event);
      Vec2 delta{};
      if (libinput_event_pointer_has_axis(pointer, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL)) {
        delta.x = static_cast<float>(libinput_event_pointer_get_axis_value(pointer,
                                                                           LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL));
      }
      if (libinput_event_pointer_has_axis(pointer, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL)) {
        delta.y = static_cast<float>(libinput_event_pointer_get_axis_value(pointer,
                                                                           LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL));
      }
      emitRawInput({.kind = platform::KmsInputEvent::Kind::PointerAxis,
                    .dx = delta.x,
                    .dy = delta.y,
                    .timeMs = libinput_event_pointer_get_time(pointer)});
      routePointer(pointerPos_, InputEvent::Kind::Scroll, MouseButton::None, delta, true);
      break;
    }
    case LIBINPUT_EVENT_POINTER_SCROLL_WHEEL:
    case LIBINPUT_EVENT_POINTER_SCROLL_FINGER:
    case LIBINPUT_EVENT_POINTER_SCROLL_CONTINUOUS: {
      auto* pointer = libinput_event_get_pointer_event(event);
      Vec2 delta{};
      if (libinput_event_pointer_has_axis(pointer, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL)) {
        delta.x = static_cast<float>(libinput_event_pointer_get_scroll_value(pointer,
                                                                             LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL));
      }
      if (libinput_event_pointer_has_axis(pointer, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL)) {
        delta.y = static_cast<float>(libinput_event_pointer_get_scroll_value(pointer,
                                                                             LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL));
      }
      emitRawInput({.kind = platform::KmsInputEvent::Kind::PointerAxis,
                    .dx = delta.x,
                    .dy = delta.y,
                    .timeMs = libinput_event_pointer_get_time(pointer)});
      routePointer(pointerPos_, InputEvent::Kind::Scroll, MouseButton::None, delta, true);
      break;
    }
    case LIBINPUT_EVENT_KEYBOARD_KEY: {
      auto* keyboard = libinput_event_get_keyboard_event(event);
      bool pressed = libinput_event_keyboard_get_key_state(keyboard) == LIBINPUT_KEY_STATE_PRESSED;
      emitRawInput({.kind = platform::KmsInputEvent::Kind::Key,
                    .pressed = pressed,
                    .key = libinput_event_keyboard_get_key(keyboard),
                    .timeMs = libinput_event_keyboard_get_time(keyboard)});
      routeKey(libinput_event_keyboard_get_key(keyboard), pressed);
      break;
    }
    case LIBINPUT_EVENT_TOUCH_DOWN:
    case LIBINPUT_EVENT_TOUCH_MOTION:
    case LIBINPUT_EVENT_TOUCH_UP: {
      auto* touch = libinput_event_get_touch_event(event);
      Size rawSize(1.f, 1.f);
      if (!connectors_.empty()) {
        drmModeModeInfo const& mode = connectors_.front().mode;
        rawSize = Size(static_cast<float>(std::max(1, static_cast<int>(mode.hdisplay))),
                       static_cast<float>(std::max(1, static_cast<int>(mode.vdisplay))));
      }
      double const rawX = libinput_event_touch_get_x_transformed(
          touch, static_cast<std::uint32_t>(rawSize.width));
      double const rawY = libinput_event_touch_get_y_transformed(
          touch, static_cast<std::uint32_t>(rawSize.height));
      emitRawInput({.kind = platform::KmsInputEvent::Kind::PointerPosition,
                    .x = rawX,
                    .y = rawY,
                    .timeMs = libinput_event_touch_get_time(touch)});
      if (libinput_event_get_type(event) == LIBINPUT_EVENT_TOUCH_DOWN ||
          libinput_event_get_type(event) == LIBINPUT_EVENT_TOUCH_UP) {
        emitRawInput({.kind = platform::KmsInputEvent::Kind::PointerButton,
                      .button = BTN_LEFT,
                      .pressed = libinput_event_get_type(event) == LIBINPUT_EVENT_TOUCH_DOWN,
                      .timeMs = libinput_event_touch_get_time(touch)});
      }
      KmsWindow* window = focusedWindow();
      if (!window) break;
      Size size = window->currentSize();
      Point p{static_cast<float>(libinput_event_touch_get_x_transformed(touch,
                                                                        static_cast<std::uint32_t>(size.width))),
              static_cast<float>(libinput_event_touch_get_y_transformed(touch,
                                                                        static_cast<std::uint32_t>(size.height)))};
      Point global = windowOrigin(window);
      global.x += p.x;
      global.y += p.y;
      if (libinput_event_get_type(event) == LIBINPUT_EVENT_TOUCH_DOWN) {
        pressedButtons_ |= 1u;
        routePointer(global, InputEvent::Kind::PointerMove);
        routePointer(global, InputEvent::Kind::PointerDown, MouseButton::Left);
      } else if (libinput_event_get_type(event) == LIBINPUT_EVENT_TOUCH_UP) {
        routePointer(pointerPos_, InputEvent::Kind::PointerUp, MouseButton::Left);
        pressedButtons_ &= static_cast<std::uint8_t>(~1u);
      } else {
        routePointer(global, InputEvent::Kind::PointerMove);
      }
      break;
    }
    default:
      if (debugKmsInput()) {
        std::fprintf(stderr, "[lambda:kms:input] ignored libinput event type %d\n",
                     static_cast<int>(libinput_event_get_type(event)));
      }
      break;
    }
    libinput_event_destroy(event);
  }
}

} // namespace lambdaui
