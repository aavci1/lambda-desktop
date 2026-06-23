#pragma once

#include <Lambda/UI/MenuItem.hpp>
#include <Lambda/UI/Shortcut.hpp>

#include <string>

namespace lambdaui::detail {

std::string standardRoleActionName(MenuRole role);
Shortcut standardRoleShortcut(MenuRole role);

} // namespace lambdaui::detail
