#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace lambdaui::compositor {

struct PendingBufferReleaseRecord {
  std::uintptr_t token = 0;
  std::uint64_t dmabufBufferId = 0;
};

struct BufferReleasePlan {
  std::vector<PendingBufferReleaseRecord> releasable;
  std::vector<PendingBufferReleaseRecord> retained;
};

[[nodiscard]] BufferReleasePlan planBufferReleases(
    std::span<PendingBufferReleaseRecord const> pending,
    std::span<std::uint64_t const> retainedDmabufBufferIds);

} // namespace lambdaui::compositor
