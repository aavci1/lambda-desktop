#include "UI/MenuRoleDefaults.hpp"

#include <Lambda/UI/KeyCodes.hpp>

namespace lambda::detail {

std::string standardRoleActionName(MenuRole role) {
  switch (role) {
  case MenuRole::AppPreferences: return "settings";
  case MenuRole::AppQuit: return "app.quit";
  case MenuRole::EditUndo: return "undo";
  case MenuRole::EditRedo: return "redo";
  case MenuRole::EditCut: return "cut";
  case MenuRole::EditCopy: return "copy";
  case MenuRole::EditPaste: return "paste";
  case MenuRole::EditDelete: return "delete";
  case MenuRole::EditSelectAll: return "select-all";
  default: return {};
  }
}

Shortcut standardRoleShortcut(MenuRole role) {
  switch (role) {
  case MenuRole::AppPreferences: return Shortcut{keys::Comma, Modifiers::Meta};
  case MenuRole::AppQuit: return shortcuts::Quit;
  case MenuRole::EditUndo: return shortcuts::Undo;
  case MenuRole::EditRedo: return shortcuts::Redo;
  case MenuRole::EditCut: return shortcuts::Cut;
  case MenuRole::EditCopy: return shortcuts::Copy;
  case MenuRole::EditPaste: return shortcuts::Paste;
  case MenuRole::EditSelectAll: return shortcuts::SelectAll;
  default: return {};
  }
}

} // namespace lambda::detail
