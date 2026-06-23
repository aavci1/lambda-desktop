#include <Lambda/SceneGraph/SceneInteraction.hpp>

#include <Lambda/SceneGraph/SceneGraph.hpp>
#include <Lambda/SceneGraph/SceneNode.hpp>
#include <Lambda/SceneGraph/SceneTraversal.hpp>

#include "Debug/PerfCounters.hpp"

#include <cstddef>
#include <cstdint>

namespace lambdaui::scenegraph {

namespace {

std::uint64_t gHitTestTraversalCountForTesting = 0;

SceneNode const* findInteractionNodeByKey(SceneNode const& node, ComponentKey const& key) {
    for (auto it = node.children().rbegin(); it != node.children().rend(); ++it) {
        if (SceneNode const* match = findInteractionNodeByKey(**it, key)) {
            return match;
        }
    }
    if (Interaction const* interaction = node.interaction();
        interaction && interaction->stableTargetKey() == key) {
        return &node;
    }
    return nullptr;
}

} // namespace

std::pair<SceneNode const*, Interaction const*> findInteractionByKey(SceneGraph const& graph,
                                                                     ComponentKey const& key) {
    if (key.empty()) {
        return {nullptr, nullptr};
    }

    SceneNode const* match = findInteractionNodeByKey(graph.root(), key);
    return match ? std::pair<SceneNode const*, Interaction const*>{match, match->interaction()}
                 : std::pair<SceneNode const*, Interaction const*>{nullptr, nullptr};
}

std::optional<InteractionHitResult> hitTestInteraction(SceneGraph const& graph, Point rootPoint) {
    return hitTestInteraction(graph, rootPoint, [](Interaction const&) { return true; });
}

std::optional<InteractionHitResult> hitTestInteraction(
    SceneGraph const& graph, Point rootPoint,
    Reactive::SmallFn<bool(Interaction const&)> const& acceptTarget) {
    ++gHitTestTraversalCountForTesting;
    if (auto hit = hitTestNode(graph.root(), rootPoint, [&](SceneNode const& node) {
            if (Interaction const* interaction = node.interaction()) {
                return acceptTarget(*interaction);
            }
            return false;
        })) {
        return InteractionHitResult{
            .node = hit->first,
            .localPoint = hit->second,
            .interaction = hit->first->interaction(),
        };
    }
    return std::nullopt;
}

std::vector<ComponentKey> collectFocusableKeys(SceneGraph const& graph) {
    std::vector<ComponentKey> out{};
    walkSceneGraph(graph.root(), [&](SceneNode const& node) {
        if (Interaction const* interaction = node.interaction();
            interaction && interaction->focusable() && !interaction->stableTargetKey().empty()) {
            out.push_back(interaction->stableTargetKey());
        }
    });
    return out;
}

namespace detail {

void resetHitTestTraversalCountForTesting() noexcept {
    gHitTestTraversalCountForTesting = 0;
}

std::uint64_t hitTestTraversalCountForTesting() noexcept {
    return gHitTestTraversalCountForTesting;
}

} // namespace detail

} // namespace lambdaui::scenegraph
