#pragma once

/// \file Lambda/UI/Views/Rectangle.hpp
///
/// Axis-aligned rounded rectangle primitive; fill, stroke, and shadow come from \ref Element modifiers
/// (\c fill, \c stroke, \c shadow).

#include <Lambda/Core/Geometry.hpp>
#include <Lambda/UI/Detail/PrimitiveForwards.hpp>
#include <Lambda/UI/ViewModifiers.hpp>

namespace lambda {

/// Axis-aligned rounded rect leaf. Size, layout-space position, flex, corners, paint, and interaction use
/// \ref Element / \ref ViewModifiers (e.g. \c size, \c position, \c fill, \c stroke).
struct Rectangle : ViewModifiers<Rectangle> {
  Size measure(MeasureContext&, LayoutConstraints const&, LayoutHints const&, TextSystem&) const;

  constexpr bool operator==(Rectangle const&) const noexcept { return true; }
};

} // namespace lambda
