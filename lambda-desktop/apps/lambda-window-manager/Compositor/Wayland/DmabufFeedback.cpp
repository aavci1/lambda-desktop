#include "Compositor/Wayland/DmabufFeedback.hpp"

#include <algorithm>
#include <limits>
#include <optional>

namespace lambdaui::compositor {
namespace {

bool samePair(DmabufFeedbackPair const& lhs, DmabufFeedbackPair const& rhs) {
  return lhs.format == rhs.format && lhs.modifier == rhs.modifier;
}

bool containsPair(std::span<DmabufFeedbackPair const> pairs, DmabufFeedbackPair const& pair) {
  return std::find_if(pairs.begin(), pairs.end(), [&](DmabufFeedbackPair const& candidate) {
           return samePair(candidate, pair);
         }) != pairs.end();
}

std::optional<std::uint16_t> appendUniqueTablePair(std::vector<DmabufFeedbackPair>& table,
                                                   DmabufFeedbackPair const& pair) {
  auto const found = std::find_if(table.begin(), table.end(), [&](DmabufFeedbackPair const& candidate) {
    return samePair(candidate, pair);
  });
  if (found != table.end()) {
    return static_cast<std::uint16_t>(std::distance(table.begin(), found));
  }
  if (table.size() > std::numeric_limits<std::uint16_t>::max()) return std::nullopt;
  table.push_back(pair);
  return static_cast<std::uint16_t>(table.size() - 1u);
}

void appendUniqueIndex(std::vector<std::uint16_t>& indices, std::uint16_t index) {
  if (std::find(indices.begin(), indices.end(), index) == indices.end()) indices.push_back(index);
}

} // namespace

DmabufFeedbackPlan buildDmabufFeedbackPlan(std::span<DmabufFeedbackPair const> rendererPairs,
                                           std::span<DmabufFeedbackPair const> scanoutPreferredPairs) {
  DmabufFeedbackPlan plan;
  DmabufFeedbackTranchePlan scanout{
      .flags = kDmabufFeedbackTrancheFlagScanout,
      .indices = {},
  };
  DmabufFeedbackTranchePlan renderer{
      .flags = 0,
      .indices = {},
  };

  for (DmabufFeedbackPair const& pair : scanoutPreferredPairs) {
    if (!containsPair(rendererPairs, pair)) continue;
    if (auto const index = appendUniqueTablePair(plan.table, pair)) {
      appendUniqueIndex(scanout.indices, *index);
    }
  }

  for (DmabufFeedbackPair const& pair : rendererPairs) {
    if (auto const index = appendUniqueTablePair(plan.table, pair)) {
      appendUniqueIndex(renderer.indices, *index);
    }
  }

  if (!scanout.indices.empty()) plan.tranches.push_back(std::move(scanout));
  if (!renderer.indices.empty()) plan.tranches.push_back(std::move(renderer));
  return plan;
}

} // namespace lambdaui::compositor
