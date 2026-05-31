#include "Compositor/Wayland/ViewporterState.hpp"

#include <doctest/doctest.h>

namespace lambda::compositor {

TEST_CASE("viewporter resources use the implemented protocol version") {
  CHECK(kViewporterVersion == 1);
  CHECK(viewporterResourceVersion(1) == 1);
  CHECK(viewporterResourceVersion(2) == 1);
}

} // namespace lambda::compositor
