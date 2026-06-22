#include <Lambda/Graphics/RenderTarget.hpp>
#include <Lambda/SceneGraph/SceneGraph.hpp>
#include <Lambda/SceneGraph/SceneRenderer.hpp>

namespace lambda {

void RenderTarget::renderScene(scenegraph::SceneGraph const& scene) {
  scenegraph::SceneRenderer renderer(canvas());
  renderer.render(scene);
}

} // namespace lambda
