#pragma once

#include <Lambda/Core/Geometry.hpp>
#include <Lambda/SceneGraph/SceneNode.hpp>

namespace lambda::detail {

inline void setLayoutPosition(scenegraph::SceneNode& node, Point origin) {
  Point const childOffset = node.position();
  node.setPosition(Point{origin.x + childOffset.x, origin.y + childOffset.y});
}

} // namespace lambda::detail
