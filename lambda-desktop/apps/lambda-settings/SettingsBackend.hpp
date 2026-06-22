#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
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

  bool operator==(SettingsDocument const&) const = default;
};

struct SettingsFileLoadResult {
  SettingsDocument document;
  std::filesystem::path path;
  std::string error;
  bool loaded = false;
  bool createdDefault = false;

  bool operator==(SettingsFileLoadResult const&) const = default;
};

struct SettingsFileSaveResult {
  bool ok = false;
  std::filesystem::path path;
  std::string error;

  bool operator==(SettingsFileSaveResult const&) const = default;
};

struct SettingsApplySummary {
  bool hasChanges = false;
  bool hasHotReload = false;
  bool hasNextWindow = false;
  std::vector<std::string> restartRequiredLabels;

  bool operator==(SettingsApplySummary const&) const = default;
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

struct ThemeSelectionStatus {
  std::vector<std::string> available;
  std::string requested;
  std::string effective;
  bool requestedAvailable = false;
  bool missingRequested = false;
  bool usingFallback = false;

  bool operator==(ThemeSelectionStatus const&) const = default;
};

[[nodiscard]] std::vector<SettingSchema> windowManagerSettingsSchema();
[[nodiscard]] std::vector<SettingSchema> shellSettingsSchema();
[[nodiscard]] std::vector<SettingSchema> filesSettingsSchema();
[[nodiscard]] std::map<std::string, std::string> schemaDefaults(std::vector<SettingSchema> const& schema);
[[nodiscard]] bool schemaIdsUnique(std::vector<SettingSchema> const& schema);
[[nodiscard]] SettingsApplySummary summarizeChangedApplyModes(std::map<std::string, std::string> const& saved,
                                                              std::map<std::string, std::string> const& current,
                                                              std::vector<SettingSchema> const& schema);

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
[[nodiscard]] SettingsDocument loadFilesSettings(std::string_view tomlText);
[[nodiscard]] std::string writeFilesSettings(std::string_view originalToml,
                                             std::map<std::string, std::string> const& updates);
[[nodiscard]] bool atomicWriteFile(std::filesystem::path const& path, std::string_view contents, std::string& error);
[[nodiscard]] std::filesystem::path windowManagerSettingsPath();
[[nodiscard]] std::filesystem::path shellSettingsPath();
[[nodiscard]] std::filesystem::path filesSettingsPath();
[[nodiscard]] SettingsFileLoadResult loadWindowManagerSettingsFile(std::filesystem::path path = {});
[[nodiscard]] SettingsFileLoadResult loadShellSettingsFile(std::filesystem::path path = {});
[[nodiscard]] SettingsFileLoadResult loadFilesSettingsFile(std::filesystem::path path = {});
[[nodiscard]] SettingsFileSaveResult saveWindowManagerSettingsFile(std::map<std::string, std::string> const& updates,
                                                                   std::filesystem::path path = {});
[[nodiscard]] SettingsFileSaveResult saveShellSettingsFile(std::map<std::string, std::string> const& updates,
                                                           std::filesystem::path path = {});
[[nodiscard]] SettingsFileSaveResult saveFilesSettingsFile(std::map<std::string, std::string> const& updates,
                                                           std::filesystem::path path = {});

[[nodiscard]] std::vector<std::string> discoverThemeNames(std::vector<std::filesystem::path> const& roots);
[[nodiscard]] ThemeSelectionStatus resolveThemeSelection(std::vector<std::filesystem::path> const& roots,
                                                        std::string requested,
                                                        std::string fallback = {});
[[nodiscard]] SystemInfo parseSystemInfo(std::string_view unameText, std::string_view meminfoText);
[[nodiscard]] std::string formatMemoryTotal(long memoryTotalKb);
[[nodiscard]] std::vector<std::pair<std::string, std::string>> systemInfoRows(SystemInfo const& info);
[[nodiscard]] SystemInfo loadSystemInfo();

} // namespace lambda_settings
