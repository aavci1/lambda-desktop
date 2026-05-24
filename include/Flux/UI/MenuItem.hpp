#pragma once

/// \file Flux/UI/MenuItem.hpp
///
/// Cross-platform application menu model.

#include <Flux/UI/Shortcut.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace flux {

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

  static MenuItem separator() { return MenuItem{.role = MenuRole::Separator}; }

  static MenuItem submenu(std::string label, std::vector<MenuItem> items) {
    return MenuItem{
        .role = MenuRole::Submenu,
        .label = std::move(label),
        .children = std::move(items),
    };
  }

  static MenuItem standard(MenuRole role) { return MenuItem{.role = role}; }

  static MenuItem action(std::string label, std::string actionName, Shortcut shortcut = {}) {
    return MenuItem{
        .label = std::move(label),
        .actionName = std::move(actionName),
        .shortcut = shortcut,
    };
  }
};

struct MenuBar {
  std::vector<MenuItem> menus;
};

struct PopupMenu {
  std::vector<MenuItem> items;
};

} // namespace flux
