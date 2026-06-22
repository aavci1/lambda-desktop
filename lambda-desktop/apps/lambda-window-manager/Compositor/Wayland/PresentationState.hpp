#pragma once

#include <algorithm>
#include <cstdint>

namespace lambda::compositor {

inline constexpr std::uint32_t kPresentationVersion = 2;

[[nodiscard]] inline std::uint32_t presentationResourceVersion(std::uint32_t boundVersion) {
  return std::min(boundVersion, kPresentationVersion);
}

} // namespace lambda::compositor
