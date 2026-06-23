#pragma once

#include "Compositor/WaylandServer.hpp"

#include <Lambda/Platform/Linux/KmsOutput.hpp>

namespace lambdaui::compositor {

void dispatchKmsInputEvent(WaylandServer& wayland, platform::KmsInputEvent const& event);

} // namespace lambdaui::compositor
