#pragma once

struct wl_display;
struct wl_surface;

namespace lambdaui {

struct WaylandNativeSurface {
  wl_display* display = nullptr;
  wl_surface* surface = nullptr;
};

} // namespace lambdaui
