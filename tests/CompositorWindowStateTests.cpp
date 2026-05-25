#include "Compositor/Window/WindowManagerInternal.hpp"

#include <doctest/doctest.h>

TEST_CASE("markToplevelMinimized only transitions visible toplevels") {
  flux::compositor::WaylandServer::Impl::Surface surface{};
  surface.role = flux::compositor::SurfaceRole::XdgToplevel;

  CHECK(flux::compositor::wm::markToplevelMinimized(&surface));
  CHECK(surface.minimized);

  CHECK_FALSE(flux::compositor::wm::markToplevelMinimized(&surface));
}

TEST_CASE("markToplevelMinimized rejects non-toplevel surfaces") {
  flux::compositor::WaylandServer::Impl::Surface popup{};
  popup.role = flux::compositor::SurfaceRole::XdgPopup;

  CHECK_FALSE(flux::compositor::wm::markToplevelMinimized(&popup));
  CHECK_FALSE(popup.minimized);

  CHECK_FALSE(flux::compositor::wm::markToplevelMinimized(nullptr));
}
