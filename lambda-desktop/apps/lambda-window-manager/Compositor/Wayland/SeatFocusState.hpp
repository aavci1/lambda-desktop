#pragma once

#include "Compositor/Wayland/WaylandServerImpl.hpp"
#include "Compositor/Wayland/XdgPopupState.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

namespace lambdaui::compositor {

struct SurfaceSeatStateRefs {
  std::vector<WaylandServer::Impl::Surface*>* focusOrder = nullptr;
  std::vector<WaylandServer::Impl::Surface*>* focusCycleList = nullptr;
  std::size_t* focusCycleIndex = nullptr;
  std::uint32_t* focusCycleStartedAtMs = nullptr;
  bool* focusCycleOverlayShown = nullptr;
  WaylandServer::Impl::Surface** pointerFocus = nullptr;
  WaylandServer::Impl::Surface** pointerButtonGrabSurface = nullptr;
  wl_client** pointerButtonGrabClient = nullptr;
  std::uint32_t* pointerButtonCount = nullptr;
  WaylandServer::Impl::Surface** keyboardFocus = nullptr;
  WaylandServer::Impl::Surface** commandLauncherModalSurface = nullptr;
  std::deque<WaylandServer::Impl::SeatSerialRecord>* seatSerials = nullptr;
  std::vector<std::unique_ptr<WaylandServer::Impl::ActivationToken>>* activationTokens = nullptr;
};

struct SurfaceSeatCleanupResult {
  bool changed = false;
  bool pointerFocusChanged = false;
  bool keyboardFocusChanged = false;
  bool commandLauncherModalCleared = false;
  bool pointerButtonGrabCleared = false;
  bool focusOrderChanged = false;
  bool focusCycleCleared = false;
  bool seatSerialsCleared = false;
  bool activationTokensCleared = false;
};

struct KeyboardFocusRequestRefs {
  bool popupGrabsEnabled = false;
  WaylandServer::Impl::XdgPopupGrab* popupGrab = nullptr;
  WaylandServer::Impl::XdgPopup** cachedGrabPopup = nullptr;
};

struct KeyboardPopupDismissRefs {
  bool popupGrabsEnabled = false;
  WaylandServer::Impl::XdgPopupGrab* popupGrab = nullptr;
  WaylandServer::Impl::XdgPopup** cachedGrabPopup = nullptr;
};

[[nodiscard]] inline WaylandServer::Impl::Surface* popupGrabKeyboardFocusSurface(
    WaylandServer::Impl::XdgPopup const* popup) {
  if (!popup || popup->dismissed || !popup->xdgSurface) return nullptr;
  return popup->xdgSurface->surface;
}

[[nodiscard]] inline WaylandServer::Impl::Surface* keyboardFocusTargetForRequest(
    KeyboardFocusRequestRefs refs,
    WaylandServer::Impl::Surface* requested) {
  if (!refs.popupGrabsEnabled || !refs.popupGrab || !refs.cachedGrabPopup) return requested;
  WaylandServer::Impl::XdgPopup* popup = xdgPopupGrabSyncTop(*refs.popupGrab, *refs.cachedGrabPopup);
  if (auto* popupSurface = popupGrabKeyboardFocusSurface(popup)) return popupSurface;
  if (refs.popupGrab->popups.empty()) xdgPopupGrabSyncTop(*refs.popupGrab, *refs.cachedGrabPopup);
  return requested;
}

[[nodiscard]] inline bool keyboardDismissShouldClearPopupGrab(KeyboardPopupDismissRefs refs) {
  if (!refs.popupGrabsEnabled || !refs.popupGrab || !refs.cachedGrabPopup) return false;
  return xdgPopupGrabSyncTop(*refs.popupGrab, *refs.cachedGrabPopup) != nullptr;
}

[[nodiscard]] inline bool keyboardFocusShouldRestoreToplevelAfterPopupDismiss(
    WaylandServer::Impl::Surface const* keyboardFocus,
    WaylandServer::Impl::XdgPopup const* popup) {
  return keyboardFocus && keyboardFocus == xdgPopupSurface(popup);
}

[[nodiscard]] inline bool keyboardFocusShouldRestoreToplevelAfterGrabDismiss(
    WaylandServer::Impl::Surface const* keyboardFocus) {
  return surfaceIsXdgPopup(keyboardFocus);
}

inline SurfaceSeatStateRefs surfaceSeatStateRefs(WaylandServer::Impl* server) {
  if (!server) return {};
  return {
      .focusOrder = &server->focusOrder_,
      .focusCycleList = &server->focusCycleList_,
      .focusCycleIndex = &server->focusCycleIndex_,
      .focusCycleStartedAtMs = &server->focusCycleStartedAtMs_,
      .focusCycleOverlayShown = &server->focusCycleOverlayShown_,
      .pointerFocus = &server->pointerFocus_,
      .pointerButtonGrabSurface = &server->pointerButtonGrabSurface_,
      .pointerButtonGrabClient = &server->pointerButtonGrabClient_,
      .pointerButtonCount = &server->pointerButtonCount_,
      .keyboardFocus = &server->keyboardFocus_,
      .commandLauncherModalSurface = &server->commandLauncherModalSurface_,
      .seatSerials = &server->seatSerials_,
      .activationTokens = &server->activationTokens_,
  };
}

inline SurfaceSeatCleanupResult clearUnmappedSurfaceSeatState(SurfaceSeatStateRefs state,
                                                              WaylandServer::Impl::Surface* surface) {
  SurfaceSeatCleanupResult result;
  if (!surface) return result;

  if (state.focusOrder) {
    std::size_t const before = state.focusOrder->size();
    std::erase(*state.focusOrder, surface);
    result.focusOrderChanged = state.focusOrder->size() != before;
  }

  if (state.focusCycleList &&
      std::ranges::find(*state.focusCycleList, surface) != state.focusCycleList->end()) {
    state.focusCycleList->clear();
    if (state.focusCycleIndex) *state.focusCycleIndex = 0;
    if (state.focusCycleStartedAtMs) *state.focusCycleStartedAtMs = 0;
    if (state.focusCycleOverlayShown) *state.focusCycleOverlayShown = false;
    result.focusCycleCleared = true;
  }

  if (state.pointerFocus && *state.pointerFocus == surface) {
    *state.pointerFocus = nullptr;
    result.pointerFocusChanged = true;
  }

  if (state.pointerButtonGrabSurface && *state.pointerButtonGrabSurface == surface) {
    *state.pointerButtonGrabSurface = nullptr;
    if (state.pointerButtonGrabClient) *state.pointerButtonGrabClient = nullptr;
    if (state.pointerButtonCount) *state.pointerButtonCount = 0;
    result.pointerButtonGrabCleared = true;
  }

  if (state.keyboardFocus && *state.keyboardFocus == surface) {
    *state.keyboardFocus = nullptr;
    result.keyboardFocusChanged = true;
  }

  if (state.commandLauncherModalSurface && *state.commandLauncherModalSurface == surface) {
    *state.commandLauncherModalSurface = nullptr;
    result.commandLauncherModalCleared = true;
  }

  if (state.seatSerials) {
    std::size_t const before = state.seatSerials->size();
    std::erase_if(*state.seatSerials, [surface](WaylandServer::Impl::SeatSerialRecord const& record) {
      return record.surface == surface;
    });
    result.seatSerialsCleared = state.seatSerials->size() != before;
  }

  if (state.activationTokens) {
    for (auto& token : *state.activationTokens) {
      if (token && token->surface == surface) {
        token->surface = nullptr;
        result.activationTokensCleared = true;
      }
    }
  }

  result.changed = result.pointerFocusChanged ||
                   result.keyboardFocusChanged ||
                   result.commandLauncherModalCleared ||
                   result.pointerButtonGrabCleared ||
                   result.focusOrderChanged ||
                   result.focusCycleCleared ||
                   result.seatSerialsCleared ||
                   result.activationTokensCleared;
  return result;
}

inline SurfaceSeatCleanupResult clearUnmappedSurfaceSeatState(WaylandServer::Impl* server,
                                                              WaylandServer::Impl::Surface* surface) {
  return clearUnmappedSurfaceSeatState(surfaceSeatStateRefs(server), surface);
}

} // namespace lambdaui::compositor
