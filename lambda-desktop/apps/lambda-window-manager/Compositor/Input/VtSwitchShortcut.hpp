#pragma once

#include <Lambda/Platform/Linux/KmsOutput.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <linux/input-event-codes.h>

namespace lambdaui::compositor {

struct VtSwitchShortcutState {
  bool leftCtrlDown = false;
  bool rightCtrlDown = false;
  bool leftAltDown = false;
  bool rightAltDown = false;
  std::array<bool, 13> consumedSessions{};
  bool consumedPreviousSession = false;
  bool consumedNextSession = false;
};

struct VtSwitchShortcutResult {
  bool consume = false;
  std::optional<int> targetSession;
  int adjacentDirection = 0;
};

[[nodiscard]] inline std::optional<int> vtSessionForFunctionKey(std::uint32_t key) noexcept {
  switch (key) {
  case KEY_F1: return 1;
  case KEY_F2: return 2;
  case KEY_F3: return 3;
  case KEY_F4: return 4;
  case KEY_F5: return 5;
  case KEY_F6: return 6;
  case KEY_F7: return 7;
  case KEY_F8: return 8;
  case KEY_F9: return 9;
  case KEY_F10: return 10;
  case KEY_F11: return 11;
  case KEY_F12: return 12;
  default: return std::nullopt;
  }
}

[[nodiscard]] inline bool isVtSwitchCtrlKey(std::uint32_t key) noexcept {
  return key == KEY_LEFTCTRL || key == KEY_RIGHTCTRL;
}

[[nodiscard]] inline bool isVtSwitchAltKey(std::uint32_t key) noexcept {
  return key == KEY_LEFTALT || key == KEY_RIGHTALT;
}

[[nodiscard]] inline bool vtSwitchCtrlDown(VtSwitchShortcutState const& state) noexcept {
  return state.leftCtrlDown || state.rightCtrlDown;
}

[[nodiscard]] inline bool vtSwitchAltDown(VtSwitchShortcutState const& state) noexcept {
  return state.leftAltDown || state.rightAltDown;
}

[[nodiscard]] inline int vtAdjacentDirectionForArrowKey(std::uint32_t key) noexcept {
  if (key == KEY_LEFT) return -1;
  if (key == KEY_RIGHT) return 1;
  return 0;
}

[[nodiscard]] inline VtSwitchShortcutResult consumedVtShortcutResult() noexcept {
  VtSwitchShortcutResult result;
  result.consume = true;
  return result;
}

[[nodiscard]] inline VtSwitchShortcutResult handleVtSwitchShortcut(VtSwitchShortcutState& state,
                                                                   platform::KmsInputEvent const& event) {
  if (event.kind == platform::KmsInputEvent::Kind::KeyboardReset) {
    state = {};
    return {};
  }
  if (event.kind != platform::KmsInputEvent::Kind::Key) return {};

  if (isVtSwitchCtrlKey(event.key)) {
    if (event.key == KEY_LEFTCTRL) {
      state.leftCtrlDown = event.pressed;
    } else {
      state.rightCtrlDown = event.pressed;
    }
    return {};
  }
  if (isVtSwitchAltKey(event.key)) {
    if (event.key == KEY_LEFTALT) {
      state.leftAltDown = event.pressed;
    } else {
      state.rightAltDown = event.pressed;
    }
    return {};
  }

  int const adjacentDirection = vtAdjacentDirectionForArrowKey(event.key);
  if (adjacentDirection != 0) {
    bool& consumed = adjacentDirection < 0 ? state.consumedPreviousSession : state.consumedNextSession;
    if (!event.pressed) {
      if (!consumed) return {};
      consumed = false;
      return consumedVtShortcutResult();
    }

    if (consumed) return consumedVtShortcutResult();
    if (!vtSwitchAltDown(state)) return {};

    consumed = true;
    VtSwitchShortcutResult result;
    result.consume = true;
    result.adjacentDirection = adjacentDirection;
    return result;
  }

  auto const target = vtSessionForFunctionKey(event.key);
  if (!target) return {};

  bool& consumed = state.consumedSessions[static_cast<std::size_t>(*target)];
  if (!event.pressed) {
    if (!consumed) return {};
    consumed = false;
    return consumedVtShortcutResult();
  }

  if (consumed) return consumedVtShortcutResult();
  if (!vtSwitchCtrlDown(state) || !vtSwitchAltDown(state)) return {};

  consumed = true;
  VtSwitchShortcutResult result;
  result.consume = true;
  result.targetSession = *target;
  return result;
}

} // namespace lambdaui::compositor
