#include "SettingsBackend.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
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

  auto output = std::find_if(schema.begin(), schema.end(), [](auto const& item) {
    return item.id == "output";
  });
  REQUIRE(output != schema.end());
  CHECK(output->applyMode == lambda_settings::ApplyMode::RestartRequired);
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
)";
  auto loaded = lambda_settings::loadWindowManagerSettings(input);
  CHECK(loaded.values.at("scale").starts_with("2."));
  CHECK(loaded.values.at("input.keyboard.layout") == "us");
  CHECK(loaded.values.at("keybindings.close") == "super+q");

  std::string output = lambda_settings::writeWindowManagerSettings(input, {
      {"scale", "1.5"},
      {"input.keyboard.layout", "tr"},
      {"keybindings.close", "super+w"},
  });
  CHECK(output.find("unknown_key") != std::string::npos);
  CHECK(output.find("1.5") != std::string::npos);
  CHECK(output.find("tr") != std::string::npos);
  CHECK(output.find("super+w") != std::string::npos);
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

  auto info = lambda_settings::parseSystemInfo("Linux 6.9.1 x86_64\n", "MemTotal:       16384000 kB\n");
  CHECK(info.kernelName == "Linux");
  CHECK(info.kernelRelease == "6.9.1");
  CHECK(info.machine == "x86_64");
  CHECK(info.memoryTotalKb == 16384000);
  std::filesystem::remove_all(root);
}
