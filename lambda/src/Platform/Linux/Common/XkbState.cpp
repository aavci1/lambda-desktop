#include "Platform/Linux/Common/XkbState.hpp"

#include <Lambda/UI/KeyCodes.hpp>

#include <sys/mman.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>

#include <cstring>

namespace lambda::linux_platform {

static KeyCode keyCodeFromXkbKeysym(xkb_keysym_t sym);
static std::string utf8FromXkbKeysym(xkb_keysym_t sym);

XkbState::XkbState() {
  context_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
}

XkbState::~XkbState() {
  if (state_) xkb_state_unref(state_);
  if (keymap_) xkb_keymap_unref(keymap_);
  if (context_) xkb_context_unref(context_);
}

bool XkbState::loadKeymapFromFd(int fd, std::uint32_t size) {
  if (!context_) {
    close(fd);
    return false;
  }
  char* map = static_cast<char*>(mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
  if (map == MAP_FAILED) {
    close(fd);
    return false;
  }
  xkb_keymap* keymap = xkb_keymap_new_from_string(context_, map, XKB_KEYMAP_FORMAT_TEXT_V1,
                                                  XKB_KEYMAP_COMPILE_NO_FLAGS);
  munmap(map, size);
  close(fd);
  return installKeymap(keymap);
}

bool XkbState::createDefaultKeymap() {
  if (!context_) return false;
  xkb_rule_names names{};
  return installKeymap(xkb_keymap_new_from_names(context_, &names, XKB_KEYMAP_COMPILE_NO_FLAGS));
}

bool XkbState::installKeymap(xkb_keymap* keymap) {
  if (!keymap) return false;
  xkb_state* state = xkb_state_new(keymap);
  if (!state) {
    xkb_keymap_unref(keymap);
    return false;
  }
  if (state_) xkb_state_unref(state_);
  if (keymap_) xkb_keymap_unref(keymap_);
  keymap_ = keymap;
  state_ = state;
  modifiers_ = Modifiers::None;
  return true;
}

void XkbState::updateModifiers(std::uint32_t depressed, std::uint32_t latched,
                               std::uint32_t locked, std::uint32_t group) {
  if (!state_) return;
  xkb_state_update_mask(state_, depressed, latched, locked, 0, 0, group);
  Modifiers mods = Modifiers::None;
  if (xkb_state_mod_name_is_active(state_, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0) {
    mods = mods | Modifiers::Shift;
  }
  if (xkb_state_mod_name_is_active(state_, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0) {
    mods = mods | Modifiers::Ctrl;
  }
  if (xkb_state_mod_name_is_active(state_, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE) > 0) {
    mods = mods | Modifiers::Alt;
  }
  if (xkb_state_mod_name_is_active(state_, XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE) > 0) {
    mods = mods | Modifiers::Meta;
  }
  modifiers_ = mods;
}

void XkbState::updateKey(std::uint32_t key, bool pressed) {
  if (!state_) return;
  xkb_state_update_key(state_, key + 8, pressed ? XKB_KEY_DOWN : XKB_KEY_UP);
  Modifiers mods = Modifiers::None;
  if (xkb_state_mod_name_is_active(state_, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0) {
    mods = mods | Modifiers::Shift;
  }
  if (xkb_state_mod_name_is_active(state_, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0) {
    mods = mods | Modifiers::Ctrl;
  }
  if (xkb_state_mod_name_is_active(state_, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE) > 0) {
    mods = mods | Modifiers::Alt;
  }
  if (xkb_state_mod_name_is_active(state_, XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE) > 0) {
    mods = mods | Modifiers::Meta;
  }
  modifiers_ = mods;
}

void XkbState::resetState() {
  if (!keymap_) {
    modifiers_ = Modifiers::None;
    return;
  }
  xkb_state* state = xkb_state_new(keymap_);
  if (!state) {
    modifiers_ = Modifiers::None;
    return;
  }
  if (state_) xkb_state_unref(state_);
  state_ = state;
  modifiers_ = Modifiers::None;
}

std::uint32_t XkbState::keysymForEvdevKey(std::uint32_t key) const {
  return state_ ? static_cast<std::uint32_t>(xkb_state_key_get_one_sym(state_, key + 8))
                : static_cast<std::uint32_t>(XKB_KEY_NoSymbol);
}

KeyCode XkbState::keyCodeForEvdevKey(std::uint32_t key) const {
  return keyCodeFromXkbKeysym(keysymForEvdevKey(key));
}

std::string XkbState::utf8ForEvdevKey(std::uint32_t key) const {
  return utf8FromXkbKeysym(keysymForEvdevKey(key));
}

bool XkbState::keyRepeats(std::uint32_t key) const {
  return keymap_ && xkb_keymap_key_repeats(keymap_, key + 8) > 0;
}

static KeyCode keyCodeFromXkbKeysym(xkb_keysym_t sym) {
  switch (sym) {
  case XKB_KEY_a:
  case XKB_KEY_A: return keys::A;
  case XKB_KEY_b:
  case XKB_KEY_B: return keys::B;
  case XKB_KEY_c:
  case XKB_KEY_C: return keys::C;
  case XKB_KEY_d:
  case XKB_KEY_D: return keys::D;
  case XKB_KEY_e:
  case XKB_KEY_E: return keys::E;
  case XKB_KEY_f:
  case XKB_KEY_F: return keys::F;
  case XKB_KEY_g:
  case XKB_KEY_G: return keys::G;
  case XKB_KEY_h:
  case XKB_KEY_H: return keys::H;
  case XKB_KEY_i:
  case XKB_KEY_I: return keys::I;
  case XKB_KEY_j:
  case XKB_KEY_J: return keys::J;
  case XKB_KEY_k:
  case XKB_KEY_K: return keys::K;
  case XKB_KEY_l:
  case XKB_KEY_L: return keys::L;
  case XKB_KEY_m:
  case XKB_KEY_M: return keys::M;
  case XKB_KEY_n:
  case XKB_KEY_N: return keys::N;
  case XKB_KEY_o:
  case XKB_KEY_O: return keys::O;
  case XKB_KEY_p:
  case XKB_KEY_P: return keys::P;
  case XKB_KEY_q:
  case XKB_KEY_Q: return keys::Q;
  case XKB_KEY_r:
  case XKB_KEY_R: return keys::R;
  case XKB_KEY_s:
  case XKB_KEY_S: return keys::S;
  case XKB_KEY_t:
  case XKB_KEY_T: return keys::T;
  case XKB_KEY_u:
  case XKB_KEY_U: return keys::U;
  case XKB_KEY_v:
  case XKB_KEY_V: return keys::V;
  case XKB_KEY_w:
  case XKB_KEY_W: return keys::W;
  case XKB_KEY_x:
  case XKB_KEY_X: return keys::X;
  case XKB_KEY_y:
  case XKB_KEY_Y: return keys::Y;
  case XKB_KEY_z:
  case XKB_KEY_Z: return keys::Z;
  case XKB_KEY_0: return keys::Digit0;
  case XKB_KEY_1: return keys::Digit1;
  case XKB_KEY_2: return keys::Digit2;
  case XKB_KEY_3: return keys::Digit3;
  case XKB_KEY_4: return keys::Digit4;
  case XKB_KEY_5: return keys::Digit5;
  case XKB_KEY_6: return keys::Digit6;
  case XKB_KEY_7: return keys::Digit7;
  case XKB_KEY_8: return keys::Digit8;
  case XKB_KEY_9: return keys::Digit9;
  case XKB_KEY_Return:
  case XKB_KEY_KP_Enter:
    return keys::Return;
  case XKB_KEY_Tab: return keys::Tab;
  case XKB_KEY_space: return keys::Space;
  case XKB_KEY_BackSpace: return keys::Delete;
  case XKB_KEY_Delete: return keys::ForwardDelete;
  case XKB_KEY_Escape: return keys::Escape;
  case XKB_KEY_Left: return keys::LeftArrow;
  case XKB_KEY_Right: return keys::RightArrow;
  case XKB_KEY_Down: return keys::DownArrow;
  case XKB_KEY_Up: return keys::UpArrow;
  case XKB_KEY_Home: return keys::Home;
  case XKB_KEY_End: return keys::End;
  case XKB_KEY_Page_Up: return keys::PageUp;
  case XKB_KEY_Page_Down: return keys::PageDown;
  case XKB_KEY_F1: return keys::F1;
  case XKB_KEY_F2: return keys::F2;
  case XKB_KEY_F3: return keys::F3;
  case XKB_KEY_F4: return keys::F4;
  case XKB_KEY_F5: return keys::F5;
  case XKB_KEY_F6: return keys::F6;
  case XKB_KEY_F7: return keys::F7;
  case XKB_KEY_F8: return keys::F8;
  case XKB_KEY_F9: return keys::F9;
  case XKB_KEY_F10: return keys::F10;
  case XKB_KEY_F11: return keys::F11;
  case XKB_KEY_F12: return keys::F12;
  case XKB_KEY_minus: return keys::Minus;
  case XKB_KEY_equal: return keys::Equal;
  case XKB_KEY_bracketleft: return keys::LeftBracket;
  case XKB_KEY_bracketright: return keys::RightBracket;
  case XKB_KEY_semicolon: return keys::Semicolon;
  case XKB_KEY_apostrophe: return keys::Quote;
  case XKB_KEY_grave: return keys::Grave;
  case XKB_KEY_backslash: return keys::Backslash;
  case XKB_KEY_comma: return keys::Comma;
  case XKB_KEY_period: return keys::Period;
  case XKB_KEY_slash: return keys::Slash;
  default: return keys::Unknown;
  }
}

static std::string utf8FromXkbKeysym(xkb_keysym_t sym) {
  char buffer[8]{};
  int const n = xkb_keysym_to_utf8(sym, buffer, sizeof(buffer));
  if (n <= 1) return {};
  auto const first = static_cast<unsigned char>(buffer[0]);
  if (first < 0x20u || first == 0x7fu) return {};
  return std::string(buffer, static_cast<std::size_t>(n - 1));
}

} // namespace lambda::linux_platform
