#include "Compositor/Input/VtSwitchShortcut.hpp"
#include "Platform/Linux/Common/VtSwitch.hpp"

#include <doctest/doctest.h>

#include <linux/input-event-codes.h>

namespace {

lambda::platform::KmsInputEvent key(std::uint32_t code, bool pressed) {
  lambda::platform::KmsInputEvent event;
  event.kind = lambda::platform::KmsInputEvent::Kind::Key;
  event.pressed = pressed;
  event.key = code;
  return event;
}

} // namespace

TEST_CASE("VT switch shortcut maps function keys to sessions") {
  CHECK(lambda::compositor::vtSessionForFunctionKey(KEY_F1) == 1);
  CHECK(lambda::compositor::vtSessionForFunctionKey(KEY_F2) == 2);
  CHECK(lambda::compositor::vtSessionForFunctionKey(KEY_F10) == 10);
  CHECK(lambda::compositor::vtSessionForFunctionKey(KEY_F11) == 11);
  CHECK(lambda::compositor::vtSessionForFunctionKey(KEY_F12) == 12);
  CHECK_FALSE(lambda::compositor::vtSessionForFunctionKey(KEY_F13).has_value());
  CHECK_FALSE(lambda::compositor::vtSessionForFunctionKey(KEY_A).has_value());
}

TEST_CASE("Ctrl Alt function key requests and consumes a VT switch") {
  lambda::compositor::VtSwitchShortcutState state;

  CHECK_FALSE(lambda::compositor::handleVtSwitchShortcut(state, key(KEY_LEFTCTRL, true)).consume);
  CHECK_FALSE(lambda::compositor::handleVtSwitchShortcut(state, key(KEY_LEFTALT, true)).consume);

  auto const press = lambda::compositor::handleVtSwitchShortcut(state, key(KEY_F2, true));
  REQUIRE(press.consume);
  REQUIRE(press.targetSession);
  CHECK(*press.targetSession == 2);

  auto const repeat = lambda::compositor::handleVtSwitchShortcut(state, key(KEY_F2, true));
  CHECK(repeat.consume);
  CHECK_FALSE(repeat.targetSession.has_value());

  auto const release = lambda::compositor::handleVtSwitchShortcut(state, key(KEY_F2, false));
  CHECK(release.consume);
  CHECK_FALSE(release.targetSession.has_value());

  CHECK_FALSE(lambda::compositor::handleVtSwitchShortcut(state, key(KEY_LEFTALT, false)).consume);
  CHECK_FALSE(lambda::compositor::handleVtSwitchShortcut(state, key(KEY_LEFTCTRL, false)).consume);
}

TEST_CASE("VT switch shortcut requires both Ctrl and Alt") {
  lambda::compositor::VtSwitchShortcutState state;

  CHECK_FALSE(lambda::compositor::handleVtSwitchShortcut(state, key(KEY_LEFTCTRL, true)).consume);
  auto const ctrlOnly = lambda::compositor::handleVtSwitchShortcut(state, key(KEY_F3, true));
  CHECK_FALSE(ctrlOnly.consume);
  CHECK_FALSE(ctrlOnly.targetSession.has_value());

  CHECK_FALSE(lambda::compositor::handleVtSwitchShortcut(state, key(KEY_F3, false)).consume);
  CHECK_FALSE(lambda::compositor::handleVtSwitchShortcut(state, key(KEY_LEFTCTRL, false)).consume);
}

TEST_CASE("VT switch shortcut accepts right-side modifiers and resets state") {
  lambda::compositor::VtSwitchShortcutState state;

  CHECK_FALSE(lambda::compositor::handleVtSwitchShortcut(state, key(KEY_RIGHTCTRL, true)).consume);
  CHECK_FALSE(lambda::compositor::handleVtSwitchShortcut(state, key(KEY_RIGHTALT, true)).consume);

  auto const press = lambda::compositor::handleVtSwitchShortcut(state, key(KEY_F12, true));
  REQUIRE(press.consume);
  REQUIRE(press.targetSession);
  CHECK(*press.targetSession == 12);

  auto const reset = lambda::compositor::handleVtSwitchShortcut(
      state,
      {.kind = lambda::platform::KmsInputEvent::Kind::KeyboardReset});
  CHECK_FALSE(reset.consume);

  auto const afterReset = lambda::compositor::handleVtSwitchShortcut(state, key(KEY_F12, false));
  CHECK_FALSE(afterReset.consume);
  CHECK_FALSE(afterReset.targetSession.has_value());
}

TEST_CASE("VT switch shortcut tracks left and right modifiers independently") {
  lambda::compositor::VtSwitchShortcutState state;

  CHECK_FALSE(lambda::compositor::handleVtSwitchShortcut(state, key(KEY_LEFTCTRL, true)).consume);
  CHECK_FALSE(lambda::compositor::handleVtSwitchShortcut(state, key(KEY_RIGHTCTRL, true)).consume);
  CHECK_FALSE(lambda::compositor::handleVtSwitchShortcut(state, key(KEY_LEFTCTRL, false)).consume);
  CHECK_FALSE(lambda::compositor::handleVtSwitchShortcut(state, key(KEY_LEFTALT, true)).consume);

  auto const press = lambda::compositor::handleVtSwitchShortcut(state, key(KEY_F4, true));
  REQUIRE(press.consume);
  REQUIRE(press.targetSession);
  CHECK(*press.targetSession == 4);
}

TEST_CASE("Alt arrow keys request adjacent VT switches") {
  lambda::compositor::VtSwitchShortcutState state;

  CHECK_FALSE(lambda::compositor::handleVtSwitchShortcut(state, key(KEY_LEFTALT, true)).consume);

  auto const previous = lambda::compositor::handleVtSwitchShortcut(state, key(KEY_LEFT, true));
  REQUIRE(previous.consume);
  CHECK_FALSE(previous.targetSession.has_value());
  CHECK(previous.adjacentDirection == -1);

  auto const previousRepeat = lambda::compositor::handleVtSwitchShortcut(state, key(KEY_LEFT, true));
  CHECK(previousRepeat.consume);
  CHECK(previousRepeat.adjacentDirection == 0);

  auto const previousRelease = lambda::compositor::handleVtSwitchShortcut(state, key(KEY_LEFT, false));
  CHECK(previousRelease.consume);
  CHECK(previousRelease.adjacentDirection == 0);

  auto const next = lambda::compositor::handleVtSwitchShortcut(state, key(KEY_RIGHT, true));
  REQUIRE(next.consume);
  CHECK_FALSE(next.targetSession.has_value());
  CHECK(next.adjacentDirection == 1);

  auto const nextRelease = lambda::compositor::handleVtSwitchShortcut(state, key(KEY_RIGHT, false));
  CHECK(nextRelease.consume);
  CHECK(nextRelease.adjacentDirection == 0);
}

TEST_CASE("Arrow keys without Alt are not VT switch shortcuts") {
  lambda::compositor::VtSwitchShortcutState state;

  auto const left = lambda::compositor::handleVtSwitchShortcut(state, key(KEY_LEFT, true));
  CHECK_FALSE(left.consume);
  CHECK(left.adjacentDirection == 0);

  auto const right = lambda::compositor::handleVtSwitchShortcut(state, key(KEY_RIGHT, true));
  CHECK_FALSE(right.consume);
  CHECK(right.adjacentDirection == 0);
}

TEST_CASE("Adjacent VT selection follows the kernel allocated-session mask") {
  using lambda::linux_platform::adjacentAllocatedVtSession;
  using lambda::linux_platform::vtStateMaskContainsSession;

  std::uint16_t const allocated =
      static_cast<std::uint16_t>((1u << 1) | (1u << 2) | (1u << 7) | (1u << 12));

  CHECK(vtStateMaskContainsSession(allocated, 1));
  CHECK(vtStateMaskContainsSession(allocated, 12));
  CHECK_FALSE(vtStateMaskContainsSession(allocated, 3));
  CHECK_FALSE(vtStateMaskContainsSession(allocated, 0));

  CHECK(adjacentAllocatedVtSession(2, allocated, 1) == 7);
  CHECK(adjacentAllocatedVtSession(7, allocated, -1) == 2);
  CHECK(adjacentAllocatedVtSession(12, allocated, 1) == 1);
  CHECK(adjacentAllocatedVtSession(1, allocated, -1) == 12);
}

TEST_CASE("Adjacent VT selection reports no target when only the current VT is allocated") {
  using lambda::linux_platform::adjacentAllocatedVtSession;
  using lambda::linux_platform::adjacentNumberedVtSession;

  std::uint16_t const onlyCurrent = static_cast<std::uint16_t>(1u << 4);
  CHECK_FALSE(adjacentAllocatedVtSession(4, onlyCurrent, 1).has_value());
  CHECK_FALSE(adjacentAllocatedVtSession(4, onlyCurrent, -1).has_value());

  CHECK(adjacentNumberedVtSession(4, 1) == 5);
  CHECK(adjacentNumberedVtSession(1, -1) == 15);
}
