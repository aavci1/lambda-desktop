#include "Compositor/SceneDamage.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace lambda::compositor {
namespace {

using RegionRect = CommittedSurfaceSnapshot::RegionRect;

constexpr std::size_t kMaxDamageRects = 64;

bool rectEmpty(RegionRect const& rect) {
  return rect.width <= 0 || rect.height <= 0;
}

bool rectsEqual(RegionRect const& lhs, RegionRect const& rhs) {
  return lhs.x == rhs.x &&
         lhs.y == rhs.y &&
         lhs.width == rhs.width &&
         lhs.height == rhs.height;
}

bool regionRectsEqual(std::vector<RegionRect> const& lhs,
                      std::vector<RegionRect> const& rhs) {
  return lhs.size() == rhs.size() &&
         std::equal(lhs.begin(), lhs.end(), rhs.begin(), rectsEqual);
}

bool colorsEqual(Color const& lhs, Color const& rhs) {
  return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b && lhs.a == rhs.a;
}

bool cornerRadiiEqual(CornerRadius const& lhs, CornerRadius const& rhs) {
  return lhs.topLeft == rhs.topLeft &&
         lhs.topRight == rhs.topRight &&
         lhs.bottomRight == rhs.bottomRight &&
         lhs.bottomLeft == rhs.bottomLeft;
}

bool backgroundEffectsEqual(SurfaceBackgroundEffectSnapshot const& lhs,
                            SurfaceBackgroundEffectSnapshot const& rhs) {
  return lhs.blurRadius == rhs.blurRadius &&
         colorsEqual(lhs.baseColor, rhs.baseColor) &&
         colorsEqual(lhs.tint, rhs.tint) &&
         colorsEqual(lhs.borderColor, rhs.borderColor) &&
         lhs.cornerRadiusSet == rhs.cornerRadiusSet &&
         cornerRadiiEqual(lhs.cornerRadius, rhs.cornerRadius) &&
         lhs.shape == rhs.shape &&
         lhs.calloutPlacement == rhs.calloutPlacement &&
         lhs.arrowWidth == rhs.arrowWidth &&
         lhs.arrowHeight == rhs.arrowHeight;
}

RegionRect fullOutputRect(std::int32_t outputWidth, std::int32_t outputHeight) {
  return RegionRect{.x = 0, .y = 0, .width = outputWidth, .height = outputHeight};
}

RegionRect clippedRect(RegionRect rect, std::int32_t outputWidth, std::int32_t outputHeight) {
  std::int32_t const left = std::clamp(rect.x, 0, outputWidth);
  std::int32_t const top = std::clamp(rect.y, 0, outputHeight);
  std::int32_t const right = std::clamp(rect.x + rect.width, 0, outputWidth);
  std::int32_t const bottom = std::clamp(rect.y + rect.height, 0, outputHeight);
  return RegionRect{.x = left, .y = top, .width = right - left, .height = bottom - top};
}

void makeFullDamage(SceneDamageResult& damage,
                    std::int32_t outputWidth,
                    std::int32_t outputHeight) {
  damage.fullOutput = outputWidth > 0 && outputHeight > 0;
  damage.rects.clear();
  if (damage.fullOutput) damage.rects.push_back(fullOutputRect(outputWidth, outputHeight));
}

void appendDamageRect(SceneDamageResult& damage,
                      RegionRect rect,
                      std::int32_t outputWidth,
                      std::int32_t outputHeight) {
  if (damage.fullOutput || outputWidth <= 0 || outputHeight <= 0) return;
  rect = clippedRect(rect, outputWidth, outputHeight);
  if (rectEmpty(rect)) return;
  if (rect.x <= 0 && rect.y <= 0 && rect.width >= outputWidth && rect.height >= outputHeight) {
    makeFullDamage(damage, outputWidth, outputHeight);
    return;
  }
  if (damage.rects.size() >= kMaxDamageRects) {
    makeFullDamage(damage, outputWidth, outputHeight);
    return;
  }
  damage.rects.push_back(rect);
}

bool surfaceOrderChanged(std::vector<CommittedSurfaceSnapshot> const& previous,
                         std::vector<CommittedSurfaceSnapshot> const& current) {
  auto containsId = [](std::vector<CommittedSurfaceSnapshot> const& surfaces, std::uint64_t id) {
    return std::any_of(surfaces.begin(), surfaces.end(), [id](auto const& surface) {
      return surface.id == id;
    });
  };
  std::vector<std::uint64_t> previousCommon;
  previousCommon.reserve(previous.size());
  for (auto const& surface : previous) {
    if (surface.id != 0 && containsId(current, surface.id)) previousCommon.push_back(surface.id);
  }
  std::vector<std::uint64_t> currentCommon;
  currentCommon.reserve(current.size());
  for (auto const& surface : current) {
    if (surface.id != 0 && containsId(previous, surface.id)) currentCommon.push_back(surface.id);
  }
  return previousCommon != currentCommon;
}

bool contentMappingEqual(CommittedSurfaceSnapshot const& lhs,
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

bool frameVisualEqual(CommittedSurfaceSnapshot const& lhs,
                      CommittedSurfaceSnapshot const& rhs) {
  return rectsEqual(sceneSurfaceFrameRect(lhs), sceneSurfaceFrameRect(rhs)) &&
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
         lhs.activeSizing == rhs.activeSizing &&
         lhs.pacingSizing == rhs.pacingSizing &&
         lhs.geometryAnimationGrowing == rhs.geometryAnimationGrowing &&
         lhs.shadowClipTop == rhs.shadowClipTop &&
         lhs.shadowClipBottom == rhs.shadowClipBottom &&
         lhs.windowClipTop == rhs.windowClipTop &&
         lhs.windowClipBottom == rhs.windowClipBottom &&
         backgroundEffectsEqual(lhs.backgroundEffect, rhs.backgroundEffect) &&
         regionRectsEqual(lhs.backgroundBlurRects, rhs.backgroundBlurRects);
}

bool canMapBufferDamage(CommittedSurfaceSnapshot const& previous,
                        CommittedSurfaceSnapshot const& current) {
  return frameVisualEqual(previous, current) &&
         contentMappingEqual(previous, current) &&
         current.bufferTransform == 0 &&
         current.bufferWidth > 0 &&
         current.bufferHeight > 0 &&
         current.width > 0 &&
         current.height > 0;
}

RegionRect mapBufferDamageToSurface(CommittedSurfaceSnapshot const& surface,
                                    RegionRect const& damage) {
  double const sourceX = surface.sourceX;
  double const sourceY = surface.sourceY;
  double const sourceWidth = surface.sourceWidth > 0.f
                                 ? static_cast<double>(surface.sourceWidth)
                                 : static_cast<double>(surface.bufferWidth);
  double const sourceHeight = surface.sourceHeight > 0.f
                                  ? static_cast<double>(surface.sourceHeight)
                                  : static_cast<double>(surface.bufferHeight);
  if (sourceWidth <= 0.0 || sourceHeight <= 0.0) return sceneSurfaceContentRect(surface);

  double const left = std::max(static_cast<double>(damage.x), sourceX);
  double const top = std::max(static_cast<double>(damage.y), sourceY);
  double const right = std::min(static_cast<double>(damage.x + damage.width), sourceX + sourceWidth);
  double const bottom = std::min(static_cast<double>(damage.y + damage.height), sourceY + sourceHeight);
  if (right <= left || bottom <= top) return {};

  double const scaleX = static_cast<double>(surface.width) / sourceWidth;
  double const scaleY = static_cast<double>(surface.height) / sourceHeight;
  double const mappedLeft = static_cast<double>(surface.x) + (left - sourceX) * scaleX;
  double const mappedTop = static_cast<double>(surface.y) + (top - sourceY) * scaleY;
  double const mappedRight = static_cast<double>(surface.x) + (right - sourceX) * scaleX;
  double const mappedBottom = static_cast<double>(surface.y) + (bottom - sourceY) * scaleY;

  std::int32_t const x0 = static_cast<std::int32_t>(std::floor(mappedLeft));
  std::int32_t const y0 = static_cast<std::int32_t>(std::floor(mappedTop));
  std::int32_t const x1 = static_cast<std::int32_t>(std::ceil(mappedRight));
  std::int32_t const y1 = static_cast<std::int32_t>(std::ceil(mappedBottom));
  return RegionRect{.x = x0, .y = y0, .width = x1 - x0, .height = y1 - y0};
}

bool cursorSnapshotsEqual(std::optional<CommittedSurfaceSnapshot> const& previous,
                          std::optional<CommittedSurfaceSnapshot> const& current) {
  if (previous.has_value() != current.has_value()) return false;
  if (!previous && !current) return true;
  return previous->id == current->id &&
         previous->serial == current->serial &&
         contentMappingEqual(*previous, *current);
}

CommittedSurfaceSnapshot retainedDamageSnapshot(CommittedSurfaceSnapshot snapshot) {
  snapshot.bufferDamageRects.clear();
  snapshot.rgbaPixels.reset();
  snapshot.shmPixels = nullptr;
  snapshot.shmPixelBytes = 0;
  snapshot.dmabufPlanes.clear();
  return snapshot;
}

std::vector<CommittedSurfaceSnapshot>
retainedDamageSnapshots(std::vector<CommittedSurfaceSnapshot> const& surfaces) {
  std::vector<CommittedSurfaceSnapshot> retained;
  retained.reserve(surfaces.size());
  for (auto const& surface : surfaces) {
    retained.push_back(retainedDamageSnapshot(surface));
  }
  return retained;
}

std::optional<CommittedSurfaceSnapshot>
retainedDamageSnapshot(std::optional<CommittedSurfaceSnapshot> const& cursor) {
  if (!cursor) return std::nullopt;
  return retainedDamageSnapshot(*cursor);
}

void storeDamageState(SceneDamageState& state,
                      std::vector<CommittedSurfaceSnapshot> const& surfaces,
                      std::optional<CommittedSurfaceSnapshot> const& cursor,
                      std::int32_t outputWidth,
                      std::int32_t outputHeight) {
  state.initialized = true;
  state.outputWidth = outputWidth;
  state.outputHeight = outputHeight;
  state.surfaces = retainedDamageSnapshots(surfaces);
  state.cursor = retainedDamageSnapshot(cursor);
}

} // namespace

CommittedSurfaceSnapshot::RegionRect
sceneSurfaceContentRect(CommittedSurfaceSnapshot const& surface) {
  return RegionRect{
      .x = surface.x,
      .y = surface.y,
      .width = std::max(0, surface.width),
      .height = std::max(0, surface.height),
  };
}

CommittedSurfaceSnapshot::RegionRect
sceneSurfaceFrameRect(CommittedSurfaceSnapshot const& surface) {
  std::int32_t const titleBarHeight = std::max(0, surface.titleBarHeight);
  return RegionRect{
      .x = surface.x,
      .y = surface.y - titleBarHeight,
      .width = std::max(0, surface.width),
      .height = std::max(0, surface.height + titleBarHeight),
  };
}

SceneDamageResult updateSceneDamage(SceneDamageState& state,
                                    std::vector<CommittedSurfaceSnapshot> const& surfaces,
                                    std::optional<CommittedSurfaceSnapshot> const& cursor,
                                    std::int32_t outputWidth,
                                    std::int32_t outputHeight,
                                    bool forceFullDamage) {
  SceneDamageResult damage;
  if (forceFullDamage ||
      !state.initialized ||
      state.outputWidth != outputWidth ||
      state.outputHeight != outputHeight) {
    makeFullDamage(damage, outputWidth, outputHeight);
    storeDamageState(state, surfaces, cursor, outputWidth, outputHeight);
    return damage;
  }

  if (surfaceOrderChanged(state.surfaces, surfaces)) {
    makeFullDamage(damage, outputWidth, outputHeight);
  }

  std::unordered_map<std::uint64_t, CommittedSurfaceSnapshot const*> previousById;
  previousById.reserve(state.surfaces.size());
  for (auto const& previous : state.surfaces) {
    if (previous.id != 0) previousById[previous.id] = &previous;
  }

  std::unordered_map<std::uint64_t, CommittedSurfaceSnapshot const*> currentById;
  currentById.reserve(surfaces.size());
  for (auto const& current : surfaces) {
    if (current.id != 0) currentById[current.id] = &current;
  }

  for (auto const& previous : state.surfaces) {
    if (previous.id != 0 && currentById.contains(previous.id)) continue;
    appendDamageRect(damage, sceneSurfaceFrameRect(previous), outputWidth, outputHeight);
  }

  for (auto const& current : surfaces) {
    auto const previous = previousById.find(current.id);
    if (current.id == 0 || previous == previousById.end()) {
      appendDamageRect(damage, sceneSurfaceFrameRect(current), outputWidth, outputHeight);
      continue;
    }

    CommittedSurfaceSnapshot const& old = *previous->second;
    if (old.serial == current.serial &&
        old.dmabufBufferId == current.dmabufBufferId &&
        contentMappingEqual(old, current) &&
        frameVisualEqual(old, current)) {
      continue;
    }

    if (!canMapBufferDamage(old, current)) {
      appendDamageRect(damage, sceneSurfaceFrameRect(old), outputWidth, outputHeight);
      appendDamageRect(damage, sceneSurfaceFrameRect(current), outputWidth, outputHeight);
      continue;
    }

    if (current.bufferDamageRects.empty()) {
      appendDamageRect(damage, sceneSurfaceContentRect(current), outputWidth, outputHeight);
      continue;
    }

    for (RegionRect const& rect : current.bufferDamageRects) {
      appendDamageRect(damage, mapBufferDamageToSurface(current, rect), outputWidth, outputHeight);
    }
  }

  if (!cursorSnapshotsEqual(state.cursor, cursor)) {
    if (state.cursor) appendDamageRect(damage, sceneSurfaceContentRect(*state.cursor), outputWidth, outputHeight);
    if (cursor) appendDamageRect(damage, sceneSurfaceContentRect(*cursor), outputWidth, outputHeight);
  }

  storeDamageState(state, surfaces, cursor, outputWidth, outputHeight);
  return damage;
}

} // namespace lambda::compositor
