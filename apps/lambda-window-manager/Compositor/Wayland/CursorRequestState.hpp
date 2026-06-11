#pragma once

#include "Compositor/Wayland/WaylandServerImpl.hpp"

#include <array>
#include <cstdint>
#include <deque>

namespace lambda::compositor {

inline constexpr std::array kPointerCursorSerialKinds{
    SeatSerialKind::PointerEnter,
    SeatSerialKind::PointerButtonPress,
    SeatSerialKind::PointerButtonRelease,
};

struct CursorRequestSeatState {
  WaylandServer::Impl::Surface* pointerFocus = nullptr;
  WaylandServer::Impl::Surface* pointerButtonGrabSurface = nullptr;
  wl_client* pointerButtonGrabClient = nullptr;
  std::uint32_t pointerButtonCount = 0;
  std::deque<WaylandServer::Impl::SeatSerialRecord> const* seatSerials = nullptr;
};

inline CursorRequestSeatState cursorRequestSeatState(WaylandServer::Impl const* server) {
  if (!server) return {};
  return {
      .pointerFocus = server->pointerFocus_,
      .pointerButtonGrabSurface = server->pointerButtonGrabSurface_,
      .pointerButtonGrabClient = server->pointerButtonGrabClient_,
      .pointerButtonCount = server->pointerButtonCount_,
      .seatSerials = &server->seatSerials_,
  };
}

inline bool cursorRequestHasActiveButtonGrab(CursorRequestSeatState const& state) {
  return state.pointerButtonCount > 0 &&
         state.pointerButtonGrabSurface &&
         state.pointerButtonGrabClient;
}

inline bool cursorRequestSerialMatches(CursorRequestSeatState const& state,
                                       wl_client* client,
                                       std::uint32_t serial,
                                       WaylandServer::Impl::Surface const* surface) {
  return state.seatSerials &&
         surface &&
         seatSerialIsValid(*state.seatSerials, serial, client, surface, kPointerCursorSerialKinds);
}

inline WaylandServer::Impl::Surface* cursorRequestValidationSurface(CursorRequestSeatState const& state,
                                                                    wl_client* client,
                                                                    std::uint32_t serial) {
  if (!client || serial == 0 || !state.seatSerials) return nullptr;

  if (cursorRequestHasActiveButtonGrab(state)) {
    if (state.pointerButtonGrabClient != client) return nullptr;
    if (cursorRequestSerialMatches(state, client, serial, state.pointerButtonGrabSurface)) {
      return state.pointerButtonGrabSurface;
    }
    if (cursorRequestSerialMatches(state, client, serial, state.pointerFocus)) {
      return state.pointerFocus;
    }
    return nullptr;
  }

  if (cursorRequestSerialMatches(state, client, serial, state.pointerFocus)) {
    return state.pointerFocus;
  }
  return nullptr;
}

inline bool cursorRequestSerialValid(WaylandServer::Impl const* server,
                                     wl_client* client,
                                     std::uint32_t serial) {
  return cursorRequestValidationSurface(cursorRequestSeatState(server), client, serial) != nullptr;
}

} // namespace lambda::compositor
