#include <Lambda/SceneGraph/PathNode.hpp>
#include <Lambda/SceneGraph/Renderer.hpp>

#include <utility>

namespace lambda::scenegraph {

PathNode::PathNode(Rect bounds, Path path, FillStyle fill, StrokeStyle stroke, ShadowStyle shadow)
    : SceneNode(SceneNodeKind::Path, bounds)
    , path_(std::move(path))
    , fill_(std::move(fill))
    , stroke_(std::move(stroke))
    , shadow_(std::move(shadow)) {}

PathNode::~PathNode() = default;

Path const& PathNode::path() const noexcept {
    return path_;
}

FillStyle const& PathNode::fill() const noexcept {
    return fill_;
}

StrokeStyle const& PathNode::stroke() const noexcept {
    return stroke_;
}

ShadowStyle const& PathNode::shadow() const noexcept {
    return shadow_;
}

void PathNode::setPath(Path pathValue) {
    if (path_.contentHash() == pathValue.contentHash()) {
        return;
    }
    path_ = std::move(pathValue);
    markDirty();
}

void PathNode::setFill(FillStyle fillValue) {
    if (fill_ == fillValue) {
        return;
    }
    fill_ = std::move(fillValue);
    markDirty();
}

void PathNode::setStroke(StrokeStyle strokeValue) {
    if (stroke_ == strokeValue) {
        return;
    }
    stroke_ = std::move(strokeValue);
    markDirty();
}

void PathNode::setShadow(ShadowStyle shadowValue) {
    if (shadow_ == shadowValue) {
        return;
    }
    shadow_ = std::move(shadowValue);
    markDirty();
}

Rect PathNode::localBounds() const noexcept {
    return path_.getBounds();
}

void PathNode::render(Renderer& renderer) const {
    renderer.drawPath(path_, fill_, stroke_, shadow_);
}

} // namespace lambda::scenegraph
