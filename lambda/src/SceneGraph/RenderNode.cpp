#include <Lambda/SceneGraph/RenderNode.hpp>

#include <Lambda/SceneGraph/Renderer.hpp>

#include <utility>

namespace lambda::scenegraph {

RenderNode::RenderNode(Rect bounds, DrawFunction draw)
    : SceneNode(SceneNodeKind::Render, bounds), draw_(std::move(draw)) {}

RenderNode::~RenderNode() = default;

RenderNode::DrawFunction const &RenderNode::draw() const noexcept {
    return draw_;
}

RenderNode::Purity RenderNode::purity() const noexcept {
    return purity_;
}

void RenderNode::setDraw(DrawFunction drawValue) {
    draw_ = std::move(drawValue);
    purity_ = Purity::Unknown;
    markDirty();
}

void RenderNode::setPurity(Purity purityValue) {
    if (purity_ == purityValue) {
        return;
    }
    purity_ = purityValue;
    markDirty();
}

void RenderNode::invalidate() {
    markDirty();
}

void RenderNode::render(Renderer &renderer) const {
    Canvas *canvas = renderer.canvas();
    if (!canvas || !draw_) {
        return;
    }
    draw_(*canvas, localBounds());
}

bool RenderNode::canPrepareRenderOps() const noexcept {
    return purity_ == Purity::Pure;
}

} // namespace lambda::scenegraph
