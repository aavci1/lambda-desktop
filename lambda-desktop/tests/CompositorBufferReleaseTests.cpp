#include "Compositor/Wayland/BufferRelease.hpp"

#include <doctest/doctest.h>

#include <array>

TEST_CASE("buffer release plan keeps retained dmabufs pending") {
  using namespace lambda::compositor;

  std::array pending{
      PendingBufferReleaseRecord{.token = 1, .dmabufBufferId = 0},
      PendingBufferReleaseRecord{.token = 2, .dmabufBufferId = 11},
      PendingBufferReleaseRecord{.token = 3, .dmabufBufferId = 12},
  };
  std::array<std::uint64_t, 1> retained{12};

  BufferReleasePlan const plan = planBufferReleases(pending, retained);

  REQUIRE(plan.releasable.size() == 2);
  CHECK(plan.releasable[0].token == 1);
  CHECK(plan.releasable[1].token == 2);
  REQUIRE(plan.retained.size() == 1);
  CHECK(plan.retained[0].token == 3);
}

TEST_CASE("buffer release plan releases retained dmabufs after KMS drops them") {
  using namespace lambda::compositor;

  std::array pending{
      PendingBufferReleaseRecord{.token = 3, .dmabufBufferId = 12},
  };
  std::array<std::uint64_t, 0> retained{};

  BufferReleasePlan const plan = planBufferReleases(pending, retained);

  REQUIRE(plan.releasable.size() == 1);
  CHECK(plan.releasable[0].token == 3);
  CHECK(plan.retained.empty());
}
