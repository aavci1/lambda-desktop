#pragma once

#include "TerminalCore.hpp"

namespace flux {
class Application;
class Window;
} // namespace flux

namespace lambda_terminal {

void installTerminalView(flux::Application& app, flux::Window& window, TerminalConfig config);

} // namespace lambda_terminal
