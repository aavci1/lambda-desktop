#pragma once

/// \file Lambda/UI/Views/PopoverCalloutShape.hpp
///
/// Part of the Lambda public API.


#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Core/Color.hpp>
#include <Lambda/Graphics/Styles.hpp>
#include <Lambda/Reactive/Bindable.hpp>
#include <Lambda/UI/Element.hpp>
#include <Lambda/UI/Views/PopoverPlacement.hpp>
#include <Lambda/UI/Views/Rectangle.hpp>

#include <memory>
#include <optional>

namespace lambdaui {

class MountContext;
namespace scenegraph {
class SceneNode;
} // namespace scenegraph

/// Single filled/stroked path: rounded card + optional callout triangle (merged outline).
struct PopoverCalloutShape : ViewModifiers<PopoverCalloutShape> {
  Size measure(MeasureContext&, LayoutConstraints const&, LayoutHints const&, TextSystem&) const;
  std::unique_ptr<scenegraph::SceneNode> mount(MountContext&) const;

  /// Side on which the callout arrow is attached.
  Reactive::Bindable<PopoverPlacement> placement{PopoverPlacement::Below};
  /// Draws the callout arrow when true.
  bool arrow = true;
  /// Inset between the outer shape and `content`.
  float padding = 12.f;
  /// Card corner radii.
  CornerRadius cornerRadius{10.f};
  /// Card fill color.
  Color backgroundColor = Color::hex(0xFFFFFF);
  /// Card border color.
  Color borderColor = Color::hex(0xE0E0E6);
  /// Card border thickness.
  float borderWidth = 1.f;
  /// Optional maximum content size before clipping / internal layout constraints.
  std::optional<Size> maxSize{};
  /// Content rendered inside the popover chrome.
  Element content{Rectangle{}};

  static constexpr float kArrowW = 16.f;
  static constexpr float kArrowH = 8.f;
};

} // namespace lambdaui
