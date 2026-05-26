#include "Compositor/Window/WindowManagerInternal.hpp"

#include <doctest/doctest.h>

#include <memory>
#include <vector>

namespace {

std::unique_ptr<flux::compositor::WaylandServer::Impl::Surface> testSurface(std::uint64_t id,
                                                                            bool minimized = false) {
  auto surface = std::make_unique<flux::compositor::WaylandServer::Impl::Surface>();
  surface->id = id;
  surface->role = flux::compositor::SurfaceRole::XdgToplevel;
  surface->minimized = minimized;
  return surface;
}

} // namespace

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

TEST_CASE("restoreSurfaceForShellFocus restores minimized toplevels") {
  flux::compositor::WaylandServer::Impl::Surface surface{};
  surface.role = flux::compositor::SurfaceRole::XdgToplevel;
  surface.minimized = true;

  CHECK(flux::compositor::wm::restoreSurfaceForShellFocus(&surface));
  CHECK_FALSE(surface.minimized);
}

TEST_CASE("restoreSurfaceForShellFocus rejects non-toplevel surfaces") {
  flux::compositor::WaylandServer::Impl::Surface popup{};
  popup.role = flux::compositor::SurfaceRole::XdgPopup;
  popup.minimized = true;

  CHECK_FALSE(flux::compositor::wm::restoreSurfaceForShellFocus(&popup));
  CHECK(popup.minimized);

  CHECK_FALSE(flux::compositor::wm::restoreSurfaceForShellFocus(nullptr));
}

TEST_CASE("focus order helpers skip minimized windows and fall back to stacking order") {
  auto first = testSurface(1);
  auto second = testSurface(2, true);
  auto third = testSurface(3);
  std::vector<std::unique_ptr<flux::compositor::WaylandServer::Impl::Surface>> surfaces;
  surfaces.push_back(std::move(first));
  surfaces.push_back(std::move(second));
  surfaces.push_back(std::move(third));

  std::vector<flux::compositor::WaylandServer::Impl::Surface*> focusOrder{
      surfaces[0].get(),
      surfaces[1].get(),
      surfaces[2].get(),
  };

  CHECK(flux::compositor::wm::mostRecentToplevelFromOrders(focusOrder, surfaces) == surfaces[2].get());
  CHECK(flux::compositor::wm::previousFocusedToplevelFromOrders(focusOrder, surfaces, surfaces[2].get()) ==
        surfaces[0].get());

  surfaces[2]->minimized = true;
  CHECK(flux::compositor::wm::mostRecentToplevelFromOrders(focusOrder, surfaces) == surfaces[0].get());
  CHECK(flux::compositor::wm::previousFocusedToplevelFromOrders(focusOrder, surfaces, surfaces[0].get()) == nullptr);

  focusOrder.clear();
  surfaces[2]->minimized = false;
  CHECK(flux::compositor::wm::mostRecentToplevelFromOrders(focusOrder, surfaces) == surfaces[2].get());
}

TEST_CASE("presentation eligibility excludes minimized toplevels and dismissed popups") {
  flux::compositor::WaylandServer::Impl::Surface visible{};
  visible.role = flux::compositor::SurfaceRole::XdgToplevel;
  CHECK(flux::compositor::wm::surfaceEligibleForPresentation(&visible));

  visible.minimized = true;
  CHECK_FALSE(flux::compositor::wm::surfaceEligibleForPresentation(&visible));

  flux::compositor::WaylandServer::Impl::XdgPopup popupRole{};
  flux::compositor::WaylandServer::Impl::Surface popup{};
  popup.role = flux::compositor::SurfaceRole::XdgPopup;
  popup.xdgPopup = &popupRole;
  CHECK(flux::compositor::wm::surfaceEligibleForPresentation(&popup));
  popupRole.dismissed = true;
  CHECK_FALSE(flux::compositor::wm::surfaceEligibleForPresentation(&popup));
}

TEST_CASE("shell app id matching accepts built-in app aliases") {
  CHECK(flux::compositor::wm::shellAppIdMatches("terminal", "lambda-terminal"));
  CHECK(flux::compositor::wm::shellAppIdMatches("terminal", "foot"));
  CHECK(flux::compositor::wm::shellAppIdMatches("browser", "firefox"));
  CHECK(flux::compositor::wm::shellAppIdMatches("files", "lambda-files"));
  CHECK(flux::compositor::wm::shellAppIdMatches("lambda-files", "files"));
  CHECK(flux::compositor::wm::shellAppIdMatches("files", "org.gnome.Nautilus"));
  CHECK(flux::compositor::wm::shellAppIdMatches("settings", "lambda-settings"));
  CHECK(flux::compositor::wm::shellAppIdMatches("lambda-settings", "settings"));
  CHECK(flux::compositor::wm::shellAppIdMatches("lambda-settings", "lambda-settings"));
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

TEST_CASE("xdg window geometry validates positive size") {
  CHECK(flux::compositor::wm::xdgWindowGeometrySizeValid(1, 1));
  CHECK_FALSE(flux::compositor::wm::xdgWindowGeometrySizeValid(0, 1));
  CHECK_FALSE(flux::compositor::wm::xdgWindowGeometrySizeValid(1, 0));
  CHECK_FALSE(flux::compositor::wm::xdgWindowGeometrySizeValid(-1, 1));
}

TEST_CASE("xdg window geometry offsets surface-local input") {
  flux::compositor::WaylandServer::Impl::Surface surface{};
  surface.role = flux::compositor::SurfaceRole::XdgToplevel;
  surface.windowX = 100;
  surface.windowY = 80;
  surface.xdgWindowGeometrySet = true;
  surface.xdgWindowGeometryX = 32;
  surface.xdgWindowGeometryY = 24;
  surface.xdgWindowGeometryWidth = 960;
  surface.xdgWindowGeometryHeight = 640;

  CHECK(flux::compositor::wm::surfaceBufferOriginX(&surface) == doctest::Approx(68.f));
  CHECK(flux::compositor::wm::surfaceBufferOriginY(&surface) == doctest::Approx(56.f));
  CHECK(flux::compositor::wm::surfaceLocalX(&surface, 110.f) == doctest::Approx(42.f));
  CHECK(flux::compositor::wm::surfaceLocalY(&surface, 90.f) == doctest::Approx(34.f));
}

TEST_CASE("xdg toplevel size hints reject negative and inverted dimensions") {
  using flux::compositor::wm::ToplevelSizeHints;
  CHECK(flux::compositor::wm::toplevelSizeHintsValid(ToplevelSizeHints{}));
  CHECK(flux::compositor::wm::toplevelSizeHintsValid({
      .minWidth = 320,
      .minHeight = 200,
      .maxWidth = 640,
      .maxHeight = 480,
  }));
  CHECK_FALSE(flux::compositor::wm::toplevelSizeHintsValid({
      .minWidth = -1,
  }));
  CHECK_FALSE(flux::compositor::wm::toplevelSizeHintsValid({
      .minWidth = 641,
      .maxWidth = 640,
  }));
  CHECK_FALSE(flux::compositor::wm::toplevelSizeHintsValid({
      .minHeight = 481,
      .maxHeight = 480,
  }));
}

TEST_CASE("xdg toplevel size hints clamp interactive resize geometry around anchored edges") {
  using flux::compositor::ResizeEdge;
  using flux::compositor::WindowGeometry;
  flux::compositor::wm::ToplevelSizeHints const hints{
      .minWidth = 240,
      .minHeight = 180,
      .maxWidth = 640,
      .maxHeight = 480,
  };

  auto rightBottom = flux::compositor::wm::clampToplevelGeometryToSizeHints(
      WindowGeometry{.x = 100, .y = 100, .width = 800, .height = 700},
      hints,
      ResizeEdge::Right | ResizeEdge::Bottom);
  CHECK(rightBottom.x == 100);
  CHECK(rightBottom.y == 100);
  CHECK(rightBottom.width == 640);
  CHECK(rightBottom.height == 480);

  auto leftTop = flux::compositor::wm::clampToplevelGeometryToSizeHints(
      WindowGeometry{.x = 100, .y = 100, .width = 120, .height = 90},
      hints,
      ResizeEdge::Left | ResizeEdge::Top);
  CHECK(leftTop.x == -20);
  CHECK(leftTop.y == 10);
  CHECK(leftTop.width == 240);
  CHECK(leftTop.height == 180);
}
