#pragma once

#include "Compositor/WaylandServer.hpp"

#include <Lambda/Platform/Linux/KmsOutput.hpp>

namespace lambda::compositor {

void dispatchKmsInputEvent(WaylandServer& wayland, platform::KmsInputEvent const& event);

} // namespace lambda::compositor
