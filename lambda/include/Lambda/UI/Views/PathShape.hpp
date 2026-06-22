#pragma once

/// \file Lambda/UI/Views/PathShape.hpp
///
/// Part of the Lambda public API.


#include <Lambda/Graphics/Path.hpp>
#include <Lambda/UI/Detail/PrimitiveForwards.hpp>
#include <Lambda/UI/ViewModifiers.hpp>

namespace lambda {

/// Scene path primitive (name avoids clashing with `lambda::Path`). Fill, stroke, and shadow use \ref Element
/// modifiers (\c fill, \c stroke, \c shadow).
struct PathShape : ViewModifiers<PathShape> {
  Size measure(MeasureContext&, LayoutConstraints const&, LayoutHints const&, TextSystem&) const;

  /// Vector path to render in local coordinates.
  Path path{};

  bool operator==(PathShape const& other) const {
    return path == other.path;
  }
};

} // namespace lambda
