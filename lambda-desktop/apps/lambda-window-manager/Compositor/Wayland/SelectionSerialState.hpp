#pragma once

#include "Compositor/Wayland/WaylandServerImpl.hpp"

#include <array>

namespace lambda::compositor {

inline constexpr std::array kDataDeviceDragStartSerialKinds{
    SeatSerialKind::PointerButtonPress,
};

inline constexpr std::array kSelectionSetSerialKinds{
    SeatSerialKind::KeyboardEnter,
    SeatSerialKind::KeyboardKey,
    SeatSerialKind::PointerButtonPress,
};

inline bool dataDeviceStartDragAcceptsSerialKind(SeatSerialKind kind) {
  return kind == SeatSerialKind::PointerButtonPress;
}

inline bool selectionSetAcceptsSerialKind(SeatSerialKind kind) {
  return kind == SeatSerialKind::KeyboardEnter ||
         kind == SeatSerialKind::KeyboardKey ||
         kind == SeatSerialKind::PointerButtonPress;
}

} // namespace lambda::compositor
