#include <Lambda/SceneGraph/RectNode.hpp>
#include <Lambda/SceneGraph/Renderer.hpp>

#include <algorithm>
#include <utility>

namespace lambdaui::scenegraph {

namespace {

Rect expandForShadow(Rect rect, ShadowStyle const &shadow) noexcept {
    float const blur = shadow.isNone() ? 0.f : shadow.radius;
    float const left = std::max(0.f, blur - shadow.offset.x);
    float const right = std::max(0.f, blur + shadow.offset.x);
    float const top = std::max(0.f, blur - shadow.offset.y);
    float const bottom = std::max(0.f, blur + shadow.offset.y);
    rect.x -= left;
    rect.y -= top;
    rect.width += left + right;
    rect.height += top + bottom;
    return rect;
}

Rect insetRect(Rect rect, float inset) noexcept {
    rect.x += inset;
    rect.y += inset;
    rect.width = std::max(0.f, rect.width - 2.f * inset);
    rect.height = std::max(0.f, rect.height - 2.f * inset);
    return rect;
}

CornerRadius insetCornerRadius(CornerRadius radius, float inset) noexcept {
    radius.topLeft = std::max(0.f, radius.topLeft - inset);
    radius.topRight = std::max(0.f, radius.topRight - inset);
    radius.bottomRight = std::max(0.f, radius.bottomRight - inset);
    radius.bottomLeft = std::max(0.f, radius.bottomLeft - inset);
    return radius;
}

} // namespace

RectNode::RectNode(Rect bounds, FillStyle fill, StrokeStyle stroke, CornerRadius cornerRadius,
                   ShadowStyle shadow)
    : SceneNode(SceneNodeKind::Rect, bounds)
    , fill_(std::move(fill))
    , stroke_(std::move(stroke))
    , cornerRadius_(cornerRadius)
    , shadow_(shadow) {}

RectNode::~RectNode() = default;

FillStyle const &RectNode::fill() const noexcept {
    return fill_;
}

StrokeStyle const &RectNode::stroke() const noexcept {
    return stroke_;
}

CornerRadius RectNode::cornerRadius() const noexcept {
    return cornerRadius_;
}

ShadowStyle const &RectNode::shadow() const noexcept {
    return shadow_;
}

bool RectNode::clipsContents() const noexcept {
    return clipsContents_;
}

float RectNode::opacity() const noexcept {
    return opacity_;
}

void RectNode::setFill(FillStyle fill) {
    if (fill_ == fill) {
        return;
    }
    fill_ = std::move(fill);
    markDirty();
}

void RectNode::setStroke(StrokeStyle stroke) {
    if (stroke_ == stroke) {
        return;
    }
    stroke_ = std::move(stroke);
    markDirty();
}

void RectNode::setCornerRadius(CornerRadius cornerRadius) {
    if (cornerRadius_ == cornerRadius) {
        return;
    }
    cornerRadius_ = cornerRadius;
    markDirty();
}

void RectNode::setShadow(ShadowStyle shadow) {
    if (shadow_ == shadow) {
        return;
    }
    shadow_ = shadow;
    markDirty();
}

void RectNode::setClipsContents(bool clipsContents) noexcept {
    if (clipsContents_ == clipsContents) {
        return;
    }
    clipsContents_ = clipsContents;
    markSubtreeDirty();
}

void RectNode::setOpacity(float opacity) noexcept {
    float const next = std::clamp(opacity, 0.f, 1.f);
    if (opacity_ == next) {
        return;
    }
    opacity_ = next;
    markSubtreeDirty();
}

Rect RectNode::localBounds() const noexcept {
    return expandForShadow(Rect::sharp(0.f, 0.f, size().width, size().height), shadow_);
}

void RectNode::render(Renderer &renderer) const {
    Rect const bounds = Rect::sharp(0.f, 0.f, size().width, size().height);
    renderer.drawRect(bounds, cornerRadius_, fill_, StrokeStyle::none(), shadow_);
    if (stroke_.isNone() || stroke_.width <= 0.f) {
        return;
    }
    float const strokeInset = stroke_.width * 0.5f;
    renderer.drawRect(insetRect(bounds, strokeInset),
                      insetCornerRadius(cornerRadius_, strokeInset),
                      FillStyle::none(),
                      stroke_,
                      ShadowStyle::none());
}

} // namespace lambdaui::scenegraph
