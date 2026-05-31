#pragma once

#include "Compositor/Wayland/WaylandServerImpl.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace lambda::compositor {

using PointerConstraintRegionRect = CommittedSurfaceSnapshot::RegionRect;

[[nodiscard]] inline bool pointerConstraintRectEmpty(PointerConstraintRegionRect const& rect) {
  return rect.width <= 0 || rect.height <= 0;
}

[[nodiscard]] inline bool pointerConstraintRegionsEqual(std::vector<PointerConstraintRegionRect> const& a,
                                                        std::vector<PointerConstraintRegionRect> const& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i].x != b[i].x || a[i].y != b[i].y || a[i].width != b[i].width || a[i].height != b[i].height) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] inline PointerConstraintRegionRect pointerConstraintSurfaceRect(
    WaylandServer::Impl::Surface const* surface) {
  std::int32_t const width = surface && surface->frameWidth > 0
                                 ? surface->frameWidth
                                 : surface ? std::max(0,
                                                       surfaceTransformedBufferWidth(surface) /
                                                           std::max(1, surface->bufferState.scale))
                                           : 0;
  std::int32_t const height = surface && surface->frameHeight > 0
                                  ? surface->frameHeight
                                  : surface ? std::max(0,
                                                        surfaceTransformedBufferHeight(surface) /
                                                            std::max(1, surface->bufferState.scale))
                                            : 0;
  return {.x = 0,
          .y = 0,
          .width = width,
          .height = height};
}

[[nodiscard]] inline PointerConstraintRegionRect pointerConstraintIntersectRect(
    PointerConstraintRegionRect const& a,
    PointerConstraintRegionRect const& b) {
  std::int64_t const left = std::max<std::int64_t>(a.x, b.x);
  std::int64_t const top = std::max<std::int64_t>(a.y, b.y);
  std::int64_t const right = std::min<std::int64_t>(static_cast<std::int64_t>(a.x) + a.width,
                                                   static_cast<std::int64_t>(b.x) + b.width);
  std::int64_t const bottom = std::min<std::int64_t>(static_cast<std::int64_t>(a.y) + a.height,
                                                    static_cast<std::int64_t>(b.y) + b.height);
  if (left >= right || top >= bottom) return {};
  return {.x = static_cast<std::int32_t>(left),
          .y = static_cast<std::int32_t>(top),
          .width = static_cast<std::int32_t>(right - left),
          .height = static_cast<std::int32_t>(bottom - top)};
}

[[nodiscard]] inline std::vector<PointerConstraintRegionRect> pointerConstraintSurfaceInputRegion(
    WaylandServer::Impl::Surface const* surface) {
  PointerConstraintRegionRect const bounds = pointerConstraintSurfaceRect(surface);
  if (pointerConstraintRectEmpty(bounds)) return {};
  if (!surface || surface->regionState.inputRegionInfinite) return {bounds};

  std::vector<PointerConstraintRegionRect> rects;
  rects.reserve(surface->regionState.inputRegionRects.size());
  for (auto const& rect : surface->regionState.inputRegionRects) {
    PointerConstraintRegionRect const clipped = pointerConstraintIntersectRect(bounds, rect);
    if (!pointerConstraintRectEmpty(clipped)) rects.push_back(clipped);
  }
  return rects;
}

[[nodiscard]] inline std::vector<PointerConstraintRegionRect> pointerConstraintEffectiveRegion(
    WaylandServer::Impl::PointerConstraint const* constraint) {
  if (!constraint || !constraint->surface) return {};
  std::vector<PointerConstraintRegionRect> const inputRegion = pointerConstraintSurfaceInputRegion(constraint->surface);
  if (constraint->regionInfinite) return inputRegion;

  std::vector<PointerConstraintRegionRect> rects;
  rects.reserve(inputRegion.size() * std::max<std::size_t>(constraint->regionRects.size(), 1u));
  for (auto const& inputRect : inputRegion) {
    for (auto const& constraintRect : constraint->regionRects) {
      PointerConstraintRegionRect const clipped = pointerConstraintIntersectRect(inputRect, constraintRect);
      if (!pointerConstraintRectEmpty(clipped)) rects.push_back(clipped);
    }
  }
  return rects;
}

inline bool rebuildPointerConstraintEffectiveRegion(WaylandServer::Impl::PointerConstraint* constraint) {
  if (!constraint) return false;
  std::vector<PointerConstraintRegionRect> effective = pointerConstraintEffectiveRegion(constraint);
  bool const changed = !pointerConstraintRegionsEqual(constraint->effectiveRegionRects, effective);
  constraint->effectiveRegionRects = std::move(effective);
  return changed;
}

[[nodiscard]] inline bool pointerConstraintHasPendingState(WaylandServer::Impl::PointerConstraint const* constraint) {
  return constraint && !constraint->defunct && (constraint->pendingRegionSet || constraint->pendingCursorHintSet);
}

[[nodiscard]] inline bool pointerConstraintShouldDestroyAfterDeactivation(
    WaylandServer::Impl::PointerConstraint const* constraint) {
  return constraint && !constraint->active &&
         constraint->lifetime == ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_ONESHOT;
}

[[nodiscard]] inline WaylandServer::Impl::PointerConstraintCommitState takePointerConstraintPendingState(
    WaylandServer::Impl::PointerConstraint* constraint) {
  WaylandServer::Impl::PointerConstraintCommitState state;
  if (!pointerConstraintHasPendingState(constraint)) return state;
  state.constraint = constraint;
  if (constraint->pendingRegionSet) {
    state.regionInfinite = constraint->pendingRegionInfinite;
    state.regionRects = std::move(constraint->pendingRegionRects);
    state.regionSet = true;
    constraint->pendingRegionInfinite = true;
    constraint->pendingRegionSet = false;
  }
  if (constraint->pendingCursorHintSet) {
    state.cursorHintX = constraint->pendingCursorHintX;
    state.cursorHintY = constraint->pendingCursorHintY;
    state.cursorHintSet = true;
    constraint->pendingCursorHintSet = false;
  }
  return state;
}

inline bool applyPointerConstraintCommitState(WaylandServer::Impl::PointerConstraintCommitState const& state) {
  auto* constraint = state.constraint;
  if (!constraint || constraint->defunct) return false;
  bool stateChanged = false;
  if (state.regionSet) {
    constraint->regionInfinite = state.regionInfinite;
    constraint->regionRects = state.regionRects;
    stateChanged = true;
  }
  if (state.cursorHintSet) {
    constraint->cursorHintX = state.cursorHintX;
    constraint->cursorHintY = state.cursorHintY;
    constraint->cursorHintSet = true;
    stateChanged = true;
  }
  return rebuildPointerConstraintEffectiveRegion(constraint) || stateChanged;
}

inline bool applyPointerConstraintPendingState(WaylandServer::Impl::PointerConstraint* constraint) {
  if (!constraint || constraint->defunct) return false;
  auto state = takePointerConstraintPendingState(constraint);
  if (!state.constraint) return rebuildPointerConstraintEffectiveRegion(constraint);
  return applyPointerConstraintCommitState(state);
}

[[nodiscard]] inline std::vector<WaylandServer::Impl::PointerConstraintCommitState>
takePointerConstraintPendingStates(WaylandServer::Impl* server,
                                   WaylandServer::Impl::Surface const* surface) {
  std::vector<WaylandServer::Impl::PointerConstraintCommitState> states;
  if (!server || !surface) return states;
  for (auto const& constraint : server->pointerConstraints_) {
    if (constraint->surface != surface || !pointerConstraintHasPendingState(constraint.get())) continue;
    states.push_back(takePointerConstraintPendingState(constraint.get()));
  }
  return states;
}

inline void mergePointerConstraintCommitStates(
    std::vector<WaylandServer::Impl::PointerConstraintCommitState>& cached,
    std::vector<WaylandServer::Impl::PointerConstraintCommitState>&& incoming) {
  for (auto& incomingState : incoming) {
    if (!incomingState.constraint) continue;
    auto cachedIt = std::find_if(cached.begin(),
                                 cached.end(),
                                 [&incomingState](auto const& state) {
                                   return state.constraint == incomingState.constraint;
                                 });
    if (cachedIt == cached.end()) {
      cached.push_back(std::move(incomingState));
      continue;
    }
    if (incomingState.regionSet) {
      cachedIt->regionInfinite = incomingState.regionInfinite;
      cachedIt->regionRects = std::move(incomingState.regionRects);
      cachedIt->regionSet = true;
    }
    if (incomingState.cursorHintSet) {
      cachedIt->cursorHintX = incomingState.cursorHintX;
      cachedIt->cursorHintY = incomingState.cursorHintY;
      cachedIt->cursorHintSet = true;
    }
  }
}

[[nodiscard]] inline bool pointerConstraintRegionContainsLocalPoint(
    WaylandServer::Impl::PointerConstraint const* constraint,
    float localX,
    float localY) {
  if (!constraint) return false;
  for (auto const& rect : constraint->effectiveRegionRects) {
    if (localX >= static_cast<float>(rect.x) &&
        localY >= static_cast<float>(rect.y) &&
        localX < static_cast<float>(rect.x + rect.width) &&
        localY < static_cast<float>(rect.y + rect.height)) {
      return true;
    }
  }
  return false;
}

inline bool clampPointerConstraintLocalPoint(WaylandServer::Impl::PointerConstraint const* constraint,
                                             float& localX,
                                             float& localY) {
  if (!constraint || constraint->effectiveRegionRects.empty()) return false;
  if (pointerConstraintRegionContainsLocalPoint(constraint, localX, localY)) return true;

  float bestX = localX;
  float bestY = localY;
  float bestDistance = std::numeric_limits<float>::max();
  for (auto const& rect : constraint->effectiveRegionRects) {
    if (pointerConstraintRectEmpty(rect)) continue;
    float const minX = static_cast<float>(rect.x);
    float const minY = static_cast<float>(rect.y);
    float const maxX = static_cast<float>(rect.x + std::max(0, rect.width - 1));
    float const maxY = static_cast<float>(rect.y + std::max(0, rect.height - 1));
    float const candidateX = std::clamp(localX, minX, maxX);
    float const candidateY = std::clamp(localY, minY, maxY);
    float const dx = candidateX - localX;
    float const dy = candidateY - localY;
    float const distance = dx * dx + dy * dy;
    if (distance < bestDistance) {
      bestDistance = distance;
      bestX = candidateX;
      bestY = candidateY;
    }
  }
  if (bestDistance == std::numeric_limits<float>::max()) return false;
  localX = bestX;
  localY = bestY;
  return true;
}

inline bool clampPointerConstraintGlobalPoint(WaylandServer::Impl::PointerConstraint const* constraint,
                                              float& globalX,
                                              float& globalY) {
  if (!constraint || !constraint->surface) return false;
  float localX = globalX - static_cast<float>(constraint->surface->windowX);
  float localY = globalY - static_cast<float>(constraint->surface->windowY);
  if (!clampPointerConstraintLocalPoint(constraint, localX, localY)) return false;
  globalX = static_cast<float>(constraint->surface->windowX) + localX;
  globalY = static_cast<float>(constraint->surface->windowY) + localY;
  return true;
}

} // namespace lambda::compositor
