#pragma once

#include <memory>

#include <Lambda/UI/Window.hpp>

#include "Window.hpp"

namespace lambdaui::platform {

std::unique_ptr<Window> createWindow(WindowConfig const& config);

} // namespace lambdaui::platform
