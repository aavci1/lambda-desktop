#include "Compositor/Window/WindowManagerInternal.hpp"
#include "Compositor/Wayland/XdgPopupState.hpp"
#include "Compositor/Wayland/XdgSurfaceState.hpp"
#include "Compositor/Wayland/XdgToplevelState.hpp"

#include <doctest/doctest.h>

#include <array>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

namespace {

std::unique_ptr<lambda::compositor::WaylandServer::Impl::Surface> testSurface(std::uint64_t id,
                                                                            bool minimized = false) {
  auto surface = std::make_unique<lambda::compositor::WaylandServer::Impl::Surface>();
  surface->id = id;
  surface->role = lambda::compositor::SurfaceRole::XdgToplevel;
  surface->minimized = minimized;
  return surface;
}

} // namespace

TEST_CASE("markToplevelMinimized only transitions visible toplevels") {
  lambda::compositor::WaylandServer::Impl::Surface surface{};
  surface.role = lambda::compositor::SurfaceRole::XdgToplevel;

  CHECK(lambda::compositor::wm::markToplevelMinimized(&surface));
  CHECK(surface.minimized);

  CHECK_FALSE(lambda::compositor::wm::markToplevelMinimized(&surface));
}

TEST_CASE("markToplevelMinimized rejects non-toplevel surfaces") {
  lambda::compositor::WaylandServer::Impl::Surface popup{};
  popup.role = lambda::compositor::SurfaceRole::XdgPopup;

  CHECK_FALSE(lambda::compositor::wm::markToplevelMinimized(&popup));
  CHECK_FALSE(popup.minimized);

  CHECK_FALSE(lambda::compositor::wm::markToplevelMinimized(nullptr));
}

TEST_CASE("restoreSurfaceForShellFocus restores minimized toplevels") {
  lambda::compositor::WaylandServer::Impl::Surface surface{};
  surface.role = lambda::compositor::SurfaceRole::XdgToplevel;
  surface.minimized = true;

  CHECK(lambda::compositor::wm::restoreSurfaceForShellFocus(&surface));
  CHECK_FALSE(surface.minimized);
}

TEST_CASE("restoreSurfaceForShellFocus rejects non-toplevel surfaces") {
  lambda::compositor::WaylandServer::Impl::Surface popup{};
  popup.role = lambda::compositor::SurfaceRole::XdgPopup;
  popup.minimized = true;

  CHECK_FALSE(lambda::compositor::wm::restoreSurfaceForShellFocus(&popup));
  CHECK(popup.minimized);

  CHECK_FALSE(lambda::compositor::wm::restoreSurfaceForShellFocus(nullptr));
}

TEST_CASE("focus order helpers skip minimized windows and fall back to stacking order") {
  auto first = testSurface(1);
  auto second = testSurface(2, true);
  auto third = testSurface(3);
  std::vector<std::unique_ptr<lambda::compositor::WaylandServer::Impl::Surface>> surfaces;
  surfaces.push_back(std::move(first));
  surfaces.push_back(std::move(second));
  surfaces.push_back(std::move(third));

  std::vector<lambda::compositor::WaylandServer::Impl::Surface*> focusOrder{
      surfaces[0].get(),
      surfaces[1].get(),
      surfaces[2].get(),
  };

  CHECK(lambda::compositor::wm::mostRecentToplevelFromOrders(focusOrder, surfaces) == surfaces[2].get());
  CHECK(lambda::compositor::wm::previousFocusedToplevelFromOrders(focusOrder, surfaces, surfaces[2].get()) ==
        surfaces[0].get());

  surfaces[2]->minimized = true;
  CHECK(lambda::compositor::wm::mostRecentToplevelFromOrders(focusOrder, surfaces) == surfaces[0].get());
  CHECK(lambda::compositor::wm::previousFocusedToplevelFromOrders(focusOrder, surfaces, surfaces[0].get()) == nullptr);

  focusOrder.clear();
  surfaces[2]->minimized = false;
  CHECK(lambda::compositor::wm::mostRecentToplevelFromOrders(focusOrder, surfaces) == surfaces[2].get());
}

TEST_CASE("presentation eligibility excludes minimized toplevels and dismissed popups") {
  lambda::compositor::WaylandServer::Impl::Surface visible{};
  visible.role = lambda::compositor::SurfaceRole::XdgToplevel;
  CHECK(lambda::compositor::wm::surfaceEligibleForPresentation(&visible));

  visible.minimized = true;
  CHECK_FALSE(lambda::compositor::wm::surfaceEligibleForPresentation(&visible));

  lambda::compositor::WaylandServer::Impl::XdgPopup popupRole{};
  lambda::compositor::WaylandServer::Impl::Surface popup{};
  popup.role = lambda::compositor::SurfaceRole::XdgPopup;
  popup.xdgPopup = &popupRole;
  CHECK(lambda::compositor::wm::surfaceEligibleForPresentation(&popup));
  popupRole.dismissed = true;
  CHECK_FALSE(lambda::compositor::wm::surfaceEligibleForPresentation(&popup));
}

TEST_CASE("xdg popup parent validation accepts only constructed xdg roles") {
  CHECK(lambda::compositor::xdgPopupParentHasValidRole(nullptr));

  lambda::compositor::WaylandServer::Impl::XdgSurface parent{};
  lambda::compositor::WaylandServer::Impl::Surface surface{};
  parent.surface = &surface;

  surface.role = lambda::compositor::SurfaceRole::None;
  CHECK_FALSE(lambda::compositor::xdgPopupParentHasValidRole(&parent));

  surface.role = lambda::compositor::SurfaceRole::XdgSurface;
  CHECK_FALSE(lambda::compositor::xdgPopupParentHasValidRole(&parent));

  surface.role = lambda::compositor::SurfaceRole::LayerSurface;
  CHECK_FALSE(lambda::compositor::xdgPopupParentHasValidRole(&parent));

  surface.role = lambda::compositor::SurfaceRole::XdgToplevel;
  CHECK(lambda::compositor::xdgPopupParentHasValidRole(&parent));

  surface.role = lambda::compositor::SurfaceRole::XdgPopup;
  CHECK(lambda::compositor::xdgPopupParentHasValidRole(&parent));
}

TEST_CASE("xdg surface role object validation follows constructed xdg roles") {
  CHECK_FALSE(lambda::compositor::xdgSurfaceHasConstructedRoleObject(nullptr));

  lambda::compositor::WaylandServer::Impl::XdgSurface xdgSurface{};
  CHECK_FALSE(lambda::compositor::xdgSurfaceHasConstructedRoleObject(&xdgSurface));

  lambda::compositor::WaylandServer::Impl::Surface surface{};
  xdgSurface.surface = &surface;

  surface.role = lambda::compositor::SurfaceRole::None;
  CHECK_FALSE(lambda::compositor::xdgSurfaceHasConstructedRoleObject(&xdgSurface));

  surface.role = lambda::compositor::SurfaceRole::XdgSurface;
  CHECK_FALSE(lambda::compositor::xdgSurfaceHasConstructedRoleObject(&xdgSurface));

  surface.role = lambda::compositor::SurfaceRole::LayerSurface;
  CHECK_FALSE(lambda::compositor::xdgSurfaceHasConstructedRoleObject(&xdgSurface));

  surface.role = lambda::compositor::SurfaceRole::XdgToplevel;
  CHECK(lambda::compositor::xdgSurfaceHasConstructedRoleObject(&xdgSurface));

  surface.role = lambda::compositor::SurfaceRole::XdgPopup;
  CHECK(lambda::compositor::xdgSurfaceHasConstructedRoleObject(&xdgSurface));
}

TEST_CASE("xdg surface creation rejects surfaces with an existing buffer") {
  lambda::compositor::WaylandServer::Impl::Surface surface{};
  CHECK_FALSE(lambda::compositor::xdgSurfaceCreationHasExistingBuffer(&surface));

  surface.bufferState.buffer = reinterpret_cast<wl_resource*>(0x1);
  CHECK(lambda::compositor::xdgSurfaceCreationHasExistingBuffer(&surface));
}

TEST_CASE("xdg surface buffer commits require role object and configure ack") {
  using lambda::compositor::XdgSurfaceBufferCommitReadiness;

  CHECK(lambda::compositor::xdgSurfaceBufferCommitReadiness(nullptr) == XdgSurfaceBufferCommitReadiness::Ready);

  lambda::compositor::WaylandServer::Impl::Surface surface{};
  lambda::compositor::WaylandServer::Impl::XdgSurface xdgSurface{};
  xdgSurface.surface = &surface;

  surface.role = lambda::compositor::SurfaceRole::XdgSurface;
  CHECK(lambda::compositor::xdgSurfaceBufferCommitReadiness(&xdgSurface) ==
        XdgSurfaceBufferCommitReadiness::MissingRoleObject);

  surface.role = lambda::compositor::SurfaceRole::XdgToplevel;
  CHECK(lambda::compositor::xdgSurfaceBufferCommitReadiness(&xdgSurface) ==
        XdgSurfaceBufferCommitReadiness::Unconfigured);

  xdgSurface.configured = true;
  CHECK(lambda::compositor::xdgSurfaceBufferCommitReadiness(&xdgSurface) == XdgSurfaceBufferCommitReadiness::Ready);
}

TEST_CASE("xdg surface configure state resets on unmap") {
  lambda::compositor::WaylandServer::Impl::XdgSurface xdgSurface{};
  CHECK_FALSE(lambda::compositor::resetXdgSurfaceConfigureStateForUnmap(&xdgSurface));

  xdgSurface.configured = true;
  xdgSurface.configureList.push_back(lambda::compositor::WaylandServer::Impl::XdgConfigure{
      .serial = 12,
      .role = lambda::compositor::SurfaceRole::XdgToplevel,
      .width = 640,
      .height = 480,
  });
  xdgSurface.pendingConfigure = lambda::compositor::WaylandServer::Impl::XdgConfigure{
      .serial = 12,
      .role = lambda::compositor::SurfaceRole::XdgToplevel,
  };
  xdgSurface.currentConfigure = lambda::compositor::WaylandServer::Impl::XdgConfigure{
      .serial = 11,
      .role = lambda::compositor::SurfaceRole::XdgToplevel,
  };

  CHECK(lambda::compositor::resetXdgSurfaceConfigureStateForUnmap(&xdgSurface));
  CHECK_FALSE(xdgSurface.configured);
  CHECK(xdgSurface.configureList.empty());
  CHECK_FALSE(xdgSurface.pendingConfigure.has_value());
  CHECK_FALSE(xdgSurface.currentConfigure.has_value());
  CHECK_FALSE(lambda::compositor::resetXdgSurfaceConfigureStateForUnmap(&xdgSurface));
}

TEST_CASE("xdg toplevel interactive requests require a configured toplevel surface") {
  CHECK_FALSE(lambda::compositor::xdgToplevelSurfaceConfigured(nullptr));

  lambda::compositor::WaylandServer::Impl::XdgToplevel toplevel{};
  CHECK_FALSE(lambda::compositor::xdgToplevelSurfaceConfigured(&toplevel));

  lambda::compositor::WaylandServer::Impl::XdgSurface xdgSurface{};
  toplevel.xdgSurface = &xdgSurface;
  CHECK_FALSE(lambda::compositor::xdgToplevelSurfaceConfigured(&toplevel));

  lambda::compositor::WaylandServer::Impl::Surface surface{};
  xdgSurface.surface = &surface;
  xdgSurface.configured = true;

  surface.role = lambda::compositor::SurfaceRole::XdgSurface;
  CHECK_FALSE(lambda::compositor::xdgToplevelSurfaceConfigured(&toplevel));

  surface.role = lambda::compositor::SurfaceRole::XdgPopup;
  CHECK_FALSE(lambda::compositor::xdgToplevelSurfaceConfigured(&toplevel));

  surface.role = lambda::compositor::SurfaceRole::XdgToplevel;
  CHECK(lambda::compositor::xdgToplevelSurfaceConfigured(&toplevel));

  xdgSurface.configured = false;
  CHECK_FALSE(lambda::compositor::xdgToplevelSurfaceConfigured(&toplevel));
}

TEST_CASE("xdg toplevel title validation follows strict UTF-8") {
  auto bytes = [](std::initializer_list<unsigned char> values) {
    std::string result;
    result.reserve(values.size());
    for (unsigned char value : values) {
      result.push_back(static_cast<char>(value));
    }
    return result;
  };

  CHECK(lambda::compositor::xdgToplevelTitleUtf8Valid(""));
  CHECK(lambda::compositor::xdgToplevelTitleUtf8Valid("Settings"));
  CHECK(lambda::compositor::xdgToplevelTitleUtf8Valid(bytes({0xC2, 0xA2})));
  CHECK(lambda::compositor::xdgToplevelTitleUtf8Valid(bytes({0xE2, 0x82, 0xAC})));
  CHECK(lambda::compositor::xdgToplevelTitleUtf8Valid(bytes({0xF0, 0x9F, 0x98, 0x80})));

  CHECK_FALSE(lambda::compositor::xdgToplevelTitleUtf8Valid(bytes({0x80})));
  CHECK_FALSE(lambda::compositor::xdgToplevelTitleUtf8Valid(bytes({0xC0, 0x80})));
  CHECK_FALSE(lambda::compositor::xdgToplevelTitleUtf8Valid(bytes({0xE0, 0x80, 0x80})));
  CHECK_FALSE(lambda::compositor::xdgToplevelTitleUtf8Valid(bytes({0xED, 0xA0, 0x80})));
  CHECK_FALSE(lambda::compositor::xdgToplevelTitleUtf8Valid(bytes({0xF5, 0x80, 0x80, 0x80})));
  CHECK_FALSE(lambda::compositor::xdgToplevelTitleUtf8Valid(bytes({0xE2, 0x82})));
}

TEST_CASE("xdg toplevel parent retention requires a mapped toplevel parent") {
  lambda::compositor::WaylandServer::Impl::XdgToplevel parent{};
  CHECK_FALSE(lambda::compositor::xdgToplevelMapped(&parent));
  CHECK(lambda::compositor::xdgToplevelRetainedParent(&parent) == nullptr);

  lambda::compositor::WaylandServer::Impl::XdgSurface xdgSurface{};
  lambda::compositor::WaylandServer::Impl::Surface surface{};
  parent.xdgSurface = &xdgSurface;
  xdgSurface.surface = &surface;
  surface.role = lambda::compositor::SurfaceRole::XdgToplevel;

  parent.mapped = false;
  CHECK_FALSE(lambda::compositor::xdgToplevelMapped(&parent));
  CHECK(lambda::compositor::xdgToplevelRetainedParent(&parent) == nullptr);

  parent.mapped = true;
  CHECK(lambda::compositor::xdgToplevelMapped(&parent));
  CHECK(lambda::compositor::xdgToplevelRetainedParent(&parent) == &parent);

  surface.role = lambda::compositor::SurfaceRole::XdgSurface;
  CHECK_FALSE(lambda::compositor::xdgToplevelMapped(&parent));
  CHECK(lambda::compositor::xdgToplevelRetainedParent(&parent) == nullptr);
}

TEST_CASE("xdg popup topmost validation detects live child popups") {
  lambda::compositor::WaylandServer::Impl::Surface parentSurface{};
  lambda::compositor::WaylandServer::Impl::Surface unrelatedSurface{};
  lambda::compositor::WaylandServer::Impl::XdgSurface parentXdg{};
  parentXdg.surface = &parentSurface;

  lambda::compositor::WaylandServer::Impl::XdgPopup parent{};
  parent.xdgSurface = &parentXdg;

  lambda::compositor::WaylandServer::Impl::XdgPopup unrelated{};
  unrelated.parentSurface = &unrelatedSurface;

  lambda::compositor::WaylandServer::Impl::XdgPopup dismissedChild{};
  dismissedChild.parentSurface = &parentSurface;
  dismissedChild.dismissed = true;

  std::array<lambda::compositor::WaylandServer::Impl::XdgPopup const*, 3> noLiveChild{
      &parent,
      &unrelated,
      &dismissedChild,
  };
  CHECK_FALSE(lambda::compositor::xdgPopupHasLiveChild(noLiveChild, &parent));

  lambda::compositor::WaylandServer::Impl::XdgPopup liveChild{};
  liveChild.parentSurface = &parentSurface;
  std::array<lambda::compositor::WaylandServer::Impl::XdgPopup const*, 2> withLiveChild{
      &parent,
      &liveChild,
  };
  CHECK(lambda::compositor::xdgPopupHasLiveChild(withLiveChild, &parent));
}

TEST_CASE("shell app id matching accepts built-in app aliases") {
  CHECK(lambda::compositor::wm::shellAppIdMatches("terminal", "lambda-terminal"));
  CHECK(lambda::compositor::wm::shellAppIdMatches("terminal", "foot"));
  CHECK(lambda::compositor::wm::shellAppIdMatches("browser", "firefox"));
  CHECK(lambda::compositor::wm::shellAppIdMatches("files", "lambda-files"));
  CHECK(lambda::compositor::wm::shellAppIdMatches("lambda-files", "files"));
  CHECK(lambda::compositor::wm::shellAppIdMatches("files", "org.gnome.Nautilus"));
  CHECK(lambda::compositor::wm::shellAppIdMatches("settings", "lambda-settings"));
  CHECK(lambda::compositor::wm::shellAppIdMatches("lambda-settings", "settings"));
  CHECK(lambda::compositor::wm::shellAppIdMatches("lambda-settings", "lambda-settings"));
}

TEST_CASE("surface input region defaults to full surface and can exclude points") {
  lambda::compositor::WaylandServer::Impl::Surface surface{};

  CHECK(lambda::compositor::wm::inputRegionContains(&surface, 500.f, 500.f));

  surface.regionState.inputRegionInfinite = false;
  surface.regionState.inputRegionRects.push_back({.x = 10, .y = 20, .width = 30, .height = 40});

  CHECK(lambda::compositor::wm::inputRegionContains(&surface, 10.f, 20.f));
  CHECK(lambda::compositor::wm::inputRegionContains(&surface, 39.9f, 59.9f));
  CHECK_FALSE(lambda::compositor::wm::inputRegionContains(&surface, 9.9f, 20.f));
  CHECK_FALSE(lambda::compositor::wm::inputRegionContains(&surface, 40.f, 20.f));
  CHECK_FALSE(lambda::compositor::wm::inputRegionContains(&surface, 10.f, 60.f));
}

TEST_CASE("surface pending region state does not affect committed input region") {
  lambda::compositor::WaylandServer::Impl::Surface surface{};

  surface.pendingRegionState.inputRegionInfinite = false;
  surface.pendingRegionState.inputRegionRects.push_back({.x = 10, .y = 20, .width = 30, .height = 40});
  surface.pendingRegionState.inputRegionSet = true;

  CHECK(lambda::compositor::wm::inputRegionContains(&surface, 500.f, 500.f));

  surface.regionState.inputRegionInfinite = surface.pendingRegionState.inputRegionInfinite;
  surface.regionState.inputRegionRects = surface.pendingRegionState.inputRegionRects;

  CHECK(lambda::compositor::wm::inputRegionContains(&surface, 10.f, 20.f));
  CHECK_FALSE(lambda::compositor::wm::inputRegionContains(&surface, 500.f, 500.f));
}

TEST_CASE("surface pending damage state is separate from committed damage") {
  lambda::compositor::WaylandServer::Impl::Surface surface{};

  surface.pendingDamageState.bufferRects.push_back({.x = 0, .y = 0, .width = 20, .height = 10});

  CHECK(surface.damageState.bufferRects.empty());

  surface.damageState.bufferRects = surface.pendingDamageState.bufferRects;
  surface.pendingDamageState.bufferRects.clear();

  REQUIRE(surface.damageState.bufferRects.size() == 1);
  CHECK(surface.damageState.bufferRects[0].width == 20);
  CHECK(surface.pendingDamageState.bufferRects.empty());
}

TEST_CASE("surface viewport pending state does not affect committed display size") {
  lambda::compositor::WaylandServer::Impl::Surface surface{};
  surface.width = 400;
  surface.height = 200;
  surface.bufferState.scale = 2;

  CHECK(lambda::compositor::surfaceCommittedDisplayWidth(&surface) == 200);
  CHECK(lambda::compositor::surfaceCommittedDisplayHeight(&surface) == 100);

  surface.pendingViewportState.destinationSet = true;
  surface.pendingViewportState.destinationWidth = 320;
  surface.pendingViewportState.destinationHeight = 180;

  CHECK(lambda::compositor::surfaceCommittedDisplayWidth(&surface) == 200);
  CHECK(lambda::compositor::surfaceCommittedDisplayHeight(&surface) == 100);

  surface.viewportState = surface.pendingViewportState;

  CHECK(lambda::compositor::surfaceCommittedDisplayWidth(&surface) == 320);
  CHECK(lambda::compositor::surfaceCommittedDisplayHeight(&surface) == 180);
}

TEST_CASE("surface pending buffer state does not affect committed display size or offset") {
  lambda::compositor::WaylandServer::Impl::Surface surface{};
  surface.width = 400;
  surface.height = 200;
  surface.bufferState.scale = 2;

  surface.pendingBufferState.scale = 4;
  surface.pendingBufferState.scaleSet = true;
  surface.pendingBufferState.transform = WL_OUTPUT_TRANSFORM_90;
  surface.pendingBufferState.transformSet = true;
  surface.pendingBufferState.offsetX = 12;
  surface.pendingBufferState.offsetY = 24;
  surface.pendingBufferState.offsetSet = true;

  CHECK(lambda::compositor::surfaceCommittedDisplayWidth(&surface) == 200);
  CHECK(lambda::compositor::surfaceCommittedDisplayHeight(&surface) == 100);
  CHECK(surface.bufferState.offsetX == 0);
  CHECK(surface.bufferState.offsetY == 0);

  surface.bufferState.scale = surface.pendingBufferState.scale;
  surface.bufferState.transform = surface.pendingBufferState.transform;
  surface.bufferState.offsetX = surface.pendingBufferState.offsetX;
  surface.bufferState.offsetY = surface.pendingBufferState.offsetY;

  CHECK(lambda::compositor::surfaceCommittedDisplayWidth(&surface) == 50);
  CHECK(lambda::compositor::surfaceCommittedDisplayHeight(&surface) == 100);
  CHECK(surface.bufferState.offsetX == 12);
  CHECK(surface.bufferState.offsetY == 24);
}

TEST_CASE("xdg window geometry validates positive size") {
  CHECK(lambda::compositor::wm::xdgWindowGeometrySizeValid(1, 1));
  CHECK_FALSE(lambda::compositor::wm::xdgWindowGeometrySizeValid(0, 1));
  CHECK_FALSE(lambda::compositor::wm::xdgWindowGeometrySizeValid(1, 0));
  CHECK_FALSE(lambda::compositor::wm::xdgWindowGeometrySizeValid(-1, 1));
}

TEST_CASE("xdg window geometry offsets surface-local input") {
  lambda::compositor::WaylandServer::Impl::Surface surface{};
  surface.role = lambda::compositor::SurfaceRole::XdgToplevel;
  surface.windowX = 100;
  surface.windowY = 80;
  surface.xdgRoleState.windowGeometrySet = true;
  surface.xdgRoleState.windowGeometry = lambda::compositor::WindowGeometry{
      .x = 32,
      .y = 24,
      .width = 960,
      .height = 640,
  };

  CHECK(lambda::compositor::wm::surfaceBufferOriginX(&surface) == doctest::Approx(68.f));
  CHECK(lambda::compositor::wm::surfaceBufferOriginY(&surface) == doctest::Approx(56.f));
  CHECK(lambda::compositor::wm::surfaceLocalX(&surface, 110.f) == doctest::Approx(42.f));
  CHECK(lambda::compositor::wm::surfaceLocalY(&surface, 90.f) == doctest::Approx(34.f));
}

TEST_CASE("xdg toplevel size hints reject negative and inverted dimensions") {
  using lambda::compositor::wm::ToplevelSizeHints;
  CHECK(lambda::compositor::wm::toplevelSizeHintsValid(ToplevelSizeHints{}));
  CHECK(lambda::compositor::wm::toplevelSizeHintsValid({
      .minWidth = 320,
      .minHeight = 200,
      .maxWidth = 640,
      .maxHeight = 480,
  }));
  CHECK_FALSE(lambda::compositor::wm::toplevelSizeHintsValid({
      .minWidth = -1,
  }));
  CHECK_FALSE(lambda::compositor::wm::toplevelSizeHintsValid({
      .minWidth = 641,
      .maxWidth = 640,
  }));
  CHECK_FALSE(lambda::compositor::wm::toplevelSizeHintsValid({
      .minHeight = 481,
      .maxHeight = 480,
  }));
}

TEST_CASE("xdg toplevel pending size hints are validated as commit state") {
  lambda::compositor::WaylandServer::Impl::XdgToplevel toplevel{};
  toplevel.minWidth = 320;
  toplevel.minHeight = 200;
  toplevel.maxWidth = 640;
  toplevel.maxHeight = 480;

  CHECK(lambda::compositor::wm::toplevelPendingSizeHintsValid(&toplevel));

  toplevel.pendingMaxWidth = 300;
  toplevel.pendingMaxHeight = 480;
  toplevel.pendingMaxSizeSet = true;
  CHECK_FALSE(lambda::compositor::wm::toplevelPendingSizeHintsValid(&toplevel));

  toplevel.pendingMaxWidth = 640;
  CHECK(lambda::compositor::wm::toplevelPendingSizeHintsValid(&toplevel));

  toplevel.pendingMinWidth = -1;
  toplevel.pendingMinHeight = 200;
  toplevel.pendingMinSizeSet = true;
  CHECK_FALSE(lambda::compositor::wm::toplevelPendingSizeHintsValid(&toplevel));
}

TEST_CASE("xdg toplevel size hints clamp interactive resize geometry around anchored edges") {
  using lambda::compositor::ResizeEdge;
  using lambda::compositor::WindowGeometry;
  lambda::compositor::wm::ToplevelSizeHints const hints{
      .minWidth = 240,
      .minHeight = 180,
      .maxWidth = 640,
      .maxHeight = 480,
  };

  auto rightBottom = lambda::compositor::wm::clampToplevelGeometryToSizeHints(
      WindowGeometry{.x = 100, .y = 100, .width = 800, .height = 700},
      hints,
      ResizeEdge::Right | ResizeEdge::Bottom);
  CHECK(rightBottom.x == 100);
  CHECK(rightBottom.y == 100);
  CHECK(rightBottom.width == 640);
  CHECK(rightBottom.height == 480);

  auto leftTop = lambda::compositor::wm::clampToplevelGeometryToSizeHints(
      WindowGeometry{.x = 100, .y = 100, .width = 120, .height = 90},
      hints,
      ResizeEdge::Left | ResizeEdge::Top);
  CHECK(leftTop.x == -20);
  CHECK(leftTop.y == 10);
  CHECK(leftTop.width == 240);
  CHECK(leftTop.height == 180);
}
