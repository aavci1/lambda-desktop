#include "Compositor/Surface/CommittedSurfacePainter.hpp"

#include "Compositor/Chrome/WindowChromeRenderer.hpp"
#include "Detail/ResizeTrace.hpp"

#include <algorithm>
#include <cmath>

namespace flux::compositor {
namespace {

float clamp01(float value) {
  return std::clamp(value, 0.f, 1.f);
}

float easeOutCubic(float value) {
  float const t = clamp01(value);
  float const inverse = 1.f - t;
  return 1.f - inverse * inverse * inverse;
}

bool renderSnapshotChanged(CommittedSurfaceSnapshot const& current,
                           SurfaceVisualState const& visual) {
  if (!visual.hasLastSnapshot) return true;
  auto const& previous = visual.lastSnapshot;
  return current.x != previous.x || current.y != previous.y ||
         current.width != previous.width || current.height != previous.height ||
         current.bufferWidth != previous.bufferWidth || current.bufferHeight != previous.bufferHeight ||
         current.activeSizing != previous.activeSizing ||
         current.serial != previous.serial ||
         current.sourceX != previous.sourceX || current.sourceY != previous.sourceY ||
         current.sourceWidth != previous.sourceWidth || current.sourceHeight != previous.sourceHeight ||
         current.destinationWidth != previous.destinationWidth ||
         current.destinationHeight != previous.destinationHeight;
}

} // namespace

bool shouldTraceRenderSnapshot(CommittedSurfaceSnapshot const& current,
                               SurfaceVisualState const& visual) {
  return detail::resizeTraceEnabled() && renderSnapshotChanged(current, visual);
}

void drawCommittedSurfaceSnapshot(Canvas& canvas,
                                  TextSystem& textSystem,
                                  CommittedSurfaceSnapshot const& surface,
                                  SurfaceVisualState& visual,
                                  Image& clientImage,
                                  std::chrono::steady_clock::time_point frameTime,
                                  ChromeConfig const& chrome,
                                  bool animationsEnabled) {
  if (visual.firstSeen.time_since_epoch().count() == 0) visual.firstSeen = frameTime;

  if (shouldTraceRenderSnapshot(surface, visual)) {
    auto const imageSize = clientImage.size();
    detail::resizeTrace(
        "compositor-render",
        "render-snapshot surface=%llu window=%d,%d frame=%dx%d buffer=%dx%d "
        "image=%dx%d source=%.1f,%.1f %.1fx%.1f dest=%dx%d serial=%llu\n",
        static_cast<unsigned long long>(surface.id),
        surface.x,
        surface.y,
        surface.width,
        surface.height,
        surface.bufferWidth,
        surface.bufferHeight,
        static_cast<int>(imageSize.width),
        static_cast<int>(imageSize.height),
        surface.sourceX,
        surface.sourceY,
        surface.sourceWidth,
        surface.sourceHeight,
        surface.destinationWidth,
        surface.destinationHeight,
        static_cast<unsigned long long>(surface.serial));
  }
  visual.lastSnapshot = surface;
  visual.hasLastSnapshot = true;

  float const windowX = static_cast<float>(surface.x);
  float const windowY = static_cast<float>(surface.y);
  float const windowWidth = static_cast<float>(surface.width);
  float const windowHeight = static_cast<float>(surface.height);
  float const titleBarHeight = static_cast<float>(surface.titleBarHeight);
  bool const cutoutChrome = surface.serverSideDecorated && surface.cutoutsBound && !surface.cutoutsRejected;
  CornerRadius const contentCorners = cutoutChrome
                                          ? CornerRadius{chrome.windowCornerRadius}
                                          : (titleBarHeight > 0.f
                                                 ? CornerRadius{0.f, 0.f, chrome.windowCornerRadius, chrome.windowCornerRadius}
                                                 : CornerRadius{});
  float const animationMs = static_cast<float>(
      std::chrono::duration_cast<std::chrono::milliseconds>(frameTime - visual.firstSeen).count());
  float const openProgress = animationsEnabled ? easeOutCubic(animationMs / 140.f) : 1.f;
  float const openScale = 0.965f + 0.035f * openProgress;
  float const openOpacity = openProgress;
  float const outerHeight = windowHeight + titleBarHeight;
  Point const pivot{windowX + windowWidth * 0.5f, windowY - titleBarHeight + outerHeight * 0.5f};
  canvas.save();
  canvas.setOpacity(canvas.opacity() * openOpacity);
  if (openScale < 1.f) {
    canvas.translate(pivot.x, pivot.y);
    canvas.scale(openScale);
    canvas.translate(-pivot.x, -pivot.y);
  }
  if (!cutoutChrome) drawWindowChrome(canvas, textSystem, surface, chrome);
  float const sourceWidth = surface.sourceWidth > 0.f
                                ? surface.sourceWidth
                                : static_cast<float>(clientImage.size().width);
  float const sourceHeight = surface.sourceHeight > 0.f
                                 ? surface.sourceHeight
                                 : static_cast<float>(clientImage.size().height);
  bool const clientContentSmallerThanFrame =
      surface.destinationWidth > 0 &&
      surface.destinationHeight > 0 &&
      (surface.destinationWidth != static_cast<int>(std::lround(windowWidth)) ||
       surface.destinationHeight != static_cast<int>(std::lround(windowHeight)));
  float const contentWidth = clientContentSmallerThanFrame
                                 ? static_cast<float>(surface.destinationWidth)
                                 : windowWidth;
  float const contentHeight = clientContentSmallerThanFrame
                                  ? static_cast<float>(surface.destinationHeight)
                                  : windowHeight;
  canvas.save();
  canvas.clipRect(Rect::sharp(windowX, windowY, windowWidth, windowHeight), contentCorners);
  canvas.drawImage(clientImage,
                   Rect::sharp(surface.sourceX,
                               surface.sourceY,
                               sourceWidth,
                               sourceHeight),
                   Rect::sharp(windowX,
                               windowY,
                               contentWidth,
                               contentHeight),
                   CornerRadius{});
  if (clientContentSmallerThanFrame) {
    float const rightPad = std::max(0.f, windowWidth - contentWidth);
    float const bottomPad = std::max(0.f, windowHeight - contentHeight);
    float const edgeSourceWidth = std::max(1.f, sourceWidth);
    float const edgeSourceHeight = std::max(1.f, sourceHeight);
    if (rightPad > 0.f) {
      canvas.drawImage(clientImage,
                       Rect::sharp(surface.sourceX + edgeSourceWidth - 1.f,
                                   surface.sourceY,
                                   1.f,
                                   edgeSourceHeight),
                       Rect::sharp(windowX + contentWidth,
                                   windowY,
                                   rightPad,
                                   contentHeight),
                       CornerRadius{});
    }
    if (bottomPad > 0.f) {
      canvas.drawImage(clientImage,
                       Rect::sharp(surface.sourceX,
                                   surface.sourceY + edgeSourceHeight - 1.f,
                                   edgeSourceWidth,
                                   1.f),
                       Rect::sharp(windowX,
                                   windowY + contentHeight,
                                   contentWidth,
                                   bottomPad),
                       CornerRadius{});
    }
    if (rightPad > 0.f && bottomPad > 0.f) {
      canvas.drawImage(clientImage,
                       Rect::sharp(surface.sourceX + edgeSourceWidth - 1.f,
                                   surface.sourceY + edgeSourceHeight - 1.f,
                                   1.f,
                                   1.f),
                       Rect::sharp(windowX + contentWidth,
                                   windowY + contentHeight,
                                   rightPad,
                                   bottomPad),
                       CornerRadius{});
    }
  }
  canvas.restore();
  if (cutoutChrome) drawWindowChrome(canvas, textSystem, surface, chrome);
  canvas.restore();
}

} // namespace flux::compositor
