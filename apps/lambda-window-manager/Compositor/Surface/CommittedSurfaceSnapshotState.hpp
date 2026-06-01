#pragma once

#include "Compositor/Wayland/WaylandTypes.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace lambda::compositor {

template <typename Visitor>
void visitCommittedSurfaceContentShape(CommittedSurfaceSnapshot const& surface,
                                       Visitor&& visit) {
  visit(surface.width);
  visit(surface.height);
  visit(surface.committedWidth);
  visit(surface.committedHeight);
  visit(surface.bufferWidth);
  visit(surface.bufferHeight);
  visit(surface.bufferTransform);
  visit(surface.sourceX);
  visit(surface.sourceY);
  visit(surface.sourceWidth);
  visit(surface.sourceHeight);
  visit(surface.destinationWidth);
  visit(surface.destinationHeight);
}

template <typename Visitor>
void visitCommittedSurfaceContentMapping(CommittedSurfaceSnapshot const& surface,
                                         Visitor&& visit) {
  visit(surface.x);
  visit(surface.y);
  visitCommittedSurfaceContentShape(surface, visit);
}

template <typename Visitor>
void visitCommittedSurfaceRegionRect(CommittedSurfaceSnapshot::RegionRect const& rect,
                                     Visitor&& visit) {
  visit(rect.x);
  visit(rect.y);
  visit(rect.width);
  visit(rect.height);
}

template <typename Visitor>
void visitCommittedSurfaceBackgroundEffect(SurfaceBackgroundEffectSnapshot const& effect,
                                           Visitor&& visit) {
  visit(effect.blurRadius);
  visit(effect.baseColor.r);
  visit(effect.baseColor.g);
  visit(effect.baseColor.b);
  visit(effect.baseColor.a);
  visit(effect.tint.r);
  visit(effect.tint.g);
  visit(effect.tint.b);
  visit(effect.tint.a);
  visit(effect.borderColor.r);
  visit(effect.borderColor.g);
  visit(effect.borderColor.b);
  visit(effect.borderColor.a);
  visit(effect.cornerRadiusSet);
  visit(effect.cornerRadius.topLeft);
  visit(effect.cornerRadius.topRight);
  visit(effect.cornerRadius.bottomRight);
  visit(effect.cornerRadius.bottomLeft);
  visit(static_cast<std::uint8_t>(effect.shape));
  visit(static_cast<std::uint8_t>(effect.calloutPlacement));
  visit(effect.arrowWidth);
  visit(effect.arrowHeight);
}

template <typename Visitor>
void visitCommittedSurfaceFrameVisualState(CommittedSurfaceSnapshot const& surface,
                                           Visitor&& visit) {
  visit(surface.titleBarHeight);
  visit(surface.title);
  visit(surface.serverSideDecorated);
  visit(surface.cutoutsBound);
  visit(surface.cutoutsRejected);
  visit(surface.closeButtonHovered);
  visit(surface.closeButtonPressed);
  visit(surface.maximizeButtonHovered);
  visit(surface.maximizeButtonPressed);
  visit(surface.minimizeButtonHovered);
  visit(surface.minimizeButtonPressed);
  visit(surface.focused);
  visit(surface.fullscreen);
  visit(surface.activeSizing);
  visit(surface.pacingSizing);
  visit(surface.geometryAnimationGrowing);
  visit(surface.shadowClipTop);
  visit(surface.shadowClipBottom);
  visit(surface.windowClipTop);
  visit(surface.windowClipBottom);
  visitCommittedSurfaceBackgroundEffect(surface.backgroundEffect, visit);
  visit(surface.backgroundBlurRects.size());
  for (auto const& rect : surface.backgroundBlurRects) {
    visitCommittedSurfaceRegionRect(rect, visit);
  }
}

[[nodiscard]] inline CommittedSurfaceSnapshot::RegionRect
committedSurfaceContentRect(CommittedSurfaceSnapshot const& surface) {
  return CommittedSurfaceSnapshot::RegionRect{
      .x = surface.x,
      .y = surface.y,
      .width = std::max(0, surface.width),
      .height = std::max(0, surface.height),
  };
}

[[nodiscard]] inline CommittedSurfaceSnapshot::RegionRect
committedSurfaceFrameRect(CommittedSurfaceSnapshot const& surface) {
  std::int32_t const titleBarHeight = std::max(0, surface.titleBarHeight);
  return CommittedSurfaceSnapshot::RegionRect{
      .x = surface.x,
      .y = surface.y - titleBarHeight,
      .width = std::max(0, surface.width),
      .height = std::max(0, surface.height + titleBarHeight),
  };
}

[[nodiscard]] inline bool committedSurfaceRegionRectsEqual(
    CommittedSurfaceSnapshot::RegionRect const& lhs,
    CommittedSurfaceSnapshot::RegionRect const& rhs) {
  return lhs.x == rhs.x &&
         lhs.y == rhs.y &&
         lhs.width == rhs.width &&
         lhs.height == rhs.height;
}

[[nodiscard]] inline bool committedSurfaceRegionListsEqual(
    std::vector<CommittedSurfaceSnapshot::RegionRect> const& lhs,
    std::vector<CommittedSurfaceSnapshot::RegionRect> const& rhs) {
  return lhs.size() == rhs.size() &&
         std::equal(lhs.begin(), lhs.end(), rhs.begin(), committedSurfaceRegionRectsEqual);
}

[[nodiscard]] inline bool committedSurfaceBackgroundEffectsEqual(
    SurfaceBackgroundEffectSnapshot const& lhs,
    SurfaceBackgroundEffectSnapshot const& rhs) {
  return lhs.blurRadius == rhs.blurRadius &&
         lhs.baseColor.r == rhs.baseColor.r &&
         lhs.baseColor.g == rhs.baseColor.g &&
         lhs.baseColor.b == rhs.baseColor.b &&
         lhs.baseColor.a == rhs.baseColor.a &&
         lhs.tint.r == rhs.tint.r &&
         lhs.tint.g == rhs.tint.g &&
         lhs.tint.b == rhs.tint.b &&
         lhs.tint.a == rhs.tint.a &&
         lhs.borderColor.r == rhs.borderColor.r &&
         lhs.borderColor.g == rhs.borderColor.g &&
         lhs.borderColor.b == rhs.borderColor.b &&
         lhs.borderColor.a == rhs.borderColor.a &&
         lhs.cornerRadiusSet == rhs.cornerRadiusSet &&
         lhs.cornerRadius.topLeft == rhs.cornerRadius.topLeft &&
         lhs.cornerRadius.topRight == rhs.cornerRadius.topRight &&
         lhs.cornerRadius.bottomRight == rhs.cornerRadius.bottomRight &&
         lhs.cornerRadius.bottomLeft == rhs.cornerRadius.bottomLeft &&
         lhs.shape == rhs.shape &&
         lhs.calloutPlacement == rhs.calloutPlacement &&
         lhs.arrowWidth == rhs.arrowWidth &&
         lhs.arrowHeight == rhs.arrowHeight;
}

[[nodiscard]] inline bool committedSurfaceContentMappingEqual(
    CommittedSurfaceSnapshot const& lhs,
    CommittedSurfaceSnapshot const& rhs) {
  return lhs.x == rhs.x &&
         lhs.y == rhs.y &&
         lhs.width == rhs.width &&
         lhs.height == rhs.height &&
         lhs.bufferWidth == rhs.bufferWidth &&
         lhs.bufferHeight == rhs.bufferHeight &&
         lhs.bufferTransform == rhs.bufferTransform &&
         lhs.sourceX == rhs.sourceX &&
         lhs.sourceY == rhs.sourceY &&
         lhs.sourceWidth == rhs.sourceWidth &&
         lhs.sourceHeight == rhs.sourceHeight &&
         lhs.destinationWidth == rhs.destinationWidth &&
         lhs.destinationHeight == rhs.destinationHeight;
}

[[nodiscard]] inline bool committedSurfaceFrameVisualStateEqual(
    CommittedSurfaceSnapshot const& lhs,
    CommittedSurfaceSnapshot const& rhs) {
  return committedSurfaceRegionRectsEqual(committedSurfaceFrameRect(lhs), committedSurfaceFrameRect(rhs)) &&
         lhs.title == rhs.title &&
         lhs.serverSideDecorated == rhs.serverSideDecorated &&
         lhs.cutoutsBound == rhs.cutoutsBound &&
         lhs.cutoutsRejected == rhs.cutoutsRejected &&
         lhs.closeButtonHovered == rhs.closeButtonHovered &&
         lhs.closeButtonPressed == rhs.closeButtonPressed &&
         lhs.maximizeButtonHovered == rhs.maximizeButtonHovered &&
         lhs.maximizeButtonPressed == rhs.maximizeButtonPressed &&
         lhs.minimizeButtonHovered == rhs.minimizeButtonHovered &&
         lhs.minimizeButtonPressed == rhs.minimizeButtonPressed &&
         lhs.focused == rhs.focused &&
         lhs.fullscreen == rhs.fullscreen &&
         lhs.activeSizing == rhs.activeSizing &&
         lhs.pacingSizing == rhs.pacingSizing &&
         lhs.geometryAnimationGrowing == rhs.geometryAnimationGrowing &&
         lhs.shadowClipTop == rhs.shadowClipTop &&
         lhs.shadowClipBottom == rhs.shadowClipBottom &&
         lhs.windowClipTop == rhs.windowClipTop &&
         lhs.windowClipBottom == rhs.windowClipBottom &&
         committedSurfaceBackgroundEffectsEqual(lhs.backgroundEffect, rhs.backgroundEffect) &&
         committedSurfaceRegionListsEqual(lhs.backgroundBlurRects, rhs.backgroundBlurRects);
}

} // namespace lambda::compositor
