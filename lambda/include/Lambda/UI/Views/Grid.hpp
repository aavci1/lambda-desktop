#pragma once

/// \file Lambda/UI/Views/Grid.hpp
///
/// Part of the Lambda public API.

#include <Lambda/Layout/Alignment.hpp>
#include <Lambda/UI/Element.hpp>
#include <Lambda/UI/ViewModifiers.hpp>

#include <cstddef>
#include <vector>

namespace lambda {

/// Fixed-column grid: children flow left-to-right, top-to-bottom (row-major).
/// Use `.colSpan(...)` or `.rowSpan(...)` on child elements to occupy multiple adjacent tracks.
///
/// When the available width is unknown (`innerWidth == 0`), `cellW` is zero and each child is
/// measured with unbounded width. Views that expand to fill width (e.g. `Rectangle` with no
/// frame) may measure at zero width — the grid reports `{0, totalHeight}` until a parent assigns a
/// finite width, same idea as `VStack`.
///
/// `Spacer` has no flex axis here (unlike `VStack` / `HStack`), but it remains a valid child: it is
/// measured like other cells, occupies a full column in row-major order, and produces no output.
/// Slot / key indices stay aligned through the normal child measurement path.
///
/// For outer inset or clipping, use **`.padding(float)`** / **`.clipContent(bool)`** on the wrapping
/// `Element`.
struct Grid : ViewModifiers<Grid> {
  Size measure(MeasureContext&, LayoutConstraints const&, LayoutHints const&, TextSystem&) const;
  std::unique_ptr<scenegraph::SceneNode> mount(MountContext&) const;

  /// Number of columns. Values below 1 are clamped to 1 during layout.
  std::size_t columns = 2;
  /// Gap between columns (horizontal) and between rows (vertical).
  float horizontalSpacing = 8.f;
  float verticalSpacing = 8.f;
  /// Alignment of each child within its cell when the child is narrower or shorter than the cell.
  Alignment horizontalAlignment = Alignment::Center;
  Alignment verticalAlignment = Alignment::Center;
  /// Children in row-major order (left-to-right, top-to-bottom).
  std::vector<Element> children;

};

} // namespace lambda
