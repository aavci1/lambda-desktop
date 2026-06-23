#pragma once

/// \file Lambda/SceneGraph/SceneRenderer.hpp
///
/// Recursive renderer for the pure scene graph.

#include <memory>

namespace lambdaui {

class Canvas;

namespace scenegraph {

class PreparedRenderOps;
class Renderer;
class SceneGraph;
class SceneNode;

class SceneRenderer {
  public:
    explicit SceneRenderer(Canvas &canvas);
    explicit SceneRenderer(Renderer &renderer);
    ~SceneRenderer();

    SceneRenderer(SceneRenderer const &) = delete;
    SceneRenderer &operator=(SceneRenderer const &) = delete;
    SceneRenderer(SceneRenderer &&) = delete;
    SceneRenderer &operator=(SceneRenderer &&) = delete;

    void render(SceneGraph const &graph);
    void render(SceneNode const &node);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace scenegraph
} // namespace lambdaui
