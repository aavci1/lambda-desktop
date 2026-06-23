#include "Compositor/Window/WindowManagerInternal.hpp"
#include "Compositor/Wayland/ActivationState.hpp"
#include "Compositor/Wayland/PointerConstraintState.hpp"
#include "Compositor/Wayland/SeatFocusState.hpp"
#include "Compositor/Wayland/XdgPopupState.hpp"
#include "Compositor/Wayland/XdgSurfaceState.hpp"
#include "Compositor/Wayland/XdgToplevelState.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <deque>
#include <initializer_list>
#include <linux/input-event-codes.h>
#include <memory>
#include <string>
#include <vector>

namespace {

std::unique_ptr<lambdaui::compositor::WaylandServer::Impl::Surface> testSurface(std::uint64_t id,
                                                                            bool minimized = false) {
  auto surface = std::make_unique<lambdaui::compositor::WaylandServer::Impl::Surface>();
  surface->id = id;
  surface->role = lambdaui::compositor::SurfaceRole::XdgToplevel;
  surface->minimized = minimized;
  return surface;
}

} // namespace

TEST_CASE("markToplevelMinimized only transitions visible toplevels") {
  lambdaui::compositor::WaylandServer::Impl::Surface surface{};
  surface.role = lambdaui::compositor::SurfaceRole::XdgToplevel;

  CHECK(lambdaui::compositor::wm::markToplevelMinimized(&surface));
  CHECK(surface.minimized);

  CHECK_FALSE(lambdaui::compositor::wm::markToplevelMinimized(&surface));
}

TEST_CASE("markToplevelMinimized rejects non-toplevel surfaces") {
  lambdaui::compositor::WaylandServer::Impl::Surface popup{};
  popup.role = lambdaui::compositor::SurfaceRole::XdgPopup;

  CHECK_FALSE(lambdaui::compositor::wm::markToplevelMinimized(&popup));
  CHECK_FALSE(popup.minimized);

  CHECK_FALSE(lambdaui::compositor::wm::markToplevelMinimized(nullptr));
}

TEST_CASE("restoreSurfaceForShellFocus restores minimized toplevels") {
  lambdaui::compositor::WaylandServer::Impl::Surface surface{};
  surface.role = lambdaui::compositor::SurfaceRole::XdgToplevel;
  surface.minimized = true;

  CHECK(lambdaui::compositor::wm::restoreSurfaceForShellFocus(&surface));
  CHECK_FALSE(surface.minimized);
}

TEST_CASE("restoreSurfaceForShellFocus rejects non-toplevel surfaces") {
  lambdaui::compositor::WaylandServer::Impl::Surface popup{};
  popup.role = lambdaui::compositor::SurfaceRole::XdgPopup;
  popup.minimized = true;

  CHECK_FALSE(lambdaui::compositor::wm::restoreSurfaceForShellFocus(&popup));
  CHECK(popup.minimized);

  CHECK_FALSE(lambdaui::compositor::wm::restoreSurfaceForShellFocus(nullptr));
}

TEST_CASE("focus order helpers skip minimized windows and fall back to stacking order") {
  auto first = testSurface(1);
  auto second = testSurface(2, true);
  auto third = testSurface(3);
  std::vector<std::unique_ptr<lambdaui::compositor::WaylandServer::Impl::Surface>> surfaces;
  surfaces.push_back(std::move(first));
  surfaces.push_back(std::move(second));
  surfaces.push_back(std::move(third));

  std::vector<lambdaui::compositor::WaylandServer::Impl::Surface*> focusOrder{
      surfaces[0].get(),
      surfaces[1].get(),
      surfaces[2].get(),
  };

  CHECK(lambdaui::compositor::wm::mostRecentToplevelFromOrders(focusOrder, surfaces) == surfaces[2].get());
  CHECK(lambdaui::compositor::wm::previousFocusedToplevelFromOrders(focusOrder, surfaces, surfaces[2].get()) ==
        surfaces[0].get());

  surfaces[2]->minimized = true;
  CHECK(lambdaui::compositor::wm::mostRecentToplevelFromOrders(focusOrder, surfaces) == surfaces[0].get());
  CHECK(lambdaui::compositor::wm::previousFocusedToplevelFromOrders(focusOrder, surfaces, surfaces[0].get()) == nullptr);

  focusOrder.clear();
  surfaces[2]->minimized = false;
  CHECK(lambdaui::compositor::wm::mostRecentToplevelFromOrders(focusOrder, surfaces) == surfaces[2].get());
}

TEST_CASE("focus cycle list remains stable while focus order mutates") {
  auto first = testSurface(1);
  auto second = testSurface(2);
  auto third = testSurface(3);
  std::vector<std::unique_ptr<lambdaui::compositor::WaylandServer::Impl::Surface>> surfaces;
  surfaces.push_back(std::move(first));
  surfaces.push_back(std::move(second));
  surfaces.push_back(std::move(third));

  std::vector<lambdaui::compositor::WaylandServer::Impl::Surface*> focusOrder{
      surfaces[0].get(),
      surfaces[1].get(),
      surfaces[2].get(),
  };
  auto cycle = lambdaui::compositor::wm::focusCycleListFromOrders(focusOrder, surfaces, surfaces[2].get());
  REQUIRE(cycle.size() == 3);
  CHECK(cycle[0] == surfaces[2].get());
  CHECK(cycle[1] == surfaces[1].get());
  CHECK(cycle[2] == surfaces[0].get());

  std::size_t index = 0;
  index = lambdaui::compositor::wm::advancedFocusCycleIndex(index, cycle.size(), true);
  CHECK(cycle[index] == surfaces[1].get());

  focusOrder = {
      surfaces[0].get(),
      surfaces[2].get(),
      surfaces[1].get(),
  };
  index = lambdaui::compositor::wm::advancedFocusCycleIndex(index, cycle.size(), true);
  CHECK(cycle[index] == surfaces[0].get());
}

TEST_CASE("focus cycle list supports reverse cycling and excludes minimized windows") {
  auto first = testSurface(1);
  auto second = testSurface(2, true);
  auto third = testSurface(3);
  std::vector<std::unique_ptr<lambdaui::compositor::WaylandServer::Impl::Surface>> surfaces;
  surfaces.push_back(std::move(first));
  surfaces.push_back(std::move(second));
  surfaces.push_back(std::move(third));

  std::vector<lambdaui::compositor::WaylandServer::Impl::Surface*> focusOrder{
      surfaces[0].get(),
      surfaces[1].get(),
      surfaces[2].get(),
  };
  auto cycle = lambdaui::compositor::wm::focusCycleListFromOrders(focusOrder, surfaces, surfaces[2].get());
  REQUIRE(cycle.size() == 2);
  CHECK(cycle[0] == surfaces[2].get());
  CHECK(cycle[1] == surfaces[0].get());
  CHECK(lambdaui::compositor::wm::advancedFocusCycleIndex(0, cycle.size(), false) == 1);
}

TEST_CASE("cycle focus shortcut treats shift as reverse direction for existing bindings") {
  lambdaui::compositor::ShortcutBinding cycle{
      .action = lambdaui::compositor::ShortcutAction::CycleFocus,
      .key = KEY_TAB,
      .meta = true,
  };
  CHECK(lambdaui::compositor::wm::shortcutBindingMatches(cycle, true, false, false, false));
  CHECK(lambdaui::compositor::wm::shortcutBindingMatches(cycle, true, false, false, true));

  lambdaui::compositor::ShortcutBinding close{
      .action = lambdaui::compositor::ShortcutAction::CloseFocused,
      .key = KEY_Q,
      .meta = true,
  };
  CHECK(lambdaui::compositor::wm::shortcutBindingMatches(close, true, false, false, false));
  CHECK_FALSE(lambdaui::compositor::wm::shortcutBindingMatches(close, true, false, false, true));
}

TEST_CASE("presentation eligibility excludes minimized toplevels and dismissed popups") {
  lambdaui::compositor::WaylandServer::Impl::Surface visible{};
  visible.role = lambdaui::compositor::SurfaceRole::XdgToplevel;
  CHECK(lambdaui::compositor::wm::surfaceEligibleForPresentation(&visible));

  visible.minimized = true;
  CHECK_FALSE(lambdaui::compositor::wm::surfaceEligibleForPresentation(&visible));

  lambdaui::compositor::WaylandServer::Impl::XdgPopup popupRole{};
  lambdaui::compositor::WaylandServer::Impl::Surface popup{};
  popup.role = lambdaui::compositor::SurfaceRole::XdgPopup;
  popup.xdgPopup = &popupRole;
  CHECK(lambdaui::compositor::wm::surfaceEligibleForPresentation(&popup));
  popupRole.dismissed = true;
  CHECK_FALSE(lambdaui::compositor::wm::surfaceEligibleForPresentation(&popup));
}

TEST_CASE("xdg activation tokens are matched only after commit") {
  auto token = std::make_unique<lambdaui::compositor::WaylandServer::Impl::ActivationToken>();
  token->token = "lambda-test";

  CHECK(lambdaui::compositor::activationTokenMutable(token.get()));
  CHECK_FALSE(lambdaui::compositor::activationTokenMatches(token.get(), "lambda-test"));

  std::vector<std::unique_ptr<lambdaui::compositor::WaylandServer::Impl::ActivationToken>> tokens;
  tokens.push_back(std::move(token));
  CHECK(lambdaui::compositor::activationTokenForName(tokens, "lambda-test") == nullptr);

  tokens.front()->committed = true;
  CHECK_FALSE(lambdaui::compositor::activationTokenMutable(tokens.front().get()));
  CHECK(lambdaui::compositor::activationTokenMatches(tokens.front().get(), "lambda-test"));
  CHECK_FALSE(lambdaui::compositor::activationTokenMatches(tokens.front().get(), "missing"));
  CHECK(lambdaui::compositor::activationTokenForName(tokens, "lambda-test") == tokens.front().get());
  CHECK(lambdaui::compositor::activationTokenForName(tokens, "missing") == nullptr);
}

TEST_CASE("xdg activation tokens stop matching after the wlroots timeout") {
  auto token = std::make_unique<lambdaui::compositor::WaylandServer::Impl::ActivationToken>();
  token->token = "lambda-test";
  token->committed = true;
  token->expiresAtMs = 1'000;

  std::vector<std::unique_ptr<lambdaui::compositor::WaylandServer::Impl::ActivationToken>> tokens;
  tokens.push_back(std::move(token));

  CHECK(lambdaui::compositor::kActivationTokenTimeoutMs == 30'000);
  CHECK_FALSE(lambdaui::compositor::activationTokenExpired(tokens.front().get(), 999));
  CHECK(lambdaui::compositor::activationTokenForName(tokens, "lambda-test", 999) == tokens.front().get());

  CHECK(lambdaui::compositor::activationTokenExpired(tokens.front().get(), 1'000));
  CHECK(lambdaui::compositor::activationTokenForName(tokens, "lambda-test", 1'000) == nullptr);
}

TEST_CASE("xdg activation token focused surface validation follows keyboard or pointer focus") {
  lambdaui::compositor::WaylandServer::Impl::Surface focused{};
  lambdaui::compositor::WaylandServer::Impl::Surface other{};
  lambdaui::compositor::WaylandServer::Impl::ActivationToken token{};

  CHECK(lambdaui::compositor::activationTokenFocusedSurfaceValid(nullptr, nullptr, &token));

  token.surface = &focused;
  CHECK_FALSE(lambdaui::compositor::activationTokenFocusedSurfaceValid(nullptr, nullptr, &token));

  CHECK(lambdaui::compositor::activationTokenFocusedSurfaceValid(&focused, nullptr, &token));

  CHECK(lambdaui::compositor::activationTokenFocusedSurfaceValid(nullptr, &focused, &token));

  token.surface = &other;
  CHECK_FALSE(lambdaui::compositor::activationTokenFocusedSurfaceValid(nullptr, &focused, &token));
}

TEST_CASE("pointer constraint region is pending until surface commit") {
  lambdaui::compositor::WaylandServer::Impl::Surface surface{};
  surface.width = 100;
  surface.height = 80;
  surface.windowX = 10;
  surface.windowY = 20;

  lambdaui::compositor::WaylandServer::Impl::PointerConstraint constraint{};
  constraint.surface = &surface;
  REQUIRE(lambdaui::compositor::rebuildPointerConstraintEffectiveRegion(&constraint));
  CHECK(lambdaui::compositor::pointerConstraintRegionContainsLocalPoint(&constraint, 90.f, 70.f));

  constraint.pendingRegionInfinite = false;
  constraint.pendingRegionRects = {{.x = 20, .y = 10, .width = 30, .height = 20}};
  constraint.pendingRegionSet = true;

  CHECK(lambdaui::compositor::pointerConstraintRegionContainsLocalPoint(&constraint, 90.f, 70.f));
  CHECK(lambdaui::compositor::applyPointerConstraintPendingState(&constraint));
  CHECK_FALSE(lambdaui::compositor::pointerConstraintRegionContainsLocalPoint(&constraint, 90.f, 70.f));
  CHECK(lambdaui::compositor::pointerConstraintRegionContainsLocalPoint(&constraint, 25.f, 15.f));

  float globalX = 90.f;
  float globalY = 70.f;
  CHECK(lambdaui::compositor::clampPointerConstraintGlobalPoint(&constraint, globalX, globalY));
  CHECK(globalX == 59.f);
  CHECK(globalY == 49.f);
}

TEST_CASE("pointer constraint effective region follows surface input region") {
  lambdaui::compositor::WaylandServer::Impl::Surface surface{};
  surface.width = 100;
  surface.height = 80;
  surface.regionState.inputRegionInfinite = false;
  surface.regionState.inputRegionRects = {
      {.x = 0, .y = 0, .width = 40, .height = 40},
      {.x = 50, .y = 0, .width = 20, .height = 20},
  };

  lambdaui::compositor::WaylandServer::Impl::PointerConstraint constraint{};
  constraint.surface = &surface;
  constraint.regionInfinite = false;
  constraint.regionRects = {{.x = 30, .y = 10, .width = 40, .height = 20}};

  CHECK(lambdaui::compositor::rebuildPointerConstraintEffectiveRegion(&constraint));
  REQUIRE(constraint.effectiveRegionRects.size() == 2);
  CHECK(constraint.effectiveRegionRects[0].x == 30);
  CHECK(constraint.effectiveRegionRects[0].y == 10);
  CHECK(constraint.effectiveRegionRects[0].width == 10);
  CHECK(constraint.effectiveRegionRects[0].height == 20);
  CHECK(constraint.effectiveRegionRects[1].x == 50);
  CHECK(constraint.effectiveRegionRects[1].y == 10);
  CHECK(constraint.effectiveRegionRects[1].width == 20);
  CHECK(constraint.effectiveRegionRects[1].height == 10);
}

TEST_CASE("pointer constraint default region follows committed frame size during pending toplevel configure") {
  lambdaui::compositor::WaylandServer::Impl::Surface surface{};
  surface.role = lambdaui::compositor::SurfaceRole::XdgToplevel;
  surface.width = 1280;
  surface.height = 960;
  surface.bufferState.scale = 2;
  surface.frameWidth = 900;
  surface.frameHeight = 700;
  surface.pendingResizeConfigure = true;

  lambdaui::compositor::WaylandServer::Impl::PointerConstraint constraint{};
  constraint.surface = &surface;

  REQUIRE(lambdaui::compositor::rebuildPointerConstraintEffectiveRegion(&constraint));
  REQUIRE(constraint.effectiveRegionRects.size() == 1);
  CHECK(constraint.effectiveRegionRects[0].width == 640);
  CHECK(constraint.effectiveRegionRects[0].height == 480);
  CHECK(lambdaui::compositor::pointerConstraintRegionContainsLocalPoint(&constraint, 639.f, 479.f));
  CHECK_FALSE(lambdaui::compositor::pointerConstraintRegionContainsLocalPoint(&constraint, 640.f, 480.f));
}

TEST_CASE("locked pointer cursor hint is committed with pointer constraint state") {
  lambdaui::compositor::WaylandServer::Impl::PointerConstraint constraint{};
  constraint.pendingCursorHintX = 12.5f;
  constraint.pendingCursorHintY = 7.25f;
  constraint.pendingCursorHintSet = true;

  CHECK_FALSE(constraint.cursorHintSet);
  CHECK(lambdaui::compositor::applyPointerConstraintPendingState(&constraint));
  CHECK(constraint.cursorHintSet);
  CHECK(constraint.cursorHintX == 12.5f);
  CHECK(constraint.cursorHintY == 7.25f);
}

TEST_CASE("pointer constraint cached commit state does not consume later pending requests") {
  lambdaui::compositor::WaylandServer::Impl::Surface surface{};
  surface.width = 100;
  surface.height = 80;

  lambdaui::compositor::WaylandServer::Impl::PointerConstraint constraint{};
  constraint.surface = &surface;
  constraint.pendingRegionInfinite = false;
  constraint.pendingRegionRects = {{.x = 10, .y = 10, .width = 20, .height = 20}};
  constraint.pendingRegionSet = true;

  auto cached = lambdaui::compositor::takePointerConstraintPendingState(&constraint);
  CHECK(cached.regionSet);
  CHECK_FALSE(constraint.pendingRegionSet);

  constraint.pendingRegionInfinite = false;
  constraint.pendingRegionRects = {{.x = 40, .y = 40, .width = 20, .height = 20}};
  constraint.pendingRegionSet = true;

  CHECK(lambdaui::compositor::applyPointerConstraintCommitState(cached));
  CHECK(lambdaui::compositor::pointerConstraintRegionContainsLocalPoint(&constraint, 15.f, 15.f));
  CHECK_FALSE(lambdaui::compositor::pointerConstraintRegionContainsLocalPoint(&constraint, 45.f, 45.f));
  CHECK(constraint.pendingRegionSet);

  CHECK(lambdaui::compositor::applyPointerConstraintPendingState(&constraint));
  CHECK_FALSE(lambdaui::compositor::pointerConstraintRegionContainsLocalPoint(&constraint, 15.f, 15.f));
  CHECK(lambdaui::compositor::pointerConstraintRegionContainsLocalPoint(&constraint, 45.f, 45.f));
}

TEST_CASE("oneshot pointer constraints are destroyed after deactivation") {
  lambdaui::compositor::WaylandServer::Impl::PointerConstraint persistent{};
  persistent.lifetime = ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT;
  persistent.active = false;
  CHECK_FALSE(lambdaui::compositor::pointerConstraintShouldDestroyAfterDeactivation(&persistent));

  lambdaui::compositor::WaylandServer::Impl::PointerConstraint oneshot{};
  oneshot.lifetime = ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_ONESHOT;
  oneshot.active = true;
  CHECK_FALSE(lambdaui::compositor::pointerConstraintShouldDestroyAfterDeactivation(&oneshot));

  oneshot.active = false;
  CHECK(lambdaui::compositor::pointerConstraintShouldDestroyAfterDeactivation(&oneshot));
}

TEST_CASE("xdg popup parent validation accepts only constructed xdg roles") {
  CHECK(lambdaui::compositor::xdgPopupParentHasValidRole(nullptr));

  lambdaui::compositor::WaylandServer::Impl::XdgSurface parent{};
  lambdaui::compositor::WaylandServer::Impl::Surface surface{};
  parent.surface = &surface;

  surface.role = lambdaui::compositor::SurfaceRole::None;
  CHECK_FALSE(lambdaui::compositor::xdgPopupParentHasValidRole(&parent));

  surface.role = lambdaui::compositor::SurfaceRole::XdgSurface;
  CHECK_FALSE(lambdaui::compositor::xdgPopupParentHasValidRole(&parent));

  surface.role = lambdaui::compositor::SurfaceRole::LayerSurface;
  CHECK_FALSE(lambdaui::compositor::xdgPopupParentHasValidRole(&parent));

  surface.role = lambdaui::compositor::SurfaceRole::XdgToplevel;
  CHECK(lambdaui::compositor::xdgPopupParentHasValidRole(&parent));

  surface.role = lambdaui::compositor::SurfaceRole::XdgPopup;
  CHECK(lambdaui::compositor::xdgPopupParentHasValidRole(&parent));
}

TEST_CASE("xdg popup layer-surface reparenting preserves relative coordinates") {
  auto const moved = lambdaui::compositor::xdgPopupReparentGeometry({
      .oldParentX = 100,
      .oldParentY = 50,
      .newParentX = 300,
      .newParentY = 90,
      .popupWindowX = 128,
      .popupWindowY = 74,
  });
  CHECK(moved.popupWindowX == 328);
  CHECK(moved.popupWindowY == 114);
  CHECK(moved.configuredX == 28);
  CHECK(moved.configuredY == 24);

  auto const parentless = lambdaui::compositor::xdgPopupReparentGeometry({
      .newParentX = 300,
      .newParentY = 90,
      .popupWindowX = 20,
      .popupWindowY = 30,
  });
  CHECK(parentless.popupWindowX == 320);
  CHECK(parentless.popupWindowY == 120);
  CHECK(parentless.configuredX == 20);
  CHECK(parentless.configuredY == 30);
}

TEST_CASE("xdg surface role object validation follows constructed xdg roles") {
  CHECK_FALSE(lambdaui::compositor::xdgSurfaceHasConstructedRoleObject(nullptr));

  lambdaui::compositor::WaylandServer::Impl::XdgSurface xdgSurface{};
  CHECK_FALSE(lambdaui::compositor::xdgSurfaceHasConstructedRoleObject(&xdgSurface));

  lambdaui::compositor::WaylandServer::Impl::Surface surface{};
  xdgSurface.surface = &surface;

  surface.role = lambdaui::compositor::SurfaceRole::None;
  CHECK_FALSE(lambdaui::compositor::xdgSurfaceHasConstructedRoleObject(&xdgSurface));

  surface.role = lambdaui::compositor::SurfaceRole::XdgSurface;
  CHECK_FALSE(lambdaui::compositor::xdgSurfaceHasConstructedRoleObject(&xdgSurface));

  surface.role = lambdaui::compositor::SurfaceRole::LayerSurface;
  CHECK_FALSE(lambdaui::compositor::xdgSurfaceHasConstructedRoleObject(&xdgSurface));

  surface.role = lambdaui::compositor::SurfaceRole::XdgToplevel;
  CHECK(lambdaui::compositor::xdgSurfaceHasConstructedRoleObject(&xdgSurface));

  surface.role = lambdaui::compositor::SurfaceRole::XdgPopup;
  CHECK(lambdaui::compositor::xdgSurfaceHasConstructedRoleObject(&xdgSurface));
}

TEST_CASE("xdg surface creation rejects surfaces with an existing buffer") {
  lambdaui::compositor::WaylandServer::Impl::Surface surface{};
  CHECK_FALSE(lambdaui::compositor::xdgSurfaceCreationHasExistingBuffer(&surface));

  surface.bufferState.buffer = reinterpret_cast<wl_resource*>(0x1);
  CHECK(lambdaui::compositor::xdgSurfaceCreationHasExistingBuffer(&surface));
}

TEST_CASE("xdg surface buffer commits require role object and configure ack") {
  using lambdaui::compositor::XdgSurfaceBufferCommitReadiness;

  CHECK(lambdaui::compositor::xdgSurfaceBufferCommitReadiness(nullptr) == XdgSurfaceBufferCommitReadiness::Ready);

  lambdaui::compositor::WaylandServer::Impl::Surface surface{};
  lambdaui::compositor::WaylandServer::Impl::XdgSurface xdgSurface{};
  xdgSurface.surface = &surface;

  surface.role = lambdaui::compositor::SurfaceRole::XdgSurface;
  CHECK(lambdaui::compositor::xdgSurfaceBufferCommitReadiness(&xdgSurface) ==
        XdgSurfaceBufferCommitReadiness::MissingRoleObject);

  surface.role = lambdaui::compositor::SurfaceRole::XdgToplevel;
  CHECK(lambdaui::compositor::xdgSurfaceBufferCommitReadiness(&xdgSurface) ==
        XdgSurfaceBufferCommitReadiness::Unconfigured);

  xdgSurface.configured = true;
  CHECK(lambdaui::compositor::xdgSurfaceBufferCommitReadiness(&xdgSurface) == XdgSurfaceBufferCommitReadiness::Ready);
}

TEST_CASE("xdg surface configure state resets on unmap") {
  lambdaui::compositor::WaylandServer::Impl::XdgSurface xdgSurface{};
  CHECK_FALSE(lambdaui::compositor::resetXdgSurfaceConfigureStateForUnmap(&xdgSurface));

  xdgSurface.configured = true;
  xdgSurface.configureList.push_back(lambdaui::compositor::WaylandServer::Impl::XdgConfigure{
      .serial = 12,
      .role = lambdaui::compositor::SurfaceRole::XdgToplevel,
      .width = 640,
      .height = 480,
  });
  xdgSurface.pendingConfigure = lambdaui::compositor::WaylandServer::Impl::XdgConfigure{
      .serial = 12,
      .role = lambdaui::compositor::SurfaceRole::XdgToplevel,
  };
  xdgSurface.currentConfigure = lambdaui::compositor::WaylandServer::Impl::XdgConfigure{
      .serial = 11,
      .role = lambdaui::compositor::SurfaceRole::XdgToplevel,
  };

  CHECK(lambdaui::compositor::resetXdgSurfaceConfigureStateForUnmap(&xdgSurface));
  CHECK_FALSE(xdgSurface.configured);
  CHECK(xdgSurface.configureList.empty());
  CHECK_FALSE(xdgSurface.pendingConfigure.has_value());
  CHECK_FALSE(xdgSurface.currentConfigure.has_value());
  CHECK_FALSE(lambdaui::compositor::resetXdgSurfaceConfigureStateForUnmap(&xdgSurface));
}

TEST_CASE("xdg surface ack_configure consumes serial range into pending configure state") {
  using lambdaui::compositor::SurfaceRole;
  using lambdaui::compositor::WaylandServer;
  using lambdaui::compositor::XdgConfigureAckStatus;

  WaylandServer::Impl::XdgSurface xdgSurface{};
  xdgSurface.configureList.push_back(WaylandServer::Impl::XdgConfigure{
      .serial = 10,
      .role = SurfaceRole::XdgToplevel,
      .width = 640,
      .height = 480,
      .hasWindowGeometry = true,
      .windowX = 8,
      .windowY = 12,
      .windowWidth = 624,
      .windowHeight = 456,
  });
  xdgSurface.configureList.push_back(WaylandServer::Impl::XdgConfigure{
      .serial = 11,
      .role = SurfaceRole::XdgToplevel,
      .width = 800,
      .height = 600,
  });
  xdgSurface.configureList.push_back(WaylandServer::Impl::XdgConfigure{
      .serial = 12,
      .role = SurfaceRole::XdgToplevel,
      .width = 1024,
      .height = 768,
  });

  auto missing = lambdaui::compositor::acknowledgeXdgSurfaceConfigure(&xdgSurface, 9, 10);
  CHECK(missing.status == XdgConfigureAckStatus::UnknownSerial);
  CHECK_FALSE(missing.ackedConfigure.has_value());
  CHECK_FALSE(missing.ackedResizeConfigure.has_value());
  CHECK_FALSE(xdgSurface.configured);
  CHECK(xdgSurface.configureList.size() == 3);
  CHECK_FALSE(xdgSurface.pendingConfigure.has_value());
  CHECK_FALSE(xdgSurface.currentConfigure.has_value());

  auto acked = lambdaui::compositor::acknowledgeXdgSurfaceConfigure(&xdgSurface, 11, 10);
  CHECK(acked.status == XdgConfigureAckStatus::Acked);
  REQUIRE(acked.ackedConfigure.has_value());
  CHECK(acked.ackedConfigure->serial == 11);
  REQUIRE(acked.ackedResizeConfigure.has_value());
  CHECK(acked.ackedResizeConfigure->serial == 10);
  CHECK(acked.ackedResizeConfigure->hasWindowGeometry);
  CHECK(xdgSurface.configured);
  REQUIRE(xdgSurface.pendingConfigure.has_value());
  CHECK(xdgSurface.pendingConfigure->serial == 11);
  CHECK_FALSE(xdgSurface.currentConfigure.has_value());
  REQUIRE(xdgSurface.configureList.size() == 1);
  CHECK(xdgSurface.configureList.front().serial == 12);

  CHECK(lambdaui::compositor::commitPendingXdgConfigure(&xdgSurface));
  CHECK_FALSE(xdgSurface.pendingConfigure.has_value());
  REQUIRE(xdgSurface.currentConfigure.has_value());
  CHECK(xdgSurface.currentConfigure->serial == 11);
  CHECK_FALSE(lambdaui::compositor::commitPendingXdgConfigure(&xdgSurface));
}

TEST_CASE("xdg surface ack_configure does not consume newer resize configure") {
  using lambdaui::compositor::SurfaceRole;
  using lambdaui::compositor::WaylandServer;
  using lambdaui::compositor::XdgConfigureAckStatus;

  WaylandServer::Impl::XdgSurface xdgSurface{};
  xdgSurface.configureList.push_back(WaylandServer::Impl::XdgConfigure{
      .serial = 20,
      .role = SurfaceRole::XdgToplevel,
      .width = 640,
      .height = 480,
  });
  xdgSurface.configureList.push_back(WaylandServer::Impl::XdgConfigure{
      .serial = 21,
      .role = SurfaceRole::XdgToplevel,
      .width = 800,
      .height = 600,
  });

  auto acked = lambdaui::compositor::acknowledgeXdgSurfaceConfigure(&xdgSurface, 20, 21);
  CHECK(acked.status == XdgConfigureAckStatus::Acked);
  REQUIRE(acked.ackedConfigure.has_value());
  CHECK(acked.ackedConfigure->serial == 20);
  CHECK_FALSE(acked.ackedResizeConfigure.has_value());
  REQUIRE(xdgSurface.pendingConfigure.has_value());
  CHECK(xdgSurface.pendingConfigure->serial == 20);
  REQUIRE(xdgSurface.configureList.size() == 1);
  CHECK(xdgSurface.configureList.front().serial == 21);
}

TEST_CASE("xdg toplevel interactive requests require a configured toplevel surface") {
  CHECK_FALSE(lambdaui::compositor::xdgToplevelSurfaceConfigured(nullptr));

  lambdaui::compositor::WaylandServer::Impl::XdgToplevel toplevel{};
  CHECK_FALSE(lambdaui::compositor::xdgToplevelSurfaceConfigured(&toplevel));

  lambdaui::compositor::WaylandServer::Impl::XdgSurface xdgSurface{};
  toplevel.xdgSurface = &xdgSurface;
  CHECK_FALSE(lambdaui::compositor::xdgToplevelSurfaceConfigured(&toplevel));

  lambdaui::compositor::WaylandServer::Impl::Surface surface{};
  xdgSurface.surface = &surface;
  xdgSurface.configured = true;

  surface.role = lambdaui::compositor::SurfaceRole::XdgSurface;
  CHECK_FALSE(lambdaui::compositor::xdgToplevelSurfaceConfigured(&toplevel));

  surface.role = lambdaui::compositor::SurfaceRole::XdgPopup;
  CHECK_FALSE(lambdaui::compositor::xdgToplevelSurfaceConfigured(&toplevel));

  surface.role = lambdaui::compositor::SurfaceRole::XdgToplevel;
  CHECK(lambdaui::compositor::xdgToplevelSurfaceConfigured(&toplevel));

  xdgSurface.configured = false;
  CHECK_FALSE(lambdaui::compositor::xdgToplevelSurfaceConfigured(&toplevel));
}

TEST_CASE("interactive frame size follows committed content while toplevel configure is pending") {
  lambdaui::compositor::WaylandServer::Impl::Surface surface{};
  surface.role = lambdaui::compositor::SurfaceRole::XdgToplevel;
  surface.width = 1280;
  surface.height = 960;
  surface.bufferState.scale = 2;
  surface.frameWidth = 900;
  surface.frameHeight = 700;

  auto live = lambdaui::compositor::wm::interactiveFrameDisplaySize(&surface);
  CHECK(live.width == 900);
  CHECK(live.height == 700);
  CHECK_FALSE(lambdaui::compositor::wm::toplevelHasPendingUncommittedFrame(&surface));

  surface.awaitingConfigureCommit = true;
  auto awaiting = lambdaui::compositor::wm::interactiveFrameDisplaySize(&surface);
  CHECK(awaiting.width == 640);
  CHECK(awaiting.height == 480);
  CHECK(lambdaui::compositor::wm::toplevelHasPendingUncommittedFrame(&surface));

  surface.awaitingConfigureCommit = false;
  surface.resizeConfigureInFlight = true;
  auto resizing = lambdaui::compositor::wm::interactiveFrameDisplaySize(&surface);
  CHECK(resizing.width == 640);
  CHECK(resizing.height == 480);

  surface.role = lambdaui::compositor::SurfaceRole::LayerSurface;
  auto layer = lambdaui::compositor::wm::interactiveFrameDisplaySize(&surface);
  CHECK(layer.width == 900);
  CHECK(layer.height == 700);
  CHECK_FALSE(lambdaui::compositor::wm::toplevelHasPendingUncommittedFrame(&surface));
}

TEST_CASE("interactive frame size does not invent committed content for empty pending toplevels") {
  lambdaui::compositor::WaylandServer::Impl::Surface surface{};
  surface.role = lambdaui::compositor::SurfaceRole::XdgToplevel;
  surface.frameWidth = 900;
  surface.frameHeight = 700;
  surface.awaitingConfigureCommit = true;

  CHECK_FALSE(lambdaui::compositor::wm::surfaceHasCommittedDisplaySize(&surface));
  auto empty = lambdaui::compositor::wm::interactiveFrameDisplaySize(&surface);
  CHECK(empty.width == 900);
  CHECK(empty.height == 700);

  surface.viewportState.destinationSet = true;
  surface.viewportState.destinationWidth = 640;
  surface.viewportState.destinationHeight = 480;
  CHECK(lambdaui::compositor::wm::surfaceHasCommittedDisplaySize(&surface));
  auto viewport = lambdaui::compositor::wm::interactiveFrameDisplaySize(&surface);
  CHECK(viewport.width == 640);
  CHECK(viewport.height == 480);
}

TEST_CASE("window geometry helper uses interactive frame size during pending toplevel configure") {
  lambdaui::compositor::WaylandServer::Impl::Surface surface{};
  surface.role = lambdaui::compositor::SurfaceRole::XdgToplevel;
  surface.windowX = 50;
  surface.windowY = 70;
  surface.width = 1280;
  surface.height = 960;
  surface.bufferState.scale = 2;
  surface.frameWidth = 900;
  surface.frameHeight = 700;

  auto live = lambdaui::compositor::wm::windowGeometryFor(&surface);
  CHECK(live.x == 50);
  CHECK(live.y == 70);
  CHECK(live.width == 900);
  CHECK(live.height == 700);

  surface.pendingResizeConfigure = true;
  auto pending = lambdaui::compositor::wm::windowGeometryFor(&surface);
  CHECK(pending.x == 50);
  CHECK(pending.y == 70);
  CHECK(pending.width == 640);
  CHECK(pending.height == 480);
}

TEST_CASE("usable window geometry helper rejects empty surfaces and follows pending configure size") {
  lambdaui::compositor::WaylandServer::Impl::Surface empty{};
  empty.role = lambdaui::compositor::SurfaceRole::XdgToplevel;
  CHECK_FALSE(lambdaui::compositor::wm::usableWindowGeometryFor(&empty).has_value());

  lambdaui::compositor::WaylandServer::Impl::Surface surface{};
  surface.role = lambdaui::compositor::SurfaceRole::XdgToplevel;
  surface.windowX = 25;
  surface.windowY = 35;
  surface.width = 1024;
  surface.height = 768;
  surface.bufferState.scale = 2;
  surface.frameWidth = 900;
  surface.frameHeight = 700;
  surface.pendingResizeConfigure = true;

  auto geometry = lambdaui::compositor::wm::usableWindowGeometryFor(&surface);
  REQUIRE(geometry.has_value());
  CHECK(geometry->x == 25);
  CHECK(geometry->y == 35);
  CHECK(geometry->width == 512);
  CHECK(geometry->height == 384);
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

  CHECK(lambdaui::compositor::xdgToplevelTitleUtf8Valid(""));
  CHECK(lambdaui::compositor::xdgToplevelTitleUtf8Valid("Settings"));
  CHECK(lambdaui::compositor::xdgToplevelTitleUtf8Valid(bytes({0xC2, 0xA2})));
  CHECK(lambdaui::compositor::xdgToplevelTitleUtf8Valid(bytes({0xE2, 0x82, 0xAC})));
  CHECK(lambdaui::compositor::xdgToplevelTitleUtf8Valid(bytes({0xF0, 0x9F, 0x98, 0x80})));

  CHECK_FALSE(lambdaui::compositor::xdgToplevelTitleUtf8Valid(bytes({0x80})));
  CHECK_FALSE(lambdaui::compositor::xdgToplevelTitleUtf8Valid(bytes({0xC0, 0x80})));
  CHECK_FALSE(lambdaui::compositor::xdgToplevelTitleUtf8Valid(bytes({0xE0, 0x80, 0x80})));
  CHECK_FALSE(lambdaui::compositor::xdgToplevelTitleUtf8Valid(bytes({0xED, 0xA0, 0x80})));
  CHECK_FALSE(lambdaui::compositor::xdgToplevelTitleUtf8Valid(bytes({0xF5, 0x80, 0x80, 0x80})));
  CHECK_FALSE(lambdaui::compositor::xdgToplevelTitleUtf8Valid(bytes({0xE2, 0x82})));
}

TEST_CASE("xdg toplevel parent retention requires a mapped toplevel parent") {
  lambdaui::compositor::WaylandServer::Impl::XdgToplevel parent{};
  CHECK_FALSE(lambdaui::compositor::xdgToplevelMapped(&parent));
  CHECK(lambdaui::compositor::xdgToplevelRetainedParent(&parent) == nullptr);

  lambdaui::compositor::WaylandServer::Impl::XdgSurface xdgSurface{};
  lambdaui::compositor::WaylandServer::Impl::Surface surface{};
  parent.xdgSurface = &xdgSurface;
  xdgSurface.surface = &surface;
  surface.role = lambdaui::compositor::SurfaceRole::XdgToplevel;

  parent.mapped = false;
  CHECK_FALSE(lambdaui::compositor::xdgToplevelMapped(&parent));
  CHECK(lambdaui::compositor::xdgToplevelRetainedParent(&parent) == nullptr);

  parent.mapped = true;
  CHECK(lambdaui::compositor::xdgToplevelMapped(&parent));
  CHECK(lambdaui::compositor::xdgToplevelRetainedParent(&parent) == &parent);

  surface.role = lambdaui::compositor::SurfaceRole::XdgSurface;
  CHECK_FALSE(lambdaui::compositor::xdgToplevelMapped(&parent));
  CHECK(lambdaui::compositor::xdgToplevelRetainedParent(&parent) == nullptr);
}

TEST_CASE("xdg toplevel client state resets on unmap") {
  lambdaui::compositor::WaylandServer::Impl::XdgToplevel parent{};
  lambdaui::compositor::WaylandServer::Impl::XdgToplevel toplevel{};
  CHECK_FALSE(lambdaui::compositor::resetXdgToplevelClientStateForUnmap(&toplevel));

  toplevel.parent = &parent;
  toplevel.mapped = true;
  toplevel.title = "Settings";
  toplevel.appId = "lambda-settings";

  CHECK(lambdaui::compositor::resetXdgToplevelClientStateForUnmap(&toplevel));
  CHECK(toplevel.parent == nullptr);
  CHECK_FALSE(toplevel.mapped);
  CHECK(toplevel.title.empty());
  CHECK(toplevel.appId.empty());
  CHECK_FALSE(lambdaui::compositor::resetXdgToplevelClientStateForUnmap(&toplevel));
}

TEST_CASE("unmapped surface cleanup drops stale seat focus and serial state") {
  using lambdaui::compositor::SurfaceSeatStateRefs;
  using lambdaui::compositor::WaylandServer;

  WaylandServer::Impl::Surface surface{};
  WaylandServer::Impl::Surface other{};
  auto* client = reinterpret_cast<wl_client*>(std::uintptr_t{1});
  std::vector<WaylandServer::Impl::Surface*> focusOrder{&other, &surface};
  std::vector<WaylandServer::Impl::Surface*> focusCycleList{&surface, &other};
  std::size_t focusCycleIndex = 1;
  std::uint32_t focusCycleStartedAtMs = 42;
  bool focusCycleOverlayShown = true;
  WaylandServer::Impl::Surface* pointerFocus = &surface;
  WaylandServer::Impl::Surface* pointerButtonGrabSurface = &surface;
  wl_client* pointerButtonGrabClient = client;
  std::uint32_t pointerButtonCount = 2;
  WaylandServer::Impl::Surface* keyboardFocus = &surface;
  WaylandServer::Impl::Surface* commandLauncherModalSurface = &surface;
  std::deque<WaylandServer::Impl::SeatSerialRecord> serials{
      {.serial = 10, .client = client, .surface = &surface},
      {.serial = 11, .client = client, .surface = &other},
  };
  std::vector<std::unique_ptr<WaylandServer::Impl::ActivationToken>> tokens;
  auto token = std::make_unique<WaylandServer::Impl::ActivationToken>();
  token->surface = &surface;
  tokens.push_back(std::move(token));

  auto cleanup = lambdaui::compositor::clearUnmappedSurfaceSeatState(SurfaceSeatStateRefs{
      .focusOrder = &focusOrder,
      .focusCycleList = &focusCycleList,
      .focusCycleIndex = &focusCycleIndex,
      .focusCycleStartedAtMs = &focusCycleStartedAtMs,
      .focusCycleOverlayShown = &focusCycleOverlayShown,
      .pointerFocus = &pointerFocus,
      .pointerButtonGrabSurface = &pointerButtonGrabSurface,
      .pointerButtonGrabClient = &pointerButtonGrabClient,
      .pointerButtonCount = &pointerButtonCount,
      .keyboardFocus = &keyboardFocus,
      .commandLauncherModalSurface = &commandLauncherModalSurface,
      .seatSerials = &serials,
      .activationTokens = &tokens,
  }, &surface);

  CHECK(cleanup.changed);
  CHECK(cleanup.pointerFocusChanged);
  CHECK(cleanup.keyboardFocusChanged);
  CHECK(cleanup.commandLauncherModalCleared);
  CHECK(cleanup.pointerButtonGrabCleared);
  CHECK(cleanup.focusOrderChanged);
  CHECK(cleanup.focusCycleCleared);
  CHECK(cleanup.seatSerialsCleared);
  CHECK(cleanup.activationTokensCleared);
  CHECK(focusOrder == std::vector<WaylandServer::Impl::Surface*>{&other});
  CHECK(focusCycleList.empty());
  CHECK(focusCycleIndex == 0);
  CHECK(focusCycleStartedAtMs == 0);
  CHECK_FALSE(focusCycleOverlayShown);
  CHECK(pointerFocus == nullptr);
  CHECK(pointerButtonGrabSurface == nullptr);
  CHECK(pointerButtonGrabClient == nullptr);
  CHECK(pointerButtonCount == 0);
  CHECK(keyboardFocus == nullptr);
  CHECK(commandLauncherModalSurface == nullptr);
  REQUIRE(serials.size() == 1);
  CHECK(serials.front().surface == &other);
  REQUIRE(tokens.size() == 1);
  CHECK(tokens.front()->surface == nullptr);

  cleanup = lambdaui::compositor::clearUnmappedSurfaceSeatState(SurfaceSeatStateRefs{
      .focusOrder = &focusOrder,
      .focusCycleList = &focusCycleList,
      .focusCycleIndex = &focusCycleIndex,
      .focusCycleStartedAtMs = &focusCycleStartedAtMs,
      .focusCycleOverlayShown = &focusCycleOverlayShown,
      .pointerFocus = &pointerFocus,
      .pointerButtonGrabSurface = &pointerButtonGrabSurface,
      .pointerButtonGrabClient = &pointerButtonGrabClient,
      .pointerButtonCount = &pointerButtonCount,
      .keyboardFocus = &keyboardFocus,
      .commandLauncherModalSurface = &commandLauncherModalSurface,
      .seatSerials = &serials,
      .activationTokens = &tokens,
  }, &surface);
  CHECK_FALSE(cleanup.changed);
}

TEST_CASE("xdg popup topmost validation detects live child popups") {
  lambdaui::compositor::WaylandServer::Impl::Surface parentSurface{};
  lambdaui::compositor::WaylandServer::Impl::Surface unrelatedSurface{};
  lambdaui::compositor::WaylandServer::Impl::XdgSurface parentXdg{};
  parentXdg.surface = &parentSurface;

  lambdaui::compositor::WaylandServer::Impl::XdgPopup parent{};
  parent.xdgSurface = &parentXdg;

  lambdaui::compositor::WaylandServer::Impl::XdgPopup unrelated{};
  unrelated.parentSurface = &unrelatedSurface;

  lambdaui::compositor::WaylandServer::Impl::XdgPopup dismissedChild{};
  dismissedChild.parentSurface = &parentSurface;
  dismissedChild.dismissed = true;

  std::array<lambdaui::compositor::WaylandServer::Impl::XdgPopup const*, 3> noLiveChild{
      &parent,
      &unrelated,
      &dismissedChild,
  };
  CHECK_FALSE(lambdaui::compositor::xdgPopupHasLiveChild(noLiveChild, &parent));

  lambdaui::compositor::WaylandServer::Impl::XdgPopup liveChild{};
  liveChild.parentSurface = &parentSurface;
  std::array<lambdaui::compositor::WaylandServer::Impl::XdgPopup const*, 2> withLiveChild{
      &parent,
      &liveChild,
  };
  CHECK(lambdaui::compositor::xdgPopupHasLiveChild(withLiveChild, &parent));
}

TEST_CASE("xdg popup parent references include dismissed popups for cleanup") {
  lambdaui::compositor::WaylandServer::Impl::Surface parentSurface{};
  lambdaui::compositor::WaylandServer::Impl::Surface otherSurface{};
  lambdaui::compositor::WaylandServer::Impl::XdgPopup child{};
  child.parentSurface = &parentSurface;

  CHECK(lambdaui::compositor::xdgPopupReferencesParentSurface(&child, &parentSurface));
  CHECK_FALSE(lambdaui::compositor::xdgPopupReferencesParentSurface(&child, &otherSurface));
  CHECK_FALSE(lambdaui::compositor::xdgPopupReferencesParentSurface(&child, nullptr));
  CHECK_FALSE(lambdaui::compositor::xdgPopupReferencesParentSurface(nullptr, &parentSurface));

  child.dismissed = true;
  CHECK(lambdaui::compositor::xdgPopupReferencesParentSurface(&child, &parentSurface));
}

TEST_CASE("xdg popup grab stack preserves parent grabs while child is active") {
  lambdaui::compositor::WaylandServer::Impl::XdgPopupGrab grab{};
  lambdaui::compositor::WaylandServer::Impl::XdgPopup parent{};
  lambdaui::compositor::WaylandServer::Impl::XdgPopup child{};
  lambdaui::compositor::WaylandServer::Impl::XdgPopup* cachedTop = &parent;
  auto* client = reinterpret_cast<wl_client*>(std::uintptr_t{1});
  auto* seat = reinterpret_cast<wl_resource*>(std::uintptr_t{2});

  CHECK(lambdaui::compositor::xdgPopupGrabSyncTop(grab, cachedTop) == nullptr);
  CHECK(cachedTop == nullptr);

  CHECK(lambdaui::compositor::xdgPopupGrabPush(grab, &parent, client, seat));
  CHECK(lambdaui::compositor::xdgPopupGrabTop(grab) == &parent);
  CHECK(lambdaui::compositor::xdgPopupGrabSyncTop(grab, cachedTop) == &parent);
  CHECK(cachedTop == &parent);
  CHECK(lambdaui::compositor::xdgPopupGrabContains(grab, &parent));
  CHECK(parent.grabbed);
  CHECK(parent.grabSeatResource == seat);

  CHECK(lambdaui::compositor::xdgPopupGrabPush(grab, &child, client, seat));
  CHECK(lambdaui::compositor::xdgPopupGrabTop(grab) == &child);
  CHECK(lambdaui::compositor::xdgPopupGrabSyncTop(grab, cachedTop) == &child);
  CHECK(cachedTop == &child);
  CHECK(parent.grabbed);
  CHECK(child.grabbed);

  CHECK(lambdaui::compositor::xdgPopupGrabRemove(grab, &child));
  CHECK_FALSE(child.grabbed);
  CHECK(child.grabSeatResource == nullptr);
  CHECK(lambdaui::compositor::xdgPopupGrabTop(grab) == &parent);
  CHECK(lambdaui::compositor::xdgPopupGrabSyncTop(grab, cachedTop) == &parent);
  CHECK(cachedTop == &parent);
  CHECK(parent.grabbed);
  CHECK(grab.client == client);
  CHECK(grab.seatResource == seat);

  CHECK(lambdaui::compositor::xdgPopupGrabRemove(grab, &parent));
  CHECK_FALSE(parent.grabbed);
  CHECK(grab.popups.empty());
  CHECK(lambdaui::compositor::xdgPopupGrabSyncTop(grab, cachedTop) == nullptr);
  CHECK(cachedTop == nullptr);
  CHECK(grab.client == nullptr);
  CHECK(grab.seatResource == nullptr);
}

TEST_CASE("xdg popup grab clears when its seat resource is destroyed") {
  lambdaui::compositor::WaylandServer::Impl::XdgPopupGrab grab{};
  lambdaui::compositor::WaylandServer::Impl::XdgPopup parent{};
  lambdaui::compositor::WaylandServer::Impl::XdgPopup child{};
  lambdaui::compositor::WaylandServer::Impl::XdgPopup* cachedTop = nullptr;
  auto* client = reinterpret_cast<wl_client*>(std::uintptr_t{1});
  auto* seat = reinterpret_cast<wl_resource*>(std::uintptr_t{2});
  auto* otherSeat = reinterpret_cast<wl_resource*>(std::uintptr_t{3});

  REQUIRE(lambdaui::compositor::xdgPopupGrabPush(grab, &parent, client, seat));
  REQUIRE(lambdaui::compositor::xdgPopupGrabPush(grab, &child, client, seat));
  REQUIRE(lambdaui::compositor::xdgPopupGrabSyncTop(grab, cachedTop) == &child);

  CHECK(lambdaui::compositor::xdgPopupGrabReferencesSeatResource(grab, seat));
  CHECK_FALSE(lambdaui::compositor::xdgPopupGrabReferencesSeatResource(grab, otherSeat));
  CHECK_FALSE(lambdaui::compositor::xdgPopupGrabClearForSeatResource(grab, cachedTop, otherSeat));
  CHECK(parent.grabbed);
  CHECK(child.grabbed);
  CHECK(cachedTop == &child);
  CHECK(grab.seatResource == seat);

  CHECK(lambdaui::compositor::xdgPopupGrabClearForSeatResource(grab, cachedTop, seat));
  CHECK_FALSE(parent.grabbed);
  CHECK_FALSE(child.grabbed);
  CHECK(parent.grabSeatResource == nullptr);
  CHECK(child.grabSeatResource == nullptr);
  CHECK(grab.popups.empty());
  CHECK(grab.client == nullptr);
  CHECK(grab.seatResource == nullptr);
  CHECK(cachedTop == nullptr);
}

TEST_CASE("keyboard focus helper pins focus to the active popup grab") {
  using lambdaui::compositor::KeyboardFocusRequestRefs;
  using lambdaui::compositor::SurfaceRole;
  using lambdaui::compositor::WaylandServer;

  WaylandServer::Impl::Surface requested{};
  requested.role = SurfaceRole::XdgToplevel;
  WaylandServer::Impl::Surface popupSurface{};
  popupSurface.role = SurfaceRole::XdgPopup;
  WaylandServer::Impl::XdgSurface popupXdg{};
  popupXdg.surface = &popupSurface;
  WaylandServer::Impl::XdgPopup popup{};
  popup.xdgSurface = &popupXdg;
  WaylandServer::Impl::XdgPopupGrab grab{};
  WaylandServer::Impl::XdgPopup* cachedTop = nullptr;

  REQUIRE(lambdaui::compositor::xdgPopupGrabPush(grab, &popup, nullptr, nullptr));

  CHECK(lambdaui::compositor::keyboardFocusTargetForRequest(KeyboardFocusRequestRefs{
            .popupGrabsEnabled = true,
            .popupGrab = &grab,
            .cachedGrabPopup = &cachedTop,
        }, &requested) == &popupSurface);
  CHECK(cachedTop == &popup);

  CHECK(lambdaui::compositor::keyboardFocusTargetForRequest(KeyboardFocusRequestRefs{
            .popupGrabsEnabled = false,
            .popupGrab = &grab,
            .cachedGrabPopup = &cachedTop,
        }, &requested) == &requested);

  popup.dismissed = true;
  CHECK(lambdaui::compositor::keyboardFocusTargetForRequest(KeyboardFocusRequestRefs{
            .popupGrabsEnabled = true,
            .popupGrab = &grab,
            .cachedGrabPopup = &cachedTop,
        }, &requested) == &requested);
}

TEST_CASE("keyboard focus helper clears stale cached popup grab top") {
  using lambdaui::compositor::KeyboardFocusRequestRefs;
  using lambdaui::compositor::WaylandServer;

  WaylandServer::Impl::Surface requested{};
  WaylandServer::Impl::XdgPopup oldPopup{};
  WaylandServer::Impl::XdgPopupGrab emptyGrab{};
  WaylandServer::Impl::XdgPopup* cachedTop = &oldPopup;

  CHECK(lambdaui::compositor::keyboardFocusTargetForRequest(KeyboardFocusRequestRefs{
            .popupGrabsEnabled = true,
            .popupGrab = &emptyGrab,
            .cachedGrabPopup = &cachedTop,
        }, &requested) == &requested);
  CHECK(cachedTop == nullptr);
}

TEST_CASE("keyboard focus restoration helpers follow popup focus state") {
  using lambdaui::compositor::SurfaceRole;
  using lambdaui::compositor::WaylandServer;

  WaylandServer::Impl::Surface popupSurface{};
  popupSurface.role = SurfaceRole::XdgPopup;
  WaylandServer::Impl::Surface toplevelSurface{};
  toplevelSurface.role = SurfaceRole::XdgToplevel;
  WaylandServer::Impl::XdgSurface popupXdg{};
  popupXdg.surface = &popupSurface;
  WaylandServer::Impl::XdgPopup popup{};
  popup.xdgSurface = &popupXdg;

  CHECK(lambdaui::compositor::keyboardFocusShouldRestoreToplevelAfterPopupDismiss(&popupSurface, &popup));
  CHECK_FALSE(lambdaui::compositor::keyboardFocusShouldRestoreToplevelAfterPopupDismiss(&toplevelSurface, &popup));
  CHECK_FALSE(lambdaui::compositor::keyboardFocusShouldRestoreToplevelAfterPopupDismiss(nullptr, &popup));

  CHECK(lambdaui::compositor::keyboardFocusShouldRestoreToplevelAfterGrabDismiss(&popupSurface));
  CHECK_FALSE(lambdaui::compositor::keyboardFocusShouldRestoreToplevelAfterGrabDismiss(&toplevelSurface));
  CHECK_FALSE(lambdaui::compositor::keyboardFocusShouldRestoreToplevelAfterGrabDismiss(nullptr));
}

TEST_CASE("keyboard popup dismissal clears active popup grab stacks") {
  using lambdaui::compositor::KeyboardPopupDismissRefs;
  using lambdaui::compositor::WaylandServer;

  WaylandServer::Impl::XdgPopupGrab grab{};
  WaylandServer::Impl::XdgPopup parent{};
  WaylandServer::Impl::XdgPopup child{};
  WaylandServer::Impl::XdgPopup* cachedTop = nullptr;

  REQUIRE(lambdaui::compositor::xdgPopupGrabPush(grab, &parent, nullptr, nullptr));
  REQUIRE(lambdaui::compositor::xdgPopupGrabPush(grab, &child, nullptr, nullptr));

  CHECK(lambdaui::compositor::keyboardDismissShouldClearPopupGrab(KeyboardPopupDismissRefs{
      .popupGrabsEnabled = true,
      .popupGrab = &grab,
      .cachedGrabPopup = &cachedTop,
  }));
  CHECK(cachedTop == &child);

  cachedTop = &parent;
  CHECK_FALSE(lambdaui::compositor::keyboardDismissShouldClearPopupGrab(KeyboardPopupDismissRefs{
      .popupGrabsEnabled = false,
      .popupGrab = &grab,
      .cachedGrabPopup = &cachedTop,
  }));
  CHECK(cachedTop == &parent);

  lambdaui::compositor::xdgPopupGrabClear(grab);
  CHECK_FALSE(lambdaui::compositor::keyboardDismissShouldClearPopupGrab(KeyboardPopupDismissRefs{
      .popupGrabsEnabled = true,
      .popupGrab = &grab,
      .cachedGrabPopup = &cachedTop,
  }));
  CHECK(cachedTop == nullptr);
}

TEST_CASE("xdg popup grab requests are rejected after commit or existing grab") {
  lambdaui::compositor::WaylandServer::Impl::XdgPopup popup{};

  CHECK(lambdaui::compositor::xdgPopupGrabRequestAllowed(&popup));

  popup.committed = true;
  CHECK_FALSE(lambdaui::compositor::xdgPopupGrabRequestAllowed(&popup));

  popup.committed = false;
  popup.grabbed = true;
  CHECK_FALSE(lambdaui::compositor::xdgPopupGrabRequestAllowed(&popup));

  popup.grabbed = false;
  popup.dismissed = true;
  CHECK_FALSE(lambdaui::compositor::xdgPopupGrabRequestAllowed(&popup));
}

TEST_CASE("shell app id matching accepts built-in app aliases") {
  CHECK(lambdaui::compositor::wm::shellAppIdMatches("terminal", "lambda-terminal"));
  CHECK(lambdaui::compositor::wm::shellAppIdMatches("terminal", "foot"));
  CHECK(lambdaui::compositor::wm::shellAppIdMatches("browser", "firefox"));
  CHECK(lambdaui::compositor::wm::shellAppIdMatches("files", "lambda-files"));
  CHECK(lambdaui::compositor::wm::shellAppIdMatches("lambda-files", "files"));
  CHECK(lambdaui::compositor::wm::shellAppIdMatches("files", "org.gnome.Nautilus"));
  CHECK(lambdaui::compositor::wm::shellAppIdMatches("settings", "lambda-settings"));
  CHECK(lambdaui::compositor::wm::shellAppIdMatches("lambda-settings", "settings"));
  CHECK(lambdaui::compositor::wm::shellAppIdMatches("lambda-settings", "lambda-settings"));
}

TEST_CASE("surface input region defaults to full surface and can exclude points") {
  lambdaui::compositor::WaylandServer::Impl::Surface surface{};

  CHECK(lambdaui::compositor::wm::inputRegionContains(&surface, 500.f, 500.f));

  surface.regionState.inputRegionInfinite = false;
  surface.regionState.inputRegionRects.push_back({.x = 10, .y = 20, .width = 30, .height = 40});

  CHECK(lambdaui::compositor::wm::inputRegionContains(&surface, 10.f, 20.f));
  CHECK(lambdaui::compositor::wm::inputRegionContains(&surface, 39.9f, 59.9f));
  CHECK_FALSE(lambdaui::compositor::wm::inputRegionContains(&surface, 9.9f, 20.f));
  CHECK_FALSE(lambdaui::compositor::wm::inputRegionContains(&surface, 40.f, 20.f));
  CHECK_FALSE(lambdaui::compositor::wm::inputRegionContains(&surface, 10.f, 60.f));
}

TEST_CASE("surface pending region state does not affect committed input region") {
  lambdaui::compositor::WaylandServer::Impl::Surface surface{};

  surface.pendingRegionState.inputRegionInfinite = false;
  surface.pendingRegionState.inputRegionRects.push_back({.x = 10, .y = 20, .width = 30, .height = 40});
  surface.pendingRegionState.inputRegionSet = true;

  CHECK(lambdaui::compositor::wm::inputRegionContains(&surface, 500.f, 500.f));

  surface.regionState.inputRegionInfinite = surface.pendingRegionState.inputRegionInfinite;
  surface.regionState.inputRegionRects = surface.pendingRegionState.inputRegionRects;

  CHECK(lambdaui::compositor::wm::inputRegionContains(&surface, 10.f, 20.f));
  CHECK_FALSE(lambdaui::compositor::wm::inputRegionContains(&surface, 500.f, 500.f));
}

TEST_CASE("surface pending damage state is separate from committed damage") {
  lambdaui::compositor::WaylandServer::Impl::Surface surface{};

  surface.pendingDamageState.bufferRects.push_back({.x = 0, .y = 0, .width = 20, .height = 10});

  CHECK(surface.damageState.bufferRects.empty());

  surface.damageState.bufferRects = surface.pendingDamageState.bufferRects;
  surface.pendingDamageState.bufferRects.clear();

  REQUIRE(surface.damageState.bufferRects.size() == 1);
  CHECK(surface.damageState.bufferRects[0].width == 20);
  CHECK(surface.pendingDamageState.bufferRects.empty());
}

TEST_CASE("surface viewport pending state does not affect committed display size") {
  lambdaui::compositor::WaylandServer::Impl::Surface surface{};
  surface.width = 400;
  surface.height = 200;
  surface.bufferState.scale = 2;

  CHECK(lambdaui::compositor::surfaceCommittedDisplayWidth(&surface) == 200);
  CHECK(lambdaui::compositor::surfaceCommittedDisplayHeight(&surface) == 100);

  surface.pendingViewportState.destinationSet = true;
  surface.pendingViewportState.destinationWidth = 320;
  surface.pendingViewportState.destinationHeight = 180;

  CHECK(lambdaui::compositor::surfaceCommittedDisplayWidth(&surface) == 200);
  CHECK(lambdaui::compositor::surfaceCommittedDisplayHeight(&surface) == 100);

  surface.viewportState = surface.pendingViewportState;

  CHECK(lambdaui::compositor::surfaceCommittedDisplayWidth(&surface) == 320);
  CHECK(lambdaui::compositor::surfaceCommittedDisplayHeight(&surface) == 180);
}

TEST_CASE("surface pending buffer state does not affect committed display size or offset") {
  lambdaui::compositor::WaylandServer::Impl::Surface surface{};
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

  CHECK(lambdaui::compositor::surfaceCommittedDisplayWidth(&surface) == 200);
  CHECK(lambdaui::compositor::surfaceCommittedDisplayHeight(&surface) == 100);
  CHECK(surface.bufferState.offsetX == 0);
  CHECK(surface.bufferState.offsetY == 0);

  surface.bufferState.scale = surface.pendingBufferState.scale;
  surface.bufferState.transform = surface.pendingBufferState.transform;
  surface.bufferState.offsetX = surface.pendingBufferState.offsetX;
  surface.bufferState.offsetY = surface.pendingBufferState.offsetY;

  CHECK(lambdaui::compositor::surfaceCommittedDisplayWidth(&surface) == 50);
  CHECK(lambdaui::compositor::surfaceCommittedDisplayHeight(&surface) == 100);
  CHECK(surface.bufferState.offsetX == 12);
  CHECK(surface.bufferState.offsetY == 24);
}

TEST_CASE("xdg window geometry validates positive size") {
  CHECK(lambdaui::compositor::wm::xdgWindowGeometrySizeValid(1, 1));
  CHECK_FALSE(lambdaui::compositor::wm::xdgWindowGeometrySizeValid(0, 1));
  CHECK_FALSE(lambdaui::compositor::wm::xdgWindowGeometrySizeValid(1, 0));
  CHECK_FALSE(lambdaui::compositor::wm::xdgWindowGeometrySizeValid(-1, 1));
}

TEST_CASE("xdg window geometry offsets surface-local input") {
  lambdaui::compositor::WaylandServer::Impl::Surface surface{};
  surface.role = lambdaui::compositor::SurfaceRole::XdgToplevel;
  surface.windowX = 100;
  surface.windowY = 80;
  surface.xdgRoleState.windowGeometrySet = true;
  surface.xdgRoleState.windowGeometry = lambdaui::compositor::WindowGeometry{
      .x = 32,
      .y = 24,
      .width = 960,
      .height = 640,
  };

  CHECK(lambdaui::compositor::wm::surfaceBufferOriginX(&surface) == doctest::Approx(68.f));
  CHECK(lambdaui::compositor::wm::surfaceBufferOriginY(&surface) == doctest::Approx(56.f));
  CHECK(lambdaui::compositor::wm::surfaceLocalX(&surface, 110.f) == doctest::Approx(42.f));
  CHECK(lambdaui::compositor::wm::surfaceLocalY(&surface, 90.f) == doctest::Approx(34.f));
}

TEST_CASE("xdg toplevel size hints reject negative and inverted dimensions") {
  using lambdaui::compositor::wm::ToplevelSizeHints;
  CHECK(lambdaui::compositor::wm::toplevelSizeHintsValid(ToplevelSizeHints{}));
  CHECK(lambdaui::compositor::wm::toplevelSizeHintsValid({
      .minWidth = 320,
      .minHeight = 200,
      .maxWidth = 640,
      .maxHeight = 480,
  }));
  CHECK_FALSE(lambdaui::compositor::wm::toplevelSizeHintsValid({
      .minWidth = -1,
  }));
  CHECK_FALSE(lambdaui::compositor::wm::toplevelSizeHintsValid({
      .minWidth = 641,
      .maxWidth = 640,
  }));
  CHECK_FALSE(lambdaui::compositor::wm::toplevelSizeHintsValid({
      .minHeight = 481,
      .maxHeight = 480,
  }));
}

TEST_CASE("xdg toplevel pending size hints are validated as commit state") {
  lambdaui::compositor::WaylandServer::Impl::XdgToplevel toplevel{};
  toplevel.minWidth = 320;
  toplevel.minHeight = 200;
  toplevel.maxWidth = 640;
  toplevel.maxHeight = 480;

  CHECK(lambdaui::compositor::wm::toplevelPendingSizeHintsValid(&toplevel));

  toplevel.pendingMaxWidth = 300;
  toplevel.pendingMaxHeight = 480;
  toplevel.pendingMaxSizeSet = true;
  CHECK_FALSE(lambdaui::compositor::wm::toplevelPendingSizeHintsValid(&toplevel));

  toplevel.pendingMaxWidth = 640;
  CHECK(lambdaui::compositor::wm::toplevelPendingSizeHintsValid(&toplevel));

  toplevel.pendingMinWidth = -1;
  toplevel.pendingMinHeight = 200;
  toplevel.pendingMinSizeSet = true;
  CHECK_FALSE(lambdaui::compositor::wm::toplevelPendingSizeHintsValid(&toplevel));
}

TEST_CASE("xdg resize configure gate allows acked interactive resize updates") {
  auto idle = lambdaui::compositor::xdgResizeConfigureGate(false, false, false);
  CHECK(idle.sendConfigure);
  CHECK_FALSE(idle.rememberPending);

  auto duplicate = lambdaui::compositor::xdgResizeConfigureGate(true, false, true);
  CHECK_FALSE(duplicate.sendConfigure);
  CHECK_FALSE(duplicate.rememberPending);

  auto beforeAck = lambdaui::compositor::xdgResizeConfigureGate(true, false, false);
  CHECK_FALSE(beforeAck.sendConfigure);
  CHECK(beforeAck.rememberPending);

  auto afterAck = lambdaui::compositor::xdgResizeConfigureGate(true, true, false);
  CHECK(afterAck.sendConfigure);
  CHECK_FALSE(afterAck.rememberPending);
}

TEST_CASE("xdg toplevel size hints clamp interactive resize geometry around anchored edges") {
  using lambdaui::compositor::ResizeEdge;
  using lambdaui::compositor::WindowGeometry;
  lambdaui::compositor::wm::ToplevelSizeHints const hints{
      .minWidth = 240,
      .minHeight = 180,
      .maxWidth = 640,
      .maxHeight = 480,
  };

  auto rightBottom = lambdaui::compositor::wm::clampToplevelGeometryToSizeHints(
      WindowGeometry{.x = 100, .y = 100, .width = 800, .height = 700},
      hints,
      ResizeEdge::Right | ResizeEdge::Bottom);
  CHECK(rightBottom.x == 100);
  CHECK(rightBottom.y == 100);
  CHECK(rightBottom.width == 640);
  CHECK(rightBottom.height == 480);

  auto leftTop = lambdaui::compositor::wm::clampToplevelGeometryToSizeHints(
      WindowGeometry{.x = 100, .y = 100, .width = 120, .height = 90},
      hints,
      ResizeEdge::Left | ResizeEdge::Top);
  CHECK(leftTop.x == -20);
  CHECK(leftTop.y == 10);
  CHECK(leftTop.width == 240);
  CHECK(leftTop.height == 180);
}
