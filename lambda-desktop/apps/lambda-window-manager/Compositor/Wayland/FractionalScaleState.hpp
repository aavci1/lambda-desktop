#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace lambdaui::compositor {

inline constexpr std::uint32_t kFractionalScaleVersion = 1;

[[nodiscard]] inline std::uint32_t fractionalScaleResourceVersion(std::uint32_t boundVersion) {
  return std::min(boundVersion, kFractionalScaleVersion);
}

[[nodiscard]] inline std::uint32_t fractionalScalePreferredScale120(float scale) {
  return static_cast<std::uint32_t>(std::clamp(std::lround(scale * 120.f), 60l, 480l));
}

} // namespace lambdaui::compositor
