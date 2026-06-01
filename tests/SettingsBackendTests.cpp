#include "SettingsBackend.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace {

struct ScopedEnv {
  explicit ScopedEnv(char const* name)
      : name(name) {
    if (char const* value = std::getenv(name); value) {
      hadOriginal = true;
      original = value;
    }
  }

  ~ScopedEnv() {
    if (hadOriginal) {
      setenv(name, original.c_str(), 1);
    } else {
      unsetenv(name);
    }
  }

  char const* name = nullptr;
  bool hadOriginal = false;
  std::string original;
};

std::filesystem::path tempRoot(char const* name) {
  auto path = std::filesystem::temp_directory_path() /
              (std::string(name) + "-" + std::to_string(static_cast<unsigned long long>(getpid())));
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path);
  return path;
}

lambda_settings::SettingSchema schema(std::string id, lambda_settings::SettingType type) {
  return lambda_settings::SettingSchema{.id = std::move(id), .type = type, .enumValues = {"cover", "contain"}};
}

} // namespace

TEST_CASE("Settings schema descriptors are unique and expose apply modes") {
  auto schema = lambda_settings::windowManagerSettingsSchema();
  CHECK(lambda_settings::schemaIdsUnique(schema));
  auto defaults = lambda_settings::schemaDefaults(schema);
  CHECK(defaults.at("background") == "#3380f2");
  CHECK(defaults.at("keybindings.close") == "super+q");
  CHECK(defaults.at("keybindings.screenshot") == "super+shift+3");

  auto output = std::find_if(schema.begin(), schema.end(), [](auto const& item) {
    return item.id == "output";
  });
  REQUIRE(output != schema.end());
  CHECK(output->applyMode == lambda_settings::ApplyMode::RestartRequired);
}

TEST_CASE("Settings Shell schema descriptors are unique and expose defaults") {
  auto schema = lambda_settings::shellSettingsSchema();
  CHECK(lambda_settings::schemaIdsUnique(schema));
  auto defaults = lambda_settings::schemaDefaults(schema);
  CHECK(defaults.at("dock.pinned") ==
        "lambda-files,lambda-editor,lambda-preview,lambda-terminal,lambda-settings,firefox");
  CHECK(defaults.at("dock.bottom_gap") == "8");
  CHECK(defaults.at("dock.item_size") == "48");
  CHECK(defaults.at("dock.corner_radius") == "18");
  CHECK(defaults.at("dock.clock_format") == "%a %d %b, %H:%M");
  CHECK(defaults.at("clipboard_history.persist") == "false");
  CHECK(defaults.at("launcher.empty_query") == "recommended");
}

TEST_CASE("Settings validation rejects invalid color number enum path and shortcut values") {
  CHECK(lambda_settings::validateSettingValue(schema("color", lambda_settings::SettingType::Color), "#aabbcc"));
  CHECK_FALSE(lambda_settings::validateSettingValue(schema("color", lambda_settings::SettingType::Color), "blue"));
  CHECK(lambda_settings::validateSettingValue(schema("number", lambda_settings::SettingType::Integer), "42"));
  CHECK_FALSE(lambda_settings::validateSettingValue(schema("number", lambda_settings::SettingType::Integer), "4.2"));
  CHECK(lambda_settings::validateSettingValue(schema("float", lambda_settings::SettingType::Float), "1.25"));
  CHECK(lambda_settings::validateSettingValue(schema("enum", lambda_settings::SettingType::Enum), "cover"));
  CHECK_FALSE(lambda_settings::validateSettingValue(schema("enum", lambda_settings::SettingType::Enum), "zoom"));
  CHECK_FALSE(lambda_settings::validateSettingValue(schema("path", lambda_settings::SettingType::Path), ""));
  CHECK(lambda_settings::validateSettingValue(schema("shortcut", lambda_settings::SettingType::Shortcut), "super+q"));
}

TEST_CASE("Settings model supports dirty revert reset and save") {
  lambda_settings::SettingsState state{{{"background", "#3380f2"}, {"scale", "2.0"}}};
  CHECK_FALSE(state.dirty());
  state.set("scale", "1.5");
  CHECK(state.dirty());
  CHECK(state.value("scale") == "1.5");
  state.revert();
  CHECK(state.value("scale") == "2.0");
  state.set("scale", "1.25");
  state.markSaved();
  CHECK_FALSE(state.dirty());
  state.resetToDefaults();
  CHECK(state.value("scale") == "2.0");
}

TEST_CASE("Settings Window Manager config round trip preserves unknown keys") {
  std::string const input = R"(
unknown_key = "keep"
scale = 2.0
[input.keyboard]
layout = "us"
[keybindings]
close = "super+q"
screenshot = "super+shift+3"
screenshot_region = "super+shift+4"
screenshot_active_window = "super+shift+5"
)";
  auto loaded = lambda_settings::loadWindowManagerSettings(input);
  CHECK(loaded.values.at("scale").starts_with("2"));
  CHECK(loaded.values.at("input.keyboard.layout") == "us");
  CHECK(loaded.values.at("keybindings.close") == "super+q");
  CHECK(loaded.values.at("keybindings.screenshot") == "super+shift+3");
  CHECK(loaded.values.at("keybindings.screenshot_region") == "super+shift+4");
  CHECK(loaded.values.at("keybindings.screenshot_active_window") == "super+shift+5");

  std::string output = lambda_settings::writeWindowManagerSettings(input, {
      {"scale", "1.5"},
      {"input.keyboard.layout", "tr"},
      {"keybindings.close", "super+w"},
      {"keybindings.screenshot", "super+shift+s"},
  });
  CHECK(output.find("unknown_key") != std::string::npos);
  CHECK(output.find("1.5") != std::string::npos);
  CHECK(output.find("tr") != std::string::npos);
  CHECK(output.find("super+w") != std::string::npos);
  CHECK(output.find("super+shift+s") != std::string::npos);
}

TEST_CASE("Settings Shell config round trip preserves unknown keys") {
  std::string const input = R"(
unknown_root = "keep"
[appearance]
icon_theme = "Adwaita"
unknown_appearance = "keep"
[dock]
pinned = ["lambda-files", "lambda-terminal"]
show_running_unpinned = true
bottom_gap = 6
item_size = 36
corner_radius = 20
clock_format = "%H:%M"
[clipboard_history]
enabled = true
max_entries = 100
[notifications]
do_not_disturb = false
history_limit = 100
[launcher]
empty_query = "recommended"
max_results = 12
)";
  auto loaded = lambda_settings::loadShellSettings(input);
  CHECK(loaded.values.at("appearance.icon_theme") == "Adwaita");
  CHECK(loaded.values.at("dock.pinned") == "lambda-files,lambda-terminal");
  CHECK(loaded.values.at("dock.bottom_gap") == "6");
  CHECK(loaded.values.at("dock.item_size") == "36");
  CHECK(loaded.values.at("dock.corner_radius") == "20");
  CHECK(loaded.values.at("dock.clock_format") == "%H:%M");
  CHECK(loaded.values.at("clipboard_history.max_entries") == "100");
  CHECK(loaded.values.at("notifications.do_not_disturb") == "false");
  CHECK(loaded.values.at("launcher.max_results") == "12");

  std::string output = lambda_settings::writeShellSettings(input, {
      {"appearance.icon_theme", "Lambda"},
      {"dock.pinned", "lambda-terminal,lambda-settings"},
      {"dock.item_size", "40"},
      {"dock.bottom_gap", "8"},
      {"dock.corner_radius", "18"},
      {"dock.clock_format", "%a %d %b, %H:%M"},
      {"dock.show_running_unpinned", "false"},
      {"clipboard_history.enabled", "false"},
      {"clipboard_history.max_entries", "25"},
      {"notifications.do_not_disturb", "true"},
      {"launcher.empty_query", "apps"},
      {"launcher.max_results", "6"},
  });
  CHECK(output.find("unknown_root") != std::string::npos);
  CHECK(output.find("unknown_appearance") != std::string::npos);
  CHECK(output.find("Lambda") != std::string::npos);
  CHECK(output.find("lambda-settings") != std::string::npos);
  CHECK(output.find("item_size = 40") != std::string::npos);
  CHECK(output.find("show_running_unpinned = false") != std::string::npos);
  CHECK(output.find("enabled = false") != std::string::npos);
  CHECK(output.find("max_entries = 25") != std::string::npos);
  CHECK(output.find("do_not_disturb = true") != std::string::npos);
  bool const emptyQueryWritten = output.find("empty_query = 'apps'") != std::string::npos ||
                                 output.find("empty_query = \"apps\"") != std::string::npos;
  CHECK(emptyQueryWritten);
}

TEST_CASE("Settings atomic write replaces file and reports errors") {
  auto root = tempRoot("lambda-settings-atomic-test");
  auto path = root / "config.toml";
  {
    std::ofstream(path) << "old";
  }
  std::string error;
  CHECK(lambda_settings::atomicWriteFile(path, "new", error));
  std::ifstream file(path);
  std::string contents;
  file >> contents;
  CHECK(contents == "new");
  CHECK(error.empty());
  std::filesystem::remove_all(root);
}

TEST_CASE("Settings atomic write failure leaves original file intact") {
  auto root = tempRoot("lambda-settings-atomic-failure-test");
  auto path = root / "config.toml";
  {
    std::ofstream(path) << "original";
  }
  std::filesystem::create_directory(root / "config.toml.tmp");

  std::string error;
  CHECK_FALSE(lambda_settings::atomicWriteFile(path, "new", error));
  CHECK_FALSE(error.empty());
  std::ifstream file(path);
  std::string contents;
  file >> contents;
  CHECK(contents == "original");
  CHECK(std::filesystem::is_directory(root / "config.toml.tmp"));

  std::filesystem::remove_all(root);
}

TEST_CASE("Settings file helpers resolve create load and save owner configs") {
  ScopedEnv wmConfig("LAMBDA_WINDOW_MANAGER_CONFIG");
  ScopedEnv shellConfig("LAMBDA_SHELL_CONFIG");
  ScopedEnv xdgConfig("XDG_CONFIG_HOME");
  auto root = tempRoot("lambda-settings-file-helper-test");
  auto wmPath = root / "wm" / "config.toml";
  auto shellPath = root / "shell" / "config.toml";
  setenv("LAMBDA_WINDOW_MANAGER_CONFIG", wmPath.c_str(), 1);
  setenv("LAMBDA_SHELL_CONFIG", shellPath.c_str(), 1);
  setenv("XDG_CONFIG_HOME", (root / "xdg").c_str(), 1);

  CHECK(lambda_settings::windowManagerSettingsPath() == wmPath);
  CHECK(lambda_settings::shellSettingsPath() == shellPath);

  auto createdWm = lambda_settings::loadWindowManagerSettingsFile();
  CHECK(createdWm.createdDefault);
  CHECK(createdWm.path == wmPath);
  CHECK(createdWm.error.empty());
  CHECK(std::filesystem::exists(wmPath));
  CHECK(createdWm.document.values.at("background") == "#3380f2");

  auto savedWm = lambda_settings::saveWindowManagerSettingsFile({
      {"scale", "1.5"},
      {"input.keyboard.repeat_rate", "42"},
  });
  CHECK(savedWm.ok);
  auto loadedWm = lambda_settings::loadWindowManagerSettingsFile();
  CHECK(loadedWm.loaded);
  CHECK(loadedWm.document.values.at("scale").find("1.5") == 0);
  CHECK(loadedWm.document.values.at("input.keyboard.repeat_rate") == "42");

  auto invalidWm = lambda_settings::saveWindowManagerSettingsFile({{"scale", "not-a-number"}});
  CHECK_FALSE(invalidWm.ok);
  CHECK(invalidWm.error.find("Scale") != std::string::npos);

  auto createdShell = lambda_settings::loadShellSettingsFile();
  CHECK(createdShell.createdDefault);
  CHECK(createdShell.path == shellPath);
  CHECK(createdShell.error.empty());
  CHECK(std::filesystem::exists(shellPath));
  CHECK(createdShell.document.values.at("dock.position") == "bottom");
  CHECK(createdShell.document.values.at("dock.item_size") == "48");
  CHECK(createdShell.document.values.at("dock.bottom_gap") == "8");
  CHECK(createdShell.document.values.at("dock.clock_format") == "%a %d %b, %H:%M");

  auto savedShell = lambda_settings::saveShellSettingsFile({
      {"dock.show_running_unpinned", "false"},
      {"launcher.max_results", "8"},
  });
  CHECK(savedShell.ok);
  auto loadedShell = lambda_settings::loadShellSettingsFile();
  CHECK(loadedShell.loaded);
  CHECK(loadedShell.document.values.at("dock.show_running_unpinned") == "false");
  CHECK(loadedShell.document.values.at("launcher.max_results") == "8");

  auto invalidShell = lambda_settings::saveShellSettingsFile({{"dock.position", "floating"}});
  CHECK_FALSE(invalidShell.ok);
  CHECK(invalidShell.error.find("Dock position") != std::string::npos);

  std::filesystem::remove_all(root);
}

TEST_CASE("Settings shortcut conflict detection normalizes modifier order") {
  CHECK(lambda_settings::shortcutConflicts({
      {"close", "super+shift+q"},
      {"other", "shift+super+q"},
  }));
  CHECK_FALSE(lambda_settings::shortcutConflicts({
      {"close", "super+q"},
      {"other", "super+w"},
  }));
}

TEST_CASE("Settings wallpaper path normalization handles home absolute and relative paths") {
  std::filesystem::path const config = "/home/user/.config/lambda-window-manager/config.toml";
  std::filesystem::path const home = "/home/user";
  CHECK(lambda_settings::normalizeWallpaperPath(config, "~/Pictures/wall.png", home) ==
        "/home/user/Pictures/wall.png");
  CHECK(lambda_settings::normalizeWallpaperPath(config, "/data/wall.png", home) == "/data/wall.png");
  CHECK(lambda_settings::normalizeWallpaperPath(config, "wallpapers/wall.png", home) ==
        "/home/user/.config/lambda-window-manager/wallpapers/wall.png");
}

TEST_CASE("Settings theme discovery and system info parsing use fixtures") {
  auto root = tempRoot("lambda-settings-theme-test");
  std::filesystem::create_directories(root / "Adwaita");
  std::filesystem::create_directories(root / "Lambda");
  auto themes = lambda_settings::discoverThemeNames({root});
  CHECK(themes == std::vector<std::string>{"Adwaita", "Lambda"});

  auto selected = lambda_settings::resolveThemeSelection({root}, "Lambda", "Adwaita");
  CHECK(selected.available == themes);
  CHECK(selected.requested == "Lambda");
  CHECK(selected.effective == "Lambda");
  CHECK(selected.requestedAvailable);
  CHECK_FALSE(selected.missingRequested);
  CHECK_FALSE(selected.usingFallback);

  auto missing = lambda_settings::resolveThemeSelection({root}, "Missing", "Adwaita");
  CHECK(missing.effective == "Adwaita");
  CHECK_FALSE(missing.requestedAvailable);
  CHECK(missing.missingRequested);
  CHECK(missing.usingFallback);

  auto noFallback = lambda_settings::resolveThemeSelection({root / "absent"}, "Missing", "Adwaita");
  CHECK(noFallback.available.empty());
  CHECK(noFallback.effective.empty());
  CHECK(noFallback.missingRequested);
  CHECK(noFallback.usingFallback);

  auto info = lambda_settings::parseSystemInfo("Linux 6.9.1 x86_64\n", "MemTotal:       16384000 kB\n");
  CHECK(info.kernelName == "Linux");
  CHECK(info.kernelRelease == "6.9.1");
  CHECK(info.machine == "x86_64");
  CHECK(info.memoryTotalKb == 16384000);
  CHECK(lambda_settings::formatMemoryTotal(info.memoryTotalKb) == "16 GiB");
  auto rows = lambda_settings::systemInfoRows(info);
  REQUIRE(rows.size() == 7);
  CHECK(rows[0] == std::pair<std::string, std::string>{"Kernel", "Linux 6.9.1"});
  CHECK(rows[1] == std::pair<std::string, std::string>{"Architecture", "x86_64"});
  CHECK(rows[2] == std::pair<std::string, std::string>{"Memory", "16 GiB"});
  CHECK(rows[3] == std::pair<std::string, std::string>{"Processor", "Unavailable"});
  CHECK(lambda_settings::systemInfoRows({})[0].second == "Unavailable");
  std::filesystem::remove_all(root);
}
