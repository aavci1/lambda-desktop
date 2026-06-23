#pragma once

/// \file Lambda/UI/MenuItem.hpp
///
/// Cross-platform application menu model.

#include <Lambda/UI/Shortcut.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace lambdaui {

enum class MenuRole : std::uint8_t {
  None,
  AppAbout,
  AppPreferences,
  AppHide,
  AppHideOthers,
  AppShowAll,
  AppQuit,
  EditUndo,
  EditRedo,
  EditCut,
  EditCopy,
  EditPaste,
  EditDelete,
  EditSelectAll,
  WindowMinimize,
  WindowZoom,
  WindowFullscreen,
  WindowBringAllToFront,
  HelpSearch,
  Separator,
  Submenu,
};

struct MenuItem {
  MenuRole role = MenuRole::None;
  std::string label;
  std::string actionName;
  std::function<void()> handler;
  Shortcut shortcut;
  std::vector<MenuItem> children;
  std::function<bool()> isEnabled;
  bool checked = false;

  static MenuItem separator() {
    MenuItem item;
    item.role = MenuRole::Separator;
    return item;
  }

  static MenuItem submenu(std::string label, std::vector<MenuItem> items) {
    MenuItem item;
    item.role = MenuRole::Submenu;
    item.label = std::move(label);
    item.children = std::move(items);
    return item;
  }

  static MenuItem standard(MenuRole role) {
    MenuItem item;
    item.role = role;
    return item;
  }

  static MenuItem action(std::string label, std::string actionName, Shortcut shortcut = {}) {
    MenuItem item;
    item.label = std::move(label);
    item.actionName = std::move(actionName);
    item.shortcut = shortcut;
    return item;
  }
};

struct MenuBar {
  std::vector<MenuItem> menus;
};

struct PopupMenu {
  std::vector<MenuItem> items;
};

} // namespace lambdaui
