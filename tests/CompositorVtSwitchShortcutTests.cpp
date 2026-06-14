#include "Compositor/Input/VtSwitchShortcut.hpp"

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
