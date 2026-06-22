#pragma once

#include <algorithm>
#include <cstdint>

namespace lambda::compositor {

inline constexpr std::uint32_t kXdgOutputVersion = 3;
inline constexpr std::uint32_t kXdgOutputDoneDeprecatedSinceVersion = 3;
inline constexpr std::uint32_t kWlOutputDoneSinceVersion = 2;

enum class XdgOutputDoneKind : std::uint8_t {
  None,
  XdgOutput,
  WlOutput,
};

[[nodiscard]] inline std::uint32_t xdgOutputResourceVersion(std::uint32_t boundVersion) {
  return std::min(boundVersion, kXdgOutputVersion);
}

[[nodiscard]] inline XdgOutputDoneKind xdgOutputDoneKind(std::uint32_t xdgOutputVersion,
                                                         std::uint32_t wlOutputVersion,
                                                         bool includeWlOutputDone = true) {
  if (xdgOutputVersion < kXdgOutputDoneDeprecatedSinceVersion) return XdgOutputDoneKind::XdgOutput;
  return includeWlOutputDone && wlOutputVersion >= kWlOutputDoneSinceVersion ? XdgOutputDoneKind::WlOutput
                                                                             : XdgOutputDoneKind::None;
}

[[nodiscard]] inline bool xdgOutputLogicalSizeChanged(std::int32_t currentWidth,
                                                      std::int32_t currentHeight,
                                                      std::int32_t nextWidth,
                                                      std::int32_t nextHeight) {
  return currentWidth != nextWidth || currentHeight != nextHeight;
}

[[nodiscard]] inline bool xdgOutputLogicalGeometryChanged(std::int32_t currentX,
                                                          std::int32_t currentY,
                                                          std::int32_t currentWidth,
                                                          std::int32_t currentHeight,
                                                          std::int32_t nextX,
                                                          std::int32_t nextY,
                                                          std::int32_t nextWidth,
                                                          std::int32_t nextHeight) {
  return currentX != nextX ||
         currentY != nextY ||
         xdgOutputLogicalSizeChanged(currentWidth, currentHeight, nextWidth, nextHeight);
}

} // namespace lambda::compositor
