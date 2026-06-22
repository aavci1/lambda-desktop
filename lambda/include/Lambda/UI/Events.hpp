#pragma once

/// \file Lambda/UI/Events.hpp
///
/// Part of the Lambda public API.


#include <Lambda/Core/Geometry.hpp>
#include <Lambda/UI/Input.hpp>

#include <any>
#include <cstdint>
#include <string>
#include <variant>

namespace lambda {

class Window;

struct WindowLifecycleEvent {
  enum class Kind : std::uint8_t { Registered, Unregistered, OutputAdded, OutputRemoved };
  Kind kind = Kind::Registered;
  /// Stable handle from the platform window; valid for window registration events.
  unsigned int handle = 0;
  /// Valid only when `kind == Registered` (during `Window` construction).
  Window* window = nullptr;
  /// Valid for output lifecycle events on platforms that expose named outputs.
  std::string outputName;
};

struct WindowEvent {
  enum class Kind : std::uint8_t { Resize, FocusGained, FocusLost, DpiChanged, CloseRequest };
  Kind kind = Kind::Resize;
  unsigned int handle = 0;
  Size size{};
  /// Uniform DPI scale retained for source compatibility. Prefer dpiX/dpiY for new platform code.
  float dpi = 1.0f;
  float dpiX = 1.0f;
  float dpiY = 1.0f;
};

struct InputEvent {
  enum class Kind : std::uint8_t {
    PointerEnter,
    PointerLeave,
    PointerMove,
    PointerDown,
    PointerUp,
    Scroll,
    KeyDown,
    KeyUp,
    TextInput,
    TouchBegin,
    TouchMove,
    TouchEnd
  };
  Kind kind = Kind::PointerMove;
  unsigned int handle = 0;
  Vec2 position{};
  /// Wheel / trackpad deltas when `kind == Scroll`; unused otherwise.
  Vec2 scrollDelta{};
  /// When `kind == Scroll`: true if deltas are in logical pixels (trackpad); false if line units
  /// (mouse wheel) and should be scaled before use (see Runtime scroll handling).
  bool preciseScrollDelta = true;
  MouseButton button = MouseButton::None;
  KeyCode key = 0;
  Modifiers modifiers = Modifiers::None;
  /// Bitmask of mouse buttons currently held at the time of this event.
  /// Bit 0 = primary (left), bit 1 = secondary (right), bit 2 = middle.
  /// Populated on pointer and scroll events from platforms that support it; 0 otherwise.
  std::uint8_t pressedButtons = 0;
  /// Backend input serial for operations that must be tied to the triggering user event.
  /// Wayland uses this for xdg_toplevel.move/resize. Other backends leave it at 0.
  std::uint32_t platformSerial = 0;
  std::string text{};
};

/// Posted when an `Application`-scheduled repeating timer fires (main queue / run loop).
struct TimerEvent {
  /// Monotonic steady-clock instant when the timer delivered (nanoseconds since `steady_clock` epoch).
  std::int64_t deadlineNanos = 0;
  /// Opaque id from `Application::scheduleRepeatingTimer`; pass to `Application::cancelTimer`.
  std::uint64_t timerId = 0;
  /// Optional window association; `0` if none. Useful for routing redraws to a specific window.
  unsigned int windowHandle = 0;
};

/// Posted by the platform frame pump when a window reaches a present boundary (typically display vsync).
struct FrameEvent {
  /// Monotonic steady-clock instant associated with this frame boundary.
  std::int64_t deadlineNanos = 0;
  /// Window handle that should be considered ready for present.
  unsigned int windowHandle = 0;
};

struct CustomEvent {
  std::uint32_t type = 0;
  std::any payload{};
};

using Event = std::variant<WindowLifecycleEvent, WindowEvent, InputEvent, TimerEvent, FrameEvent, CustomEvent>;

} // namespace lambda
