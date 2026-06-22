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

  bool operator==(AppRegistryEntry const&) const = default;
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
[[nodiscard]] bool executableInPath(std::string const& executable);
[[nodiscard]] std::vector<std::filesystem::path> defaultXdgApplicationDirs();
[[nodiscard]] std::vector<std::filesystem::path> defaultLocalLambdaAppDirs();
[[nodiscard]] std::vector<std::string> defaultLocalLambdaAppNames();
[[nodiscard]] std::vector<AppRegistryEntry> discoverInstalledDesktopApps(
    std::vector<std::filesystem::path> const& applicationDirs,
    TryExecResolver const& tryExecResolver = {});
[[nodiscard]] std::vector<AppRegistryEntry> discoverLocalLambdaApps(
    std::vector<std::filesystem::path> const& appDirs,
    std::vector<std::string> const& appNames);
[[nodiscard]] std::vector<AppRegistryEntry> mergeAppRegistryEntries(std::vector<AppRegistryEntry> installed,
                                                                    std::vector<AppRegistryEntry> local);
[[nodiscard]] std::vector<AppRegistryEntry> builtinFallbackAppEntries();
[[nodiscard]] std::vector<AppRegistryEntry> buildDefaultAppRegistry(
    std::vector<std::filesystem::path> const& appDirs,
    std::vector<std::filesystem::path> const& applicationDirs,
    TryExecResolver const& tryExecResolver = {});
[[nodiscard]] std::optional<std::string> resolveAppLaunchCommand(std::string_view requestedAppId,
                                                                 std::vector<AppRegistryEntry> const& apps);
[[nodiscard]] std::filesystem::path lookupIconThemePath(std::filesystem::path const& themeRoot,
                                                        std::string const& iconName,
                                                        int preferredSize = 48);
[[nodiscard]] std::string configuredIconThemeName();
[[nodiscard]] std::vector<std::filesystem::path> defaultIconThemeRoots(std::string const& themeName);
[[nodiscard]] std::filesystem::path resolveIconThemePath(std::string const& iconName,
                                                         std::string const& themeName,
                                                         int preferredSize = 48);

} // namespace lambda_shell
