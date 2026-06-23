#pragma once

/// \file Lambda/SceneGraph/SceneTraversal.hpp
///
/// Generic traversal and hit-testing helpers for the pure scenegraph.

#include <Lambda/SceneGraph/RectNode.hpp>
#include <Lambda/SceneGraph/SceneBounds.hpp>
#include <Lambda/SceneGraph/SceneNode.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace lambdaui::scenegraph {

namespace detail {

inline bool roundedRectContains(Rect const& rect, CornerRadius cornerRadius, Point p) {
  if (!rect.contains(p)) {
    return false;
  }
  if (cornerRadius.isZero()) {
    return true;
  }

  float const width = rect.width;
  float const height = rect.height;
  float const maxRadius = std::min(width, height) * 0.5f;
  cornerRadius.topLeft = std::min(cornerRadius.topLeft, maxRadius);
  cornerRadius.topRight = std::min(cornerRadius.topRight, maxRadius);
  cornerRadius.bottomRight = std::min(cornerRadius.bottomRight, maxRadius);
  cornerRadius.bottomLeft = std::min(cornerRadius.bottomLeft, maxRadius);

  auto fixEdge = [](float& a, float& b, float len) {
    if (a + b > len && len > 0.f) {
      float const scale = len / (a + b);
      a *= scale;
      b *= scale;
    }
  };
  fixEdge(cornerRadius.topLeft, cornerRadius.topRight, width);
  fixEdge(cornerRadius.bottomLeft, cornerRadius.bottomRight, width);
  fixEdge(cornerRadius.topLeft, cornerRadius.bottomLeft, height);
  fixEdge(cornerRadius.topRight, cornerRadius.bottomRight, height);

  auto pointInCorner = [&](float left, float top, float radius) {
    if (radius <= 0.f) {
      return true;
    }
    float const cx = left + radius;
    float const cy = top + radius;
    float const dx = p.x - cx;
    float const dy = p.y - cy;
    return dx * dx + dy * dy <= radius * radius;
  };

  float const right = rect.x + rect.width;
  float const bottom = rect.y + rect.height;
  if (p.x < rect.x + cornerRadius.topLeft && p.y < rect.y + cornerRadius.topLeft) {
    return pointInCorner(rect.x, rect.y, cornerRadius.topLeft);
  }
  if (p.x > right - cornerRadius.topRight && p.y < rect.y + cornerRadius.topRight) {
    return pointInCorner(right - cornerRadius.topRight * 2.f, rect.y, cornerRadius.topRight);
  }
  if (p.x > right - cornerRadius.bottomRight && p.y > bottom - cornerRadius.bottomRight) {
    return pointInCorner(right - cornerRadius.bottomRight * 2.f, bottom - cornerRadius.bottomRight * 2.f,
                         cornerRadius.bottomRight);
  }
  if (p.x < rect.x + cornerRadius.bottomLeft && p.y > bottom - cornerRadius.bottomLeft) {
    return pointInCorner(rect.x, bottom - cornerRadius.bottomLeft * 2.f, cornerRadius.bottomLeft);
  }
  return true;
}

} // namespace detail

inline Point pointInChildSpace(SceneNode const& child, Point point) {
  Mat3 const& transform = child.transform();
  if (transform.isTranslationOnly()) {
    Point const position = child.position();
    return Point{point.x - position.x - transform.m[6],
                 point.y - position.y - transform.m[7]};
  }
  Mat3 const childToParent = Mat3::translate(child.position()) * child.transform();
  return childToParent.inverse().apply(point);
}

template<typename Visitor>
void walkSceneGraph(SceneNode const& node, Visitor&& visitor) {
  visitor(node);
  for (std::unique_ptr<SceneNode> const& child : node.children()) {
    walkSceneGraph(*child, visitor);
  }
}

template<typename AcceptTarget>
std::optional<std::pair<SceneNode const*, Point>> hitTestNode(SceneNode const& node, Point point,
                                                              AcceptTarget&& acceptTarget) {
  Rect const localBounds = node.localBounds();
  bool const insideOwnBounds = localBounds.contains(point);

  if (node.kind() == SceneNodeKind::Rect) {
    RectNode const& rectNode = static_cast<RectNode const&>(node);
    if (rectNode.clipsContents() &&
        !detail::roundedRectContains(
            Rect::sharp(0.f, 0.f, node.size().width, node.size().height),
            rectNode.cornerRadius(), point)) {
      return std::nullopt;
    }
  }

  for (auto it = node.children().rbegin(); it != node.children().rend(); ++it) {
    Point const childPoint = pointInChildSpace(**it, point);
    Rect const childVisualBounds = detail::subtreeLocalVisualBounds(**it);
    if (childVisualBounds.width > 0.f && childVisualBounds.height > 0.f &&
        !childVisualBounds.contains(childPoint)) {
      continue;
    }
    if (auto hit = hitTestNode(**it, childPoint, std::forward<AcceptTarget>(acceptTarget))) {
      return hit;
    }
  }

  if (insideOwnBounds && acceptTarget(node)) {
    return std::pair<SceneNode const*, Point>{&node, point};
  }
  return std::nullopt;
}

inline std::optional<Point> localPointForNode(SceneNode const& node, Point point,
                                              SceneNode const* target) {
  if (&node == target) {
    return point;
  }
  for (std::unique_ptr<SceneNode> const& child : node.children()) {
    if (auto local = localPointForNode(*child, pointInChildSpace(*child, point), target)) {
      return local;
    }
  }
  return std::nullopt;
}

} // namespace lambdaui::scenegraph
