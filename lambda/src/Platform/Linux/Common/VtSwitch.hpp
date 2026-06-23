#pragma once

#include <cstdint>
#include <optional>

namespace lambdaui::linux_platform {

constexpr int kFirstAdjacentVtSession = 1;
constexpr int kLastAdjacentVtSession = 15;

[[nodiscard]] inline bool vtStateMaskContainsSession(std::uint16_t mask, int session) noexcept {
  // VT_GETSTATE reserves bit 0 for /dev/tty0; VT n is reported in bit n.
  if (session < kFirstAdjacentVtSession || session > kLastAdjacentVtSession) return false;
  return (mask & static_cast<std::uint16_t>(1u << session)) != 0;
}

[[nodiscard]] inline std::optional<int> adjacentNumberedVtSession(int current,
                                                                  int direction,
                                                                  int first = kFirstAdjacentVtSession,
                                                                  int last = kLastAdjacentVtSession) noexcept {
  if (direction == 0 || first <= 0 || last < first || current < first || current > last) return std::nullopt;
  int target = current + (direction < 0 ? -1 : 1);
  if (target < first) target = last;
  if (target > last) target = first;
  return target;
}

[[nodiscard]] inline std::optional<int> adjacentAllocatedVtSession(int current,
                                                                   std::uint16_t allocatedMask,
                                                                   int direction,
                                                                   int first = kFirstAdjacentVtSession,
                                                                   int last = kLastAdjacentVtSession) noexcept {
  if (direction == 0 || first <= 0 || last < first || current < first || current > last) return std::nullopt;

  int candidate = current;
  for (int checked = 0; checked < (last - first); ++checked) {
    auto const next = adjacentNumberedVtSession(candidate, direction, first, last);
    if (!next || *next == current) return std::nullopt;
    candidate = *next;
    if (vtStateMaskContainsSession(allocatedMask, candidate)) return candidate;
  }
  return std::nullopt;
}

} // namespace lambdaui::linux_platform
