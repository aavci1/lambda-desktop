#include "Compositor/Wayland/CursorRequestState.hpp"
#include "Compositor/Wayland/WaylandServerImpl.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <deque>

TEST_CASE("seat serial ledger validates client surface and kind") {
  using lambda::compositor::SeatSerialKind;
  using lambda::compositor::WaylandServer;
  using lambda::compositor::issueSeatSerial;
  using lambda::compositor::seatSerialIsValid;

  std::uint32_t nextSerial = 1;
  std::deque<WaylandServer::Impl::SeatSerialRecord> records;
  WaylandServer::Impl::Surface surfaceA{};
  WaylandServer::Impl::Surface surfaceB{};
  auto* clientA = reinterpret_cast<wl_client*>(std::uintptr_t{1});
  auto* clientB = reinterpret_cast<wl_client*>(std::uintptr_t{2});
  std::array pointerKinds{
      SeatSerialKind::PointerEnter,
      SeatSerialKind::PointerButtonPress,
      SeatSerialKind::PointerButtonRelease,
  };
  std::array keyboardKinds{SeatSerialKind::KeyboardKey};
  std::array pointerPressOnly{SeatSerialKind::PointerButtonPress};
  std::array selectionKinds{
      SeatSerialKind::KeyboardEnter,
      SeatSerialKind::KeyboardKey,
      SeatSerialKind::PointerButtonPress,
  };

  std::uint32_t const serial =
      issueSeatSerial(nextSerial, records, SeatSerialKind::PointerButtonPress, clientA, &surfaceA);
  std::uint32_t const releaseSerial =
      issueSeatSerial(nextSerial, records, SeatSerialKind::PointerButtonRelease, clientA, &surfaceA);
  std::uint32_t const keyboardEnterSerial =
      issueSeatSerial(nextSerial, records, SeatSerialKind::KeyboardEnter, clientA, &surfaceA);

  CHECK(seatSerialIsValid(records, serial, clientA, &surfaceA, pointerKinds));
  CHECK(seatSerialIsValid(records, serial, clientA, nullptr, pointerKinds));
  CHECK_FALSE(seatSerialIsValid(records, serial, clientB, &surfaceA, pointerKinds));
  CHECK_FALSE(seatSerialIsValid(records, serial, clientB, nullptr, pointerKinds));
  CHECK_FALSE(seatSerialIsValid(records, serial, clientA, &surfaceB, pointerKinds));
  CHECK_FALSE(seatSerialIsValid(records, serial, clientA, &surfaceA, keyboardKinds));
  CHECK_FALSE(seatSerialIsValid(records, releaseSerial, clientA, &surfaceA, pointerPressOnly));
  CHECK(seatSerialIsValid(records, keyboardEnterSerial, clientA, &surfaceA, selectionKinds));
  CHECK_FALSE(seatSerialIsValid(records, 0, clientA, &surfaceA, pointerKinds));
}

TEST_CASE("seat serial ledger trims old records and clears destroyed surfaces") {
  using lambda::compositor::SeatSerialKind;
  using lambda::compositor::WaylandServer;
  using lambda::compositor::clearSeatSerialsForSurface;
  using lambda::compositor::issueSeatSerial;
  using lambda::compositor::seatSerialIsValid;

  std::uint32_t nextSerial = 1;
  std::deque<WaylandServer::Impl::SeatSerialRecord> records;
  WaylandServer::Impl::Surface surface{};
  auto* client = reinterpret_cast<wl_client*>(std::uintptr_t{1});
  std::array pointerKinds{SeatSerialKind::PointerEnter, SeatSerialKind::PointerButtonPress};

  std::uint32_t firstSerial = 0;
  std::uint32_t lastSerial = 0;
  for (int i = 0; i < 70; ++i) {
    std::uint32_t const serial =
        issueSeatSerial(nextSerial, records, SeatSerialKind::PointerEnter, client, &surface);
    if (i == 0) firstSerial = serial;
    lastSerial = serial;
  }

  CHECK_FALSE(seatSerialIsValid(records, firstSerial, client, &surface, pointerKinds));
  CHECK(seatSerialIsValid(records, lastSerial, client, &surface, pointerKinds));

  clearSeatSerialsForSurface(records, &surface);
  CHECK_FALSE(seatSerialIsValid(records, lastSerial, client, &surface, pointerKinds));
}

TEST_CASE("cursor request validation follows pointer focus without button grab") {
  using lambda::compositor::CursorRequestSeatState;
  using lambda::compositor::SeatSerialKind;
  using lambda::compositor::WaylandServer;
  using lambda::compositor::cursorRequestValidationSurface;
  using lambda::compositor::issueSeatSerial;

  std::uint32_t nextSerial = 1;
  std::deque<WaylandServer::Impl::SeatSerialRecord> records;
  WaylandServer::Impl::Surface focused{};
  WaylandServer::Impl::Surface other{};
  auto* focusedClient = reinterpret_cast<wl_client*>(std::uintptr_t{1});
  auto* otherClient = reinterpret_cast<wl_client*>(std::uintptr_t{2});

  std::uint32_t const focusedEnter =
      issueSeatSerial(nextSerial, records, SeatSerialKind::PointerEnter, focusedClient, &focused);
  std::uint32_t const otherEnter =
      issueSeatSerial(nextSerial, records, SeatSerialKind::PointerEnter, otherClient, &other);

  CursorRequestSeatState state{
      .pointerFocus = &focused,
      .seatSerials = &records,
  };
  CHECK(cursorRequestValidationSurface(state, focusedClient, focusedEnter) == &focused);
  CHECK(cursorRequestValidationSurface(state, otherClient, otherEnter) == nullptr);

  state.pointerFocus = &other;
  CHECK(cursorRequestValidationSurface(state, otherClient, otherEnter) == &other);
  CHECK(cursorRequestValidationSurface(state, focusedClient, focusedEnter) == nullptr);
}

TEST_CASE("cursor request validation honors implicit pointer button grab client") {
  using lambda::compositor::CursorRequestSeatState;
  using lambda::compositor::SeatSerialKind;
  using lambda::compositor::WaylandServer;
  using lambda::compositor::cursorRequestValidationSurface;
  using lambda::compositor::issueSeatSerial;

  std::uint32_t nextSerial = 1;
  std::deque<WaylandServer::Impl::SeatSerialRecord> records;
  WaylandServer::Impl::Surface grabSurface{};
  WaylandServer::Impl::Surface focusedOtherClient{};
  WaylandServer::Impl::Surface focusedSameClient{};
  auto* grabClient = reinterpret_cast<wl_client*>(std::uintptr_t{1});
  auto* otherClient = reinterpret_cast<wl_client*>(std::uintptr_t{2});

  std::uint32_t const grabPress =
      issueSeatSerial(nextSerial, records, SeatSerialKind::PointerButtonPress, grabClient, &grabSurface);
  std::uint32_t const otherEnter =
      issueSeatSerial(nextSerial, records, SeatSerialKind::PointerEnter, otherClient, &focusedOtherClient);
  std::uint32_t const sameClientEnter =
      issueSeatSerial(nextSerial, records, SeatSerialKind::PointerEnter, grabClient, &focusedSameClient);

  CursorRequestSeatState state{
      .pointerFocus = &focusedOtherClient,
      .pointerButtonGrabSurface = &grabSurface,
      .pointerButtonGrabClient = grabClient,
      .pointerButtonCount = 1,
      .seatSerials = &records,
  };
  CHECK(cursorRequestValidationSurface(state, grabClient, grabPress) == &grabSurface);
  CHECK(cursorRequestValidationSurface(state, otherClient, otherEnter) == nullptr);

  state.pointerFocus = &focusedSameClient;
  CHECK(cursorRequestValidationSurface(state, grabClient, sameClientEnter) == &focusedSameClient);

  state.pointerButtonCount = 0;
  state.pointerButtonGrabSurface = nullptr;
  state.pointerButtonGrabClient = nullptr;
  state.pointerFocus = &focusedOtherClient;
  CHECK(cursorRequestValidationSurface(state, otherClient, otherEnter) == &focusedOtherClient);
}
