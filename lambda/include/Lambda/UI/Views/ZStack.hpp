#pragma once

/// \file Lambda/UI/Views/ZStack.hpp
///
/// Part of the Lambda public API.


#include <Lambda/Layout/Alignment.hpp>
#include <Lambda/UI/Element.hpp>

#include <vector>

namespace lambdaui {

/// Overlays children in one stack. There is no padding field on `ZStack`: inset via **`.padding()`** on
/// the wrapping `Element`, another stack, or the outer frame in the tree if you need margins.
///
/// Layout uses the parent’s proposed size when finite; each child is measured against that box, then
/// the stack’s reported size is `max` of that inner box and the largest child on each axis (after the
/// same fallback when an axis is unknown). That matches `build`, where each child frame uses
/// `max(intrinsic, inner)` so ancestors see the true footprint (e.g. scrollable content taller than
/// the viewport proposal).
///
/// During `build`, the shared inner width/height uses the same `max(proposed, largest child)` rule
/// as `measure` before laying out children. Each child receives that full box as its assigned size,
/// so nested layouts resolve against the actual container space instead of shrink-wrapping by
/// default. Alignment is then applied against the child’s resolved frame within that shared box.
///
/// **Overlay composition:** siblings that share one coordinate system (e.g. a track `Rectangle` and a
/// thumb `Rectangle` with `frame` positions relative to each other) should use `Start` / `Start`
/// so every layer keeps the same origin. Set the alignments to `Center` when you want to center
/// independent children (e.g. a label over a full-bleed background).
///
/// To clip children to the stack’s bounds (e.g. scroll viewport), chain **`.clipContent(true)`** on the
/// `Element` that wraps this `ZStack` (same as other views).
struct ZStack : ViewModifiers<ZStack> {
  Size measure(MeasureContext&, LayoutConstraints const&, LayoutHints const&, TextSystem&) const;

  Alignment horizontalAlignment = Alignment::Start;
  Alignment verticalAlignment = Alignment::Start;
  std::vector<Element> children;

};

} // namespace lambdaui
