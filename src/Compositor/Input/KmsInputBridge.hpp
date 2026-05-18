#pragma once

#include "Compositor/WaylandServer.hpp"

#include <Flux/Platform/Linux/KmsOutput.hpp>

namespace flux::compositor {

void dispatchKmsInputEvent(WaylandServer& wayland, platform::KmsInputEvent const& event);

} // namespace flux::compositor
