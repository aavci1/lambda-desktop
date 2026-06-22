#pragma once

/// \file Lambda/UI/Cursor.hpp
///
/// Public UI cursor value type.

#include <cstdint>

namespace lambda {

enum class Cursor : std::uint8_t {
  Inherit,
  Arrow,
  IBeam,
  Hand,
  ResizeEW,
  ResizeNS,
  ResizeNESW,
  ResizeNWSE,
  ResizeAll,
  Crosshair,
  NotAllowed,
};

} // namespace lambda
