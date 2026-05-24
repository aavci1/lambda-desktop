#pragma once

#include <Flux/Core/Geometry.hpp>
#include <Flux/Core/Color.hpp>
#include <Flux/Graphics/TextCacheDebugOverlay.hpp>

#include <optional>

namespace flux {

class Canvas;
class OverlayManager;
class Runtime;
namespace scenegraph {
class SceneGraph;
class SceneRenderer;
}

void renderWindowFrame(scenegraph::SceneRenderer& renderer, Canvas& canvas,
                       std::optional<scenegraph::SceneGraph> const& sceneGraph,
                       Size windowSize, OverlayManager const& overlays, Runtime const* runtime, Color clearColor,
                       std::optional<Color> glassTint,
                       TextCacheRingBuffer& textCacheRing);

} // namespace flux
