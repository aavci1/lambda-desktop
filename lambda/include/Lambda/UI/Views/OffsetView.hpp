#pragma once

/// \file Lambda/UI/Views/OffsetView.hpp
///
/// Part of the Lambda public API.


#include <Lambda/Core/Geometry.hpp>
#include <Lambda/UI/Element.hpp>
#include <Lambda/UI/Hooks.hpp>

#include <vector>

namespace lambda {

enum class ScrollAxis { Vertical, Horizontal, Both };

/// Internal: applies a translation to scroll content. Used by `ScrollView`.
/// Children are stacked along the scroll axis with no extra gap between siblings; use a single
/// `VStack`/`HStack` (or similar) as the child when you need spacing.
struct OffsetView : ViewModifiers<OffsetView> {
  Size measure(MeasureContext&, LayoutConstraints const&, LayoutHints const&, TextSystem&) const;
  std::unique_ptr<scenegraph::SceneNode> mount(MountContext&) const;

  /// Content translation in local coordinates.
  Point offset{};
  /// Which axes participate in scrolling / stacking.
  ScrollAxis axis = ScrollAxis::Vertical;
  /// Resolved viewport size written during layout.
  Signal<Size> viewportSize{};
  /// Resolved content size written during layout.
  Signal<Size> contentSize{};
  /// Children translated by `offset`.
  std::vector<Element> children;

};

} // namespace lambda
