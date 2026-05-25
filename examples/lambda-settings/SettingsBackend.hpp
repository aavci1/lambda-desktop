#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace lambda_settings {

enum class SettingType {
  Boolean,
  Integer,
  Float,
  Color,
  Enum,
  Path,
  Shortcut,
  String,
};

enum class ApplyMode {
  HotReload,
  NextWindow,
  RestartRequired,
};

struct SettingSchema {
  std::string id;
  std::string label;
  SettingType type = SettingType::String;
  ApplyMode applyMode = ApplyMode::HotReload;
  std::vector<std::string> enumValues;
  std::string defaultValue;

  bool operator==(SettingSchema const&) const = default;
};

struct SettingsDocument {
  std::string originalToml;
  std::map<std::string, std::string> values;
};

class SettingsState {
public:
  explicit SettingsState(std::map<std::string, std::string> defaults = {});

  [[nodiscard]] std::string value(std::string const& id) const;
  [[nodiscard]] bool dirty() const noexcept { return values_ != saved_; }
  void set(std::string id, std::string value);
  void revert();
  void resetToDefaults();
  void markSaved();
  [[nodiscard]] std::map<std::string, std::string> const& values() const noexcept { return values_; }

private:
  std::map<std::string, std::string> defaults_;
  std::map<std::string, std::string> saved_;
  std::map<std::string, std::string> values_;
};

struct SystemInfo {
  std::string kernelName;
  std::string kernelRelease;
  std::string machine;
  long memoryTotalKb = 0;
};

[[nodiscard]] std::vector<SettingSchema> windowManagerSettingsSchema();
[[nodiscard]] std::vector<SettingSchema> shellSettingsSchema();
[[nodiscard]] std::map<std::string, std::string> schemaDefaults(std::vector<SettingSchema> const& schema);
[[nodiscard]] bool schemaIdsUnique(std::vector<SettingSchema> const& schema);

[[nodiscard]] bool validateSettingValue(SettingSchema const& schema, std::string const& value);
[[nodiscard]] bool shortcutConflicts(std::map<std::string, std::string> const& shortcuts);
[[nodiscard]] std::filesystem::path normalizeWallpaperPath(std::filesystem::path const& configPath,
                                                           std::string const& value,
                                                           std::filesystem::path const& home);

[[nodiscard]] SettingsDocument loadWindowManagerSettings(std::string_view tomlText);
[[nodiscard]] std::string writeWindowManagerSettings(std::string_view originalToml,
                                                     std::map<std::string, std::string> const& updates);
[[nodiscard]] SettingsDocument loadShellSettings(std::string_view tomlText);
[[nodiscard]] std::string writeShellSettings(std::string_view originalToml,
                                             std::map<std::string, std::string> const& updates);
[[nodiscard]] bool atomicWriteFile(std::filesystem::path const& path, std::string_view contents, std::string& error);

[[nodiscard]] std::vector<std::string> discoverThemeNames(std::vector<std::filesystem::path> const& roots);
[[nodiscard]] SystemInfo parseSystemInfo(std::string_view unameText, std::string_view meminfoText);

} // namespace lambda_settings
