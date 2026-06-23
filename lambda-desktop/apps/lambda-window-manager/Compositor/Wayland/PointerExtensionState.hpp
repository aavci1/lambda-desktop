#pragma once

#include "Compositor/Wayland/WaylandServerImpl.hpp"

#include <algorithm>
#include <cstdint>

namespace lambdaui::compositor {

inline constexpr std::uint32_t kRelativePointerVersion = 1;
inline constexpr std::uint32_t kPointerConstraintsVersion = 1;

[[nodiscard]] inline std::uint32_t relativePointerResourceVersion(std::uint32_t boundVersion) {
  return std::min(boundVersion, kRelativePointerVersion);
}

[[nodiscard]] inline std::uint32_t pointerConstraintsResourceVersion(std::uint32_t boundVersion) {
  return std::min(boundVersion, kPointerConstraintsVersion);
}

[[nodiscard]] inline bool relativePointerUsesPointer(WaylandServer::Impl::RelativePointer const* relativePointer,
                                                     wl_resource* pointerResource) {
  return relativePointer && relativePointer->pointer == pointerResource;
}

[[nodiscard]] inline bool pointerConstraintUsesPointer(WaylandServer::Impl::PointerConstraint const* constraint,
                                                       wl_resource* pointerResource) {
  return constraint && constraint->pointer == pointerResource;
}

} // namespace lambdaui::compositor
