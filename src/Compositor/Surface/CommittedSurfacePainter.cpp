#include "Compositor/Surface/CommittedSurfacePainter.hpp"

#include "Compositor/Chrome/WindowFrameGeometry.hpp"
#include "Compositor/Chrome/WindowChromeRenderer.hpp"
#include "Detail/ResizeTrace.hpp"

#if FLUX_VULKAN
#include "Graphics/Vulkan/VulkanCanvas.hpp"
#endif

#include <algorithm>
#include <cmath>
#include <optional>
#include <span>

namespace flux::compositor {
namespace {

float clamp01(float value) {
  return std::clamp(value, 0.f, 1.f);
}

Color withOpacity(Color color, float opacity) {
  color.a *= clamp01(opacity);
  return color;
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
#if FLUX_VULKAN
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
  bool usesSurfaceRegions = false;
  bool customMaterial = false;
  float blurRadius = 0.f;
  Color baseColor{};
  Color tintColor{};
  Color borderColor{};
  CornerRadius cornerRadius{};
  bool cornerRadiusSet = false;
};

ResolvedGlassMaterial resolvedGlassMaterial(CommittedSurfaceSnapshot const& surface, ChromeConfig const& chrome) {
  if (!surface.backgroundBlurRects.empty()) {
    if (surface.backgroundEffect.usesDefaultMaterial) {
      if (!chrome.windowGlassEnabled || chrome.glass.blurRadius <= 0.f) {
        return {};
      }
      return ResolvedGlassMaterial{
          .enabled = true,
          .usesSurfaceRegions = true,
          .customMaterial = false,
          .blurRadius = chrome.glass.blurRadius,
          .baseColor = withOpacity(chrome.glass.baseColor, chrome.glass.opacity),
          .tintColor = withOpacity(chrome.glass.tintColor, chrome.glass.opacity),
          .borderColor = chrome.glass.borderColor,
      };
    }
    return ResolvedGlassMaterial{
        .enabled = surface.backgroundEffect.blurRadius > 0.f,
        .usesSurfaceRegions = true,
        .customMaterial = true,
        .blurRadius = surface.backgroundEffect.blurRadius,
        .baseColor = surface.backgroundEffect.baseColor,
        .tintColor = surface.backgroundEffect.tint,
        .borderColor = surface.backgroundEffect.borderColor,
        .cornerRadius = surface.backgroundEffect.cornerRadius,
        .cornerRadiusSet = surface.backgroundEffect.cornerRadiusSet,
    };
  }
  if (!chrome.windowGlassEnabled || !surface.defaultGlassEligible || chrome.glass.blurRadius <= 0.f) {
    return {};
  }
  return ResolvedGlassMaterial{
      .enabled = true,
      .usesSurfaceRegions = false,
      .customMaterial = false,
      .blurRadius = chrome.glass.blurRadius,
      .baseColor = withOpacity(chrome.glass.baseColor, chrome.glass.opacity),
      .tintColor = withOpacity(chrome.glass.tintColor, chrome.glass.opacity),
      .borderColor = chrome.glass.borderColor,
  };
}

bool materialShouldCoverFrame(CommittedSurfaceSnapshot const& surface,
                              ResolvedGlassMaterial const& material,
                              Rect const& rect,
                              Rect const& fullContentRect) {
  return material.enabled &&
         windowExternalTitleBarHeight(surface) > 0.f &&
         sameRect(rect, fullContentRect);
}

bool materialShouldCoverAnimatedSurface(CommittedSurfaceSnapshot const& surface,
                                        ResolvedGlassMaterial const& material) {
  return material.enabled &&
         material.usesSurfaceRegions &&
         surface.geometryAnimationGrowing &&
         backgroundEffectCoversCommittedContent(surface);
}

std::span<CommittedSurfaceSnapshot::RegionRect const> glassMaterialRegions(
    CommittedSurfaceSnapshot const& surface,
    ResolvedGlassMaterial const& material,
    CommittedSurfaceSnapshot::RegionRect const& defaultRegion) {
  if (!material.enabled) return {};
  if (material.usesSurfaceRegions) return std::span<CommittedSurfaceSnapshot::RegionRect const>(surface.backgroundBlurRects);
  return std::span<CommittedSurfaceSnapshot::RegionRect const>(&defaultRegion, 1);
}

void drawSurfaceBackgroundBlur(Canvas& canvas,
                               CommittedSurfaceSnapshot const& surface,
                               ChromeConfig const& chrome,
                               Rect const& fullContentRect,
                               CornerRadius const& contentCorners,
                               CornerRadius const& windowCorners) {
  ResolvedGlassMaterial const material = resolvedGlassMaterial(surface, chrome);
  if (!material.enabled) return;

  CommittedSurfaceSnapshot::RegionRect defaultRegion{
      .x = 0,
      .y = 0,
      .width = surface.width,
      .height = surface.height,
  };
  std::span<CommittedSurfaceSnapshot::RegionRect const> regions = glassMaterialRegions(surface, material, defaultRegion);

  auto drawMaterialRect = [&](Rect const& rect, CornerRadius const& corners) {
    canvas.drawBackdropBlur(rect, material.blurRadius, Colors::transparent, corners);
    if (material.baseColor.a > 0.f) {
      canvas.drawRect(rect,
                      corners,
                      FillStyle::solid(material.baseColor),
                      StrokeStyle::none(),
                      ShadowStyle::none());
    }
    if (material.tintColor.a > 0.f) {
      canvas.drawRect(rect,
                      corners,
                      FillStyle::solid(material.tintColor),
                      StrokeStyle::none(),
                      ShadowStyle::none());
    }
  };

  auto drawFrameMaterial = [&] {
    Rect const frameRect = windowFrameRect(surface);
    CornerRadius const frameCorners = material.cornerRadiusSet
                                          ? material.cornerRadius
                                          : windowCorners;
    drawMaterialRect(frameRect, frameCorners);
  };

  if (materialShouldCoverAnimatedSurface(surface, material)) {
    if (windowExternalTitleBarHeight(surface) > 0.f) {
      drawFrameMaterial();
    } else {
      CornerRadius const corners = material.cornerRadiusSet ? material.cornerRadius : contentCorners;
      drawMaterialRect(fullContentRect, corners);
    }
    return;
  }

  for (auto const& region : regions) {
    Rect const requested = Rect::sharp(static_cast<float>(surface.x + region.x),
                                      static_cast<float>(surface.y + region.y),
                                      static_cast<float>(region.width),
                                      static_cast<float>(region.height));
    Rect const rect = intersectRect(requested, fullContentRect);
    if (rect.width <= 0.f || rect.height <= 0.f) continue;
    CornerRadius const effectCorners = material.cornerRadiusSet ? material.cornerRadius : contentCorners;
    CornerRadius const corners = sameRect(rect, fullContentRect)
                                     ? effectCorners
                                     : cornerRadiusForPiece(fullContentRect, rect, effectCorners);
    if (materialShouldCoverFrame(surface, material, rect, fullContentRect)) {
      drawFrameMaterial();
      continue;
    }
    canvas.drawBackdropBlur(rect, material.blurRadius, Colors::transparent, corners);
    if (material.baseColor.a > 0.f) {
      canvas.drawRect(rect,
                      corners,
                      FillStyle::solid(material.baseColor),
                      StrokeStyle::none(),
                      ShadowStyle::none());
    }
    if (material.tintColor.a > 0.f) {
      canvas.drawRect(rect,
                      corners,
                      FillStyle::solid(material.tintColor),
                      StrokeStyle::none(),
                      ShadowStyle::none());
    }
  }
}

void drawSurfaceMaterialBorder(Canvas& canvas,
                               CommittedSurfaceSnapshot const& surface,
                               ChromeConfig const& chrome,
                               Rect const& fullContentRect,
                               CornerRadius const& contentCorners,
                               CornerRadius const& windowCorners) {
  ResolvedGlassMaterial const material = resolvedGlassMaterial(surface, chrome);
  if (!material.enabled || !material.usesSurfaceRegions || material.borderColor.a <= 0.f) return;

  if (windowExternalTitleBarHeight(surface) > 0.f) {
    Rect const frameRect = windowFrameRect(surface);
    CornerRadius const frameCorners = material.cornerRadiusSet
                                          ? material.cornerRadius
                                          : windowCorners;
    canvas.drawRect(frameRect,
                    frameCorners,
                    FillStyle::none(),
                    StrokeStyle::solid(material.borderColor, 1.f),
                    ShadowStyle::none());
    return;
  }

  std::span<CommittedSurfaceSnapshot::RegionRect const> regions;
  regions = std::span<CommittedSurfaceSnapshot::RegionRect const>(surface.backgroundBlurRects);

  for (auto const& region : regions) {
    Rect const requested = Rect::sharp(static_cast<float>(surface.x + region.x),
                                      static_cast<float>(surface.y + region.y),
                                      static_cast<float>(region.width),
                                      static_cast<float>(region.height));
    Rect const rect = intersectRect(requested, fullContentRect);
    if (rect.width <= 0.f || rect.height <= 0.f) continue;
    CornerRadius const effectCorners = material.cornerRadiusSet
                                           ? material.cornerRadius
                                           : contentCorners;
    CornerRadius const corners = sameRect(rect, fullContentRect)
                                     ? effectCorners
                                     : cornerRadiusForPiece(fullContentRect, rect, effectCorners);
    canvas.drawRect(rect,
                    corners,
                    FillStyle::none(),
                    StrokeStyle::solid(material.borderColor, 1.f),
                    ShadowStyle::none());
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
  float const titleBarHeight = windowExternalTitleBarHeight(surface);
  bool const cutoutChrome = windowUsesCutoutChrome(surface);
  CornerRadius const windowCorners = chrome.windowCornerRadius;
  CornerRadius const contentCorners = windowContentCornerRadius(surface, windowCorners);
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
  drawSurfaceBackgroundBlur(canvas, surface, chrome, fullContentRect, contentCorners, windowCorners);
  drawWindowFrameShadow(canvas, surface, chrome);
  if (!cutoutChrome) drawWindowChrome(canvas, textSystem, surface, chrome);
  canvas.save();
  canvas.clipRect(fullContentRect, contentCorners, true);
  drawClientSurfacePiece(canvas,
                         clientImage,
                         Rect::sharp(surface.sourceX,
                                     surface.sourceY,
                                     sourceWidth,
                                     sourceHeight),
                         Rect::sharp(windowX,
                                     windowY,
                                     contentWidth,
                                     contentHeight),
                         fullContentRect,
                         contentCorners);
  if (clientContentSmallerThanFrame && !geometryAnimationContentSizeMismatch) {
    float const rightPad = std::max(0.f, windowWidth - contentWidth);
    float const bottomPad = std::max(0.f, windowHeight - contentHeight);
    float const edgeSourceWidth = std::max(1.f, sourceWidth);
    float const edgeSourceHeight = std::max(1.f, sourceHeight);
    if (rightPad > 0.f) {
      drawClientSurfacePiece(canvas,
                             clientImage,
                             Rect::sharp(surface.sourceX + edgeSourceWidth - 1.f,
                                         surface.sourceY,
                                         1.f,
                                         edgeSourceHeight),
                             Rect::sharp(windowX + contentWidth,
                                         windowY,
                                         rightPad,
                                         contentHeight),
                             fullContentRect,
                             contentCorners);
    }
    if (bottomPad > 0.f) {
      drawClientSurfacePiece(canvas,
                             clientImage,
                             Rect::sharp(surface.sourceX,
                                         surface.sourceY + edgeSourceHeight - 1.f,
                                         edgeSourceWidth,
                                         1.f),
                             Rect::sharp(windowX,
                                         windowY + contentHeight,
                                         contentWidth,
                                         bottomPad),
                             fullContentRect,
                             contentCorners);
    }
    if (rightPad > 0.f && bottomPad > 0.f) {
      drawClientSurfacePiece(canvas,
                             clientImage,
                             Rect::sharp(surface.sourceX + edgeSourceWidth - 1.f,
                                         surface.sourceY + edgeSourceHeight - 1.f,
                                         1.f,
                                         1.f),
                             Rect::sharp(windowX + contentWidth,
                                         windowY + contentHeight,
                                         rightPad,
                                         bottomPad),
                             fullContentRect,
                             contentCorners);
    }
  }
  canvas.restore();
  drawSurfaceMaterialBorder(canvas, surface, chrome, fullContentRect, contentCorners, windowCorners);
  if (cutoutChrome) drawWindowChrome(canvas, textSystem, surface, chrome);
  drawWindowFrameBorder(canvas, surface, chrome);
  canvas.restore();
}

} // namespace flux::compositor
