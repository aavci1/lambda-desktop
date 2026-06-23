#pragma once

/// \file Lambda/UI/Views/ScaleAroundCenter.hpp
///
/// Part of the Lambda public API.


#include <Lambda/UI/Element.hpp>
#include <Lambda/Reactive/Bindable.hpp>

namespace lambdaui {

/// Scales a single child around the center of the layout slot (used for press feedback).
struct ScaleAroundCenter : ViewModifiers<ScaleAroundCenter> {
  Size measure(MeasureContext&, LayoutConstraints const&, LayoutHints const&, TextSystem&) const;
  std::unique_ptr<scenegraph::SceneNode> mount(MountContext&) const;

  /// Uniform scale multiplier applied before per-axis overrides.
  Reactive::Bindable<float> scale{1.f};
  /// Horizontal scale multiplier.
  Reactive::Bindable<float> scaleX{1.f};
  /// Vertical scale multiplier.
  Reactive::Bindable<float> scaleY{1.f};
  /// Content to transform.
  Element child;

};

} // namespace lambdaui
