#include "Compositor/Wayland/DmabufFeedback.hpp"

#include <doctest/doctest.h>

#include <array>

TEST_CASE("dmabuf feedback keeps renderer fallback when scanout preferences exist") {
  using namespace lambda::compositor;

  std::array rendererPairs{
      DmabufFeedbackPair{.format = 1, .modifier = 10},
      DmabufFeedbackPair{.format = 1, .modifier = 20},
      DmabufFeedbackPair{.format = 2, .modifier = 10},
  };
  std::array scanoutPairs{
      DmabufFeedbackPair{.format = 1, .modifier = 20},
  };

  DmabufFeedbackPlan const plan = buildDmabufFeedbackPlan(rendererPairs, scanoutPairs);

  REQUIRE(plan.table.size() == 3);
  REQUIRE(plan.tranches.size() == 2);
  CHECK(plan.table[0].format == 1);
  CHECK(plan.table[0].modifier == 20);
  CHECK(plan.tranches[0].flags == kDmabufFeedbackTrancheFlagScanout);
  CHECK(plan.tranches[0].indices == std::vector<std::uint16_t>{0});
  CHECK(plan.tranches[1].flags == 0);
  CHECK(plan.tranches[1].indices.size() == 3);
}

TEST_CASE("dmabuf feedback ignores scanout pairs the renderer cannot import") {
  using namespace lambda::compositor;

  std::array rendererPairs{
      DmabufFeedbackPair{.format = 1, .modifier = 10},
      DmabufFeedbackPair{.format = 1, .modifier = 10},
      DmabufFeedbackPair{.format = 2, .modifier = 10},
  };
  std::array scanoutPairs{
      DmabufFeedbackPair{.format = 3, .modifier = 30},
      DmabufFeedbackPair{.format = 1, .modifier = 10},
      DmabufFeedbackPair{.format = 1, .modifier = 10},
  };

  DmabufFeedbackPlan const plan = buildDmabufFeedbackPlan(rendererPairs, scanoutPairs);

  REQUIRE(plan.table.size() == 2);
  REQUIRE(plan.tranches.size() == 2);
  CHECK(plan.tranches[0].indices == std::vector<std::uint16_t>{0});
  CHECK(plan.tranches[1].indices == std::vector<std::uint16_t>{0, 1});
}
