#include "Shell/ShellModels.hpp"
#include "Shell/UI/LambdaShellTypes.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace {

std::filesystem::path tempRoot(char const* name) {
  auto path = std::filesystem::temp_directory_path() /
              (std::string(name) + "-" + std::to_string(static_cast<unsigned long long>(getpid())));
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path);
  return path;
}

} // namespace

TEST_CASE("Shell dock model combines pinned apps and running windows") {
  std::vector<lambda_shell::AppRegistryEntry> pinned{
      {.appId = "files", .name = "Files", .icon = "files"},
      {.appId = "terminal", .name = "Terminal", .icon = "terminal"},
  };
  std::vector<lambda_shell::ShellWindowSnapshot> windows{
      {.id = 10, .appId = "lambda-files", .title = "Files", .focused = true},
      {.id = 11, .appId = "org.example.Editor", .title = "Editor"},
      {.id = 12, .appId = "lambda-terminal", .title = "Terminal", .minimized = true},
  };

  auto dock = lambda_shell::buildDockModel(pinned, windows);
  REQUIRE(dock.size() == 3);
  CHECK(dock[0].appId == "files");
  CHECK(dock[0].running);
  CHECK(dock[0].focused);
  CHECK(dock[0].windowIds == std::vector<std::uint64_t>{10});
  CHECK(dock[1].appId == "terminal");
  CHECK(dock[1].minimized);
  CHECK(dock[2].appId == "org.example.Editor");
  CHECK_FALSE(dock[2].pinned);
}

TEST_CASE("Shell dock model ignores placeholder window identities") {
  std::vector<lambda_shell::AppRegistryEntry> pinned{
      {.appId = "terminal", .name = "Terminal", .icon = "terminal"},
  };
  std::vector<lambda_shell::ShellWindowSnapshot> windows{
      {.id = 21, .appId = "unknown", .title = "unknown"},
      {.id = 22, .appId = "toggle-demo", .title = "Lambda - Toggle demo", .focused = true},
  };

  auto dock = lambda_shell::buildDockModel(pinned, windows);
  REQUIRE(dock.size() == 2);
  CHECK(dock[0].appId == "terminal");
  CHECK(dock[1].appId == "toggle-demo");
  CHECK(dock[1].focused);
}

TEST_CASE("Shell dock click behavior launches focuses or restores") {
  CHECK(lambda_shell::dockClickAction({.appId = "files"}).kind == lambda_shell::DockClickKind::LaunchApp);
  CHECK(lambda_shell::dockClickAction({.appId = "files", .running = true}).kind ==
        lambda_shell::DockClickKind::FocusApp);
  CHECK(lambda_shell::dockClickAction({.appId = "files", .running = true, .minimized = true, .windowIds = {1}}).kind ==
        lambda_shell::DockClickKind::RestoreApp);
}

TEST_CASE("Shell launcher ranking handles prefix acronym fuzzy running and recent boosts") {
  std::vector<lambda_shell::AppRegistryEntry> apps{
      {.appId = "lambda-files", .name = "Files", .keywords = {"folder"}},
      {.appId = "lambda-settings", .name = "System Settings", .keywords = {"preferences"}},
      {.appId = "lambda-terminal", .name = "Terminal"},
      {.appId = "org.example.Hidden", .name = "Hidden", .hidden = true},
  };
  std::vector<lambda_shell::ShellWindowSnapshot> windows{{.id = 1, .appId = "lambda-terminal"}};
  auto prefix = lambda_shell::rankLauncherApps(apps, windows, {}, "fi");
  REQUIRE(prefix.size() == 1);
  CHECK(prefix[0].app.appId == "lambda-files");

  auto acronym = lambda_shell::rankLauncherApps(apps, windows, {}, "ss");
  REQUIRE(acronym.size() == 1);
  CHECK(acronym[0].app.appId == "lambda-settings");

  auto running = lambda_shell::rankLauncherApps(apps, windows, {"lambda-files"}, "ter");
  REQUIRE(running.size() == 1);
  CHECK(running[0].app.appId == "lambda-terminal");
  CHECK(running[0].running);

  auto empty = lambda_shell::rankLauncherApps(apps, windows, {"lambda-files"}, "");
  REQUIRE(empty.size() == 3);
  CHECK(empty[0].app.appId == "lambda-terminal");
  CHECK(empty[1].app.appId == "lambda-files");
}

TEST_CASE("Shell launcher merges app window settings and shell action providers") {
  std::vector<lambda_shell::AppRegistryEntry> apps{
      {.appId = "lambda-terminal", .name = "Terminal", .icon = "terminal"},
      {.appId = "org.example.Editor", .name = "Editor", .icon = "editor", .keywords = {"text"}},
  };
  std::vector<lambda_shell::ShellWindowSnapshot> windows{
      {.id = 7, .appId = "org.example.Editor", .title = "Editor - notes.txt"},
      {.id = 8, .appId = "lambda-terminal", .title = "Terminal", .focused = true},
  };
  std::vector<lambda_shell::SettingsPanelEntry> settings{
      {.id = "network", .title = "Network", .subtitle = "Wi-Fi and Ethernet", .icon = "network",
       .keywords = {"wifi", "internet"}},
  };
  std::vector<lambda_shell::ShellActionEntry> actions{
      {.id = "toggle-do-not-disturb", .title = "Toggle Do Not Disturb", .icon = "notifications",
       .keywords = {"notifications", "dnd"}},
  };

  auto terminal = lambda_shell::buildLauncherResults(apps, windows, settings, actions, {}, "term");
  REQUIRE_FALSE(terminal.empty());
  CHECK(terminal[0].kind == lambda_shell::LauncherResultKind::App);
  CHECK(terminal[0].id == "lambda-terminal");
  CHECK(terminal[0].running);
  CHECK(lambda_shell::launcherActivationForResult(terminal[0]) ==
        lambda_shell::LauncherAction{.kind = lambda_shell::LauncherActionKind::FocusApp,
                                     .target = "lambda-terminal"});

  auto editor = lambda_shell::buildLauncherResults(apps, windows, settings, actions, {}, "notes");
  REQUIRE_FALSE(editor.empty());
  CHECK(editor[0].kind == lambda_shell::LauncherResultKind::Window);
  CHECK(editor[0].windowId == 7);
  CHECK(lambda_shell::launcherActivationForResult(editor[0]) ==
        lambda_shell::LauncherAction{.kind = lambda_shell::LauncherActionKind::FocusWindow,
                                     .target = "7",
                                     .windowId = 7});

  auto network = lambda_shell::buildLauncherResults(apps, windows, settings, actions, {}, "wifi");
  REQUIRE(network.size() == 1);
  CHECK(network[0].kind == lambda_shell::LauncherResultKind::SettingsPanel);
  CHECK(lambda_shell::launcherActivationForResult(network[0]).kind ==
        lambda_shell::LauncherActionKind::OpenSettingsPanel);

  auto dnd = lambda_shell::buildLauncherResults(apps, windows, settings, actions, {}, "dnd");
  REQUIRE(dnd.size() == 1);
  CHECK(dnd[0].kind == lambda_shell::LauncherResultKind::ShellAction);
  CHECK(lambda_shell::launcherActivationForResult(dnd[0]).kind ==
        lambda_shell::LauncherActionKind::RunShellAction);
}

TEST_CASE("Shell launcher returns deterministic empty and provider error states") {
  auto empty = lambda_shell::buildLauncherResults({}, {}, {}, {}, {}, "", 12);
  REQUIRE(empty.size() == 1);
  CHECK(empty[0].kind == lambda_shell::LauncherResultKind::EmptyState);
  CHECK(empty[0].disabled);
  CHECK(lambda_shell::launcherActivationForResult(empty[0]).kind == lambda_shell::LauncherActionKind::None);

  auto noResults = lambda_shell::buildLauncherResults({}, {}, {}, {}, {}, "zzzz", 12);
  REQUIRE(noResults.size() == 1);
  CHECK(noResults[0].id == "launcher-no-results");

  auto errors = lambda_shell::buildLauncherResults({}, {}, {}, {}, {}, "anything", 12, {
      {.providerId = "apps", .message = "desktop files unavailable"},
  });
  REQUIRE(errors.size() == 1);
  CHECK(errors[0].kind == lambda_shell::LauncherResultKind::ErrorState);
  CHECK(errors[0].providerId == "apps");
  CHECK(errors[0].subtitle == "desktop files unavailable");
}

TEST_CASE("Shell notification model groups dismisses clears and honors DND") {
  lambda_shell::NotificationCenterModel notifications{2};
  auto first = notifications.add("files", "Done", "Copied");
  auto second = notifications.add("files", "Done", "Moved");
  auto third = notifications.add("terminal", "Build", "Failed");

  CHECK(first == 1);
  CHECK(second == 2);
  CHECK(third == 3);
  CHECK(notifications.history().size() == 2);
  CHECK(notifications.groupCount("files") == 1);
  CHECK(notifications.visible().size() == 2);

  CHECK(notifications.dismiss(second));
  CHECK(notifications.groupCount("files") == 0);
  notifications.setDoNotDisturb(true);
  CHECK(notifications.visible().empty());
  notifications.setDoNotDisturb(false);
  notifications.clearAll();
  CHECK(notifications.visible().empty());
}

TEST_CASE("Shell clipboard history dedupes respects limits and disabled state") {
  lambda_shell::ClipboardHistoryModel clipboard{3};
  clipboard.addText("one");
  clipboard.addText("two");
  clipboard.addText("three");
  clipboard.addText("four");
  CHECK(clipboard.entries() == std::vector<std::string>{"four", "three", "two"});

  clipboard.addText("three");
  CHECK(clipboard.entries() == std::vector<std::string>{"three", "four", "two"});

  clipboard.setEnabled(false);
  clipboard.addText("ignored");
  CHECK(clipboard.entries() == std::vector<std::string>{"three", "four", "two"});

  clipboard.clear();
  CHECK(clipboard.entries().empty());
}

TEST_CASE("Shell clipboard history applies privacy and persistence policy") {
  lambda_shell::ShellConfig config = lambda_shell::defaultShellConfig();
  config.clipboardHistoryMaxEntries = 2;
  config.clipboardHistoryMaxTextBytes = 4;
  config.clipboardHistoryPersist = false;
  config.clipboardHistoryRecordPrimarySelection = false;

  lambda_shell::ClipboardHistoryModel clipboard{lambda_shell::clipboardHistoryPolicy(config)};
  clipboard.addText("keep");
  clipboard.addText("large");
  clipboard.addText("primary", lambda_shell::ClipboardHistorySource::PrimarySelection);
  CHECK(clipboard.entries() == std::vector<std::string>{"keep"});
  CHECK(clipboard.entriesForPersistence().empty());

  config.clipboardHistoryPersist = true;
  config.clipboardHistoryRecordPrimarySelection = true;
  config.clipboardHistoryMaxTextBytes = 16;
  clipboard.setPolicy(lambda_shell::clipboardHistoryPolicy(config));
  clipboard.addText("primary", lambda_shell::ClipboardHistorySource::PrimarySelection);
  clipboard.addText("next");
  CHECK(clipboard.entries() == std::vector<std::string>{"next", "primary"});
  CHECK(clipboard.entriesForPersistence() == clipboard.entries());

  config.clipboardHistoryEnabled = false;
  clipboard.setPolicy(lambda_shell::clipboardHistoryPolicy(config));
  clipboard.addText("ignored");
  CHECK(clipboard.entries() == std::vector<std::string>{"next", "primary"});
  CHECK(clipboard.entriesForPersistence().empty());
}

TEST_CASE("Shell quick settings summary orders available providers first") {
  std::vector<lambda_shell::QuickSettingState> providers{
      {.id = "bluetooth", .label = "Bluetooth"},
      {.id = "battery", .label = "Battery", .availability = lambda_shell::QuickSettingAvailability::Unknown},
      {.id = "wifi", .label = "Wi-Fi", .availability = lambda_shell::QuickSettingAvailability::Available,
       .enabled = true},
      {.id = "audio", .label = "Audio", .availability = lambda_shell::QuickSettingAvailability::Available},
  };
  auto summary = lambda_shell::quickSettingsSummary(std::move(providers));
  REQUIRE(summary.size() == 4);
  CHECK(summary[0].id == "audio");
  CHECK(summary[1].id == "wifi");
  CHECK(summary[2].id == "battery");
  CHECK(summary[3].id == "bluetooth");
}

TEST_CASE("Shell status modules classify available unknown and unavailable values") {
  lambda_shell::ShellSystemStatusSnapshot snapshot{
      .network = "online",
      .wifi = "unknown",
      .bluetooth = "unavailable",
      .volume = "44%",
  };

  auto modules = lambda_shell::shellStatusModules(snapshot, {
      "network",
      "wifi",
      "bluetooth",
      "volume",
      "battery",
      "notifications",
      "clock",
      "custom",
  });
  REQUIRE(modules.size() == 8);
  CHECK(modules[0].availability == lambda_shell::QuickSettingAvailability::Available);
  CHECK(modules[1].availability == lambda_shell::QuickSettingAvailability::Unknown);
  CHECK(modules[2].availability == lambda_shell::QuickSettingAvailability::Unavailable);
  CHECK(modules[3].value == "44%");
  CHECK(modules[4].availability == lambda_shell::QuickSettingAvailability::Unavailable);
  CHECK(modules[5].availability == lambda_shell::QuickSettingAvailability::Available);
  CHECK(modules[6].availability == lambda_shell::QuickSettingAvailability::Available);
  CHECK(modules[7].label == "custom");
}

TEST_CASE("Shell docklet status items expose real and unavailable states") {
  auto unavailable = lambda_shell::dockletStatusItems({});
  REQUIRE(unavailable.size() == 4);
  CHECK(unavailable[0].id == "network");
  CHECK(unavailable[0].availability == lambda_shell::StatusAvailability::Unavailable);
  CHECK(unavailable[1].id == "bluetooth");
  CHECK(unavailable[1].availability == lambda_shell::StatusAvailability::Unavailable);

  lambda_shell::SystemStatus status{
      .network = "online",
      .wifi = "Lambda",
      .bluetooth = "off",
      .volume = "55%",
      .battery = "88%",
  };
  auto items = lambda_shell::dockletStatusItems(status);
  REQUIRE(items.size() == 4);
  CHECK(items[0].id == "network");
  CHECK(items[0].label == "Lambda");
  CHECK(items[0].availability == lambda_shell::StatusAvailability::Available);
  CHECK(items[0].active);
  CHECK(items[1].id == "bluetooth");
  CHECK(items[1].availability == lambda_shell::StatusAvailability::Available);
  CHECK_FALSE(items[1].active);
  CHECK(items[2].label == "55%");
  CHECK(items[3].label == "88%");
}

TEST_CASE("Shell snapshot parser handles reordered fields and escaped strings") {
  auto snapshot = lambda_shell::parseShellSnapshot(R"({
    "type":"lambda.windowManager.snapshot",
    "windows":[
      {"focused":false,"title":"Editor \"One\"","state":"normal","appId":"org.example.Editor","id":9},
      {"state":"minimized","id":11,"appId":"lambda-terminal","title":"Terminal","focused":true}
    ],
    "apps":[
      {"command":"editor %f","name":"Editor","id":"org.example.Editor","icon":"editor"},
      {"name":"Terminal","id":"lambda-terminal","command":"lambda-terminal"}
    ],
    "activeWindowId":11,
    "system":{"battery":"95%","volume":"44%","bluetooth":"off","wifi":"Home","network":"online"}
  })");

  REQUIRE(snapshot.apps.size() == 2);
  CHECK(snapshot.apps[0].appId == "org.example.Editor");
  CHECK(snapshot.apps[0].name == "Editor");
  CHECK(snapshot.apps[0].command == "editor %f");

  REQUIRE(snapshot.windows.size() == 2);
  CHECK(snapshot.windows[0] == lambda_shell::ShellWindowSnapshot{
                                 .id = 9,
                                 .appId = "org.example.Editor",
                                 .title = "Editor \"One\"",
                             });
  CHECK(snapshot.windows[1] == lambda_shell::ShellWindowSnapshot{
                                 .id = 11,
                                 .appId = "lambda-terminal",
                                 .title = "Terminal",
                                 .focused = true,
                                 .minimized = true,
                             });
  CHECK(snapshot.activeWindowId == 11);
}

TEST_CASE("Shell config parses defaults and invalid fallback") {
  auto defaults = lambda_shell::defaultShellConfig();
  CHECK(defaults.dockPinned == std::vector<std::string>{
                                   "lambda-files",
                                   "lambda-editor",
                                   "lambda-preview",
                                   "lambda-terminal",
                                   "lambda-settings",
                                   "firefox",
                               });
  CHECK(defaults.dockItemSize == 48);
  CHECK(defaults.dockBottomGap == 8);
  CHECK(defaults.dockCornerRadius == 18);
  CHECK(defaults.dockClockFormat == "%a %d %b, %H:%M");
  CHECK(defaults.clipboardHistoryEnabled);
  CHECK(defaults.clipboardHistoryMaxEntries == 100);
  CHECK(defaults.notificationHistoryLimit == 100);
  CHECK(defaults.launcherMaxResults == 12);

  auto parsed = lambda_shell::parseShellConfig(R"(
[appearance]
icon_theme = "Adwaita"
reduced_motion = true
[dock]
pinned = ["lambda-terminal", "lambda-files"]
auto_hide = true
item_size = 36
bottom_gap = 6
corner_radius = 20
clock_format = "%H:%M"
show_running_unpinned = false
[quick_settings]
modules = ["audio", "battery"]
[clipboard_history]
enabled = false
max_entries = 5
max_text_bytes = 4096
persist = true
record_primary_selection = true
[notifications]
do_not_disturb = true
banner_timeout_seconds = 8
history_limit = 7
[launcher]
empty_query = "apps"
max_results = 4
)");
  CHECK(parsed.iconTheme == "Adwaita");
  CHECK(parsed.dockItemSize == 36);
  CHECK(parsed.reducedMotion);
  CHECK(parsed.dockPinned == std::vector<std::string>{"lambda-terminal", "lambda-files"});
  CHECK(parsed.dockAutoHide);
  CHECK(parsed.dockBottomGap == 6);
  CHECK(parsed.dockCornerRadius == 20);
  CHECK(parsed.dockClockFormat == "%H:%M");
  CHECK_FALSE(parsed.showRunningUnpinned);
  CHECK(parsed.quickSettingsModules == std::vector<std::string>{"audio", "battery"});
  CHECK_FALSE(parsed.clipboardHistoryEnabled);
  CHECK(parsed.clipboardHistoryMaxEntries == 5);
  CHECK(parsed.clipboardHistoryMaxTextBytes == 4096);
  CHECK(parsed.clipboardHistoryPersist);
  CHECK(parsed.clipboardHistoryRecordPrimarySelection);
  CHECK(parsed.notificationsDoNotDisturb);
  CHECK(parsed.notificationBannerTimeoutSeconds == 8);
  CHECK(parsed.notificationHistoryLimit == 7);
  CHECK(parsed.launcherEmptyQuery == "apps");
  CHECK(parsed.launcherMaxResults == 4);

  auto fallback = lambda_shell::parseShellConfig(R"(
[appearance]
[dock]
pinned = []
position = "floating"
bottom_gap = -1
corner_radius = 99
[clipboard_history]
enabled = maybe
max_entries = -1
[notifications]
history_limit = 2000
[launcher]
empty_query = "everything"
max_results = 1000
)");
  CHECK(fallback == defaults);

  CHECK(lambda_shell::parseShellConfig(lambda_shell::writeShellConfigToml(parsed)) == parsed);
}

TEST_CASE("Shell config accepts TOML literal string values emitted by settings") {
  auto parsed = lambda_shell::parseShellConfig(R"(
[appearance]
icon_theme = 'Lambda'
symbolic_icon_theme = 'same'
[dock]
position = 'left'
pinned = ['lambda-terminal', 'lambda-settings']
clock_format = '%H:%M'
[quick_settings]
modules = ['audio', 'battery']
[launcher]
empty_query = 'apps'
)");

  CHECK(parsed.iconTheme == "Lambda");
  CHECK(parsed.symbolicIconTheme == "same");
  CHECK(parsed.dockPosition == "left");
  CHECK(parsed.dockPinned == std::vector<std::string>{"lambda-terminal", "lambda-settings"});
  CHECK(parsed.dockClockFormat == "%H:%M");
  CHECK(parsed.quickSettingsModules == std::vector<std::string>{"audio", "battery"});
  CHECK(parsed.launcherEmptyQuery == "apps");
}

TEST_CASE("Shell config load creates defaults and reads configured pins") {
  auto root = tempRoot("lambda-shell-config-load-test");
  auto configPath = root / "lambda-shell" / "config.toml";

  auto created = lambda_shell::loadShellConfig(configPath);
  CHECK(created.path == configPath);
  CHECK(created.created);
  CHECK(created.error.empty());
  CHECK(std::filesystem::exists(configPath));
  std::ifstream defaultFile(configPath);
  std::ostringstream defaultContents;
  defaultContents << defaultFile.rdbuf();
  CHECK(lambda_shell::parseShellConfig(defaultContents.str()) == lambda_shell::defaultShellConfig());

  {
    std::ofstream(configPath) << R"(
[dock]
pinned = ["lambda-terminal", "lambda-files"]
)";
  }
  auto loaded = lambda_shell::loadShellConfig(configPath);
  CHECK_FALSE(loaded.created);
  CHECK(loaded.config.dockPinned == std::vector<std::string>{"lambda-terminal", "lambda-files"});

  std::filesystem::remove_all(root);
}
