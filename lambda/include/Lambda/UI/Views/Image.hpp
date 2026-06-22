#pragma once

/// \file Lambda/UI/Views/Image.hpp
///
/// Part of the Lambda public API.


#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Graphics/Image.hpp>
#include <Lambda/Graphics/ImageFillMode.hpp>
#include <Lambda/UI/Detail/PrimitiveForwards.hpp>
#include <Lambda/UI/ViewModifiers.hpp>

#include <memory>

namespace lambda {
class MountContext;
namespace scenegraph {
class SceneNode;
}
} // namespace lambda

namespace lambda::views {

/// Image view component. `source` references `lambda::Image` (bitmap); distinct from this `Image` view type.
/// Use \ref Element modifiers for interaction, size, opacity, and rounded corners.
struct Image : ViewModifiers<Image> {
  ::lambda::Size measure(::lambda::MeasureContext&, ::lambda::LayoutConstraints const&, ::lambda::LayoutHints const&,
                       ::lambda::TextSystem&) const;
  std::unique_ptr<::lambda::scenegraph::SceneNode> mount(::lambda::MountContext&) const;

  /// Source bitmap to display.
  std::shared_ptr<lambda::Image> source;
  /// How the bitmap fits inside the resolved frame.
  ImageFillMode fillMode = ImageFillMode::Cover;

  bool operator==(Image const& other) const {
    return source == other.source && fillMode == other.fillMode;
  }
};

} // namespace lambda::views
