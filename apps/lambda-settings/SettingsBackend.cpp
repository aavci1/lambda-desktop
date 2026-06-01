#include "SettingsBackend.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <utility>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/utsname.h>
#endif

namespace lambda_settings {
namespace {

std::string lowerAscii(std::string_view value) {
  std::string out(value);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return out;
}

bool validHexColor(std::string const& value) {
  if (value.size() != 7 && value.size() != 9) return false;
  if (value.front() != '#') return false;
  return std::all_of(value.begin() + 1, value.end(), [](unsigned char ch) {
    return std::isxdigit(ch);
  });
}

std::filesystem::path pathFromEnv(char const* name) {
  if (char const* value = std::getenv(name); value && *value) {
    return std::filesystem::path(value);
  }
  return {};
}

std::filesystem::path configHomeDirectory() {
  if (auto env = pathFromEnv("XDG_CONFIG_HOME"); !env.empty()) return env;
  if (auto home = pathFromEnv("HOME"); !home.empty()) return home / ".config";
  return std::filesystem::current_path();
}

std::map<std::string, std::string> nonEmptyDefaults(std::vector<SettingSchema> const& schema) {
  std::map<std::string, std::string> defaults;
  for (auto const& setting : schema) {
    if (!setting.defaultValue.empty()) defaults[setting.id] = setting.defaultValue;
  }
  return defaults;
}

std::optional<double> parseDouble(std::string const& value) {
  char* end = nullptr;
  double parsed = std::strtod(value.c_str(), &end);
  if (end == value.c_str() || *end != '\0') return std::nullopt;
  return parsed;
}

std::vector<std::string> splitShortcut(std::string const& shortcut) {
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (start <= shortcut.size()) {
    std::size_t end = shortcut.find('+', start);
    std::string part = lowerAscii(shortcut.substr(start, end == std::string::npos ? shortcut.size() - start : end - start));
    if (!part.empty()) parts.push_back(part);
    if (end == std::string::npos) break;
    start = end + 1u;
  }
  std::sort(parts.begin(), parts.end());
  return parts;
}

std::vector<std::string> parseStringListValue(std::string_view value) {
  std::vector<std::string> items;
  std::string trimmed;
  std::size_t begin = 0;
  while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
  std::size_t end = value.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1u]))) --end;
  trimmed = std::string(value.substr(begin, end - begin));
  if (trimmed.empty()) return items;

  if (trimmed.front() == '[') {
    try {
      toml::table table = toml::parse("value = " + trimmed);
      if (auto* array = table["value"].as_array()) {
        for (auto const& item : *array) {
          if (auto string = item.value<std::string>()) items.push_back(*string);
        }
        return items;
      }
    } catch (...) {
      return items;
    }
  }

  std::size_t start = 0;
  while (start <= trimmed.size()) {
    std::size_t comma = trimmed.find(',', start);
    std::string item = trimmed.substr(start, comma == std::string::npos ? trimmed.size() - start : comma - start);
    std::size_t itemBegin = 0;
    while (itemBegin < item.size() && std::isspace(static_cast<unsigned char>(item[itemBegin]))) ++itemBegin;
    std::size_t itemEnd = item.size();
    while (itemEnd > itemBegin && std::isspace(static_cast<unsigned char>(item[itemEnd - 1u]))) --itemEnd;
    item = item.substr(itemBegin, itemEnd - itemBegin);
    if (item.size() >= 2u && item.front() == '"' && item.back() == '"') {
      item = item.substr(1u, item.size() - 2u);
    }
    if (!item.empty()) items.push_back(std::move(item));
    if (comma == std::string::npos) break;
    start = comma + 1u;
  }
  return items;
}

toml::array tomlStringArray(std::vector<std::string> const& values) {
  toml::array array;
  for (auto const& value : values) array.push_back(value);
  return array;
}

toml::table& ensureTomlTable(toml::table& table, std::string const& section) {
  if (auto* existing = table[section].as_table()) return *existing;
  table.insert_or_assign(section, toml::table{});
  return *table[section].as_table();
}

void setTomlValue(toml::table& table, std::string const& key, std::string const& value) {
  if (key == "background" || key == "wallpaper" || key == "output" || key == "cursor_theme") {
    table.insert_or_assign(key, value);
  } else if (key == "scale") {
    table.insert_or_assign(key, std::strtod(value.c_str(), nullptr));
  } else if (key == "animations" || key == "hardware_cursor") {
    table.insert_or_assign(key, lowerAscii(value) == "true" || value == "1");
  } else if (key == "idle_blank_timeout_seconds" || key == "cursor_size") {
    table.insert_or_assign(key, static_cast<std::int64_t>(std::strtoll(value.c_str(), nullptr, 10)));
  } else if (key.starts_with("input.keyboard.")) {
    auto& input = *table.insert("input", toml::table{}).first->second.as_table();
    auto& keyboard = *input.insert("keyboard", toml::table{}).first->second.as_table();
    std::string field = key.substr(std::string("input.keyboard.").size());
    if (field == "repeat_rate" || field == "repeat_delay_ms") {
      keyboard.insert_or_assign(field, static_cast<std::int64_t>(std::strtoll(value.c_str(), nullptr, 10)));
    } else {
      keyboard.insert_or_assign(field, value);
    }
  } else if (key.starts_with("keybindings.")) {
    auto& keybindings = *table.insert("keybindings", toml::table{}).first->second.as_table();
    keybindings.insert_or_assign(key.substr(std::string("keybindings.").size()), value);
  }
}

void setShellTomlValue(toml::table& table, std::string const& key, std::string const& value) {
  auto setString = [&](std::string const& section, std::string const& field) {
    ensureTomlTable(table, section).insert_or_assign(field, value);
  };
  auto setBool = [&](std::string const& section, std::string const& field) {
    ensureTomlTable(table, section).insert_or_assign(field, lowerAscii(value) == "true" || value == "1");
  };
  auto setInt = [&](std::string const& section, std::string const& field) {
    ensureTomlTable(table, section).insert_or_assign(field, static_cast<std::int64_t>(std::strtoll(value.c_str(), nullptr, 10)));
  };
  auto setArray = [&](std::string const& section, std::string const& field) {
    ensureTomlTable(table, section).insert_or_assign(field, tomlStringArray(parseStringListValue(value)));
  };

  if (key == "appearance.icon_theme") setString("appearance", "icon_theme");
  else if (key == "appearance.symbolic_icon_theme") setString("appearance", "symbolic_icon_theme");
  else if (key == "appearance.reduced_motion") setBool("appearance", "reduced_motion");
  else if (key == "dock.position") setString("dock", "position");
  else if (key == "dock.auto_hide") setBool("dock", "auto_hide");
  else if (key == "dock.item_size") setInt("dock", "item_size");
  else if (key == "dock.bottom_gap") setInt("dock", "bottom_gap");
  else if (key == "dock.corner_radius") setInt("dock", "corner_radius");
  else if (key == "dock.clock_format") setString("dock", "clock_format");
  else if (key == "dock.show_running_unpinned") setBool("dock", "show_running_unpinned");
  else if (key == "dock.show_tooltips") setBool("dock", "show_tooltips");
  else if (key == "dock.pinned") setArray("dock", "pinned");
  else if (key == "quick_settings.modules") setArray("quick_settings", "modules");
  else if (key == "notifications.enabled") setBool("notifications", "enabled");
  else if (key == "notifications.do_not_disturb") setBool("notifications", "do_not_disturb");
  else if (key == "notifications.banner_timeout_seconds") setInt("notifications", "banner_timeout_seconds");
  else if (key == "notifications.history_limit") setInt("notifications", "history_limit");
  else if (key == "notifications.show_previews") setBool("notifications", "show_previews");
  else if (key == "clipboard_history.enabled") setBool("clipboard_history", "enabled");
  else if (key == "clipboard_history.persist") setBool("clipboard_history", "persist");
  else if (key == "clipboard_history.max_entries") setInt("clipboard_history", "max_entries");
  else if (key == "clipboard_history.max_text_bytes") setInt("clipboard_history", "max_text_bytes");
  else if (key == "clipboard_history.record_primary_selection") setBool("clipboard_history", "record_primary_selection");
  else if (key == "launcher.empty_query") setString("launcher", "empty_query");
  else if (key == "launcher.max_results") setInt("launcher", "max_results");
  else if (key == "launcher.show_categories") setBool("launcher", "show_categories");
}

std::string tomlString(toml::table const& table, char const* key) {
  if (auto value = table[key].value<std::string>()) return *value;
  return {};
}

std::string tomlStringList(toml::table const& table, char const* key) {
  auto* array = table[key].as_array();
  if (!array) return {};
  std::string output;
  for (auto const& item : *array) {
    auto value = item.value<std::string>();
    if (!value) continue;
    if (!output.empty()) output += ",";
    output += *value;
  }
  return output;
}

std::string tomlNumber(toml::table const& table, char const* key) {
  if (auto value = table[key].value<std::int64_t>()) return std::to_string(*value);
  if (auto value = table[key].value<double>()) return std::to_string(*value);
  return {};
}

std::string tomlBool(toml::table const& table, char const* key) {
  if (auto value = table[key].value<bool>()) return *value ? "true" : "false";
  return {};
}

std::optional<std::string> validationError(std::map<std::string, std::string> const& updates,
                                           std::vector<SettingSchema> const& schema) {
  std::map<std::string, SettingSchema> schemaById;
  for (auto const& setting : schema) {
    schemaById.emplace(setting.id, setting);
  }

  std::map<std::string, std::string> shortcuts;
  for (auto const& [id, value] : updates) {
    auto found = schemaById.find(id);
    if (found == schemaById.end()) continue;
    if (!validateSettingValue(found->second, value)) {
      return "Invalid value for " + found->second.label + ".";
    }
    if (found->second.type == SettingType::Shortcut) {
      shortcuts.emplace(id, value);
    }
  }

  if (shortcutConflicts(shortcuts)) {
    return "Shortcut values must be unique.";
  }
  return std::nullopt;
}

} // namespace

SettingsState::SettingsState(std::map<std::string, std::string> defaults)
    : defaults_(std::move(defaults))
    , saved_(defaults_)
    , values_(defaults_) {}

std::string SettingsState::value(std::string const& id) const {
  auto found = values_.find(id);
  return found == values_.end() ? std::string{} : found->second;
}

void SettingsState::set(std::string id, std::string value) {
  values_[std::move(id)] = std::move(value);
}

void SettingsState::revert() {
  values_ = saved_;
}

void SettingsState::resetToDefaults() {
  values_ = defaults_;
}

void SettingsState::markSaved() {
  saved_ = values_;
}

std::vector<SettingSchema> windowManagerSettingsSchema() {
  return {
      {.id = "background", .label = "Background color", .type = SettingType::Color,
       .applyMode = ApplyMode::HotReload, .defaultValue = "#3380f2"},
      {.id = "wallpaper", .label = "Wallpaper", .type = SettingType::Path,
       .applyMode = ApplyMode::HotReload},
      {.id = "wallpaper_mode", .label = "Wallpaper mode", .type = SettingType::Enum,
       .applyMode = ApplyMode::HotReload, .enumValues = {"cover", "contain", "stretch", "center", "tile"},
       .defaultValue = "cover"},
      {.id = "output", .label = "Output", .type = SettingType::String,
       .applyMode = ApplyMode::RestartRequired},
      {.id = "scale", .label = "Scale", .type = SettingType::Float,
       .applyMode = ApplyMode::HotReload, .defaultValue = "2.0"},
      {.id = "cursor_theme", .label = "Cursor theme", .type = SettingType::String,
       .applyMode = ApplyMode::HotReload},
      {.id = "cursor_size", .label = "Cursor size", .type = SettingType::Integer,
       .applyMode = ApplyMode::HotReload, .defaultValue = "24"},
      {.id = "animations", .label = "Animations", .type = SettingType::Boolean,
       .applyMode = ApplyMode::HotReload, .defaultValue = "true"},
      {.id = "hardware_cursor", .label = "Hardware cursor", .type = SettingType::Boolean,
       .applyMode = ApplyMode::HotReload, .defaultValue = "true"},
      {.id = "idle_blank_timeout_seconds", .label = "Idle blank timeout", .type = SettingType::Integer,
       .applyMode = ApplyMode::HotReload, .defaultValue = "0"},
      {.id = "input.keyboard.layout", .label = "Keyboard layout", .type = SettingType::String,
       .applyMode = ApplyMode::HotReload},
      {.id = "input.keyboard.repeat_rate", .label = "Key repeat rate", .type = SettingType::Integer,
       .applyMode = ApplyMode::HotReload, .defaultValue = "25"},
      {.id = "input.keyboard.repeat_delay_ms", .label = "Key repeat delay", .type = SettingType::Integer,
       .applyMode = ApplyMode::HotReload, .defaultValue = "600"},
      {.id = "keybindings.close", .label = "Close window", .type = SettingType::Shortcut,
       .applyMode = ApplyMode::HotReload, .defaultValue = "super+q"},
      {.id = "keybindings.screenshot", .label = "Full screenshot shortcut", .type = SettingType::Shortcut,
       .applyMode = ApplyMode::HotReload, .defaultValue = "super+shift+3"},
      {.id = "keybindings.screenshot_region", .label = "Region screenshot shortcut", .type = SettingType::Shortcut,
       .applyMode = ApplyMode::HotReload, .defaultValue = "super+shift+4"},
      {.id = "keybindings.screenshot_active_window", .label = "Window screenshot shortcut",
       .type = SettingType::Shortcut, .applyMode = ApplyMode::HotReload, .defaultValue = "super+shift+5"},
  };
}

std::vector<SettingSchema> shellSettingsSchema() {
  return {
      {.id = "appearance.icon_theme", .label = "Icon theme", .type = SettingType::String,
       .applyMode = ApplyMode::HotReload},
      {.id = "appearance.symbolic_icon_theme", .label = "Symbolic icon theme", .type = SettingType::String,
       .applyMode = ApplyMode::HotReload},
      {.id = "appearance.reduced_motion", .label = "Reduced motion", .type = SettingType::Boolean,
       .applyMode = ApplyMode::HotReload, .defaultValue = "false"},
      {.id = "dock.position", .label = "Dock position", .type = SettingType::Enum,
       .applyMode = ApplyMode::HotReload, .enumValues = {"left", "right", "bottom"}, .defaultValue = "bottom"},
      {.id = "dock.auto_hide", .label = "Dock auto-hide", .type = SettingType::Boolean,
       .applyMode = ApplyMode::HotReload, .defaultValue = "false"},
      {.id = "dock.item_size", .label = "Dock item size", .type = SettingType::Integer,
       .applyMode = ApplyMode::HotReload, .defaultValue = "48"},
      {.id = "dock.bottom_gap", .label = "Dock bottom gap", .type = SettingType::Integer,
       .applyMode = ApplyMode::HotReload, .defaultValue = "8"},
      {.id = "dock.corner_radius", .label = "Dock corner radius", .type = SettingType::Integer,
       .applyMode = ApplyMode::HotReload, .defaultValue = "18"},
      {.id = "dock.clock_format", .label = "Clock format", .type = SettingType::String,
       .applyMode = ApplyMode::HotReload, .defaultValue = "%a %d %b, %H:%M"},
      {.id = "dock.show_running_unpinned", .label = "Show running unpinned apps", .type = SettingType::Boolean,
       .applyMode = ApplyMode::HotReload, .defaultValue = "true"},
      {.id = "dock.show_tooltips", .label = "Dock tooltips", .type = SettingType::Boolean,
       .applyMode = ApplyMode::HotReload, .defaultValue = "true"},
      {.id = "dock.pinned", .label = "Pinned apps", .type = SettingType::String,
       .applyMode = ApplyMode::HotReload,
       .defaultValue = "lambda-files,lambda-editor,lambda-preview,lambda-terminal,lambda-settings,firefox"},
      {.id = "quick_settings.modules", .label = "Quick settings modules", .type = SettingType::String,
       .applyMode = ApplyMode::HotReload, .defaultValue = "network,bluetooth,audio,battery,brightness,do_not_disturb"},
      {.id = "notifications.enabled", .label = "Notifications", .type = SettingType::Boolean,
       .applyMode = ApplyMode::HotReload, .defaultValue = "true"},
      {.id = "notifications.do_not_disturb", .label = "Do not disturb", .type = SettingType::Boolean,
       .applyMode = ApplyMode::HotReload, .defaultValue = "false"},
      {.id = "notifications.banner_timeout_seconds", .label = "Notification banner timeout", .type = SettingType::Integer,
       .applyMode = ApplyMode::HotReload, .defaultValue = "6"},
      {.id = "notifications.history_limit", .label = "Notification history limit", .type = SettingType::Integer,
       .applyMode = ApplyMode::HotReload, .defaultValue = "100"},
      {.id = "notifications.show_previews", .label = "Notification previews", .type = SettingType::Boolean,
       .applyMode = ApplyMode::HotReload, .defaultValue = "true"},
      {.id = "clipboard_history.enabled", .label = "Clipboard history", .type = SettingType::Boolean,
       .applyMode = ApplyMode::HotReload, .defaultValue = "true"},
      {.id = "clipboard_history.persist", .label = "Persist clipboard history", .type = SettingType::Boolean,
       .applyMode = ApplyMode::HotReload, .defaultValue = "false"},
      {.id = "clipboard_history.max_entries", .label = "Clipboard history entries", .type = SettingType::Integer,
       .applyMode = ApplyMode::HotReload, .defaultValue = "100"},
      {.id = "clipboard_history.max_text_bytes", .label = "Clipboard max text bytes", .type = SettingType::Integer,
       .applyMode = ApplyMode::HotReload, .defaultValue = "1048576"},
      {.id = "clipboard_history.record_primary_selection", .label = "Record primary selection",
       .type = SettingType::Boolean, .applyMode = ApplyMode::HotReload, .defaultValue = "false"},
      {.id = "launcher.empty_query", .label = "Launcher empty query", .type = SettingType::Enum,
       .applyMode = ApplyMode::HotReload, .enumValues = {"recommended", "apps", "recent"},
       .defaultValue = "recommended"},
      {.id = "launcher.max_results", .label = "Launcher max results", .type = SettingType::Integer,
       .applyMode = ApplyMode::HotReload, .defaultValue = "12"},
      {.id = "launcher.show_categories", .label = "Launcher categories", .type = SettingType::Boolean,
       .applyMode = ApplyMode::HotReload, .defaultValue = "true"},
  };
}

std::map<std::string, std::string> schemaDefaults(std::vector<SettingSchema> const& schema) {
  std::map<std::string, std::string> defaults;
  for (auto const& setting : schema) defaults[setting.id] = setting.defaultValue;
  return defaults;
}

bool schemaIdsUnique(std::vector<SettingSchema> const& schema) {
  std::set<std::string> ids;
  for (auto const& setting : schema) {
    if (setting.id.empty() || !ids.insert(setting.id).second) return false;
  }
  return true;
}

bool validateSettingValue(SettingSchema const& schema, std::string const& value) {
  switch (schema.type) {
  case SettingType::Boolean: {
    std::string lower = lowerAscii(value);
    return lower == "true" || lower == "false" || lower == "1" || lower == "0";
  }
  case SettingType::Integer: {
    auto parsed = parseDouble(value);
    return parsed && std::floor(*parsed) == *parsed;
  }
  case SettingType::Float:
    return parseDouble(value).has_value();
  case SettingType::Color:
    return validHexColor(value);
  case SettingType::Enum:
    return std::find(schema.enumValues.begin(), schema.enumValues.end(), value) != schema.enumValues.end();
  case SettingType::Path:
    return !value.empty();
  case SettingType::Shortcut:
    return splitShortcut(value).size() >= 1u;
  case SettingType::String:
    return true;
  }
  return false;
}

bool shortcutConflicts(std::map<std::string, std::string> const& shortcuts) {
  std::set<std::vector<std::string>> seen;
  for (auto const& [_, shortcut] : shortcuts) {
    auto normalized = splitShortcut(shortcut);
    if (normalized.empty()) continue;
    if (!seen.insert(std::move(normalized)).second) return true;
  }
  return false;
}

std::filesystem::path normalizeWallpaperPath(std::filesystem::path const& configPath,
                                             std::string const& value,
                                             std::filesystem::path const& home) {
  if (value.empty()) return {};
  std::filesystem::path path(value);
  if (value == "~") return home;
  if (value.starts_with("~/")) path = home / value.substr(2u);
  if (path.is_absolute()) return path.lexically_normal();
  return (configPath.parent_path() / path).lexically_normal();
}

SettingsDocument loadWindowManagerSettings(std::string_view tomlText) {
  SettingsDocument document{.originalToml = std::string(tomlText)};
  toml::table table;
  try {
    table = toml::parse(std::string(tomlText));
  } catch (...) {
    return document;
  }
  auto setIf = [&](std::string key, std::string value) {
    if (!value.empty()) document.values[std::move(key)] = std::move(value);
  };
  setIf("background", tomlString(table, "background"));
  setIf("wallpaper", tomlString(table, "wallpaper"));
  setIf("output", tomlString(table, "output"));
  setIf("scale", tomlNumber(table, "scale"));
  setIf("cursor_theme", tomlString(table, "cursor_theme"));
  setIf("cursor_size", tomlNumber(table, "cursor_size"));
  setIf("animations", tomlBool(table, "animations"));
  setIf("hardware_cursor", tomlBool(table, "hardware_cursor"));
  setIf("idle_blank_timeout_seconds", tomlNumber(table, "idle_blank_timeout_seconds"));
  if (auto* input = table["input"].as_table()) {
    if (auto* keyboard = (*input)["keyboard"].as_table()) {
      setIf("input.keyboard.layout", tomlString(*keyboard, "layout"));
      setIf("input.keyboard.repeat_rate", tomlNumber(*keyboard, "repeat_rate"));
      setIf("input.keyboard.repeat_delay_ms", tomlNumber(*keyboard, "repeat_delay_ms"));
    }
  }
  if (auto* keybindings = table["keybindings"].as_table()) {
    setIf("keybindings.close", tomlString(*keybindings, "close"));
    setIf("keybindings.screenshot", tomlString(*keybindings, "screenshot"));
    setIf("keybindings.screenshot_region", tomlString(*keybindings, "screenshot_region"));
    setIf("keybindings.screenshot_active_window", tomlString(*keybindings, "screenshot_active_window"));
  }
  return document;
}

std::string writeWindowManagerSettings(std::string_view originalToml,
                                       std::map<std::string, std::string> const& updates) {
  toml::table table;
  try {
    table = toml::parse(std::string(originalToml));
  } catch (...) {
    table = toml::table{};
  }
  for (auto const& [key, value] : updates) setTomlValue(table, key, value);
  std::ostringstream out;
  out << table;
  return out.str();
}

SettingsDocument loadShellSettings(std::string_view tomlText) {
  SettingsDocument document{.originalToml = std::string(tomlText)};
  toml::table table;
  try {
    table = toml::parse(std::string(tomlText));
  } catch (...) {
    return document;
  }
  auto setIf = [&](std::string key, std::string value) {
    if (!value.empty()) document.values[std::move(key)] = std::move(value);
  };
  if (auto* appearance = table["appearance"].as_table()) {
    setIf("appearance.icon_theme", tomlString(*appearance, "icon_theme"));
    setIf("appearance.symbolic_icon_theme", tomlString(*appearance, "symbolic_icon_theme"));
    setIf("appearance.reduced_motion", tomlBool(*appearance, "reduced_motion"));
  }
  if (auto* dock = table["dock"].as_table()) {
    setIf("dock.position", tomlString(*dock, "position"));
    setIf("dock.auto_hide", tomlBool(*dock, "auto_hide"));
    setIf("dock.item_size", tomlNumber(*dock, "item_size"));
    setIf("dock.bottom_gap", tomlNumber(*dock, "bottom_gap"));
    setIf("dock.corner_radius", tomlNumber(*dock, "corner_radius"));
    setIf("dock.clock_format", tomlString(*dock, "clock_format"));
    setIf("dock.show_running_unpinned", tomlBool(*dock, "show_running_unpinned"));
    setIf("dock.show_tooltips", tomlBool(*dock, "show_tooltips"));
    setIf("dock.pinned", tomlStringList(*dock, "pinned"));
  }
  if (auto* quickSettings = table["quick_settings"].as_table()) {
    setIf("quick_settings.modules", tomlStringList(*quickSettings, "modules"));
  }
  if (auto* notifications = table["notifications"].as_table()) {
    setIf("notifications.enabled", tomlBool(*notifications, "enabled"));
    setIf("notifications.do_not_disturb", tomlBool(*notifications, "do_not_disturb"));
    setIf("notifications.banner_timeout_seconds", tomlNumber(*notifications, "banner_timeout_seconds"));
    setIf("notifications.history_limit", tomlNumber(*notifications, "history_limit"));
    setIf("notifications.show_previews", tomlBool(*notifications, "show_previews"));
  }
  if (auto* clipboard = table["clipboard_history"].as_table()) {
    setIf("clipboard_history.enabled", tomlBool(*clipboard, "enabled"));
    setIf("clipboard_history.persist", tomlBool(*clipboard, "persist"));
    setIf("clipboard_history.max_entries", tomlNumber(*clipboard, "max_entries"));
    setIf("clipboard_history.max_text_bytes", tomlNumber(*clipboard, "max_text_bytes"));
    setIf("clipboard_history.record_primary_selection", tomlBool(*clipboard, "record_primary_selection"));
  }
  if (auto* launcher = table["launcher"].as_table()) {
    setIf("launcher.empty_query", tomlString(*launcher, "empty_query"));
    setIf("launcher.max_results", tomlNumber(*launcher, "max_results"));
    setIf("launcher.show_categories", tomlBool(*launcher, "show_categories"));
  }
  return document;
}

std::string writeShellSettings(std::string_view originalToml,
                               std::map<std::string, std::string> const& updates) {
  toml::table table;
  try {
    table = toml::parse(std::string(originalToml));
  } catch (...) {
    table = toml::table{};
  }
  for (auto const& [key, value] : updates) setShellTomlValue(table, key, value);
  std::ostringstream out;
  out << table;
  return out.str();
}

bool atomicWriteFile(std::filesystem::path const& path, std::string_view contents, std::string& error) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    error = ec.message();
    return false;
  }
  std::filesystem::path temp = path;
  temp += ".tmp";
  {
    std::ofstream file(temp, std::ios::binary);
    if (!file) {
      error = "Could not open temporary file.";
      return false;
    }
    file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!file) {
      error = "Could not write temporary file.";
      return false;
    }
  }
  std::filesystem::rename(temp, path, ec);
  if (ec) {
    error = ec.message();
    std::filesystem::remove(temp, ec);
    return false;
  }
  return true;
}

std::filesystem::path windowManagerSettingsPath() {
  if (auto env = pathFromEnv("LAMBDA_WINDOW_MANAGER_CONFIG"); !env.empty()) return env;
  return configHomeDirectory() / "lambda-window-manager" / "config.toml";
}

std::filesystem::path shellSettingsPath() {
  if (auto env = pathFromEnv("LAMBDA_SHELL_CONFIG"); !env.empty()) return env;
  return configHomeDirectory() / "lambda-shell" / "config.toml";
}

SettingsFileLoadResult loadWindowManagerSettingsFile(std::filesystem::path path) {
  if (path.empty()) path = windowManagerSettingsPath();
  SettingsFileLoadResult result{.path = path};
  std::error_code ec;
  if (!std::filesystem::exists(path, ec) || ec) {
    std::string const defaults = writeWindowManagerSettings("", nonEmptyDefaults(windowManagerSettingsSchema()));
    if (!atomicWriteFile(path, defaults, result.error)) return result;
    result.createdDefault = true;
    result.document = loadWindowManagerSettings(defaults);
    return result;
  }

  std::ifstream in(path);
  if (!in) {
    result.error = "Could not read settings file.";
    return result;
  }
  std::string const contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  result.document = loadWindowManagerSettings(contents);
  result.loaded = true;
  return result;
}

SettingsFileLoadResult loadShellSettingsFile(std::filesystem::path path) {
  if (path.empty()) path = shellSettingsPath();
  SettingsFileLoadResult result{.path = path};
  std::error_code ec;
  if (!std::filesystem::exists(path, ec) || ec) {
    std::string const defaults = writeShellSettings("", nonEmptyDefaults(shellSettingsSchema()));
    if (!atomicWriteFile(path, defaults, result.error)) return result;
    result.createdDefault = true;
    result.document = loadShellSettings(defaults);
    return result;
  }

  std::ifstream in(path);
  if (!in) {
    result.error = "Could not read settings file.";
    return result;
  }
  std::string const contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  result.document = loadShellSettings(contents);
  result.loaded = true;
  return result;
}

SettingsFileSaveResult saveWindowManagerSettingsFile(std::map<std::string, std::string> const& updates,
                                                     std::filesystem::path path) {
  if (path.empty()) path = windowManagerSettingsPath();
  if (auto error = validationError(updates, windowManagerSettingsSchema())) {
    return {.path = path, .error = *error};
  }
  SettingsFileLoadResult loaded = loadWindowManagerSettingsFile(path);
  if (!loaded.error.empty()) return {.path = path, .error = loaded.error};
  std::string error;
  std::string const contents = writeWindowManagerSettings(loaded.document.originalToml, updates);
  if (!atomicWriteFile(path, contents, error)) return {.path = path, .error = error};
  return {.ok = true, .path = path};
}

SettingsFileSaveResult saveShellSettingsFile(std::map<std::string, std::string> const& updates,
                                             std::filesystem::path path) {
  if (path.empty()) path = shellSettingsPath();
  if (auto error = validationError(updates, shellSettingsSchema())) {
    return {.path = path, .error = *error};
  }
  SettingsFileLoadResult loaded = loadShellSettingsFile(path);
  if (!loaded.error.empty()) return {.path = path, .error = loaded.error};
  std::string error;
  std::string const contents = writeShellSettings(loaded.document.originalToml, updates);
  if (!atomicWriteFile(path, contents, error)) return {.path = path, .error = error};
  return {.ok = true, .path = path};
}

std::vector<std::string> discoverThemeNames(std::vector<std::filesystem::path> const& roots) {
  std::set<std::string> names;
  for (auto const& root : roots) {
    std::error_code ec;
    for (auto const& entry : std::filesystem::directory_iterator(root, ec)) {
      if (ec) break;
      if (entry.is_directory(ec) && !ec) names.insert(entry.path().filename().string());
    }
  }
  return {names.begin(), names.end()};
}

ThemeSelectionStatus resolveThemeSelection(std::vector<std::filesystem::path> const& roots,
                                           std::string requested,
                                           std::string fallback) {
  ThemeSelectionStatus status;
  status.available = discoverThemeNames(roots);
  status.requested = std::move(requested);
  auto contains = [&](std::string const& name) {
    return std::find(status.available.begin(), status.available.end(), name) != status.available.end();
  };

  if (!status.requested.empty() && contains(status.requested)) {
    status.effective = status.requested;
    status.requestedAvailable = true;
    return status;
  }

  status.missingRequested = !status.requested.empty();
  if (!fallback.empty() && contains(fallback)) {
    status.effective = std::move(fallback);
    status.usingFallback = true;
  } else {
    status.usingFallback = status.missingRequested && !fallback.empty();
  }
  return status;
}

SystemInfo parseSystemInfo(std::string_view unameText, std::string_view meminfoText) {
  SystemInfo info;
  std::istringstream uname{std::string(unameText)};
  uname >> info.kernelName >> info.kernelRelease >> info.machine;
  std::istringstream meminfo{std::string(meminfoText)};
  std::string key;
  long value = 0;
  std::string unit;
  while (meminfo >> key >> value >> unit) {
    if (key == "MemTotal:") {
      info.memoryTotalKb = value;
      break;
    }
  }
  return info;
}

std::string formatMemoryTotal(long memoryTotalKb) {
  if (memoryTotalKb <= 0) return "Unavailable";
  double const gib = static_cast<double>(memoryTotalKb) / 1024.0 / 1024.0;
  std::ostringstream out;
  out.setf(std::ios::fixed);
  out.precision(gib >= 10.0 ? 0 : 1);
  out << gib << " GiB";
  return out.str();
}

std::vector<std::pair<std::string, std::string>> systemInfoRows(SystemInfo const& info) {
  auto valueOrUnavailable = [](std::string value) {
    return value.empty() ? std::string{"Unavailable"} : std::move(value);
  };
  std::vector<std::pair<std::string, std::string>> rows;
  rows.emplace_back("Kernel", valueOrUnavailable(info.kernelName + (info.kernelRelease.empty() ? "" : " " + info.kernelRelease)));
  rows.emplace_back("Architecture", valueOrUnavailable(info.machine));
  rows.emplace_back("Memory", formatMemoryTotal(info.memoryTotalKb));
  rows.emplace_back("Processor", "Unavailable");
  rows.emplace_back("Storage", "Unavailable");
  rows.emplace_back("Graphics", "Unavailable");
  rows.emplace_back("Display", "Unavailable");
  return rows;
}

SystemInfo loadSystemInfo() {
  std::string unameText;
#if defined(__unix__) || defined(__APPLE__)
  utsname name{};
  if (uname(&name) == 0) {
    unameText = std::string{name.sysname} + " " + name.release + " " + name.machine;
  }
#endif

  std::string meminfoText;
  std::ifstream meminfo("/proc/meminfo");
  if (meminfo) {
    std::ostringstream contents;
    contents << meminfo.rdbuf();
    meminfoText = contents.str();
  }
  return parseSystemInfo(unameText, meminfoText);
}

} // namespace lambda_settings
