#pragma once

#include "Shell/ShellAppRegistry.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace lambda_shell {

struct ShellWindowSnapshot {
  std::uint64_t id = 0;
  std::string appId;
  std::string title;
  bool focused = false;
  bool minimized = false;

  bool operator==(ShellWindowSnapshot const&) const = default;
};

struct ShellSystemStatusSnapshot {
  std::string network;
  std::string wifi;
  std::string bluetooth;
  std::string volume;
  std::string battery;

  bool operator==(ShellSystemStatusSnapshot const&) const = default;
};

struct ShellDesktopSnapshot {
  std::vector<AppRegistryEntry> apps;
  std::vector<ShellWindowSnapshot> windows;
  std::uint64_t activeWindowId = 0;
  ShellSystemStatusSnapshot system;

  bool operator==(ShellDesktopSnapshot const&) const = default;
};

struct DockModelEntry {
  std::string appId;
  std::string name;
  std::string icon;
  bool pinned = false;
  bool running = false;
  bool focused = false;
  bool minimized = false;
  std::vector<std::uint64_t> windowIds;

  bool operator==(DockModelEntry const&) const = default;
};

enum class DockClickKind : std::uint8_t {
  None,
  LaunchApp,
  FocusApp,
  RestoreApp,
};

struct DockClickAction {
  DockClickKind kind = DockClickKind::None;
  std::string appId;

  bool operator==(DockClickAction const&) const = default;
};

struct LauncherRankedResult {
  AppRegistryEntry app;
  int score = 0;
  bool running = false;

  bool operator==(LauncherRankedResult const&) const = default;
};

struct SettingsPanelEntry {
  std::string id;
  std::string title;
  std::string subtitle;
  std::string icon;
  std::vector<std::string> keywords;

  bool operator==(SettingsPanelEntry const&) const = default;
};

struct ShellActionEntry {
  std::string id;
  std::string title;
  std::string subtitle;
  std::string icon;
  std::vector<std::string> keywords;

  bool operator==(ShellActionEntry const&) const = default;
};

struct LauncherProviderError {
  std::string providerId;
  std::string message;

  bool operator==(LauncherProviderError const&) const = default;
};

enum class LauncherResultKind : std::uint8_t {
  App,
  Window,
  SettingsPanel,
  ShellAction,
  EmptyState,
  ErrorState,
};

enum class LauncherActionKind : std::uint8_t {
  None,
  LaunchApp,
  FocusApp,
  FocusWindow,
  OpenSettingsPanel,
  RunShellAction,
};

struct LauncherResult {
  LauncherResultKind kind = LauncherResultKind::App;
  std::string id;
  std::string providerId;
  std::string title;
  std::string subtitle;
  std::string icon;
  int score = 0;
  bool running = false;
  bool disabled = false;
  std::uint64_t windowId = 0;

  bool operator==(LauncherResult const&) const = default;
};

struct LauncherAction {
  LauncherActionKind kind = LauncherActionKind::None;
  std::string target;
  std::uint64_t windowId = 0;

  bool operator==(LauncherAction const&) const = default;
};

struct Notification {
  std::uint64_t id = 0;
  std::string appId;
  std::string title;
  std::string body;
  bool dismissed = false;

  bool operator==(Notification const&) const = default;
};

enum class QuickSettingAvailability : std::uint8_t {
  Unavailable,
  Unknown,
  Available,
};

struct QuickSettingState {
  std::string id;
  std::string label;
  QuickSettingAvailability availability = QuickSettingAvailability::Unavailable;
  bool enabled = false;

  bool operator==(QuickSettingState const&) const = default;
};

struct ShellStatusModuleState {
  std::string id;
  std::string label;
  std::string value;
  QuickSettingAvailability availability = QuickSettingAvailability::Unavailable;

  bool operator==(ShellStatusModuleState const&) const = default;
};

struct ShellConfig {
  std::string iconTheme;
  std::string symbolicIconTheme;
  int iconSize = 48;
  bool reducedMotion = false;

  std::string dockPosition = "bottom";
  bool dockAutoHide = false;
  int dockBottomGap = 8;
  int dockCornerRadius = 18;
  std::string dockClockFormat = "%a %d %b, %H:%M";
  bool showRunningUnpinned = true;
  bool dockShowTooltips = true;
  std::vector<std::string> dockPinned{
      "lambda-files", "lambda-editor", "lambda-preview", "lambda-terminal", "lambda-settings", "firefox"};

  std::vector<std::string> quickSettingsModules{"network", "bluetooth", "audio", "battery", "brightness",
                                                "do_not_disturb"};

  bool notificationsEnabled = true;
  bool notificationsDoNotDisturb = false;
  int notificationBannerTimeoutSeconds = 6;
  std::size_t notificationHistoryLimit = 100;
  bool notificationShowPreviews = true;

  bool clipboardHistoryEnabled = true;
  bool clipboardHistoryPersist = false;
  std::size_t clipboardHistoryMaxEntries = 100;
  std::size_t clipboardHistoryMaxTextBytes = 1048576;
  bool clipboardHistoryRecordPrimarySelection = false;

  std::string launcherEmptyQuery = "recommended";
  std::size_t launcherMaxResults = 12;
  bool launcherShowCategories = true;

  bool operator==(ShellConfig const&) const = default;
};

struct ShellConfigLoadResult {
  ShellConfig config;
  std::filesystem::path path;
  std::string error;
  bool created = false;

  bool operator==(ShellConfigLoadResult const&) const = default;
};

enum class ClipboardHistorySource : std::uint8_t {
  Clipboard,
  PrimarySelection,
};

struct ClipboardHistoryPolicy {
  bool enabled = true;
  bool persist = false;
  std::size_t maxEntries = 100;
  std::size_t maxTextBytes = 1048576;
  bool recordPrimarySelection = false;

  bool operator==(ClipboardHistoryPolicy const&) const = default;
};

class NotificationCenterModel {
public:
  explicit NotificationCenterModel(std::size_t historyLimit = 50);

  std::uint64_t add(std::string appId, std::string title, std::string body);
  bool dismiss(std::uint64_t id);
  void clearAll();
  void setDoNotDisturb(bool enabled) noexcept { doNotDisturb_ = enabled; }

  [[nodiscard]] bool doNotDisturb() const noexcept { return doNotDisturb_; }
  [[nodiscard]] std::vector<Notification> visible() const;
  [[nodiscard]] std::vector<Notification> history() const { return notifications_; }
  [[nodiscard]] int groupCount(std::string_view appId) const;

private:
  std::uint64_t nextId_ = 1;
  std::size_t historyLimit_ = 50;
  bool doNotDisturb_ = false;
  std::vector<Notification> notifications_;
};

class ClipboardHistoryModel {
public:
  explicit ClipboardHistoryModel(std::size_t limit = 20);
  explicit ClipboardHistoryModel(ClipboardHistoryPolicy policy);

  void setEnabled(bool enabled) noexcept { policy_.enabled = enabled; }
  void setPolicy(ClipboardHistoryPolicy policy);
  [[nodiscard]] bool enabled() const noexcept { return policy_.enabled; }
  [[nodiscard]] bool persist() const noexcept { return policy_.persist; }
  [[nodiscard]] bool recordPrimarySelection() const noexcept { return policy_.recordPrimarySelection; }
  [[nodiscard]] std::size_t maxTextBytes() const noexcept { return policy_.maxTextBytes; }
  void addText(std::string text, ClipboardHistorySource source = ClipboardHistorySource::Clipboard);
  void clear();
  [[nodiscard]] std::vector<std::string> const& entries() const noexcept { return entries_; }
  [[nodiscard]] std::vector<std::string> entriesForPersistence() const;

private:
  ClipboardHistoryPolicy policy_{.maxEntries = 20};
  std::vector<std::string> entries_;
};

[[nodiscard]] std::vector<DockModelEntry> buildDockModel(std::vector<AppRegistryEntry> const& pinnedApps,
                                                         std::vector<ShellWindowSnapshot> const& windows);
[[nodiscard]] DockClickAction dockClickAction(DockModelEntry const& entry);
[[nodiscard]] std::vector<LauncherRankedResult> rankLauncherApps(std::vector<AppRegistryEntry> const& apps,
                                                                 std::vector<ShellWindowSnapshot> const& windows,
                                                                 std::vector<std::string> const& recentAppIds,
                                                                 std::string_view query,
                                                                 std::size_t limit = 8);
[[nodiscard]] std::vector<LauncherResult> buildLauncherResults(
    std::vector<AppRegistryEntry> const& apps,
    std::vector<ShellWindowSnapshot> const& windows,
    std::vector<SettingsPanelEntry> const& settingsPanels,
    std::vector<ShellActionEntry> const& shellActions,
    std::vector<std::string> const& recentAppIds,
    std::string_view query,
    std::size_t limit = 12,
    std::vector<LauncherProviderError> const& errors = {});
[[nodiscard]] LauncherAction launcherActivationForResult(LauncherResult const& result);
[[nodiscard]] std::vector<QuickSettingState> quickSettingsSummary(std::vector<QuickSettingState> providers);
[[nodiscard]] std::vector<ShellStatusModuleState> shellStatusModules(
    ShellSystemStatusSnapshot const& snapshot,
    std::vector<std::string> const& moduleIds);
[[nodiscard]] ShellDesktopSnapshot parseShellSnapshot(std::string_view json);
[[nodiscard]] ShellConfig defaultShellConfig();
[[nodiscard]] ClipboardHistoryPolicy clipboardHistoryPolicy(ShellConfig const& config);
[[nodiscard]] ShellConfig parseShellConfig(std::string_view tomlText);
[[nodiscard]] std::string writeShellConfigToml(ShellConfig const& config);
[[nodiscard]] std::filesystem::path shellConfigPath();
[[nodiscard]] ShellConfigLoadResult loadShellConfig(std::filesystem::path path = {});

} // namespace lambda_shell
