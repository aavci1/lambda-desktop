#include "Compositor/Wayland/Globals/Activation.hpp"
#include "Compositor/Wayland/Globals/BackgroundEffect.hpp"
#include "Compositor/Wayland/Globals/Core.hpp"
#include "Compositor/Wayland/Globals/Cutouts.hpp"
#include "Compositor/Wayland/Globals/LayerShell.hpp"
#include "Compositor/Wayland/Globals/LinuxDmabuf.hpp"
#include "Compositor/Wayland/Globals/Seat.hpp"
#include "Compositor/Wayland/Globals/Selection.hpp"
#include "Compositor/Wayland/Globals/Shm.hpp"
#include "Compositor/Wayland/Globals/XdgShell.hpp"

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

  CHECK(kSeatVersion == 7);
  CHECK(seatResourceVersion(1) == 1);
  CHECK(seatResourceVersion(7) == 7);
  CHECK(seatResourceVersion(8) == 7);
  CHECK_FALSE(seatCapabilitiesAdvertiseTouch(kSeatCapabilities));
  CHECK(seatCapabilitiesAdvertiseTouch(kSeatCapabilities | WL_SEAT_CAPABILITY_TOUCH));

  CHECK(kActivationVersion == 1);
  CHECK(activationResourceVersion(1) == 1);
  CHECK(activationResourceVersion(2) == 1);

  CHECK(kPrimarySelectionVersion == 1);
  CHECK(primarySelectionResourceVersion(1) == 1);
  CHECK(primarySelectionResourceVersion(2) == 1);

  CHECK(kDataDeviceVersion == 3);
  CHECK(dataDeviceResourceVersion(1) == 1);
  CHECK(dataDeviceResourceVersion(3) == 3);
  CHECK(dataDeviceResourceVersion(4) == 3);

  CHECK(kXdgWmBaseVersion == 6);
  CHECK(xdgWmBaseResourceVersion(1) == 1);
  CHECK(xdgWmBaseResourceVersion(6) == 6);
  CHECK(xdgWmBaseResourceVersion(7) == 6);

  CHECK(kXdgDecorationManagerVersion == 1);
  CHECK(xdgDecorationResourceVersion(1) == 1);
  CHECK(xdgDecorationResourceVersion(2) == 1);

  CHECK(kLinuxDmabufVersion == 5);
  CHECK(linuxDmabufResourceVersion(1) == 1);
  CHECK(linuxDmabufResourceVersion(5) == 5);
  CHECK(linuxDmabufResourceVersion(6) == 5);

  CHECK(kBackgroundEffectVersion == 4);
  CHECK(backgroundEffectResourceVersion(1) == 1);
  CHECK(backgroundEffectResourceVersion(4) == 4);
  CHECK(backgroundEffectResourceVersion(5) == 4);
}

} // namespace lambda::compositor
