#pragma once

/// \file Flux/UI/PopupMenu.hpp
///
/// Native/platform popup menu presentation.

#include <Flux/UI/MenuItem.hpp>

#include <functional>

namespace flux {

/// Hook: returns a function that presents a platform popup menu anchored to the last tapped element.
///
/// The returned function is safe to call from the tap handler that opened the menu. On platforms with
/// transient popup primitives, this uses the native/Wayland popup building block instead of Flux overlays.
std::function<bool(PopupMenu)> usePopupMenu();

} // namespace flux
