#include "UI/WindowRender.hpp"

#include <Flux/Graphics/Canvas.hpp>
#include <Flux/Graphics/TextSystem.hpp>
#include <Flux/SceneGraph/SceneGraph.hpp>
#include <Flux/SceneGraph/SceneRenderer.hpp>
#include <Flux/UI/Application.hpp>
#include <Flux/UI/Overlay.hpp>

#include <Flux/UI/Detail/Runtime.hpp>

#include "Detail/ResizeTrace.hpp"

#include <chrono>

namespace flux {

void renderWindowFrame(scenegraph::SceneRenderer& renderer, Canvas& canvas,
                       std::optional<scenegraph::SceneGraph> const& sceneGraph,
                       Size windowSize, OverlayManager const& overlays, Runtime const* runtime, Color clearColor,
                       TextCacheRingBuffer& textCacheRing) {
  bool const traceResize = detail::resizeTraceEnabled();
  auto phaseStart = traceResize ? std::chrono::steady_clock::now()
                                : std::chrono::steady_clock::time_point{};
  canvas.clear(clearColor);
  std::int64_t clearElapsed = 0;
  std::int64_t sceneElapsed = 0;
  std::int64_t overlaysElapsed = 0;
  std::int64_t textOverlayElapsed = 0;
  if (traceResize) {
    clearElapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - phaseStart).count();
    phaseStart = std::chrono::steady_clock::now();
  }
  if (sceneGraph) {
    renderer.render(*sceneGraph);
  }
  if (traceResize) {
    sceneElapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - phaseStart).count();
    phaseStart = std::chrono::steady_clock::now();
  }

  Rect const windowBounds = Rect::sharp(0.f, 0.f, windowSize.width, windowSize.height);
  for (std::unique_ptr<OverlayEntry> const& up : overlays.entries()) {
    OverlayEntry const& entry = *up;
    if (entry.config.backdropBlurRadius > 0.f) {
      canvas.drawBackdropBlur(windowBounds, entry.config.backdropBlurRadius, entry.config.backdropColor);
    }
    canvas.save();
    canvas.transform(Mat3::translate(Point{entry.resolvedFrame.x, entry.resolvedFrame.y}));
    renderer.render(entry.sceneGraph);
    canvas.restore();
  }
  if (traceResize) {
    overlaysElapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - phaseStart).count();
    phaseStart = std::chrono::steady_clock::now();
  }

  if (runtime && runtime->textCacheOverlayEnabled()) {
    renderTextCacheDebugOverlay(canvas, windowBounds, textCacheRing,
                                Application::instance().textSystem());
  }
  if (traceResize) {
    textOverlayElapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - phaseStart).count();
    detail::resizeTrace("window-frame",
                        "size=%.0fx%.0f overlays=%zu clear=%.3fms scene=%.3fms overlays=%.3fms textOverlay=%.3fms\n",
                        windowSize.width,
                        windowSize.height,
                        overlays.entries().size(),
                        static_cast<double>(clearElapsed) / 1000.0,
                        static_cast<double>(sceneElapsed) / 1000.0,
                        static_cast<double>(overlaysElapsed) / 1000.0,
                        static_cast<double>(textOverlayElapsed) / 1000.0);
  }
}

} // namespace flux
