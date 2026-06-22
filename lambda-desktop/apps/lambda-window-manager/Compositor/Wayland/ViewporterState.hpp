#pragma once

#include <algorithm>
#include <cstdint>

namespace lambda::compositor {

inline constexpr std::uint32_t kViewporterVersion = 1;

[[nodiscard]] inline std::uint32_t viewporterResourceVersion(std::uint32_t boundVersion) {
  return std::min(boundVersion, kViewporterVersion);
}

} // namespace lambda::compositor
