#pragma once

/// \file Lambda/SceneGraph/SceneInteraction.hpp
///
/// Interaction and focus traversal helpers for the pure scenegraph.

#include <Lambda/SceneGraph/Interaction.hpp>
#include <Lambda/Reactive/SmallFn.hpp>

#include <optional>
#include <cstdint>
#include <utility>
#include <vector>

namespace lambdaui::scenegraph {

class SceneGraph;
class SceneNode;

std::pair<SceneNode const*, Interaction const*> findInteractionByKey(SceneGraph const& graph,
                                                                     ComponentKey const& key);

std::optional<InteractionHitResult> hitTestInteraction(SceneGraph const& graph, Point rootPoint);
std::optional<InteractionHitResult> hitTestInteraction(
    SceneGraph const& graph, Point rootPoint,
    Reactive::SmallFn<bool(Interaction const&)> const& acceptTarget);

std::vector<ComponentKey> collectFocusableKeys(SceneGraph const& graph);

namespace detail {
void resetHitTestTraversalCountForTesting() noexcept;
std::uint64_t hitTestTraversalCountForTesting() noexcept;
}

} // namespace lambdaui::scenegraph
