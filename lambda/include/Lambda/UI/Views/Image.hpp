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

namespace lambdaui {
class MountContext;
namespace scenegraph {
class SceneNode;
}
} // namespace lambdaui

namespace lambdaui::views {

/// Image view component. `source` references `lambdaui::Image` (bitmap); distinct from this `Image` view type.
/// Use \ref Element modifiers for interaction, size, opacity, and rounded corners.
struct Image : ViewModifiers<Image> {
  ::lambdaui::Size measure(::lambdaui::MeasureContext&, ::lambdaui::LayoutConstraints const&, ::lambdaui::LayoutHints const&,
                       ::lambdaui::TextSystem&) const;
  std::unique_ptr<::lambdaui::scenegraph::SceneNode> mount(::lambdaui::MountContext&) const;

  /// Source bitmap to display.
  std::shared_ptr<lambdaui::Image> source;
  /// How the bitmap fits inside the resolved frame.
  ImageFillMode fillMode = ImageFillMode::Cover;

  bool operator==(Image const& other) const {
    return source == other.source && fillMode == other.fillMode;
  }
};

} // namespace lambdaui::views
