#pragma once

#include <cstdint>

namespace lambdaui::compositor {

struct XdgPositionerRules {
  std::int32_t width = 0;
  std::int32_t height = 0;
  std::int32_t anchorRectWidth = 0;
  std::int32_t anchorRectHeight = 0;
};

[[nodiscard]] constexpr bool xdgPositionerSizeInputValid(std::int32_t width, std::int32_t height) {
  return width > 0 && height > 0;
}

[[nodiscard]] constexpr bool xdgPositionerAnchorRectInputValid(std::int32_t width, std::int32_t height) {
  return width >= 0 && height >= 0;
}

[[nodiscard]] constexpr bool xdgPositionerComplete(XdgPositionerRules const& rules) {
  return rules.width > 0 && rules.anchorRectWidth > 0;
}

} // namespace lambdaui::compositor
