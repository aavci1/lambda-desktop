#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace lambdaui::compositor {

inline constexpr std::uint32_t kDmabufFeedbackTrancheFlagScanout = 1u;

struct DmabufFeedbackPair {
  std::uint32_t format = 0;
  std::uint64_t modifier = 0;
};

struct DmabufFeedbackTranchePlan {
  std::uint32_t flags = 0;
  std::vector<std::uint16_t> indices;
};

struct DmabufFeedbackPlan {
  std::vector<DmabufFeedbackPair> table;
  std::vector<DmabufFeedbackTranchePlan> tranches;
};

[[nodiscard]] DmabufFeedbackPlan buildDmabufFeedbackPlan(
    std::span<DmabufFeedbackPair const> rendererPairs,
    std::span<DmabufFeedbackPair const> scanoutPreferredPairs);

} // namespace lambdaui::compositor
