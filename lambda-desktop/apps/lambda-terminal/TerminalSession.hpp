#pragma once

#include "TerminalCore.hpp"

namespace lambdaui {
class Application;
class Window;
} // namespace lambdaui

namespace lambda_terminal {

void installTerminalView(lambdaui::Application& app, lambdaui::Window& window, TerminalConfig config);

} // namespace lambda_terminal
