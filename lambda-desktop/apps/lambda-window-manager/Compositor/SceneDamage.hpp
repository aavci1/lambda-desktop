#pragma once

#include "Compositor/Wayland/WaylandTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace lambda::compositor {

struct SceneDamageResult {
  bool fullOutput = false;
  bool backgroundFillRequired = false;
  std::vector<CommittedSurfaceSnapshot::RegionRect> rects;

  [[nodiscard]] bool empty() const noexcept { return !fullOutput && rects.empty(); }
  [[nodiscard]] std::size_t rectCount() const noexcept {
    return fullOutput ? 1u : rects.size();
  }
};

struct SceneDamageState {
  bool initialized = false;
  std::int32_t outputWidth = 0;
  std::int32_t outputHeight = 0;
  std::vector<CommittedSurfaceSnapshot> surfaces;
  std::optional<CommittedSurfaceSnapshot> cursor;
};

[[nodiscard]] CommittedSurfaceSnapshot::RegionRect
sceneSurfaceContentRect(CommittedSurfaceSnapshot const& surface);

[[nodiscard]] CommittedSurfaceSnapshot::RegionRect
sceneSurfaceFrameRect(CommittedSurfaceSnapshot const& surface);

SceneDamageResult updateSceneDamage(SceneDamageState& state,
                                    std::vector<CommittedSurfaceSnapshot> const& surfaces,
                                    std::optional<CommittedSurfaceSnapshot> const& cursor,
                                    std::int32_t outputWidth,
                                    std::int32_t outputHeight,
                                    bool forceFullDamage = false);

} // namespace lambda::compositor
