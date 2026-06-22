#include "UI/WindowRender.hpp"

#include <Lambda/Graphics/Canvas.hpp>
#include <Lambda/Graphics/Styles.hpp>
#include <Lambda/Graphics/TextSystem.hpp>
#include <Lambda/SceneGraph/SceneGraph.hpp>
#include <Lambda/SceneGraph/SceneRenderer.hpp>
#include <Lambda/UI/Application.hpp>
#include <Lambda/UI/Overlay.hpp>
#include <Lambda/UI/Window.hpp>

#include <Lambda/UI/Detail/Runtime.hpp>

#include "Detail/ResizeTrace.hpp"

#include <chrono>

namespace lambda {

void renderWindowFrame(scenegraph::SceneRenderer& renderer, Canvas& canvas,
                       std::optional<scenegraph::SceneGraph> const& sceneGraph,
                       Size windowSize, OverlayManager const& overlays, Runtime const* runtime,
                       WindowBackground const& background,
                       TextCacheRingBuffer& textCacheRing) {
  bool const traceResize = detail::resizeTraceEnabled();
  auto phaseStart = traceResize ? std::chrono::steady_clock::now()
                                : std::chrono::steady_clock::time_point{};
  canvas.clear(Colors::transparent);
  if (background.kind == WindowBackgroundKind::Fill && !background.fill.isNone() &&
      windowSize.width > 0.f && windowSize.height > 0.f) {
    canvas.drawRect(Rect::sharp(0.f, 0.f, windowSize.width, windowSize.height),
                    CornerRadius{},
                    background.fill,
                    StrokeStyle::none(),
                    ShadowStyle::none());
  }
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
    LAMBDA_RESIZE_TRACE("window-frame",
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

} // namespace lambda
