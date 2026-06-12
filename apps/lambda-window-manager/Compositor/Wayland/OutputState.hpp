#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace lambda::compositor {

inline constexpr std::uint32_t kOutputVersion = 4;

struct OutputLayoutBox {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t width = 1;
  std::int32_t height = 1;
  float scale = 1.f;
  std::int32_t transform = 0;
};

[[nodiscard]] inline std::uint32_t outputResourceVersion(std::uint32_t boundVersion) {
  return std::min(boundVersion, kOutputVersion);
}

[[nodiscard]] inline std::int32_t outputIntegerScale(float scale) {
  if (!std::isfinite(scale)) return 1;
  return std::max(1, static_cast<std::int32_t>(std::ceil(scale)));
}

[[nodiscard]] inline std::int32_t outputLogicalSize(std::int32_t physicalSize, float scale) {
  return std::max(1, static_cast<std::int32_t>(std::lround(static_cast<float>(physicalSize) /
                                                           std::max(0.5f, scale))));
}

[[nodiscard]] inline OutputLayoutBox selectedOutputLayoutBox(std::int32_t physicalWidth,
                                                             std::int32_t physicalHeight,
                                                             float scale,
                                                             std::int32_t logicalX = 0,
                                                             std::int32_t logicalY = 0,
                                                             std::int32_t transform = 0) {
  return {
      .x = logicalX,
      .y = logicalY,
      .width = outputLogicalSize(physicalWidth, scale),
      .height = outputLogicalSize(physicalHeight, scale),
      .scale = std::isfinite(scale) ? scale : 1.f,
      .transform = transform,
  };
}

} // namespace lambda::compositor
