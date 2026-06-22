#pragma once

#include <Lambda/Layout/Alignment.hpp>
#include <Lambda/Layout/LayoutEngine.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace lambda::layout {

/// When the parent assigned a frame (width/height > 0), use it; otherwise use the finite constraint span.
inline float assignedSpan(float parentSpan, float outerSpan) {
  if (parentSpan > 0.f) {
    return parentSpan;
  }

  if (std::isfinite(outerSpan) && outerSpan > 0.f) {
    return outerSpan;
  }

  return 0.f;
}

/// Main-axis span for stacks. When both a positive parent-assigned span and a positive finite
/// constraint exist, use the smaller of the two so nested stacks respect both the parent's explicit
/// slot and any outer viewport cap.
inline float stackMainAxisSpan(float parentSpan, float outerSpan) {
  if (parentSpan > 0.f && std::isfinite(outerSpan) && outerSpan > 0.f) {
    return std::min(parentSpan, outerSpan);
  }

  if (std::isfinite(outerSpan) && outerSpan > 0.f) {
    return outerSpan;
  }

  return std::max(parentSpan, 0.f);
}

/// Ensures `minWidth` / `minHeight` do not exceed finite `maxWidth` / `maxHeight` (e.g. when a parent
/// root uses min=max=window and a stack assigns a smaller cross-axis or main-axis cap to a child).
inline void clampLayoutMinToMax(LayoutConstraints& c) noexcept {
  if (std::isfinite(c.maxWidth) && c.minWidth > c.maxWidth) {
    c.minWidth = c.maxWidth;
  }
  if (std::isfinite(c.maxHeight) && c.minHeight > c.maxHeight) {
    c.minHeight = c.maxHeight;
  }
}

inline float hAlignOffset(float childW, float innerW, Alignment a) {
  switch (a) {
  case Alignment::Start:
  case Alignment::Stretch:
    return 0.f;
  case Alignment::Center:
    return (innerW - childW) * 0.5f;
  case Alignment::End:
    return innerW - childW;
  }
  return 0.f;
}

inline float vAlignOffset(float childH, float innerH, Alignment a) {
  switch (a) {
  case Alignment::Start:
  case Alignment::Stretch:
    return 0.f;
  case Alignment::Center:
    return (innerH - childH) * 0.5f;
  case Alignment::End:
    return innerH - childH;
  }
  return 0.f;
}

constexpr float kFlexEpsilon = 1e-4f;

} // namespace lambda::layout
