#include <Flux/UI/PopupMenu.hpp>

#include <Flux/UI/Detail/Runtime.hpp>
#include <Flux/UI/Window.hpp>

#include <utility>

namespace flux {

std::function<bool(PopupMenu)> usePopupMenu() {
  Runtime* runtime = Runtime::current();
  Window* window = runtime ? &runtime->window() : nullptr;
  return [runtime, window](PopupMenu menu) mutable {
    if (!runtime || !window) {
      return false;
    }
    Rect const anchor = runtime->lastTapAnchor().value_or(Rect{0.f, 0.f, 1.f, 1.f});
    return window->showPopupMenu(std::move(menu), anchor, runtime->lastTapSerial());
  };
}

} // namespace flux
