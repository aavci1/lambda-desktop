#pragma once

#include <Lambda/UI/MenuItem.hpp>
#include <Lambda/UI/Shortcut.hpp>

#include <string>

namespace lambda::detail {

std::string standardRoleActionName(MenuRole role);
Shortcut standardRoleShortcut(MenuRole role);

} // namespace lambda::detail
