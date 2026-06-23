#include "Compositor/SceneDamage.hpp"

#include "Compositor/Surface/CommittedSurfaceSnapshotState.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace lambdaui::compositor {
namespace {

using RegionRect = CommittedSurfaceSnapshot::RegionRect;

constexpr std::size_t kMaxDamageRects = 64;
constexpr std::uint64_t kDamageMergeAreaSlackDivisor = 5;

bool rectEmpty(RegionRect const& rect) {
  return rect.width <= 0 || rect.height <= 0;
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

std::uint64_t rectArea(RegionRect const& rect) {
  if (rect.width <= 0 || rect.height <= 0) return 0;
  return static_cast<std::uint64_t>(rect.width) * static_cast<std::uint64_t>(rect.height);
}

bool rectContains(RegionRect const& outer, RegionRect const& inner) {
  std::int64_t const outerRight = static_cast<std::int64_t>(outer.x) + std::max(0, outer.width);
  std::int64_t const outerBottom = static_cast<std::int64_t>(outer.y) + std::max(0, outer.height);
  std::int64_t const innerRight = static_cast<std::int64_t>(inner.x) + std::max(0, inner.width);
  std::int64_t const innerBottom = static_cast<std::int64_t>(inner.y) + std::max(0, inner.height);
  return outer.x <= inner.x &&
         outer.y <= inner.y &&
         outerRight >= innerRight &&
         outerBottom >= innerBottom;
}

RegionRect unionRect(RegionRect const& a, RegionRect const& b) {
  std::int32_t const left = std::min(a.x, b.x);
  std::int32_t const top = std::min(a.y, b.y);
  std::int32_t const right = std::max(a.x + std::max(0, a.width), b.x + std::max(0, b.width));
  std::int32_t const bottom = std::max(a.y + std::max(0, a.height), b.y + std::max(0, b.height));
  return RegionRect{.x = left, .y = top, .width = right - left, .height = bottom - top};
}

bool damageRectsMergeable(RegionRect const& a, RegionRect const& b) {
  RegionRect const merged = unionRect(a, b);
  std::uint64_t const combinedArea = rectArea(a) + rectArea(b);
  std::uint64_t const mergedArea = rectArea(merged);
  if (combinedArea == 0) return false;
  if (mergedArea <= combinedArea) return true;
  return mergedArea - combinedArea <= combinedArea / kDamageMergeAreaSlackDivisor;
}

void makeFullDamage(SceneDamageResult& damage,
                    std::int32_t outputWidth,
                    std::int32_t outputHeight) {
  damage.fullOutput = outputWidth > 0 && outputHeight > 0;
  damage.backgroundFillRequired = damage.fullOutput;
  damage.rects.clear();
  if (damage.fullOutput) damage.rects.push_back(fullOutputRect(outputWidth, outputHeight));
}

void appendDamageRect(SceneDamageResult& damage,
                      RegionRect rect,
                      std::int32_t outputWidth,
                      std::int32_t outputHeight,
                      bool backgroundFillRequired = true) {
  if (damage.fullOutput || outputWidth <= 0 || outputHeight <= 0) {
    if (damage.fullOutput && backgroundFillRequired) damage.backgroundFillRequired = true;
    return;
  }
  rect = clippedRect(rect, outputWidth, outputHeight);
  if (rectEmpty(rect)) return;
  damage.backgroundFillRequired = damage.backgroundFillRequired || backgroundFillRequired;
  if (rect.x <= 0 && rect.y <= 0 && rect.width >= outputWidth && rect.height >= outputHeight) {
    makeFullDamage(damage, outputWidth, outputHeight);
    return;
  }
  bool merged = true;
  while (merged) {
    merged = false;
    for (auto it = damage.rects.begin(); it != damage.rects.end(); ++it) {
      if (rectContains(*it, rect)) return;
      if (rectContains(rect, *it) || damageRectsMergeable(*it, rect)) {
        rect = clippedRect(unionRect(rect, *it), outputWidth, outputHeight);
        damage.rects.erase(it);
        if (rect.x <= 0 && rect.y <= 0 && rect.width >= outputWidth && rect.height >= outputHeight) {
          makeFullDamage(damage, outputWidth, outputHeight);
          return;
        }
        merged = true;
        break;
      }
    }
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
  return committedSurfaceContentMappingEqual(lhs, rhs);
}

bool frameVisualEqual(CommittedSurfaceSnapshot const& lhs,
                      CommittedSurfaceSnapshot const& rhs) {
  return committedSurfaceFrameVisualStateEqual(lhs, rhs);
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
  return committedSurfaceContentRect(surface);
}

CommittedSurfaceSnapshot::RegionRect
sceneSurfaceFrameRect(CommittedSurfaceSnapshot const& surface) {
  return committedSurfaceFrameRect(surface);
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

} // namespace lambdaui::compositor
