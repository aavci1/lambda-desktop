#pragma once

#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Core/Color.hpp>
#include <Lambda/Graphics/TextCacheDebugOverlay.hpp>

#include <optional>

namespace lambdaui {

class Canvas;
class OverlayManager;
class Runtime;
struct WindowBackground;
namespace scenegraph {
class SceneGraph;
class SceneRenderer;
}

void renderWindowFrame(scenegraph::SceneRenderer& renderer, Canvas& canvas,
                       std::optional<scenegraph::SceneGraph> const& sceneGraph,
                       Size windowSize, OverlayManager const& overlays, Runtime const* runtime,
                       WindowBackground const& background,
                       TextCacheRingBuffer& textCacheRing);

} // namespace lambdaui
