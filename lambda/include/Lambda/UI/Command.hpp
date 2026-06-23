#pragma once

/// \file Lambda/UI/Command.hpp
///
/// Part of the Lambda public API.


#include <Lambda/UI/Shortcut.hpp>
#include <Lambda/UI/IconName.hpp>

#include <functional>
#include <optional>
#include <string>

namespace lambdaui {

struct CommandDescriptor {
  /// Stable, namespaced command id. If empty, the registration key is used.
  std::string id{};

  /// Human-readable command title, e.g. "Copy", "Save File".
  std::string title{};

  /// Legacy title field retained while older call sites migrate to `title`.
  std::string label{};

  /// Optional detail text for command palette rows and future command discovery UI.
  std::string description{};

  /// Optional group/category, e.g. "File", "Edit", "View".
  std::string category{};

  /// Optional icon for toolbar/palette presentation.
  std::optional<IconName> icon{};

  /// Keyboard shortcut that triggers this command. Optional — a command can exist
  /// without a shortcut and still be triggered programmatically or from palette UI.
  Shortcut shortcut{};

  /// Whether this command should be shown in command palette/discovery UI.
  bool paletteVisible = true;

  /// When set, called every rebuild to determine if the command is currently available.
  /// Return false to suppress both shortcut dispatch and visual enabled state.
  /// When not set the command is always considered enabled.
  std::function<bool()> isEnabled{};

  [[nodiscard]] std::string displayTitle() const {
    if (!title.empty()) return title;
    return label;
  }
};

using ActionDescriptor = CommandDescriptor;

} // namespace lambdaui
