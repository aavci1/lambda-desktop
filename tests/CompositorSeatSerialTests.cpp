#include "Compositor/Wayland/CursorRequestState.hpp"
#include "Compositor/Wayland/PointerButtonGrabState.hpp"
#include "Compositor/Wayland/SelectionSerialState.hpp"
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

TEST_CASE("selection serial policy accepts only wlroots-aligned trigger serials") {
  using lambda::compositor::SeatSerialKind;
  using lambda::compositor::WaylandServer;
  using lambda::compositor::dataDeviceStartDragAcceptsSerialKind;
  using lambda::compositor::issueSeatSerial;
  using lambda::compositor::kDataDeviceDragStartSerialKinds;
  using lambda::compositor::kSelectionSetSerialKinds;
  using lambda::compositor::seatSerialIsValid;
  using lambda::compositor::selectionSetAcceptsSerialKind;

  CHECK(dataDeviceStartDragAcceptsSerialKind(SeatSerialKind::PointerButtonPress));
  CHECK_FALSE(dataDeviceStartDragAcceptsSerialKind(SeatSerialKind::PointerEnter));
  CHECK_FALSE(dataDeviceStartDragAcceptsSerialKind(SeatSerialKind::PointerButtonRelease));
  CHECK_FALSE(dataDeviceStartDragAcceptsSerialKind(SeatSerialKind::KeyboardEnter));
  CHECK_FALSE(dataDeviceStartDragAcceptsSerialKind(SeatSerialKind::KeyboardKey));
  CHECK_FALSE(dataDeviceStartDragAcceptsSerialKind(SeatSerialKind::DataDeviceEnter));

  CHECK(selectionSetAcceptsSerialKind(SeatSerialKind::KeyboardEnter));
  CHECK(selectionSetAcceptsSerialKind(SeatSerialKind::KeyboardKey));
  CHECK(selectionSetAcceptsSerialKind(SeatSerialKind::PointerButtonPress));
  CHECK_FALSE(selectionSetAcceptsSerialKind(SeatSerialKind::PointerEnter));
  CHECK_FALSE(selectionSetAcceptsSerialKind(SeatSerialKind::PointerButtonRelease));
  CHECK_FALSE(selectionSetAcceptsSerialKind(SeatSerialKind::KeyboardModifiers));
  CHECK_FALSE(selectionSetAcceptsSerialKind(SeatSerialKind::DataDeviceEnter));

  std::uint32_t nextSerial = 1;
  std::deque<WaylandServer::Impl::SeatSerialRecord> records;
  WaylandServer::Impl::Surface surface{};
  auto* client = reinterpret_cast<wl_client*>(std::uintptr_t{1});

  std::uint32_t const pointerPress =
      issueSeatSerial(nextSerial, records, SeatSerialKind::PointerButtonPress, client, &surface);
  std::uint32_t const pointerRelease =
      issueSeatSerial(nextSerial, records, SeatSerialKind::PointerButtonRelease, client, &surface);
  std::uint32_t const keyboardKey =
      issueSeatSerial(nextSerial, records, SeatSerialKind::KeyboardKey, client, &surface);
  std::uint32_t const dataDeviceEnter =
      issueSeatSerial(nextSerial, records, SeatSerialKind::DataDeviceEnter, client, &surface);

  CHECK(seatSerialIsValid(records, pointerPress, client, &surface, kDataDeviceDragStartSerialKinds));
  CHECK_FALSE(seatSerialIsValid(records, pointerRelease, client, &surface, kDataDeviceDragStartSerialKinds));
  CHECK_FALSE(seatSerialIsValid(records, keyboardKey, client, &surface, kDataDeviceDragStartSerialKinds));

  CHECK(seatSerialIsValid(records, pointerPress, client, &surface, kSelectionSetSerialKinds));
  CHECK(seatSerialIsValid(records, keyboardKey, client, &surface, kSelectionSetSerialKinds));
  CHECK_FALSE(seatSerialIsValid(records, pointerRelease, client, &surface, kSelectionSetSerialKinds));
  CHECK_FALSE(seatSerialIsValid(records, dataDeviceEnter, client, &surface, kSelectionSetSerialKinds));
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

TEST_CASE("pointer button grab helper preserves implicit grab until final release") {
  using lambda::compositor::PointerButtonGrabRefs;
  using lambda::compositor::WaylandServer;
  using lambda::compositor::pointerButtonGrabDeliveryClient;
  using lambda::compositor::pointerButtonGrabDeliverySurface;
  using lambda::compositor::pointerButtonGrabUpdateForButton;

  WaylandServer::Impl::Surface focused{};
  WaylandServer::Impl::Surface otherFocus{};
  WaylandServer::Impl::Surface* grabSurface = nullptr;
  auto* focusedClient = reinterpret_cast<wl_client*>(std::uintptr_t{1});
  auto* otherClient = reinterpret_cast<wl_client*>(std::uintptr_t{2});
  wl_client* grabClient = nullptr;
  std::uint32_t buttonCount = 0;
  PointerButtonGrabRefs refs{
      .grabSurface = &grabSurface,
      .grabClient = &grabClient,
      .buttonCount = &buttonCount,
  };

  CHECK(pointerButtonGrabDeliverySurface(refs, &focused) == &focused);
  CHECK(pointerButtonGrabDeliveryClient(refs, focusedClient) == focusedClient);

  auto transition = pointerButtonGrabUpdateForButton(refs, &focused, focusedClient, true);
  CHECK(transition.previousSurface == nullptr);
  CHECK(transition.nextSurface == &focused);
  CHECK(transition.previousClient == nullptr);
  CHECK(transition.nextClient == focusedClient);
  CHECK(transition.previousCount == 0);
  CHECK(transition.nextCount == 1);
  CHECK(grabSurface == &focused);
  CHECK(grabClient == focusedClient);
  CHECK(buttonCount == 1);

  CHECK(pointerButtonGrabDeliverySurface(refs, &otherFocus) == &focused);
  CHECK(pointerButtonGrabDeliveryClient(refs, otherClient) == focusedClient);

  transition = pointerButtonGrabUpdateForButton(refs, &focused, focusedClient, true);
  CHECK(transition.previousSurface == &focused);
  CHECK(transition.nextSurface == &focused);
  CHECK(transition.previousClient == focusedClient);
  CHECK(transition.nextClient == focusedClient);
  CHECK(transition.previousCount == 1);
  CHECK(transition.nextCount == 2);
  CHECK(buttonCount == 2);

  transition = pointerButtonGrabUpdateForButton(refs, &focused, focusedClient, false);
  CHECK(transition.nextSurface == &focused);
  CHECK(transition.nextClient == focusedClient);
  CHECK(transition.previousCount == 2);
  CHECK(transition.nextCount == 1);
  CHECK(buttonCount == 1);

  transition = pointerButtonGrabUpdateForButton(refs, &focused, focusedClient, false);
  CHECK(transition.nextSurface == nullptr);
  CHECK(transition.nextClient == nullptr);
  CHECK(transition.previousCount == 1);
  CHECK(transition.nextCount == 0);
  CHECK(grabSurface == nullptr);
  CHECK(grabClient == nullptr);
  CHECK(buttonCount == 0);
}

TEST_CASE("pointer button grab helper clears stale grabbed surfaces") {
  using lambda::compositor::PointerButtonGrabRefs;
  using lambda::compositor::WaylandServer;
  using lambda::compositor::pointerButtonGrabClearStale;
  using lambda::compositor::pointerButtonGrabDeliveryClient;
  using lambda::compositor::pointerButtonGrabDeliverySurface;

  WaylandServer::Impl::Surface focused{};
  WaylandServer::Impl::Surface* grabSurface = nullptr;
  auto* focusedClient = reinterpret_cast<wl_client*>(std::uintptr_t{1});
  auto* staleClient = reinterpret_cast<wl_client*>(std::uintptr_t{2});
  wl_client* grabClient = staleClient;
  std::uint32_t buttonCount = 1;
  PointerButtonGrabRefs refs{
      .grabSurface = &grabSurface,
      .grabClient = &grabClient,
      .buttonCount = &buttonCount,
  };

  CHECK(pointerButtonGrabClearStale(refs));
  CHECK(grabSurface == nullptr);
  CHECK(grabClient == nullptr);
  CHECK(buttonCount == 0);

  grabClient = staleClient;
  buttonCount = 1;
  CHECK(pointerButtonGrabDeliverySurface(refs, &focused) == &focused);
  CHECK(pointerButtonGrabDeliveryClient(refs, focusedClient) == focusedClient);
  CHECK(grabClient == nullptr);
  CHECK(buttonCount == 0);
}
