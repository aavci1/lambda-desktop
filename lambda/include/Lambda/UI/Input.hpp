#pragma once

/// \file Lambda/UI/Input.hpp
///
/// Public UI input value types.

#include <cstdint>

namespace lambda {

enum class FocusInputKind : std::uint8_t { Pointer, Keyboard };

enum class MouseButton : std::uint8_t { None, Left, Right, Middle, Other };

using KeyCode = std::uint16_t;

enum class Modifiers : std::uint32_t {
  None = 0,
  Shift = 1 << 0,
  Ctrl = 1 << 1,
  Alt = 1 << 2,
  Meta = 1 << 3,
};

constexpr Modifiers operator|(Modifiers a, Modifiers b) {
  return static_cast<Modifiers>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

constexpr Modifiers operator&(Modifiers a, Modifiers b) {
  return static_cast<Modifiers>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}

constexpr bool any(Modifiers m) {
  return static_cast<std::uint32_t>(m) != 0;
}

} // namespace lambda
