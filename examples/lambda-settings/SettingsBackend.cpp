#include "SettingsBackend.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

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

void setTomlValue(toml::table& table, std::string const& key, std::string const& value) {
  if (key == "background" || key == "wallpaper" || key == "output" || key == "cursor_theme") {
    table.insert_or_assign(key, value);
  } else if (key == "scale") {
    table.insert_or_assign(key, std::strtod(value.c_str(), nullptr));
  } else if (key == "animations" || key == "hardware_cursor" || key == "window_glass") {
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

std::string tomlString(toml::table const& table, char const* key) {
  if (auto value = table[key].value<std::string>()) return *value;
  return {};
}

std::string tomlNumber(toml::table const& table, char const* key) {
  if (auto value = table[key].value<double>()) return std::to_string(*value);
  if (auto value = table[key].value<std::int64_t>()) return std::to_string(*value);
  return {};
}

std::string tomlBool(toml::table const& table, char const* key) {
  if (auto value = table[key].value<bool>()) return *value ? "true" : "false";
  return {};
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
      {.id = "window_glass", .label = "Default window glass", .type = SettingType::Boolean,
       .applyMode = ApplyMode::HotReload, .defaultValue = "true"},
      {.id = "input.keyboard.layout", .label = "Keyboard layout", .type = SettingType::String,
       .applyMode = ApplyMode::HotReload},
      {.id = "input.keyboard.repeat_rate", .label = "Key repeat rate", .type = SettingType::Integer,
       .applyMode = ApplyMode::HotReload, .defaultValue = "25"},
      {.id = "input.keyboard.repeat_delay_ms", .label = "Key repeat delay", .type = SettingType::Integer,
       .applyMode = ApplyMode::HotReload, .defaultValue = "600"},
      {.id = "keybindings.close", .label = "Close window", .type = SettingType::Shortcut,
       .applyMode = ApplyMode::HotReload, .defaultValue = "super+q"},
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
  setIf("window_glass", tomlBool(table, "window_glass"));
  if (auto* input = table["input"].as_table()) {
    if (auto* keyboard = (*input)["keyboard"].as_table()) {
      setIf("input.keyboard.layout", tomlString(*keyboard, "layout"));
      setIf("input.keyboard.repeat_rate", tomlNumber(*keyboard, "repeat_rate"));
      setIf("input.keyboard.repeat_delay_ms", tomlNumber(*keyboard, "repeat_delay_ms"));
    }
  }
  if (auto* keybindings = table["keybindings"].as_table()) {
    setIf("keybindings.close", tomlString(*keybindings, "close"));
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

} // namespace lambda_settings
