#pragma once

/// \file Lambda/UI/Shortcut.hpp
///
/// Part of the Lambda public API.


#include <Lambda/UI/KeyCodes.hpp>
#include <Lambda/UI/Input.hpp>

namespace lambdaui {

/// A key combination that triggers an action.
struct Shortcut {
  KeyCode key = 0;
  Modifiers modifiers = Modifiers::None;

  constexpr bool operator==(Shortcut const&) const = default;

  /// True when this shortcut matches a KeyDown event.
  constexpr bool matches(KeyCode k, Modifiers m) const {
    // Default `Shortcut{}` means "no shortcut" (optional in ActionDescriptor). `keys::A` is 0x00 on
    // macOS, so we must not treat key==0 alone as invalid — only the full default pair is "unset".
    if (key == 0 && modifiers == Modifiers::None) {
      return false;
    }
    return k == key && m == modifiers;
  }
};

namespace shortcuts {

// Predefined common shortcuts for convenience.
// All use Modifiers::Meta (Cmd on macOS, Ctrl on Linux/Windows).
inline constexpr Shortcut Copy{keys::C, Modifiers::Meta};
inline constexpr Shortcut Cut{keys::X, Modifiers::Meta};
inline constexpr Shortcut Paste{keys::V, Modifiers::Meta};
inline constexpr Shortcut SelectAll{keys::A, Modifiers::Meta};
inline constexpr Shortcut Undo{keys::Z, Modifiers::Meta};
inline constexpr Shortcut Redo{keys::Z, Modifiers::Meta | Modifiers::Shift};
inline constexpr Shortcut Save{keys::S, Modifiers::Meta};
inline constexpr Shortcut New{keys::N, Modifiers::Meta};
inline constexpr Shortcut Open{keys::O, Modifiers::Meta};
inline constexpr Shortcut Close{keys::W, Modifiers::Meta};
inline constexpr Shortcut Quit{keys::Q, Modifiers::Meta};
inline constexpr Shortcut Find{keys::F, Modifiers::Meta};

} // namespace shortcuts

} // namespace lambdaui
