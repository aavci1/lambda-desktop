#pragma once

#include <memory>

#include <Lambda/UI/Window.hpp>

#include "Window.hpp"

namespace lambda::platform {

std::unique_ptr<Window> createWindow(WindowConfig const& config);

} // namespace lambda::platform
