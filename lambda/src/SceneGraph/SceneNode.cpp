#include <Lambda/SceneGraph/SceneNode.hpp>

#include <Lambda/SceneGraph/Interaction.hpp>
#include <Lambda/SceneGraph/Renderer.hpp>

#include "SceneGraph/LayoutDebugHooks.hpp"
#include "SceneGraph/SceneNodeInternal.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace lambdaui::scenegraph {

namespace {

thread_local int gTransientRelayoutDepth = 0;
thread_local int gRelayoutDepth = 0;

bool sameLayoutScalar(float a, float b) noexcept {
    if (a == b) {
        return true;
    }
    if (!std::isfinite(a) || !std::isfinite(b)) {
        return false;
    }
    return std::fabs(a - b) <= 0.01f;
}

bool sameLayoutConstraints(LayoutConstraints const& a, LayoutConstraints const& b) noexcept {
    return sameLayoutScalar(a.maxWidth, b.maxWidth) &&
           sameLayoutScalar(a.maxHeight, b.maxHeight) &&
           sameLayoutScalar(a.minWidth, b.minWidth) &&
           sameLayoutScalar(a.minHeight, b.minHeight);
}

struct TransientRelayoutScope {
    explicit TransientRelayoutScope(bool enabled) noexcept : enabled_(enabled) {
        if (enabled_) {
            ++gTransientRelayoutDepth;
        }
    }

    ~TransientRelayoutScope() {
        if (enabled_) {
            --gTransientRelayoutDepth;
        }
    }

    bool enabled_ = false;
};

} // namespace

namespace detail {

bool isTransientRelayout() noexcept {
    return gTransientRelayoutDepth > 0;
}

} // namespace detail

std::string_view sceneNodeKindName(SceneNodeKind kind) noexcept {
    switch (kind) {
    case SceneNodeKind::Group:
        return "Group";
  case SceneNodeKind::RasterCache:
    return "RasterCache";
  case SceneNodeKind::Rect:
    return "Rect";
  case SceneNodeKind::Text:
    return "Text";
  case SceneNodeKind::Image:
    return "Image";
    case SceneNodeKind::Path:
    return "Path";
  case SceneNodeKind::Render:
    return "Render";
  }
    return "Unknown";
}

SceneNode::SceneNode(Rect bounds) : SceneNode(SceneNodeKind::Group, bounds) {}

SceneNode::SceneNode(SceneNodeKind kind, Rect bounds) : kind_(kind), bounds_(bounds) {}

SceneNode::~SceneNode() = default;

SceneNodeKind SceneNode::kind() const noexcept {
    return kind_;
}

Rect SceneNode::bounds() const noexcept {
    return bounds_;
}

Point SceneNode::position() const noexcept {
    return Point {bounds_.x, bounds_.y};
}

Size SceneNode::size() const noexcept {
    return Size {bounds_.width, bounds_.height};
}

Mat3 const &SceneNode::transform() const noexcept {
    return transform_;
}

bool SceneNode::isDirty() const noexcept {
    return ownPaintingDirty_;
}

bool SceneNode::isSubtreeDirty() const noexcept {
    return subtreeDirty_;
}

LayoutFlow SceneNode::layoutFlow() const noexcept {
    return layoutFlow_;
}

float SceneNode::layoutSpacing() const noexcept {
    return layoutSpacing_;
}

void SceneNode::setBounds(Rect bounds) {
    if (bounds_ == bounds) {
        return;
    }
    bool const positionChanged = bounds.x != bounds_.x ||
                                 bounds.y != bounds_.y;
    bool const sizeChanged = bounds.width != bounds_.width ||
                             bounds.height != bounds_.height;
    bounds_ = bounds;
    if (sizeChanged) {
        markDirty();
    } else if (positionChanged) {
        markSubtreeDirty();
    }
}

void SceneNode::setPosition(Point position) {
    if (bounds_.x == position.x && bounds_.y == position.y) {
        return;
    }
    bounds_.x = position.x;
    bounds_.y = position.y;
    markSubtreeDirty();
}

void SceneNode::setSize(Size size) {
    if (bounds_.width == size.width && bounds_.height == size.height) {
        return;
    }
    bounds_.width = size.width;
    bounds_.height = size.height;
    markDirty();
}

void SceneNode::setTransform(Mat3 const &transformValue) {
    if (std::equal(std::begin(transform_.m), std::end(transform_.m),
                   std::begin(transformValue.m))) {
        return;
    }
    transform_ = transformValue;
    markSubtreeDirty();
}

void SceneNode::setLayoutFlow(LayoutFlow flow) noexcept {
    layoutFlow_ = flow;
}

void SceneNode::setLayoutSpacing(float spacing) noexcept {
    layoutSpacing_ = std::max(0.f, spacing);
}

void SceneNode::setLayoutConstraints(LayoutConstraints constraints) noexcept {
    layoutConstraints_ = constraints;
    hasLayoutConstraints_ = true;
}

bool SceneNode::hasLayoutConstraints() const noexcept {
    return hasLayoutConstraints_;
}

LayoutConstraints SceneNode::layoutConstraints() const noexcept {
    return layoutConstraints_;
}

void SceneNode::setRelayout(RelayoutFn relayout) {
    relayout_ = std::move(relayout);
}

bool SceneNode::relayoutStoredConstraints() {
    if (!hasLayoutConstraints_) {
        return false;
    }
    return relayout(layoutConstraints_);
}

void SceneNode::invalidateSubtreeLayout() noexcept {
    markSubtreeDirty();
}

bool SceneNode::relayout(LayoutConstraints const& constraints, bool storeConstraints) {
    if (!relayout_) {
        return false;
    }
    bool const effectiveStoreConstraints = storeConstraints && gTransientRelayoutDepth == 0;
    if (effectiveStoreConstraints && hasLayoutConstraints_ &&
        sameLayoutConstraints(layoutConstraints_, constraints) && !isSubtreeDirty()) {
        return true;
    }
    if (effectiveStoreConstraints) {
        setLayoutConstraints(constraints);
    }
    bool const shouldDump = ::lambdaui::layoutDebugEnabled() && gRelayoutDepth == 0;
    ++gRelayoutDepth;
    TransientRelayoutScope const transientScope{!storeConstraints};
    relayout_(constraints);
    --gRelayoutDepth;
    if (shouldDump && gRelayoutDepth == 0) {
        layoutDebugDumpAttached(storeConstraints ? "scene-node-relayout"
                                                : "scene-node-transient-relayout");
    }
    return true;
}

SceneNode *SceneNode::parent() const noexcept {
    return parent_;
}

std::span<std::unique_ptr<SceneNode> const> SceneNode::children() const noexcept {
    return children_;
}

std::span<std::unique_ptr<SceneNode>> SceneNode::children() noexcept {
    return children_;
}

Interaction *SceneNode::interaction() noexcept {
    return interaction_.get();
}

Interaction const *SceneNode::interaction() const noexcept {
    return interaction_.get();
}

void SceneNode::setInteraction(std::unique_ptr<Interaction> interactionValue) {
    interaction_ = std::move(interactionValue);
}

void SceneNode::appendChild(std::unique_ptr<SceneNode> child) {
    if (!child) {
        throw std::invalid_argument("SceneNode child must not be null");
    }
    if (child->parent_) {
        throw std::invalid_argument("SceneNode child already has a parent");
    }
    child->parent_ = this;
    children_.push_back(std::move(child));
    markSubtreeDirty();
}

void SceneNode::insertChild(std::size_t index, std::unique_ptr<SceneNode> child) {
    if (index > children_.size()) {
        throw std::out_of_range("SceneNode::insertChild index out of range");
    }
    if (!child) {
        throw std::invalid_argument("SceneNode child must not be null");
    }
    if (child->parent_) {
        throw std::invalid_argument("SceneNode child already has a parent");
    }
    child->parent_ = this;
    children_.insert(children_.begin() + static_cast<std::ptrdiff_t>(index),
                           std::move(child));
    markSubtreeDirty();
}

std::unique_ptr<SceneNode> SceneNode::removeChild(SceneNode &child) {
    auto it = std::find_if(children_.begin(), children_.end(),
                           [&](std::unique_ptr<SceneNode> const &current) {
                               return current.get() == &child;
                           });
    if (it == children_.end()) {
        return nullptr;
    }

    std::unique_ptr<SceneNode> removed = std::move(*it);
    children_.erase(it);
    removed->parent_ = nullptr;
    markSubtreeDirty();
    return removed;
}

std::vector<std::unique_ptr<SceneNode>> SceneNode::releaseChildren() {
    std::vector<std::unique_ptr<SceneNode>> released = std::move(children_);
    for (std::unique_ptr<SceneNode> &child : released) {
        child->parent_ = nullptr;
    }
    children_.clear();
    markSubtreeDirty();
    return released;
}

void SceneNode::replaceChildren(std::vector<std::unique_ptr<SceneNode>> children) {
    for (std::unique_ptr<SceneNode> const &child : children) {
        if (!child) {
            throw std::invalid_argument("SceneNode child must not be null");
        }
        if (child->parent_) {
            throw std::invalid_argument("SceneNode child already has a parent");
        }
    }

    for (std::unique_ptr<SceneNode> &child : children_) {
        child->parent_ = nullptr;
    }
    children_.clear();
    children_.reserve(children.size());
    for (std::unique_ptr<SceneNode> &child : children) {
        child->parent_ = this;
        children_.push_back(std::move(child));
    }
    markSubtreeDirty();
}

Rect SceneNode::localBounds() const noexcept {
    return Rect::sharp(0.f, 0.f, bounds_.width, bounds_.height);
}

void SceneNode::render(Renderer &) const {}

bool SceneNode::canPrepareRenderOps() const noexcept {
    return true;
}

std::uint64_t SceneNode::preparedRenderOpsKey(float) const noexcept {
    return 0;
}

void SceneNode::markDirty() noexcept {
    ownPaintingDirty_ = true;
    markSubtreeDirty();
}

void SceneNode::markSubtreeDirty() noexcept {
    for (SceneNode *node = this; node; node = node->parent_) {
        bool const hadCachedSubtreeBounds = node->cachedSubtreeVisualBounds_.has_value();
        node->cachedSubtreeVisualBounds_.reset();
        if (node->subtreeDirty_) {
            if (node != this && !hadCachedSubtreeBounds) {
                break;
            }
        } else {
            node->subtreeDirty_ = true;
        }
    }
}

void detail::SceneNodeAccess::markDirty(SceneNode &node) noexcept {
    node.markDirty();
}

void detail::SceneNodeAccess::markSubtreeDirty(SceneNode &node) noexcept {
    node.markSubtreeDirty();
}

void detail::SceneNodeAccess::clearDirty(SceneNode const &node) noexcept {
    node.ownPaintingDirty_ = false;
}

bool detail::SceneNodeAccess::ownPaintingDirty(SceneNode const &node) noexcept {
    return node.ownPaintingDirty_;
}

bool detail::SceneNodeAccess::subtreeDirty(SceneNode const &node) noexcept {
    return node.subtreeDirty_;
}

void detail::SceneNodeAccess::clearSubtreeDirty(SceneNode const &node) noexcept {
    node.subtreeDirty_ = false;
}

bool detail::SceneNodeAccess::preparedGroupCacheSuppressed(SceneNode const &node) noexcept {
    return node.preparedGroupCacheCooldown_ > 0;
}

void detail::SceneNodeAccess::suppressPreparedGroupCache(SceneNode const &node) noexcept {
    node.preparedGroupCacheCooldown_ = 2;
}

std::uint8_t detail::SceneNodeAccess::preparedGroupCacheCooldown(SceneNode const &node) noexcept {
    return node.preparedGroupCacheCooldown_;
}

void detail::SceneNodeAccess::decrementPreparedGroupCacheCooldown(SceneNode const &node) noexcept {
    if (node.preparedGroupCacheCooldown_ > 0) {
        --node.preparedGroupCacheCooldown_;
    }
}

std::uint64_t detail::SceneNodeAccess::preparedRenderOpsKey(SceneNode const &node) noexcept {
    return node.preparedRenderOpsKey_;
}

void detail::SceneNodeAccess::setPreparedRenderOpsKey(SceneNode const &node, std::uint64_t key) noexcept {
    node.preparedRenderOpsKey_ = key;
}

std::unique_ptr<PreparedRenderOps>& detail::SceneNodeAccess::preparedRenderOps(
    SceneNode const &node) noexcept {
    return node.preparedRenderOps_;
}

} // namespace lambdaui::scenegraph
