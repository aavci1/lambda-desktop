#include "Compositor/Surface/CommittedSurfacePainter.hpp"

#include "Compositor/Chrome/WindowFrameGeometry.hpp"
#include "Compositor/Chrome/WindowChromeRenderer.hpp"
#include "Detail/ResizeTrace.hpp"

#include <Lambda/UI/Views/PopoverCalloutPath.hpp>

#if LAMBDA_VULKAN
#include "Graphics/Vulkan/VulkanCanvas.hpp"
#endif

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <optional>

namespace lambda::compositor {
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
         current.committedWidth != previous.committedWidth ||
         current.committedHeight != previous.committedHeight ||
         current.bufferWidth != previous.bufferWidth || current.bufferHeight != previous.bufferHeight ||
         current.bufferTransform != previous.bufferTransform ||
         current.activeSizing != previous.activeSizing ||
         current.geometryAnimationGrowing != previous.geometryAnimationGrowing ||
         current.serial != previous.serial ||
         current.sourceX != previous.sourceX || current.sourceY != previous.sourceY ||
         current.sourceWidth != previous.sourceWidth || current.sourceHeight != previous.sourceHeight ||
         current.destinationWidth != previous.destinationWidth ||
         current.destinationHeight != previous.destinationHeight;
}

bool reachesEdge(float value, float edge) {
  return std::abs(value - edge) <= 0.5f;
}

bool diagnosticEnvEnabled(char const* name) {
  char const* raw = std::getenv(name);
  if (!raw || !*raw) return false;
  return std::strcmp(raw, "0") != 0 && std::strcmp(raw, "false") != 0 && std::strcmp(raw, "FALSE") != 0;
}

CornerRadius cornerRadiusForPiece(Rect const& full, Rect const& piece, CornerRadius const& outer) {
  bool const left = reachesEdge(piece.x, full.x);
  bool const top = reachesEdge(piece.y, full.y);
  bool const right = reachesEdge(piece.x + piece.width, full.x + full.width);
  bool const bottom = reachesEdge(piece.y + piece.height, full.y + full.height);
  return CornerRadius{
      left && top ? outer.topLeft : 0.f,
      right && top ? outer.topRight : 0.f,
      right && bottom ? outer.bottomRight : 0.f,
      left && bottom ? outer.bottomLeft : 0.f,
  };
}

Rect intersectRect(Rect const& a, Rect const& b) {
  float const left = std::max(a.x, b.x);
  float const top = std::max(a.y, b.y);
  float const right = std::min(a.x + a.width, b.x + b.width);
  float const bottom = std::min(a.y + a.height, b.y + b.height);
  if (right <= left || bottom <= top) return Rect::sharp(0.f, 0.f, 0.f, 0.f);
  return Rect::sharp(left, top, right - left, bottom - top);
}

bool sameRect(Rect const& a, Rect const& b) {
  return reachesEdge(a.x, b.x) &&
         reachesEdge(a.y, b.y) &&
         reachesEdge(a.x + a.width, b.x + b.width) &&
         reachesEdge(a.y + a.height, b.y + b.height);
}

bool regionCoversSurfaceContent(CommittedSurfaceSnapshot::RegionRect const& region,
                                int width,
                                int height) {
  return region.x == 0 &&
         region.y == 0 &&
         region.width >= width &&
         region.height >= height;
}

bool backgroundEffectCoversCommittedContent(CommittedSurfaceSnapshot const& surface) {
  int const width = surface.committedWidth > 0 ? surface.committedWidth : surface.width;
  int const height = surface.committedHeight > 0 ? surface.committedHeight : surface.height;
  return std::ranges::any_of(surface.backgroundBlurRects, [&](CommittedSurfaceSnapshot::RegionRect const& rect) {
    return regionCoversSurfaceContent(rect, width, height);
  });
}

std::optional<Rect> reservedWindowClip(CommittedSurfaceSnapshot const& surface) {
  float top = -100000.f;
  float bottom = 100000.f;
  if (surface.windowClipTop > 0) {
    top = static_cast<float>(surface.windowClipTop);
  }
  if (surface.windowClipBottom > 0) {
    bottom = static_cast<float>(surface.windowClipBottom);
  }
  if (bottom <= top) return std::nullopt;
  if (surface.windowClipTop <= 0 && surface.windowClipBottom <= 0) return std::nullopt;
  return Rect::sharp(-100000.f, top, 200000.f, bottom - top);
}

void drawContentPiece(Canvas& canvas,
                      Image& image,
                      Rect const& source,
                      Rect const& destination,
                      Rect const& fullDestination,
                      CornerRadius const& outerCorners) {
  canvas.drawImage(image,
                   source,
                   destination,
                   cornerRadiusForPiece(fullDestination, destination, outerCorners));
}

bool setCanvasImagePremultipliedAlpha(Canvas* canvas, bool enabled) {
#if LAMBDA_VULKAN
  return setVulkanCanvasImagePremultipliedAlpha(canvas, enabled);
#else
  (void)canvas;
  return enabled;
#endif
}

void drawClientSurfacePiece(Canvas& canvas,
                            Image& image,
                            Rect const& source,
                            Rect const& destination,
                            Rect const& fullDestination,
                            CornerRadius const& outerCorners) {
  bool const previousPremultiplied = setCanvasImagePremultipliedAlpha(&canvas, true);
  drawContentPiece(canvas, image, source, destination, fullDestination, outerCorners);
  setCanvasImagePremultipliedAlpha(&canvas, previousPremultiplied);
}

struct ResolvedGlassMaterial {
  bool enabled = false;
  float blurRadius = 0.f;
  Color baseColor{};
  Color tintColor{};
  Color borderColor{};
  CornerRadius cornerRadius{};
  bool cornerRadiusSet = false;
  BackgroundEffectShape shape = BackgroundEffectShape::RoundedRect;
  BackgroundEffectCalloutPlacement calloutPlacement = BackgroundEffectCalloutPlacement::Below;
  float arrowWidth = 16.f;
  float arrowHeight = 8.f;
};

ResolvedGlassMaterial resolvedGlassMaterial(CommittedSurfaceSnapshot const& surface) {
  if (surface.backgroundBlurRects.empty()) return {};
  return ResolvedGlassMaterial{
      .enabled = surface.backgroundEffect.blurRadius > 0.f ||
                 surface.backgroundEffect.baseColor.a > 0.f ||
                 surface.backgroundEffect.tint.a > 0.f ||
                 surface.backgroundEffect.borderColor.a > 0.f,
      .blurRadius = surface.backgroundEffect.blurRadius,
      .baseColor = surface.backgroundEffect.baseColor,
      .tintColor = surface.backgroundEffect.tint,
      .borderColor = surface.backgroundEffect.borderColor,
      .cornerRadius = surface.backgroundEffect.cornerRadius,
      .cornerRadiusSet = surface.backgroundEffect.cornerRadiusSet,
      .shape = surface.backgroundEffect.shape,
      .calloutPlacement = surface.backgroundEffect.calloutPlacement,
      .arrowWidth = surface.backgroundEffect.arrowWidth,
      .arrowHeight = surface.backgroundEffect.arrowHeight,
  };
}

bool materialShouldCoverAnimatedSurface(CommittedSurfaceSnapshot const& surface,
                                        ResolvedGlassMaterial const& material) {
  return material.enabled &&
         surface.geometryAnimationGrowing &&
         backgroundEffectCoversCommittedContent(surface);
}

PopoverPlacement popoverPlacement(BackgroundEffectCalloutPlacement placement) {
  switch (placement) {
  case BackgroundEffectCalloutPlacement::Above:
    return PopoverPlacement::Above;
  case BackgroundEffectCalloutPlacement::End:
    return PopoverPlacement::End;
  case BackgroundEffectCalloutPlacement::Start:
    return PopoverPlacement::Start;
  case BackgroundEffectCalloutPlacement::Below:
  default:
    return PopoverPlacement::Below;
  }
}

Path translatedPath(Path const& source, float dx, float dy) {
  Path result{};
  for (std::size_t i = 0; i < source.commandCount(); ++i) {
    Path::CommandView const command = source.command(i);
    switch (command.type) {
    case Path::CommandType::MoveTo:
      if (command.dataCount >= 2) result.moveTo({command.data[0] + dx, command.data[1] + dy});
      break;
    case Path::CommandType::LineTo:
      if (command.dataCount >= 2) result.lineTo({command.data[0] + dx, command.data[1] + dy});
      break;
    case Path::CommandType::QuadTo:
      if (command.dataCount >= 4) {
        result.quadTo({command.data[0] + dx, command.data[1] + dy},
                      {command.data[2] + dx, command.data[3] + dy});
      }
      break;
    case Path::CommandType::BezierTo:
      if (command.dataCount >= 6) {
        result.bezierTo({command.data[0] + dx, command.data[1] + dy},
                        {command.data[2] + dx, command.data[3] + dy},
                        {command.data[4] + dx, command.data[5] + dy});
      }
      break;
    case Path::CommandType::Rect:
      if (command.dataCount >= 8) {
        result.rect(Rect::sharp(command.data[0] + dx, command.data[1] + dy, command.data[2], command.data[3]),
                    CornerRadius{command.data[4], command.data[5], command.data[6], command.data[7]});
      }
      break;
    case Path::CommandType::Close:
      result.close();
      break;
    default:
      break;
    }
  }
  return result;
}

Rect calloutCardRect(Rect const& bounds, ResolvedGlassMaterial const& material) {
  float const arrow = std::clamp(material.arrowHeight,
                                 0.f,
                                 material.calloutPlacement == BackgroundEffectCalloutPlacement::Start ||
                                         material.calloutPlacement == BackgroundEffectCalloutPlacement::End
                                     ? bounds.width
                                     : bounds.height);
  switch (material.calloutPlacement) {
  case BackgroundEffectCalloutPlacement::Below:
    return Rect::sharp(bounds.x, bounds.y + arrow, bounds.width, std::max(0.f, bounds.height - arrow));
  case BackgroundEffectCalloutPlacement::Above:
    return Rect::sharp(bounds.x, bounds.y, bounds.width, std::max(0.f, bounds.height - arrow));
  case BackgroundEffectCalloutPlacement::End:
    return Rect::sharp(bounds.x + arrow, bounds.y, std::max(0.f, bounds.width - arrow), bounds.height);
  case BackgroundEffectCalloutPlacement::Start:
    return Rect::sharp(bounds.x, bounds.y, std::max(0.f, bounds.width - arrow), bounds.height);
  }
  return bounds;
}

Path materialPath(Rect const& rect, CornerRadius const& corners, ResolvedGlassMaterial const& material) {
  if (material.shape != BackgroundEffectShape::Callout) {
    Path path{};
    path.rect(rect, corners);
    return path;
  }

  float const arrowWidth = std::max(0.f, material.arrowWidth);
  float const arrowHeight = std::max(0.f, material.arrowHeight);
  Rect const localCard = [&] {
    switch (material.calloutPlacement) {
    case BackgroundEffectCalloutPlacement::Below:
      return Rect::sharp(0.f, arrowHeight, rect.width, std::max(0.f, rect.height - arrowHeight));
    case BackgroundEffectCalloutPlacement::Above:
      return Rect::sharp(0.f, 0.f, rect.width, std::max(0.f, rect.height - arrowHeight));
    case BackgroundEffectCalloutPlacement::End:
      return Rect::sharp(arrowHeight, 0.f, std::max(0.f, rect.width - arrowHeight), rect.height);
    case BackgroundEffectCalloutPlacement::Start:
      return Rect::sharp(0.f, 0.f, std::max(0.f, rect.width - arrowHeight), rect.height);
    }
    return Rect::sharp(0.f, 0.f, rect.width, rect.height);
  }();
  Path local = buildPopoverCalloutPath(popoverPlacement(material.calloutPlacement),
                                       corners,
                                       true,
                                       arrowWidth,
                                       arrowHeight,
                                       localCard,
                                       Size{rect.width, rect.height});
  return translatedPath(local, rect.x, rect.y);
}

#if LAMBDA_VULKAN
VulkanCalloutPlacement vulkanCalloutPlacement(BackgroundEffectCalloutPlacement placement) {
  switch (placement) {
  case BackgroundEffectCalloutPlacement::Above:
    return VulkanCalloutPlacement::Above;
  case BackgroundEffectCalloutPlacement::End:
    return VulkanCalloutPlacement::End;
  case BackgroundEffectCalloutPlacement::Start:
    return VulkanCalloutPlacement::Start;
  case BackgroundEffectCalloutPlacement::Below:
  default:
    return VulkanCalloutPlacement::Below;
  }
}
#endif

void drawSurfaceBackgroundBlur(Canvas& canvas,
                               CommittedSurfaceSnapshot const& surface,
                               Rect const& fullContentRect,
                               CornerRadius const& contentCorners) {
  if (diagnosticEnvEnabled("LWM_DIAGNOSTIC_DISABLE_SURFACE_MATERIAL")) return;
  ResolvedGlassMaterial const material = resolvedGlassMaterial(surface);
  if (!material.enabled) return;

  auto drawMaterialRect = [&](Rect const& rect, CornerRadius const& corners) {
    Rect const blurRect = material.shape == BackgroundEffectShape::Callout ? calloutCardRect(rect, material) : rect;
    canvas.drawBackdropBlur(blurRect, material.blurRadius, Colors::transparent, corners);
#if LAMBDA_VULKAN
    if (material.shape == BackgroundEffectShape::Callout &&
        drawVulkanCalloutMaterial(&canvas,
                                  rect,
                                  calloutCardRect(rect, material),
                                  corners,
                                  material.baseColor,
                                  material.tintColor,
                                  material.borderColor,
                                  1.f,
                                  vulkanCalloutPlacement(material.calloutPlacement),
                                  material.arrowWidth,
                                  material.arrowHeight)) {
      return;
    }
#endif
    if (material.baseColor.a > 0.f) {
      if (material.shape == BackgroundEffectShape::Callout) {
        Path const path = materialPath(rect, corners, material);
        canvas.drawPath(path, FillStyle::solid(material.baseColor), StrokeStyle::none(), ShadowStyle::none());
      } else {
        canvas.drawRect(rect, corners, FillStyle::solid(material.baseColor), StrokeStyle::none(), ShadowStyle::none());
      }
    }
    if (material.tintColor.a > 0.f) {
      if (material.shape == BackgroundEffectShape::Callout) {
        Path const path = materialPath(rect, corners, material);
        canvas.drawPath(path, FillStyle::solid(material.tintColor), StrokeStyle::none(), ShadowStyle::none());
      } else {
        canvas.drawRect(rect, corners, FillStyle::solid(material.tintColor), StrokeStyle::none(), ShadowStyle::none());
      }
    }
    if (material.shape == BackgroundEffectShape::Callout && material.borderColor.a > 0.f) {
      Path const path = materialPath(rect, corners, material);
      canvas.drawPath(path, FillStyle::none(), StrokeStyle::solid(material.borderColor, 1.f), ShadowStyle::none());
    }
  };

  if (materialShouldCoverAnimatedSurface(surface, material)) {
    CornerRadius const corners = material.cornerRadiusSet ? material.cornerRadius : contentCorners;
    drawMaterialRect(fullContentRect, corners);
    return;
  }

  for (auto const& region : surface.backgroundBlurRects) {
    Rect const requested = Rect::sharp(static_cast<float>(surface.x + region.x),
                                      static_cast<float>(surface.y + region.y),
                                      static_cast<float>(region.width),
                                      static_cast<float>(region.height));
    Rect const rect = intersectRect(requested, fullContentRect);
    if (rect.width <= 0.f || rect.height <= 0.f) continue;
    CornerRadius const effectCorners = material.cornerRadiusSet ? material.cornerRadius : contentCorners;
    CornerRadius const corners = material.cornerRadiusSet
                                     ? effectCorners
                                     : sameRect(rect, fullContentRect)
                                           ? effectCorners
                                           : cornerRadiusForPiece(fullContentRect, rect, effectCorners);
    drawMaterialRect(rect, corners);
  }
}

void drawSurfaceMaterialBorder(Canvas& canvas,
                               CommittedSurfaceSnapshot const& surface,
                               Rect const& fullContentRect,
                               CornerRadius const& contentCorners,
                               CornerRadius const& windowCorners) {
  ResolvedGlassMaterial const material = resolvedGlassMaterial(surface);
  if (!material.enabled || material.borderColor.a <= 0.f) return;
  if (material.shape == BackgroundEffectShape::Callout) return;

  if (windowExternalTitleBarHeight(surface) > 0.f) {
    (void)windowCorners;
    return;
  }

  for (auto const& region : surface.backgroundBlurRects) {
    Rect const requested = Rect::sharp(static_cast<float>(surface.x + region.x),
                                      static_cast<float>(surface.y + region.y),
                                      static_cast<float>(region.width),
                                      static_cast<float>(region.height));
    Rect const rect = intersectRect(requested, fullContentRect);
    if (rect.width <= 0.f || rect.height <= 0.f) continue;
    CornerRadius const effectCorners = material.cornerRadiusSet
                                           ? material.cornerRadius
                                           : contentCorners;
    CornerRadius const corners = material.cornerRadiusSet
                                     ? effectCorners
                                     : sameRect(rect, fullContentRect)
                                           ? effectCorners
                                           : cornerRadiusForPiece(fullContentRect, rect, effectCorners);
    StrokeStyle const stroke = StrokeStyle::solid(material.borderColor, 1.f);
    if (material.shape == BackgroundEffectShape::Callout) {
      Path const path = materialPath(rect, corners, material);
      canvas.drawPath(path, FillStyle::none(), stroke, ShadowStyle::none());
    } else {
      canvas.drawRect(rect, corners, FillStyle::none(), stroke, ShadowStyle::none());
    }
  }
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
    LAMBDA_RESIZE_TRACE(
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
  float const titleBarHeight = windowExternalTitleBarHeight(surface);
  bool const cutoutChrome = windowUsesCutoutChrome(surface);
  CornerRadius const windowCorners = chrome.windowCornerRadius;
  CornerRadius const contentCorners{};
  float const animationMs = static_cast<float>(
      std::chrono::duration_cast<std::chrono::milliseconds>(frameTime - visual.firstSeen).count());
  float const openProgress = animationsEnabled
                                 ? easeOutCubic(animationMs / static_cast<float>(kSurfaceOpenAnimationDuration.count()))
                                 : 1.f;
  float const openScale = 0.965f + 0.035f * openProgress;
  float const openOpacity = openProgress;
  float const outerHeight = windowHeight + titleBarHeight;
  Point const pivot{windowX + windowWidth * 0.5f, windowY - titleBarHeight + outerHeight * 0.5f};
  canvas.save();
  if (auto const windowClip = reservedWindowClip(surface)) {
    canvas.clipRect(*windowClip);
  }
  canvas.setOpacity(canvas.opacity() * openOpacity);
  if (openScale < 1.f) {
    canvas.translate(pivot.x, pivot.y);
    canvas.scale(openScale);
    canvas.translate(-pivot.x, -pivot.y);
  }
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
  bool const geometryAnimationContentSizeMismatch =
      surface.pacingSizing &&
      surface.geometryAnimationGrowing &&
      surface.committedWidth > 0 &&
      surface.committedHeight > 0 &&
      (surface.committedWidth != surface.width ||
       surface.committedHeight != surface.height);
  float const contentWidth = geometryAnimationContentSizeMismatch
                                 ? static_cast<float>(surface.committedWidth)
                                 : clientContentSmallerThanFrame
                                       ? static_cast<float>(surface.destinationWidth)
                                       : windowWidth;
  float const contentHeight = geometryAnimationContentSizeMismatch
                                  ? static_cast<float>(surface.committedHeight)
                                  : clientContentSmallerThanFrame
                                        ? static_cast<float>(surface.destinationHeight)
                                        : windowHeight;
  Rect const fullContentRect = windowContentRect(surface);
  Rect const visibleContentRect = windowVisibleContentRect(surface, chrome.contentInsetWidth, canvas.dpiScale());
  CornerRadius const visibleContentCorners{};
  bool const systemExternalChrome = !cutoutChrome && windowExternalTitleBarHeight(surface) > 0.f;
  if (!systemExternalChrome) {
    drawSurfaceBackgroundBlur(canvas, surface, visibleContentRect, visibleContentCorners);
  }
  drawWindowFrameShadow(canvas, surface, chrome);
  if (!cutoutChrome) drawWindowChrome(canvas, textSystem, surface, chrome);
  canvas.save();
  canvas.clipRect(visibleContentRect, visibleContentCorners, true);
  if (systemExternalChrome) {
    drawSurfaceBackgroundBlur(canvas, surface, visibleContentRect, visibleContentCorners);
  }
  drawClientSurfacePiece(canvas,
                         clientImage,
                         Rect::sharp(surface.sourceX,
                                     surface.sourceY,
                                     sourceWidth,
                                     sourceHeight),
                         Rect::sharp(visibleContentRect.x,
                                     visibleContentRect.y,
                                     std::min(contentWidth, visibleContentRect.width),
                                     std::min(contentHeight, visibleContentRect.height)),
                         visibleContentRect,
                         visibleContentCorners);
  if (clientContentSmallerThanFrame && !geometryAnimationContentSizeMismatch && !surface.activeSizing) {
    float const drawnContentWidth = std::min(contentWidth, visibleContentRect.width);
    float const drawnContentHeight = std::min(contentHeight, visibleContentRect.height);
    float const rightPad = std::max(0.f, visibleContentRect.width - drawnContentWidth);
    float const bottomPad = std::max(0.f, visibleContentRect.height - drawnContentHeight);
    float const edgeSourceWidth = std::max(1.f, sourceWidth);
    float const edgeSourceHeight = std::max(1.f, sourceHeight);
    if (rightPad > 0.f) {
      drawClientSurfacePiece(canvas,
                             clientImage,
                             Rect::sharp(surface.sourceX + edgeSourceWidth - 1.f,
                                         surface.sourceY,
                                         1.f,
                                         edgeSourceHeight),
                             Rect::sharp(visibleContentRect.x + drawnContentWidth,
                                         visibleContentRect.y,
                                         rightPad,
                                         drawnContentHeight),
                             visibleContentRect,
                             visibleContentCorners);
    }
    if (bottomPad > 0.f) {
      drawClientSurfacePiece(canvas,
                             clientImage,
                             Rect::sharp(surface.sourceX,
                                         surface.sourceY + edgeSourceHeight - 1.f,
                                         edgeSourceWidth,
                                         1.f),
                             Rect::sharp(visibleContentRect.x,
                                         visibleContentRect.y + drawnContentHeight,
                                         drawnContentWidth,
                                         bottomPad),
                             visibleContentRect,
                             visibleContentCorners);
    }
    if (rightPad > 0.f && bottomPad > 0.f) {
      drawClientSurfacePiece(canvas,
                             clientImage,
                             Rect::sharp(surface.sourceX + edgeSourceWidth - 1.f,
                                         surface.sourceY + edgeSourceHeight - 1.f,
                                         1.f,
                                         1.f),
                             Rect::sharp(visibleContentRect.x + drawnContentWidth,
                                         visibleContentRect.y + drawnContentHeight,
                                         rightPad,
                                         bottomPad),
                             visibleContentRect,
                             visibleContentCorners);
    }
  }
  canvas.restore();
  drawSurfaceMaterialBorder(canvas, surface, fullContentRect, contentCorners, windowCorners);
  if (cutoutChrome) drawWindowChrome(canvas, textSystem, surface, chrome);
  drawWindowFrameBorder(canvas, surface, chrome);
  canvas.restore();
}

} // namespace lambda::compositor
