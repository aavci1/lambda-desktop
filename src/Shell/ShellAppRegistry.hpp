#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lambda_shell {

struct DesktopEntry {
  std::string id;
  std::string name;
  std::string genericName;
  std::string comment;
  std::string icon;
  std::string exec;
  std::string tryExec;
  bool noDisplay = false;
  bool hidden = false;
  std::vector<std::string> categories;
  std::vector<std::string> keywords;
  std::vector<std::string> mimeTypes;
  std::string startupWmClass;
};

struct AppRegistryEntry {
  std::string appId;
  std::string name;
  std::string icon;
  std::string command;
  bool local = false;
  bool noDisplay = false;
  bool hidden = false;
  std::vector<std::string> keywords;
  std::vector<std::string> mimeTypes;
  std::string startupWmClass;
};

using TryExecResolver = std::function<bool(std::string const& executable)>;

[[nodiscard]] std::optional<DesktopEntry> parseDesktopEntry(std::string_view text,
                                                            std::string id = {});
[[nodiscard]] bool desktopEntryVisible(DesktopEntry const& entry,
                                       TryExecResolver const& tryExecResolver = {});
[[nodiscard]] AppRegistryEntry appEntryFromDesktopEntry(DesktopEntry const& entry);
[[nodiscard]] std::vector<std::string> parseDesktopExec(std::string_view exec,
                                                        std::optional<std::filesystem::path> file = std::nullopt);
[[nodiscard]] bool shellAppIdMatches(std::string_view requested, std::string_view actual);
[[nodiscard]] std::vector<AppRegistryEntry> discoverInstalledDesktopApps(
    std::vector<std::filesystem::path> const& applicationDirs,
    TryExecResolver const& tryExecResolver = {});
[[nodiscard]] std::vector<AppRegistryEntry> discoverLocalExampleApps(std::filesystem::path const& examplesDir,
                                                                     std::vector<std::string> const& appNames);
[[nodiscard]] std::vector<AppRegistryEntry> mergeAppRegistryEntries(std::vector<AppRegistryEntry> installed,
                                                                    std::vector<AppRegistryEntry> local);
[[nodiscard]] std::filesystem::path lookupIconThemePath(std::filesystem::path const& themeRoot,
                                                        std::string const& iconName,
                                                        int preferredSize = 48);

} // namespace lambda_shell
