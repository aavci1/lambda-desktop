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

TEST_CASE("focus cycle list remains stable while focus order mutates") {
  auto first = testSurface(1);
  auto second = testSurface(2);
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
  auto cycle = lambda::compositor::wm::focusCycleListFromOrders(focusOrder, surfaces, surfaces[2].get());
  REQUIRE(cycle.size() == 3);
  CHECK(cycle[0] == surfaces[2].get());
  CHECK(cycle[1] == surfaces[1].get());
  CHECK(cycle[2] == surfaces[0].get());

  std::size_t index = 0;
  index = lambda::compositor::wm::advancedFocusCycleIndex(index, cycle.size(), true);
  CHECK(cycle[index] == surfaces[1].get());

  focusOrder = {
      surfaces[0].get(),
      surfaces[2].get(),
      surfaces[1].get(),
  };
  index = lambda::compositor::wm::advancedFocusCycleIndex(index, cycle.size(), true);
  CHECK(cycle[index] == surfaces[0].get());
}

TEST_CASE("focus cycle list supports reverse cycling and excludes minimized windows") {
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
  auto cycle = lambda::compositor::wm::focusCycleListFromOrders(focusOrder, surfaces, surfaces[2].get());
  REQUIRE(cycle.size() == 2);
  CHECK(cycle[0] == surfaces[2].get());
  CHECK(cycle[1] == surfaces[0].get());
  CHECK(lambda::compositor::wm::advancedFocusCycleIndex(0, cycle.size(), false) == 1);
}

TEST_CASE("cycle focus shortcut treats shift as reverse direction for existing bindings") {
  lambda::compositor::ShortcutBinding cycle{
      .action = lambda::compositor::ShortcutAction::CycleFocus,
      .key = KEY_TAB,
      .meta = true,
  };
  CHECK(lambda::compositor::wm::shortcutBindingMatches(cycle, true, false, false, false));
  CHECK(lambda::compositor::wm::shortcutBindingMatches(cycle, true, false, false, true));

  lambda::compositor::ShortcutBinding close{
      .action = lambda::compositor::ShortcutAction::CloseFocused,
      .key = KEY_Q,
      .meta = true,
  };
  CHECK(lambda::compositor::wm::shortcutBindingMatches(close, true, false, false, false));
  CHECK_FALSE(lambda::compositor::wm::shortcutBindingMatches(close, true, false, false, true));
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

TEST_CASE("xdg activation tokens are matched only after commit") {
  auto token = std::make_unique<lambda::compositor::WaylandServer::Impl::ActivationToken>();
  token->token = "lambda-test";

  CHECK(lambda::compositor::activationTokenMutable(token.get()));
  CHECK_FALSE(lambda::compositor::activationTokenMatches(token.get(), "lambda-test"));

  std::vector<std::unique_ptr<lambda::compositor::WaylandServer::Impl::ActivationToken>> tokens;
  tokens.push_back(std::move(token));
  CHECK(lambda::compositor::activationTokenForName(tokens, "lambda-test") == nullptr);

  tokens.front()->committed = true;
  CHECK_FALSE(lambda::compositor::activationTokenMutable(tokens.front().get()));
  CHECK(lambda::compositor::activationTokenMatches(tokens.front().get(), "lambda-test"));
  CHECK_FALSE(lambda::compositor::activationTokenMatches(tokens.front().get(), "missing"));
  CHECK(lambda::compositor::activationTokenForName(tokens, "lambda-test") == tokens.front().get());
  CHECK(lambda::compositor::activationTokenForName(tokens, "missing") == nullptr);
}

TEST_CASE("xdg activation tokens stop matching after the wlroots timeout") {
  auto token = std::make_unique<lambda::compositor::WaylandServer::Impl::ActivationToken>();
  token->token = "lambda-test";
  token->committed = true;
  token->expiresAtMs = 1'000;

  std::vector<std::unique_ptr<lambda::compositor::WaylandServer::Impl::ActivationToken>> tokens;
  tokens.push_back(std::move(token));

  CHECK(lambda::compositor::kActivationTokenTimeoutMs == 30'000);
  CHECK_FALSE(lambda::compositor::activationTokenExpired(tokens.front().get(), 999));
  CHECK(lambda::compositor::activationTokenForName(tokens, "lambda-test", 999) == tokens.front().get());

  CHECK(lambda::compositor::activationTokenExpired(tokens.front().get(), 1'000));
  CHECK(lambda::compositor::activationTokenForName(tokens, "lambda-test", 1'000) == nullptr);
}

TEST_CASE("xdg activation token focused surface validation follows keyboard or pointer focus") {
  lambda::compositor::WaylandServer::Impl::Surface focused{};
  lambda::compositor::WaylandServer::Impl::Surface other{};
  lambda::compositor::WaylandServer::Impl::ActivationToken token{};

  CHECK(lambda::compositor::activationTokenFocusedSurfaceValid(nullptr, nullptr, &token));

  token.surface = &focused;
  CHECK_FALSE(lambda::compositor::activationTokenFocusedSurfaceValid(nullptr, nullptr, &token));

  CHECK(lambda::compositor::activationTokenFocusedSurfaceValid(&focused, nullptr, &token));

  CHECK(lambda::compositor::activationTokenFocusedSurfaceValid(nullptr, &focused, &token));

  token.surface = &other;
  CHECK_FALSE(lambda::compositor::activationTokenFocusedSurfaceValid(nullptr, &focused, &token));
}

TEST_CASE("pointer constraint region is pending until surface commit") {
  lambda::compositor::WaylandServer::Impl::Surface surface{};
  surface.width = 100;
  surface.height = 80;
  surface.windowX = 10;
  surface.windowY = 20;

  lambda::compositor::WaylandServer::Impl::PointerConstraint constraint{};
  constraint.surface = &surface;
  REQUIRE(lambda::compositor::rebuildPointerConstraintEffectiveRegion(&constraint));
  CHECK(lambda::compositor::pointerConstraintRegionContainsLocalPoint(&constraint, 90.f, 70.f));

  constraint.pendingRegionInfinite = false;
  constraint.pendingRegionRects = {{.x = 20, .y = 10, .width = 30, .height = 20}};
  constraint.pendingRegionSet = true;

  CHECK(lambda::compositor::pointerConstraintRegionContainsLocalPoint(&constraint, 90.f, 70.f));
  CHECK(lambda::compositor::applyPointerConstraintPendingState(&constraint));
  CHECK_FALSE(lambda::compositor::pointerConstraintRegionContainsLocalPoint(&constraint, 90.f, 70.f));
  CHECK(lambda::compositor::pointerConstraintRegionContainsLocalPoint(&constraint, 25.f, 15.f));

  float globalX = 90.f;
  float globalY = 70.f;
  CHECK(lambda::compositor::clampPointerConstraintGlobalPoint(&constraint, globalX, globalY));
  CHECK(globalX == 59.f);
  CHECK(globalY == 49.f);
}

TEST_CASE("pointer constraint effective region follows surface input region") {
  lambda::compositor::WaylandServer::Impl::Surface surface{};
  surface.width = 100;
  surface.height = 80;
  surface.regionState.inputRegionInfinite = false;
  surface.regionState.inputRegionRects = {
      {.x = 0, .y = 0, .width = 40, .height = 40},
      {.x = 50, .y = 0, .width = 20, .height = 20},
  };

  lambda::compositor::WaylandServer::Impl::PointerConstraint constraint{};
  constraint.surface = &surface;
  constraint.regionInfinite = false;
  constraint.regionRects = {{.x = 30, .y = 10, .width = 40, .height = 20}};

  CHECK(lambda::compositor::rebuildPointerConstraintEffectiveRegion(&constraint));
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
  lambda::compositor::WaylandServer::Impl::Surface surface{};
  surface.role = lambda::compositor::SurfaceRole::XdgToplevel;
  surface.width = 1280;
  surface.height = 960;
  surface.bufferState.scale = 2;
  surface.frameWidth = 900;
  surface.frameHeight = 700;
  surface.pendingResizeConfigure = true;

  lambda::compositor::WaylandServer::Impl::PointerConstraint constraint{};
  constraint.surface = &surface;

  REQUIRE(lambda::compositor::rebuildPointerConstraintEffectiveRegion(&constraint));
  REQUIRE(constraint.effectiveRegionRects.size() == 1);
  CHECK(constraint.effectiveRegionRects[0].width == 640);
  CHECK(constraint.effectiveRegionRects[0].height == 480);
  CHECK(lambda::compositor::pointerConstraintRegionContainsLocalPoint(&constraint, 639.f, 479.f));
  CHECK_FALSE(lambda::compositor::pointerConstraintRegionContainsLocalPoint(&constraint, 640.f, 480.f));
}

TEST_CASE("locked pointer cursor hint is committed with pointer constraint state") {
  lambda::compositor::WaylandServer::Impl::PointerConstraint constraint{};
  constraint.pendingCursorHintX = 12.5f;
  constraint.pendingCursorHintY = 7.25f;
  constraint.pendingCursorHintSet = true;

  CHECK_FALSE(constraint.cursorHintSet);
  CHECK(lambda::compositor::applyPointerConstraintPendingState(&constraint));
  CHECK(constraint.cursorHintSet);
  CHECK(constraint.cursorHintX == 12.5f);
  CHECK(constraint.cursorHintY == 7.25f);
}

TEST_CASE("pointer constraint cached commit state does not consume later pending requests") {
  lambda::compositor::WaylandServer::Impl::Surface surface{};
  surface.width = 100;
  surface.height = 80;

  lambda::compositor::WaylandServer::Impl::PointerConstraint constraint{};
  constraint.surface = &surface;
  constraint.pendingRegionInfinite = false;
  constraint.pendingRegionRects = {{.x = 10, .y = 10, .width = 20, .height = 20}};
  constraint.pendingRegionSet = true;

  auto cached = lambda::compositor::takePointerConstraintPendingState(&constraint);
  CHECK(cached.regionSet);
  CHECK_FALSE(constraint.pendingRegionSet);

  constraint.pendingRegionInfinite = false;
  constraint.pendingRegionRects = {{.x = 40, .y = 40, .width = 20, .height = 20}};
  constraint.pendingRegionSet = true;

  CHECK(lambda::compositor::applyPointerConstraintCommitState(cached));
  CHECK(lambda::compositor::pointerConstraintRegionContainsLocalPoint(&constraint, 15.f, 15.f));
  CHECK_FALSE(lambda::compositor::pointerConstraintRegionContainsLocalPoint(&constraint, 45.f, 45.f));
  CHECK(constraint.pendingRegionSet);

  CHECK(lambda::compositor::applyPointerConstraintPendingState(&constraint));
  CHECK_FALSE(lambda::compositor::pointerConstraintRegionContainsLocalPoint(&constraint, 15.f, 15.f));
  CHECK(lambda::compositor::pointerConstraintRegionContainsLocalPoint(&constraint, 45.f, 45.f));
}

TEST_CASE("oneshot pointer constraints are destroyed after deactivation") {
  lambda::compositor::WaylandServer::Impl::PointerConstraint persistent{};
  persistent.lifetime = ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT;
  persistent.active = false;
  CHECK_FALSE(lambda::compositor::pointerConstraintShouldDestroyAfterDeactivation(&persistent));

  lambda::compositor::WaylandServer::Impl::PointerConstraint oneshot{};
  oneshot.lifetime = ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_ONESHOT;
  oneshot.active = true;
  CHECK_FALSE(lambda::compositor::pointerConstraintShouldDestroyAfterDeactivation(&oneshot));

  oneshot.active = false;
  CHECK(lambda::compositor::pointerConstraintShouldDestroyAfterDeactivation(&oneshot));
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

TEST_CASE("xdg popup layer-surface reparenting preserves relative coordinates") {
  auto const moved = lambda::compositor::xdgPopupReparentGeometry({
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

  auto const parentless = lambda::compositor::xdgPopupReparentGeometry({
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

TEST_CASE("xdg surface ack_configure consumes serial range into pending configure state") {
  using lambda::compositor::SurfaceRole;
  using lambda::compositor::WaylandServer;
  using lambda::compositor::XdgConfigureAckStatus;

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

  auto missing = lambda::compositor::acknowledgeXdgSurfaceConfigure(&xdgSurface, 9, 10);
  CHECK(missing.status == XdgConfigureAckStatus::UnknownSerial);
  CHECK_FALSE(missing.ackedConfigure.has_value());
  CHECK_FALSE(missing.ackedResizeConfigure.has_value());
  CHECK_FALSE(xdgSurface.configured);
  CHECK(xdgSurface.configureList.size() == 3);
  CHECK_FALSE(xdgSurface.pendingConfigure.has_value());
  CHECK_FALSE(xdgSurface.currentConfigure.has_value());

  auto acked = lambda::compositor::acknowledgeXdgSurfaceConfigure(&xdgSurface, 11, 10);
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

  CHECK(lambda::compositor::commitPendingXdgConfigure(&xdgSurface));
  CHECK_FALSE(xdgSurface.pendingConfigure.has_value());
  REQUIRE(xdgSurface.currentConfigure.has_value());
  CHECK(xdgSurface.currentConfigure->serial == 11);
  CHECK_FALSE(lambda::compositor::commitPendingXdgConfigure(&xdgSurface));
}

TEST_CASE("xdg surface ack_configure does not consume newer resize configure") {
  using lambda::compositor::SurfaceRole;
  using lambda::compositor::WaylandServer;
  using lambda::compositor::XdgConfigureAckStatus;

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

  auto acked = lambda::compositor::acknowledgeXdgSurfaceConfigure(&xdgSurface, 20, 21);
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

TEST_CASE("interactive frame size follows committed content while toplevel configure is pending") {
  lambda::compositor::WaylandServer::Impl::Surface surface{};
  surface.role = lambda::compositor::SurfaceRole::XdgToplevel;
  surface.width = 1280;
  surface.height = 960;
  surface.bufferState.scale = 2;
  surface.frameWidth = 900;
  surface.frameHeight = 700;

  auto live = lambda::compositor::wm::interactiveFrameDisplaySize(&surface);
  CHECK(live.width == 900);
  CHECK(live.height == 700);
  CHECK_FALSE(lambda::compositor::wm::toplevelHasPendingUncommittedFrame(&surface));

  surface.awaitingConfigureCommit = true;
  auto awaiting = lambda::compositor::wm::interactiveFrameDisplaySize(&surface);
  CHECK(awaiting.width == 640);
  CHECK(awaiting.height == 480);
  CHECK(lambda::compositor::wm::toplevelHasPendingUncommittedFrame(&surface));

  surface.awaitingConfigureCommit = false;
  surface.resizeConfigureInFlight = true;
  auto resizing = lambda::compositor::wm::interactiveFrameDisplaySize(&surface);
  CHECK(resizing.width == 640);
  CHECK(resizing.height == 480);

  surface.role = lambda::compositor::SurfaceRole::LayerSurface;
  auto layer = lambda::compositor::wm::interactiveFrameDisplaySize(&surface);
  CHECK(layer.width == 900);
  CHECK(layer.height == 700);
  CHECK_FALSE(lambda::compositor::wm::toplevelHasPendingUncommittedFrame(&surface));
}

TEST_CASE("interactive frame size does not invent committed content for empty pending toplevels") {
  lambda::compositor::WaylandServer::Impl::Surface surface{};
  surface.role = lambda::compositor::SurfaceRole::XdgToplevel;
  surface.frameWidth = 900;
  surface.frameHeight = 700;
  surface.awaitingConfigureCommit = true;

  CHECK_FALSE(lambda::compositor::wm::surfaceHasCommittedDisplaySize(&surface));
  auto empty = lambda::compositor::wm::interactiveFrameDisplaySize(&surface);
  CHECK(empty.width == 900);
  CHECK(empty.height == 700);

  surface.viewportState.destinationSet = true;
  surface.viewportState.destinationWidth = 640;
  surface.viewportState.destinationHeight = 480;
  CHECK(lambda::compositor::wm::surfaceHasCommittedDisplaySize(&surface));
  auto viewport = lambda::compositor::wm::interactiveFrameDisplaySize(&surface);
  CHECK(viewport.width == 640);
  CHECK(viewport.height == 480);
}

TEST_CASE("window geometry helper uses interactive frame size during pending toplevel configure") {
  lambda::compositor::WaylandServer::Impl::Surface surface{};
  surface.role = lambda::compositor::SurfaceRole::XdgToplevel;
  surface.windowX = 50;
  surface.windowY = 70;
  surface.width = 1280;
  surface.height = 960;
  surface.bufferState.scale = 2;
  surface.frameWidth = 900;
  surface.frameHeight = 700;

  auto live = lambda::compositor::wm::windowGeometryFor(&surface);
  CHECK(live.x == 50);
  CHECK(live.y == 70);
  CHECK(live.width == 900);
  CHECK(live.height == 700);

  surface.pendingResizeConfigure = true;
  auto pending = lambda::compositor::wm::windowGeometryFor(&surface);
  CHECK(pending.x == 50);
  CHECK(pending.y == 70);
  CHECK(pending.width == 640);
  CHECK(pending.height == 480);
}

TEST_CASE("usable window geometry helper rejects empty surfaces and follows pending configure size") {
  lambda::compositor::WaylandServer::Impl::Surface empty{};
  empty.role = lambda::compositor::SurfaceRole::XdgToplevel;
  CHECK_FALSE(lambda::compositor::wm::usableWindowGeometryFor(&empty).has_value());

  lambda::compositor::WaylandServer::Impl::Surface surface{};
  surface.role = lambda::compositor::SurfaceRole::XdgToplevel;
  surface.windowX = 25;
  surface.windowY = 35;
  surface.width = 1024;
  surface.height = 768;
  surface.bufferState.scale = 2;
  surface.frameWidth = 900;
  surface.frameHeight = 700;
  surface.pendingResizeConfigure = true;

  auto geometry = lambda::compositor::wm::usableWindowGeometryFor(&surface);
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

TEST_CASE("xdg toplevel client state resets on unmap") {
  lambda::compositor::WaylandServer::Impl::XdgToplevel parent{};
  lambda::compositor::WaylandServer::Impl::XdgToplevel toplevel{};
  CHECK_FALSE(lambda::compositor::resetXdgToplevelClientStateForUnmap(&toplevel));

  toplevel.parent = &parent;
  toplevel.mapped = true;
  toplevel.title = "Settings";
  toplevel.appId = "lambda-settings";

  CHECK(lambda::compositor::resetXdgToplevelClientStateForUnmap(&toplevel));
  CHECK(toplevel.parent == nullptr);
  CHECK_FALSE(toplevel.mapped);
  CHECK(toplevel.title.empty());
  CHECK(toplevel.appId.empty());
  CHECK_FALSE(lambda::compositor::resetXdgToplevelClientStateForUnmap(&toplevel));
}

TEST_CASE("unmapped toplevel cleanup drops stale seat focus and serial state") {
  using lambda::compositor::SurfaceSeatStateRefs;
  using lambda::compositor::WaylandServer;

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
  std::deque<WaylandServer::Impl::SeatSerialRecord> serials{
      {.serial = 10, .client = client, .surface = &surface},
      {.serial = 11, .client = client, .surface = &other},
  };
  std::vector<std::unique_ptr<WaylandServer::Impl::ActivationToken>> tokens;
  auto token = std::make_unique<WaylandServer::Impl::ActivationToken>();
  token->surface = &surface;
  tokens.push_back(std::move(token));

  auto cleanup = lambda::compositor::clearUnmappedSurfaceSeatState(SurfaceSeatStateRefs{
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
      .seatSerials = &serials,
      .activationTokens = &tokens,
  }, &surface);

  CHECK(cleanup.changed);
  CHECK(cleanup.pointerFocusChanged);
  CHECK(cleanup.keyboardFocusChanged);
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
  REQUIRE(serials.size() == 1);
  CHECK(serials.front().surface == &other);
  REQUIRE(tokens.size() == 1);
  CHECK(tokens.front()->surface == nullptr);

  cleanup = lambda::compositor::clearUnmappedSurfaceSeatState(SurfaceSeatStateRefs{
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
      .seatSerials = &serials,
      .activationTokens = &tokens,
  }, &surface);
  CHECK_FALSE(cleanup.changed);
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

TEST_CASE("xdg popup grab stack preserves parent grabs while child is active") {
  lambda::compositor::WaylandServer::Impl::XdgPopupGrab grab{};
  lambda::compositor::WaylandServer::Impl::XdgPopup parent{};
  lambda::compositor::WaylandServer::Impl::XdgPopup child{};
  lambda::compositor::WaylandServer::Impl::XdgPopup* cachedTop = &parent;
  auto* client = reinterpret_cast<wl_client*>(std::uintptr_t{1});
  auto* seat = reinterpret_cast<wl_resource*>(std::uintptr_t{2});

  CHECK(lambda::compositor::xdgPopupGrabSyncTop(grab, cachedTop) == nullptr);
  CHECK(cachedTop == nullptr);

  CHECK(lambda::compositor::xdgPopupGrabPush(grab, &parent, client, seat));
  CHECK(lambda::compositor::xdgPopupGrabTop(grab) == &parent);
  CHECK(lambda::compositor::xdgPopupGrabSyncTop(grab, cachedTop) == &parent);
  CHECK(cachedTop == &parent);
  CHECK(lambda::compositor::xdgPopupGrabContains(grab, &parent));
  CHECK(parent.grabbed);
  CHECK(parent.grabSeatResource == seat);

  CHECK(lambda::compositor::xdgPopupGrabPush(grab, &child, client, seat));
  CHECK(lambda::compositor::xdgPopupGrabTop(grab) == &child);
  CHECK(lambda::compositor::xdgPopupGrabSyncTop(grab, cachedTop) == &child);
  CHECK(cachedTop == &child);
  CHECK(parent.grabbed);
  CHECK(child.grabbed);

  CHECK(lambda::compositor::xdgPopupGrabRemove(grab, &child));
  CHECK_FALSE(child.grabbed);
  CHECK(child.grabSeatResource == nullptr);
  CHECK(lambda::compositor::xdgPopupGrabTop(grab) == &parent);
  CHECK(lambda::compositor::xdgPopupGrabSyncTop(grab, cachedTop) == &parent);
  CHECK(cachedTop == &parent);
  CHECK(parent.grabbed);
  CHECK(grab.client == client);
  CHECK(grab.seatResource == seat);

  CHECK(lambda::compositor::xdgPopupGrabRemove(grab, &parent));
  CHECK_FALSE(parent.grabbed);
  CHECK(grab.popups.empty());
  CHECK(lambda::compositor::xdgPopupGrabSyncTop(grab, cachedTop) == nullptr);
  CHECK(cachedTop == nullptr);
  CHECK(grab.client == nullptr);
  CHECK(grab.seatResource == nullptr);
}

TEST_CASE("xdg popup grab requests are rejected after commit or existing grab") {
  lambda::compositor::WaylandServer::Impl::XdgPopup popup{};

  CHECK(lambda::compositor::xdgPopupGrabRequestAllowed(&popup));

  popup.committed = true;
  CHECK_FALSE(lambda::compositor::xdgPopupGrabRequestAllowed(&popup));

  popup.committed = false;
  popup.grabbed = true;
  CHECK_FALSE(lambda::compositor::xdgPopupGrabRequestAllowed(&popup));

  popup.grabbed = false;
  popup.dismissed = true;
  CHECK_FALSE(lambda::compositor::xdgPopupGrabRequestAllowed(&popup));
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

TEST_CASE("xdg resize configure gate allows acked interactive resize updates") {
  auto idle = lambda::compositor::xdgResizeConfigureGate(false, false, false);
  CHECK(idle.sendConfigure);
  CHECK_FALSE(idle.rememberPending);

  auto duplicate = lambda::compositor::xdgResizeConfigureGate(true, false, true);
  CHECK_FALSE(duplicate.sendConfigure);
  CHECK_FALSE(duplicate.rememberPending);

  auto beforeAck = lambda::compositor::xdgResizeConfigureGate(true, false, false);
  CHECK_FALSE(beforeAck.sendConfigure);
  CHECK(beforeAck.rememberPending);

  auto afterAck = lambda::compositor::xdgResizeConfigureGate(true, true, false);
  CHECK(afterAck.sendConfigure);
  CHECK_FALSE(afterAck.rememberPending);
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
