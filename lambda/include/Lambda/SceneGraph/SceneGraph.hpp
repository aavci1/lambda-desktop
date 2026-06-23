#pragma once

/// \file Lambda/SceneGraph/SceneGraph.hpp
///
/// Pure scene-graph container. Owns a single root node.

#include <Lambda/Core/Identity.hpp>
#include <Lambda/Core/Geometry.hpp>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace lambdaui::scenegraph {

class SceneNode;

class SceneGraph {
  public:
    SceneGraph();
    ~SceneGraph();
    explicit SceneGraph(std::unique_ptr<SceneNode> root);

    SceneGraph(SceneGraph const &) = delete;
    SceneGraph &operator=(SceneGraph const &) = delete;
    SceneGraph(SceneGraph &&) = delete;
    SceneGraph &operator=(SceneGraph &&) = delete;

    SceneNode &root() noexcept;
    SceneNode const &root() const noexcept;

    void setRoot(std::unique_ptr<SceneNode> root);
    std::unique_ptr<SceneNode> releaseRoot();
    void beginGeometryBuild();
    void finishGeometryBuild();
    void clearGeometry();
    void invalidateRenderCaches();
    void recordGeometry(ComponentKey const& key, Rect rect);
    void recordNode(ComponentKey const& key, SceneNode* node);
    bool retainSubtreeGeometry(ComponentKey const& key, Point newOrigin);
    std::unique_ptr<SceneNode> replaceNodeForKey(ComponentKey const& key, std::unique_ptr<SceneNode> node);
    void replaceSubtreeData(ComponentKey const& key, SceneGraph const& patch);

    [[nodiscard]] std::optional<Rect> rectForKey(ComponentKey const& key) const;
    [[nodiscard]] SceneNode* nodeForKey(ComponentKey const& key) const noexcept;
    [[nodiscard]] std::optional<Rect> rectForLeafKeyPrefix(ComponentKey const& key) const;
    [[nodiscard]] std::optional<Rect> rectForTapAnchor(ComponentKey const& key) const;
    [[nodiscard]] std::vector<std::pair<ComponentKey, Rect>> snapshotGeometry() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lambdaui::scenegraph
