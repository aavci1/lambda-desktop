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

TEST_CASE("surface input region defaults to full surface and can exclude points") {
  flux::compositor::WaylandServer::Impl::Surface surface{};

  CHECK(flux::compositor::wm::inputRegionContains(&surface, 500.f, 500.f));

  surface.inputRegionInfinite = false;
  surface.inputRegionRects.push_back({.x = 10, .y = 20, .width = 30, .height = 40});

  CHECK(flux::compositor::wm::inputRegionContains(&surface, 10.f, 20.f));
  CHECK(flux::compositor::wm::inputRegionContains(&surface, 39.9f, 59.9f));
  CHECK_FALSE(flux::compositor::wm::inputRegionContains(&surface, 9.9f, 20.f));
  CHECK_FALSE(flux::compositor::wm::inputRegionContains(&surface, 40.f, 20.f));
  CHECK_FALSE(flux::compositor::wm::inputRegionContains(&surface, 10.f, 60.f));
}
