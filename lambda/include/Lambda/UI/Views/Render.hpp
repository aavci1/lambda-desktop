#pragma once

/// \file Lambda/UI/Views/Render.hpp
///
/// Custom draw leaf for the UI tree. Painting runs through the active Canvas renderer.

#include <Lambda/Graphics/Canvas.hpp>
#include <Lambda/UI/Detail/PrimitiveForwards.hpp>
#include <Lambda/Layout/LayoutEngine.hpp>
#include <Lambda/UI/ViewModifiers.hpp>

#include <functional>
#include <memory>

namespace lambda {

class MountContext;
namespace scenegraph {
class SceneNode;
}

struct Render : ViewModifiers<Render> {
  /// Custom measurement callback. Return the retained leaf's desired size for the given constraints.
  std::function<Size(LayoutConstraints const&, LayoutHints const&)> measureFn{};
  /// Paint callback. Called with the node's local canvas and resolved frame.
  /// Reactive reads inside the draw must use `evaluate()` (or another tracked read such as `get()`).
  /// `peek()` reads do not register dependencies and can cause stale rendering when cached.
  std::function<void(Canvas&, Rect)> draw{};

  bool operator==(Render const& other) const {
    return static_cast<bool>(measureFn) == static_cast<bool>(other.measureFn) &&
           static_cast<bool>(draw) == static_cast<bool>(other.draw);
  }

  Size measure(MeasureContext& ctx, LayoutConstraints const& constraints, LayoutHints const& hints,
               TextSystem& textSystem) const;
  std::unique_ptr<scenegraph::SceneNode> mount(MountContext& ctx) const;

  Size measure(LayoutConstraints const& constraints, LayoutHints const& hints) const {
    if (measureFn) {
      return measureFn(constraints, hints);
    }
    return {};
  }

  void render(Canvas& canvas, Rect frame) const {
    if (draw) {
      draw(canvas, frame);
    }
  }
};

} // namespace lambda
