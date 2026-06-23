#include "Compositor/Wayland/PointerExtensionState.hpp"

#include <doctest/doctest.h>

#include <cstdint>

namespace lambdaui::compositor {

TEST_CASE("pointer extension resources use implemented protocol versions") {
  CHECK(kRelativePointerVersion == 1);
  CHECK(relativePointerResourceVersion(1) == 1);
  CHECK(relativePointerResourceVersion(2) == 1);

  CHECK(kPointerConstraintsVersion == 1);
  CHECK(pointerConstraintsResourceVersion(1) == 1);
  CHECK(pointerConstraintsResourceVersion(2) == 1);
}

TEST_CASE("pointer extension resources are matched by dependent wl_pointer") {
  auto* pointerA = reinterpret_cast<wl_resource*>(static_cast<std::uintptr_t>(1));
  auto* pointerB = reinterpret_cast<wl_resource*>(static_cast<std::uintptr_t>(2));

  WaylandServer::Impl::RelativePointer relativePointer{};
  CHECK_FALSE(relativePointerUsesPointer(nullptr, pointerA));
  CHECK_FALSE(relativePointerUsesPointer(&relativePointer, pointerA));
  relativePointer.pointer = pointerA;
  CHECK(relativePointerUsesPointer(&relativePointer, pointerA));
  CHECK_FALSE(relativePointerUsesPointer(&relativePointer, pointerB));

  WaylandServer::Impl::PointerConstraint constraint{};
  CHECK_FALSE(pointerConstraintUsesPointer(nullptr, pointerA));
  CHECK_FALSE(pointerConstraintUsesPointer(&constraint, pointerA));
  constraint.pointer = pointerA;
  CHECK(pointerConstraintUsesPointer(&constraint, pointerA));
  CHECK_FALSE(pointerConstraintUsesPointer(&constraint, pointerB));
}

} // namespace lambdaui::compositor
