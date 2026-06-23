#pragma once

#include "Compositor/Wayland/WaylandTypes.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace lambdaui::compositor {

using SurfaceUploadDamageRect = CommittedSurfaceSnapshot::RegionRect;

[[nodiscard]] inline bool emptySurfaceUploadDamageRect(SurfaceUploadDamageRect const& rect) {
  return rect.width <= 0 || rect.height <= 0;
}

[[nodiscard]] inline SurfaceUploadDamageRect clippedSurfaceUploadDamageRect(SurfaceUploadDamageRect const& rect,
                                                                            std::int32_t bufferWidth,
                                                                            std::int32_t bufferHeight) {
  std::int32_t const left = std::clamp(rect.x, 0, bufferWidth);
  std::int32_t const top = std::clamp(rect.y, 0, bufferHeight);
  std::int32_t const right = std::clamp(rect.x + rect.width, 0, bufferWidth);
  std::int32_t const bottom = std::clamp(rect.y + rect.height, 0, bufferHeight);
  return SurfaceUploadDamageRect{
      .x = left,
      .y = top,
      .width = right - left,
      .height = bottom - top,
  };
}

[[nodiscard]] inline std::uint64_t surfaceUploadDamageRectArea(SurfaceUploadDamageRect const& rect) {
  if (emptySurfaceUploadDamageRect(rect)) return 0;
  return static_cast<std::uint64_t>(rect.width) * static_cast<std::uint64_t>(rect.height);
}

[[nodiscard]] inline SurfaceUploadDamageRect unionSurfaceUploadDamageRect(SurfaceUploadDamageRect const& a,
                                                                          SurfaceUploadDamageRect const& b) {
  std::int32_t const left = std::min(a.x, b.x);
  std::int32_t const top = std::min(a.y, b.y);
  std::int32_t const right = std::max(a.x + a.width, b.x + b.width);
  std::int32_t const bottom = std::max(a.y + a.height, b.y + b.height);
  return SurfaceUploadDamageRect{
      .x = left,
      .y = top,
      .width = right - left,
      .height = bottom - top,
  };
}

[[nodiscard]] inline bool surfaceUploadDamageRectsMergeable(SurfaceUploadDamageRect const& a,
                                                            SurfaceUploadDamageRect const& b) {
  SurfaceUploadDamageRect const merged = unionSurfaceUploadDamageRect(a, b);
  std::uint64_t const inputArea = surfaceUploadDamageRectArea(a) + surfaceUploadDamageRectArea(b);
  std::uint64_t const mergedArea = surfaceUploadDamageRectArea(merged);
  return mergedArea <= inputArea + inputArea / 5u;
}

inline void appendSurfaceUploadDamageRect(std::vector<SurfaceUploadDamageRect>& rects,
                                          SurfaceUploadDamageRect rect) {
  if (emptySurfaceUploadDamageRect(rect)) return;
  for (auto it = rects.begin(); it != rects.end();) {
    if (surfaceUploadDamageRectsMergeable(*it, rect)) {
      rect = unionSurfaceUploadDamageRect(*it, rect);
      it = rects.erase(it);
    } else {
      ++it;
    }
  }
  rects.push_back(rect);
}

inline void buildSurfaceUploadDamageRects(std::vector<SurfaceUploadDamageRect> const& damage,
                                          std::vector<SurfaceUploadDamageRect>& uploadDamage,
                                          std::int32_t bufferWidth,
                                          std::int32_t bufferHeight) {
  uploadDamage.clear();
  uploadDamage.reserve(damage.size());
  for (auto const& rect : damage) {
    appendSurfaceUploadDamageRect(uploadDamage, clippedSurfaceUploadDamageRect(rect, bufferWidth, bufferHeight));
  }
}

} // namespace lambdaui::compositor
