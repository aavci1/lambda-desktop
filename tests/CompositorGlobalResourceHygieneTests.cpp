#include "Compositor/Wayland/Globals/Core.hpp"
#include "Compositor/Wayland/Globals/Cutouts.hpp"
#include "Compositor/Wayland/Globals/LayerShell.hpp"
#include "Compositor/Wayland/Globals/Shm.hpp"

#include <doctest/doctest.h>

namespace lambda::compositor {

TEST_CASE("core globals cap resources at implemented protocol versions") {
  CHECK(kCompositorVersion == 5);
  CHECK(compositorResourceVersion(1) == 1);
  CHECK(compositorResourceVersion(5) == 5);
  CHECK(compositorResourceVersion(6) == 5);

  CHECK(kSubcompositorVersion == 1);
  CHECK(subcompositorResourceVersion(1) == 1);
  CHECK(subcompositorResourceVersion(2) == 1);
}

TEST_CASE("remaining globals cap resources at implemented protocol versions") {
  CHECK(kShmVersion == 1);
  CHECK(shmResourceVersion(1) == 1);
  CHECK(shmResourceVersion(2) == 1);

  CHECK(kLayerShellVersion == 4);
  CHECK(layerShellResourceVersion(1) == 1);
  CHECK(layerShellResourceVersion(4) == 4);
  CHECK(layerShellResourceVersion(5) == 4);

  CHECK(kCutoutsVersion == 1);
  CHECK(cutoutsResourceVersion(1) == 1);
  CHECK(cutoutsResourceVersion(2) == 1);
}

} // namespace lambda::compositor
