#include "Compositor/Wayland/XdgOutputState.hpp"

#include <doctest/doctest.h>

namespace lambdaui::compositor {

TEST_CASE("xdg output resources use the implemented protocol version") {
  CHECK(kXdgOutputVersion == 3);
  CHECK(xdgOutputResourceVersion(1) == 1);
  CHECK(xdgOutputResourceVersion(3) == 3);
  CHECK(xdgOutputResourceVersion(4) == 3);
}

TEST_CASE("xdg output done event follows xdg-output and wl_output versions") {
  CHECK(xdgOutputDoneKind(1, 1) == XdgOutputDoneKind::XdgOutput);
  CHECK(xdgOutputDoneKind(2, 1) == XdgOutputDoneKind::XdgOutput);
  CHECK(xdgOutputDoneKind(3, 1) == XdgOutputDoneKind::None);
  CHECK(xdgOutputDoneKind(3, 2) == XdgOutputDoneKind::WlOutput);
  CHECK(xdgOutputDoneKind(4, 2) == XdgOutputDoneKind::WlOutput);
  CHECK(xdgOutputDoneKind(2, 2, false) == XdgOutputDoneKind::XdgOutput);
  CHECK(xdgOutputDoneKind(3, 2, false) == XdgOutputDoneKind::None);
}

TEST_CASE("xdg output logical-size updates are emitted only when size changes") {
  CHECK_FALSE(xdgOutputLogicalSizeChanged(1920, 1080, 1920, 1080));
  CHECK(xdgOutputLogicalSizeChanged(1920, 1080, 2560, 1080));
  CHECK(xdgOutputLogicalSizeChanged(1920, 1080, 1920, 1440));
}

TEST_CASE("xdg output logical-geometry updates include layout position") {
  CHECK_FALSE(xdgOutputLogicalGeometryChanged(0, 0, 1920, 1080, 0, 0, 1920, 1080));
  CHECK(xdgOutputLogicalGeometryChanged(0, 0, 1920, 1080, 1920, 0, 1920, 1080));
  CHECK(xdgOutputLogicalGeometryChanged(0, 0, 1920, 1080, 0, 1080, 1920, 1080));
  CHECK(xdgOutputLogicalGeometryChanged(0, 0, 1920, 1080, 0, 0, 2560, 1080));
  CHECK(xdgOutputLogicalGeometryChanged(0, 0, 1920, 1080, 0, 0, 1920, 1440));
}

} // namespace lambdaui::compositor
