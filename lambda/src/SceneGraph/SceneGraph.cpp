#include <Lambda/SceneGraph/SceneGraph.hpp>

#include <Lambda/SceneGraph/SceneNode.hpp>
#include <Lambda/SceneGraph/RasterCacheNode.hpp>
#include <Lambda/SceneGraph/Renderer.hpp>

#include "Debug/PerfCounters.hpp"
#include "SceneGraph/SceneNodeInternal.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_map>

namespace lambda::scenegraph {

namespace {

bool keyHasPrefix(ComponentKey const& key, ComponentKey const& prefix) {
    if (prefix.empty()) {
        return true;
    }
    if (key.size() < prefix.size()) {
        return false;
    }
    debug::perf::recordComponentKeyPrefixCompare(prefix.size());
    return key.hasPrefix(prefix);
}

void rebindNodeMappings(std::unordered_map<ComponentKey, SceneNode*, ComponentKeyHash>& nodes,
                        SceneNode* from, SceneNode* to) {
    if (!from || !to || from == to) {
        return;
    }
    for (auto& [key, node] : nodes) {
        (void)key;
        if (node == from) {
            node = to;
        }
    }
}

void invalidateNodeRenderCaches(SceneNode& node) {
    detail::SceneNodeAccess::preparedRenderOps(node).reset();
    if (node.kind() == SceneNodeKind::RasterCache) {
        static_cast<RasterCacheNode&>(node).invalidateCache();
    } else if (node.kind() == SceneNodeKind::Group) {
        detail::SceneNodeAccess::markSubtreeDirty(node);
    } else {
        detail::SceneNodeAccess::markDirty(node);
    }
    for (std::unique_ptr<SceneNode> const& child : node.children()) {
        invalidateNodeRenderCaches(*child);
    }
}

} // namespace

struct SceneGraph::Impl {
    std::unique_ptr<SceneNode> root;
    std::unordered_map<ComponentKey, Rect, ComponentKeyHash> currentGeometry;
    std::unordered_map<ComponentKey, Rect, ComponentKeyHash> previousGeometry;
    std::unordered_map<ComponentKey, Rect, ComponentKeyHash> buildingGeometry;
    std::unordered_map<ComponentKey, SceneNode*, ComponentKeyHash> currentNodes;
    std::unordered_map<ComponentKey, SceneNode*, ComponentKeyHash> buildingNodes;
};

SceneGraph::SceneGraph() : impl_(std::make_unique<Impl>()) {
    impl_->root = std::make_unique<SceneNode>();
}

SceneGraph::~SceneGraph() = default;

SceneGraph::SceneGraph(std::unique_ptr<SceneNode> root) : impl_(std::make_unique<Impl>()) {
    setRoot(std::move(root));
}

SceneNode &SceneGraph::root() noexcept {
    return *impl_->root;
}

SceneNode const &SceneGraph::root() const noexcept {
    return *impl_->root;
}

void SceneGraph::setRoot(std::unique_ptr<SceneNode> root) {
    if (!root) {
        throw std::invalid_argument("SceneGraph root must not be null");
    }
    impl_->root = std::move(root);
}

std::unique_ptr<SceneNode> SceneGraph::releaseRoot() {
    std::unique_ptr<SceneNode> released = std::move(impl_->root);
    impl_->root = std::make_unique<SceneNode>();
    return released;
}

void SceneGraph::beginGeometryBuild() {
    impl_->buildingGeometry.clear();
    impl_->buildingNodes.clear();
}

void SceneGraph::finishGeometryBuild() {
    impl_->previousGeometry.swap(impl_->currentGeometry);
    impl_->currentGeometry = std::move(impl_->buildingGeometry);
    impl_->buildingGeometry.clear();
    impl_->currentNodes = std::move(impl_->buildingNodes);
    impl_->buildingNodes.clear();
}

void SceneGraph::clearGeometry() {
    impl_->currentGeometry.clear();
    impl_->previousGeometry.clear();
    impl_->buildingGeometry.clear();
    impl_->currentNodes.clear();
    impl_->buildingNodes.clear();
}

void SceneGraph::invalidateRenderCaches() {
    if (impl_->root) {
        invalidateNodeRenderCaches(*impl_->root);
    }
}

void SceneGraph::recordGeometry(ComponentKey const& key, Rect rect) {
    if (key.empty()) {
        return;
    }
    impl_->buildingGeometry[key] = rect;
}

void SceneGraph::recordNode(ComponentKey const& key, SceneNode* node) {
    if (key.empty() || !node) {
        return;
    }
    impl_->buildingNodes[key] = node;
}

bool SceneGraph::retainSubtreeGeometry(ComponentKey const& key, Point newOrigin) {
    if (key.empty()) {
        return false;
    }
    auto const rootIt = impl_->currentGeometry.find(key);
    if (rootIt == impl_->currentGeometry.end()) {
        return false;
    }

    Point const oldOrigin{rootIt->second.x, rootIt->second.y};
    Vec2 const delta{newOrigin.x - oldOrigin.x, newOrigin.y - oldOrigin.y};
    bool retainedAny = false;
    for (auto const& [candidateKey, rect] : impl_->currentGeometry) {
        if (!keyHasPrefix(candidateKey, key)) {
            continue;
        }
        Rect translated = rect;
        translated.x += delta.x;
        translated.y += delta.y;
        impl_->buildingGeometry[candidateKey] = translated;
        retainedAny = true;
    }
    for (auto const& [candidateKey, node] : impl_->currentNodes) {
        if (keyHasPrefix(candidateKey, key)) {
            impl_->buildingNodes[candidateKey] = node;
        }
    }
    return retainedAny;
}

std::unique_ptr<SceneNode> SceneGraph::replaceNodeForKey(ComponentKey const& key,
                                                         std::unique_ptr<SceneNode> node) {
    if (!node) {
        return nullptr;
    }
    auto it = impl_->currentNodes.find(key);
    if (it == impl_->currentNodes.end()) {
        return nullptr;
    }
    SceneNode* existing = it->second;
    if (!existing) {
        return nullptr;
    }
    if (SceneNode* parent = existing->parent()) {
        std::vector<std::unique_ptr<SceneNode>> children = parent->releaseChildren();
        for (std::unique_ptr<SceneNode>& child : children) {
            if (child.get() == existing) {
                std::unique_ptr<SceneNode> removed = std::move(child);
                child = std::move(node);
                SceneNode* replacement = child.get();
                parent->replaceChildren(std::move(children));
                rebindNodeMappings(impl_->currentNodes, existing, replacement);
                return removed;
            }
        }
        parent->replaceChildren(std::move(children));
        return nullptr;
    }
    std::unique_ptr<SceneNode> removed = std::move(impl_->root);
    impl_->root = std::move(node);
    rebindNodeMappings(impl_->currentNodes, existing, impl_->root.get());
    return removed;
}

void SceneGraph::replaceSubtreeData(ComponentKey const& key, SceneGraph const& patch) {
    std::erase_if(impl_->currentGeometry, [&](auto const& entry) {
        return keyHasPrefix(entry.first, key);
    });
    std::erase_if(impl_->currentNodes, [&](auto const& entry) {
        return keyHasPrefix(entry.first, key);
    });
    for (auto const& [patchKey, rect] : patch.impl_->currentGeometry) {
        impl_->currentGeometry[patchKey] = rect;
    }
    for (auto const& [patchKey, node] : patch.impl_->currentNodes) {
        impl_->currentNodes[patchKey] = node;
    }
}

std::optional<Rect> SceneGraph::rectForKey(ComponentKey const& key) const {
    if (auto it = impl_->currentGeometry.find(key); it != impl_->currentGeometry.end()) {
        return it->second;
    }
    if (auto it = impl_->previousGeometry.find(key); it != impl_->previousGeometry.end()) {
        return it->second;
    }
    return std::nullopt;
}

SceneNode* SceneGraph::nodeForKey(ComponentKey const& key) const noexcept {
    auto it = impl_->currentNodes.find(key);
    return it == impl_->currentNodes.end() ? nullptr : it->second;
}

std::optional<Rect> SceneGraph::rectForLeafKeyPrefix(ComponentKey const& key) const {
    for (std::size_t len = key.size(); len > 0; --len) {
        if (std::optional<Rect> rect = rectForKey(key.prefix(len))) {
            return rect;
        }
    }
    return std::nullopt;
}

std::optional<Rect> SceneGraph::rectForTapAnchor(ComponentKey const& key) const {
    if (key.empty()) {
        return std::nullopt;
    }
    return rectForLeafKeyPrefix(key);
}

std::vector<std::pair<ComponentKey, Rect>> SceneGraph::snapshotGeometry() const {
    std::vector<std::pair<ComponentKey, Rect>> out;
    out.reserve(impl_->currentGeometry.size());
    for (auto const& [key, rect] : impl_->currentGeometry) {
        out.emplace_back(key, rect);
    }
    return out;
}

} // namespace lambda::scenegraph
