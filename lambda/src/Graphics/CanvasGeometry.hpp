#pragma once

#include <Lambda/Core/Geometry.hpp>

#include <algorithm>

namespace lambdaui {

inline Rect boundsOfTransformedRect(Rect const& rect, Mat3 const& transform) {
  Point const p0 = transform.apply({rect.x, rect.y});
  Point const p1 = transform.apply({rect.x + rect.width, rect.y});
  Point const p2 = transform.apply({rect.x, rect.y + rect.height});
  Point const p3 = transform.apply({rect.x + rect.width, rect.y + rect.height});
  float const minX = std::min({p0.x, p1.x, p2.x, p3.x});
  float const minY = std::min({p0.y, p1.y, p2.y, p3.y});
  float const maxX = std::max({p0.x, p1.x, p2.x, p3.x});
  float const maxY = std::max({p0.y, p1.y, p2.y, p3.y});
  return Rect::sharp(minX, minY, maxX - minX, maxY - minY);
}

inline Rect intersectRects(Rect const& a, Rect const& b) {
  float const x0 = std::max(a.x, b.x);
  float const y0 = std::max(a.y, b.y);
  float const x1 = std::min(a.x + a.width, b.x + b.width);
  float const y1 = std::min(a.y + a.height, b.y + b.height);
  if (x1 <= x0 || y1 <= y0) {
    return Rect::sharp(0.f, 0.f, 0.f, 0.f);
  }
  return Rect::sharp(x0, y0, x1 - x0, y1 - y0);
}

/// When `visible` is the axis-aligned bbox of (roundRect ∩ clip), corners on cut edges must be
/// sharp — the SDF round-rect assumes `visible` is the full shape, not a truncated round-rect
/// (see Path::rect).
inline CornerRadius cornerRadiiAfterAxisAlignedClip(Rect const& full,
                                                    Rect const& visible,
                                                    CornerRadius const& radii) {
  constexpr float eps = 1e-3f;
  CornerRadius out = radii;
  if (visible.x > full.x + eps) {
    out.topLeft = 0.f;
    out.bottomLeft = 0.f;
  }
  if (visible.x + visible.width < full.x + full.width - eps) {
    out.topRight = 0.f;
    out.bottomRight = 0.f;
  }
  if (visible.y > full.y + eps) {
    out.topLeft = 0.f;
    out.topRight = 0.f;
  }
  if (visible.y + visible.height < full.y + full.height - eps) {
    out.bottomLeft = 0.f;
    out.bottomRight = 0.f;
  }
  return out;
}

inline void clampRoundRectCornerRadii(float width, float height, CornerRadius& radii) {
  if (width <= 0.f || height <= 0.f) {
    return;
  }
  float const maxR = std::min(width, height) * 0.5f;
  radii.topLeft = std::clamp(radii.topLeft, 0.f, maxR);
  radii.topRight = std::clamp(radii.topRight, 0.f, maxR);
  radii.bottomRight = std::clamp(radii.bottomRight, 0.f, maxR);
  radii.bottomLeft = std::clamp(radii.bottomLeft, 0.f, maxR);
  auto fit = [](float& a, float& b, float len) {
    if (a + b > len && len > 0.f) {
      float const scale = len / (a + b);
      a *= scale;
      b *= scale;
    }
  };
  fit(radii.topLeft, radii.topRight, width);
  fit(radii.bottomLeft, radii.bottomRight, width);
  fit(radii.topLeft, radii.bottomLeft, height);
  fit(radii.topRight, radii.bottomRight, height);
}

} // namespace lambdaui
