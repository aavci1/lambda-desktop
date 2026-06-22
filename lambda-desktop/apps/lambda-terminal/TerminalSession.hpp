#pragma once

#include "TerminalCore.hpp"

namespace lambda {
class Application;
class Window;
} // namespace lambda

namespace lambda_terminal {

void installTerminalView(lambda::Application& app, lambda::Window& window, TerminalConfig config);

} // namespace lambda_terminal
