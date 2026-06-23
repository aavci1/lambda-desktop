#pragma once

#include <Lambda/SceneGraph/SceneNode.hpp>

#include <algorithm>

namespace lambdaui::scenegraph::detail {

inline bool rectEmpty(Rect const& rect) noexcept {
  return rect.width == 0.f && rect.height == 0.f;
}

inline Rect unionRect(Rect lhs, Rect rhs) noexcept {
  if (rectEmpty(lhs)) {
    return rhs;
  }
  if (rectEmpty(rhs)) {
    return lhs;
  }
  float const x0 = std::min(lhs.x, rhs.x);
  float const y0 = std::min(lhs.y, rhs.y);
  float const x1 = std::max(lhs.x + lhs.width, rhs.x + rhs.width);
  float const y1 = std::max(lhs.y + lhs.height, rhs.y + rhs.height);
  return Rect{x0, y0, x1 - x0, y1 - y0};
}

inline Rect transformBounds(Mat3 const& transform, Rect const& rect) noexcept {
  Point const p0 = transform.apply(Point{rect.x, rect.y});
  Point const p1 = transform.apply(Point{rect.x + rect.width, rect.y});
  Point const p2 = transform.apply(Point{rect.x, rect.y + rect.height});
  Point const p3 = transform.apply(Point{rect.x + rect.width, rect.y + rect.height});
  float const minX = std::min(std::min(p0.x, p1.x), std::min(p2.x, p3.x));
  float const minY = std::min(std::min(p0.y, p1.y), std::min(p2.y, p3.y));
  float const maxX = std::max(std::max(p0.x, p1.x), std::max(p2.x, p3.x));
  float const maxY = std::max(std::max(p0.y, p1.y), std::max(p2.y, p3.y));
  return Rect{minX, minY, maxX - minX, maxY - minY};
}

inline Rect subtreeLocalVisualBounds(SceneNode const& node) noexcept {
  if (node.cachedSubtreeVisualBounds_) {
    return *node.cachedSubtreeVisualBounds_;
  }

  Rect bounds = node.localBounds();
  for (std::unique_ptr<SceneNode> const& child : node.children()) {
    Rect const childBounds =
        transformBounds(Mat3::translate(child->position()) * child->transform(),
                        subtreeLocalVisualBounds(*child));
    bounds = unionRect(bounds, childBounds);
  }
  node.cachedSubtreeVisualBounds_ = bounds;
  return bounds;
}

} // namespace lambdaui::scenegraph::detail
