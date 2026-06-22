#include "Compositor/Wayland/BufferRelease.hpp"

#include <algorithm>

namespace lambda::compositor {
namespace {

bool isRetainedDmabuf(PendingBufferReleaseRecord const& record,
                      std::span<std::uint64_t const> retainedDmabufBufferIds) {
  return record.dmabufBufferId != 0 &&
         std::find(retainedDmabufBufferIds.begin(), retainedDmabufBufferIds.end(), record.dmabufBufferId) !=
             retainedDmabufBufferIds.end();
}

} // namespace

BufferReleasePlan planBufferReleases(std::span<PendingBufferReleaseRecord const> pending,
                                      std::span<std::uint64_t const> retainedDmabufBufferIds) {
  BufferReleasePlan plan;
  for (PendingBufferReleaseRecord const& record : pending) {
    if (record.token == 0) continue;
    if (isRetainedDmabuf(record, retainedDmabufBufferIds)) {
      plan.retained.push_back(record);
    } else {
      plan.releasable.push_back(record);
    }
  }
  return plan;
}

} // namespace lambda::compositor
