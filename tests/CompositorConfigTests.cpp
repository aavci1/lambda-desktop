#include "Compositor/Config/CompositorConfig.hpp"
#include "Compositor/Chrome/ChromeMetrics.hpp"

#include <Flux/Graphics/ImageFillMode.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <linux/input-event-codes.h>
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
    if (!hadOriginal) {
      unsetenv(name);
    } else {
      setenv(name, original.c_str(), 1);
    }
  }

  char const* name;
  bool hadOriginal = false;
  std::string original;
};

std::filesystem::path tempConfigPath() {
  auto path = std::filesystem::temp_directory_path() /
              ("flux-compositor-config-test-" + std::to_string(static_cast<unsigned long long>(getpid())) + ".toml");
  std::filesystem::remove(path);
  return path;
}

} // namespace

TEST_CASE("compositor config creates a default file when missing") {
  ScopedEnv configEnv("FLUX_COMPOSITOR_CONFIG");
  ScopedEnv outputEnv("FLUX_COMPOSITOR_OUTPUT");
  unsetenv("FLUX_COMPOSITOR_OUTPUT");
  auto const path = std::filesystem::temp_directory_path() /
                    ("flux-compositor-config-test-dir-" +
                     std::to_string(static_cast<unsigned long long>(getpid()))) /
                    "config.toml";
  std::filesystem::remove_all(path.parent_path());
  setenv("FLUX_COMPOSITOR_CONFIG", path.c_str(), 1);

  auto loaded = flux::compositor::loadConfigWithMetadata();

  CHECK(loaded.path == path.string());
  CHECK(loaded.hasModifiedAt);
  CHECK(std::filesystem::exists(path));
  CHECK(loaded.config.scale == doctest::Approx(2.0f));
  CHECK(loaded.config.backgroundColor.r == doctest::Approx(51.f / 255.f));
  CHECK(loaded.config.backgroundColor.g == doctest::Approx(128.f / 255.f));
  CHECK(loaded.config.backgroundColor.b == doctest::Approx(242.f / 255.f));
  CHECK(loaded.config.chrome.titleBarHeight == 28);
  CHECK(loaded.config.chrome.controlsWidth == 58);

  std::filesystem::remove_all(path.parent_path());
}

TEST_CASE("compositor config parses colors, wallpaper, and keybindings") {
  ScopedEnv configEnv("FLUX_COMPOSITOR_CONFIG");
  ScopedEnv outputEnv("FLUX_COMPOSITOR_OUTPUT");
  unsetenv("FLUX_COMPOSITOR_OUTPUT");
  auto const path = tempConfigPath();
  std::ofstream file(path);
  file << "background = \"#112233\"\n";
  file << "wallpaper = \"/tmp/wallpaper.png\"\n";
  file << "wallpaper_mode = \"contain\"\n";
  file << "scale = 1.5\n";
  file << "animations = false\n";
  file << "hardware_cursor = false\n";
  file << "[keybindings]\n";
  file << "snap_left = \"ctrl+alt+left\"\n";
  file.close();
  setenv("FLUX_COMPOSITOR_CONFIG", path.c_str(), 1);

  auto loaded = flux::compositor::loadConfigWithMetadata();
  auto const& config = loaded.config;
  CHECK(loaded.path == path.string());
  CHECK(loaded.hasModifiedAt);
  CHECK(config.backgroundColor.r == doctest::Approx(17.f / 255.f));
  CHECK(config.backgroundColor.g == doctest::Approx(34.f / 255.f));
  CHECK(config.backgroundColor.b == doctest::Approx(51.f / 255.f));
  CHECK(config.backgroundColor.a == doctest::Approx(1.f));
  CHECK(config.wallpaperPath == "/tmp/wallpaper.png");
  CHECK(config.wallpaperMode == flux::ImageFillMode::Fit);
  CHECK(config.scale == doctest::Approx(1.5f));
  CHECK_FALSE(config.animationsEnabled);
  CHECK_FALSE(config.hardwareCursorEnabled);

  auto snapLeft = std::find_if(config.shortcutBindings.begin(), config.shortcutBindings.end(), [](auto const& binding) {
    return binding.action == flux::compositor::WaylandServer::ShortcutAction::SnapLeft;
  });
  REQUIRE(snapLeft != config.shortcutBindings.end());
  CHECK(snapLeft->ctrl);
  CHECK(snapLeft->alt);
  CHECK_FALSE(snapLeft->meta);

  std::filesystem::remove(path);
}

TEST_CASE("compositor config resolves wallpaper paths from home and config directory") {
  ScopedEnv configEnv("FLUX_COMPOSITOR_CONFIG");
  ScopedEnv outputEnv("FLUX_COMPOSITOR_OUTPUT");
  ScopedEnv homeEnv("HOME");
  unsetenv("FLUX_COMPOSITOR_OUTPUT");

  auto const root = std::filesystem::temp_directory_path() /
                    ("flux-compositor-wallpaper-test-" +
                     std::to_string(static_cast<unsigned long long>(getpid())));
  auto const configDir = root / "config";
  auto const homeDir = root / "home";
  std::filesystem::create_directories(configDir);
  std::filesystem::create_directories(homeDir);
  setenv("HOME", homeDir.c_str(), 1);

  auto const path = configDir / "config.toml";
  {
    std::ofstream file(path);
    file << "wallpaper = \"~/Pictures/wallpaper.png\"\n";
  }
  setenv("FLUX_COMPOSITOR_CONFIG", path.c_str(), 1);
  auto homeLoaded = flux::compositor::loadConfigWithMetadata();
  REQUIRE(homeLoaded.config.wallpaperPath);
  CHECK(*homeLoaded.config.wallpaperPath == (homeDir / "Pictures/wallpaper.png").lexically_normal().string());

  {
    std::ofstream file(path);
    file << "wallpaper = \"images/wallpaper.png\"\n";
  }
  auto relativeLoaded = flux::compositor::loadConfigWithMetadata();
  REQUIRE(relativeLoaded.config.wallpaperPath);
  CHECK(*relativeLoaded.config.wallpaperPath == (configDir / "images/wallpaper.png").lexically_normal().string());

  std::filesystem::remove_all(root);
}

TEST_CASE("compositor config parses shorthand and alpha colors") {
  ScopedEnv configEnv("FLUX_COMPOSITOR_CONFIG");
  ScopedEnv outputEnv("FLUX_COMPOSITOR_OUTPUT");
  unsetenv("FLUX_COMPOSITOR_OUTPUT");
  auto const path = tempConfigPath();
  std::ofstream file(path);
  file << "background_gradient = \"#1234 #01020380\"\n";
  file.close();
  setenv("FLUX_COMPOSITOR_CONFIG", path.c_str(), 1);

  auto loaded = flux::compositor::loadConfigWithMetadata();
  auto const& config = loaded.config;
  CHECK(config.backgroundColor.r == doctest::Approx(17.f / 255.f));
  CHECK(config.backgroundColor.g == doctest::Approx(34.f / 255.f));
  CHECK(config.backgroundColor.b == doctest::Approx(51.f / 255.f));
  CHECK(config.backgroundColor.a == doctest::Approx(68.f / 255.f));
  REQUIRE(config.backgroundGradientEnd);
  CHECK(config.backgroundGradientEnd->r == doctest::Approx(1.f / 255.f));
  CHECK(config.backgroundGradientEnd->g == doctest::Approx(2.f / 255.f));
  CHECK(config.backgroundGradientEnd->b == doctest::Approx(3.f / 255.f));
  CHECK(config.backgroundGradientEnd->a == doctest::Approx(128.f / 255.f));

  std::filesystem::remove(path);
}

TEST_CASE("compositor config parses chrome section") {
  ScopedEnv configEnv("FLUX_COMPOSITOR_CONFIG");
  ScopedEnv outputEnv("FLUX_COMPOSITOR_OUTPUT");
  unsetenv("FLUX_COMPOSITOR_OUTPUT");
  auto const path = tempConfigPath();
  std::ofstream file(path);
  file << "[chrome]\n";
  file << "title_bar_height = 44\n";
  file << "controls_width = 96\n";
  file << "button_radius = 8.5\n";
  file << "resize_grip_size = 3\n";
  file << "glass_tint = \"#ffffff80\"\n";
  file << "close_hover_background = \"#ff0000\"\n";
  file << "[chrome.window_corner_radius]\n";
  file << "top_left = 6\n";
  file << "top_right = 7\n";
  file << "bottom_right = 8\n";
  file << "bottom_left = 9\n";
  file << "[chrome.dark]\n";
  file << "title_text_color = \"#e6ecf7\"\n";
  file.close();
  setenv("FLUX_COMPOSITOR_CONFIG", path.c_str(), 1);

  auto loaded = flux::compositor::loadConfigWithMetadata();
  auto const& chrome = loaded.config.chrome;
  CHECK(chrome.titleBarHeight == 44);
  CHECK(chrome.controlsWidth == 96);
  CHECK(chrome.buttonRadius == doctest::Approx(8.5f));
  CHECK(chrome.resizeGripSize == 3);
  CHECK(chrome.windowCornerRadius.topLeft == doctest::Approx(6.f));
  CHECK(chrome.windowCornerRadius.topRight == doctest::Approx(7.f));
  CHECK(chrome.windowCornerRadius.bottomRight == doctest::Approx(8.f));
  CHECK(chrome.windowCornerRadius.bottomLeft == doctest::Approx(9.f));
  CHECK(chrome.glassTint.a == doctest::Approx(128.f / 255.f));
  CHECK(chrome.closeHoverBackground.r == doctest::Approx(1.f));
  REQUIRE(loaded.config.darkChrome);
  CHECK(loaded.config.darkChrome->titleTextColor.r == doctest::Approx(230.f / 255.f));

  std::filesystem::remove(path);
}

TEST_CASE("chrome controls scale and center with title bar height") {
  flux::compositor::ChromeConfig chrome;
  chrome.titleBarHeight = 56;

  auto const metrics = flux::compositor::chromeControlsMetrics(chrome);
  CHECK(metrics.buttonSize == doctest::Approx(32.f));
  CHECK(metrics.buttonGap == doctest::Approx(8.f));
  CHECK(metrics.insetTop == doctest::Approx(12.f));

  auto const rects = flux::compositor::chromeControlRects(chrome, 10.f, 20.f, 300.f, 56.f);
  CHECK(rects.closeButton.y == doctest::Approx(32.f));
  CHECK(rects.closeButton.height == doctest::Approx(32.f));
  CHECK(rects.minimizeButton.height == doctest::Approx(32.f));
}

TEST_CASE("compositor config reports file changes") {
  ScopedEnv configEnv("FLUX_COMPOSITOR_CONFIG");
  ScopedEnv outputEnv("FLUX_COMPOSITOR_OUTPUT");
  unsetenv("FLUX_COMPOSITOR_OUTPUT");
  auto const path = tempConfigPath();
  {
    std::ofstream file(path);
    file << "background = \"#223344\"\n";
  }
  setenv("FLUX_COMPOSITOR_CONFIG", path.c_str(), 1);

  auto loaded = flux::compositor::loadConfigWithMetadata();
  CHECK_FALSE(flux::compositor::configChanged(loaded));
  std::filesystem::remove(path);
  CHECK(flux::compositor::configChanged(loaded));
}

TEST_CASE("compositor config parses gradient aliases and scale aliases") {
  ScopedEnv configEnv("FLUX_COMPOSITOR_CONFIG");
  ScopedEnv outputEnv("FLUX_COMPOSITOR_OUTPUT");
  unsetenv("FLUX_COMPOSITOR_OUTPUT");
  auto const path = tempConfigPath();
  std::ofstream file(path);
  file << "background_gradient = \"#010203,#a0b0c0\"\n";
  file << "wallpaper_mode = \"tile\"\n";
  file << "cursor_theme = \"Adwaita\"\n";
  file << "cursor_size = \"32\"\n";
  file << "output = \"HDMI-A-1\"\n";
  file << "output_scale = 1.25\n";
  file << "animations = \"off\"\n";
  file << "hardware_cursor = \"on\"\n";
  file.close();
  setenv("FLUX_COMPOSITOR_CONFIG", path.c_str(), 1);

  auto loaded = flux::compositor::loadConfigWithMetadata();
  auto const& config = loaded.config;
  CHECK(config.backgroundColor.r == doctest::Approx(1.f / 255.f));
  CHECK(config.backgroundColor.g == doctest::Approx(2.f / 255.f));
  CHECK(config.backgroundColor.b == doctest::Approx(3.f / 255.f));
  REQUIRE(config.backgroundGradientEnd);
  CHECK(config.backgroundGradientEnd->r == doctest::Approx(160.f / 255.f));
  CHECK(config.backgroundGradientEnd->g == doctest::Approx(176.f / 255.f));
  CHECK(config.backgroundGradientEnd->b == doctest::Approx(192.f / 255.f));
  CHECK(config.wallpaperMode == flux::ImageFillMode::Tile);
  REQUIRE(config.cursorTheme);
  CHECK(*config.cursorTheme == "Adwaita");
  CHECK(config.cursorSize == 32);
  REQUIRE(config.outputSelector);
  CHECK(*config.outputSelector == "HDMI-A-1");
  CHECK(config.scale == doctest::Approx(1.25f));
  CHECK_FALSE(config.animationsEnabled);
  CHECK(config.hardwareCursorEnabled);

  std::filesystem::remove(path);
}

TEST_CASE("compositor config supports per-output scale overrides") {
  ScopedEnv configEnv("FLUX_COMPOSITOR_CONFIG");
  ScopedEnv outputEnv("FLUX_COMPOSITOR_OUTPUT");
  unsetenv("FLUX_COMPOSITOR_OUTPUT");
  auto const path = tempConfigPath();
  std::ofstream file(path);
  file << "scale = 1.0\n";
  file << "[outputs.\"eDP-1\"]\n";
  file << "scale = 1.25\n";
  file << "[outputs.\"DP-1\"]\n";
  file << "scale = 2.0\n";
  file.close();
  setenv("FLUX_COMPOSITOR_CONFIG", path.c_str(), 1);

  auto loaded = flux::compositor::loadConfigWithMetadata();
  auto const& config = loaded.config;
  CHECK(config.scale == doctest::Approx(1.0f));
  REQUIRE(config.outputScales.contains("eDP-1"));
  REQUIRE(config.outputScales.contains("DP-1"));
  CHECK(config.outputScales.at("eDP-1") == doctest::Approx(1.25f));
  CHECK(config.outputScales.at("DP-1") == doctest::Approx(2.0f));
  CHECK(flux::compositor::scaleForOutput(config, "eDP-1") == doctest::Approx(1.25f));
  CHECK(flux::compositor::scaleForOutput(config, "DP-1") == doctest::Approx(2.0f));
  CHECK(flux::compositor::scaleForOutput(config, "HDMI-A-1") == doctest::Approx(1.0f));

  std::filesystem::remove(path);
}

TEST_CASE("compositor config ignores invalid values and preserves defaults") {
  ScopedEnv configEnv("FLUX_COMPOSITOR_CONFIG");
  ScopedEnv outputEnv("FLUX_COMPOSITOR_OUTPUT");
  unsetenv("FLUX_COMPOSITOR_OUTPUT");
  auto const path = tempConfigPath();
  std::ofstream file(path);
  file << "background = \"nope\"\n";
  file << "background_gradient = \"#112233 nope\"\n";
  file << "wallpaper_mode = \"invalid\"\n";
  file << "cursor_theme = \"\"\n";
  file << "cursor_size = 512\n";
  file << "scale = 9.0\n";
  file << "animations = \"maybe\"\n";
  file << "hardware_cursor = \"sometimes\"\n";
  file << "[keybindings]\n";
  file << "snap_left = \"super+left+right\"\n";
  file << "snap_right = 42\n";
  file.close();
  setenv("FLUX_COMPOSITOR_CONFIG", path.c_str(), 1);

  auto loaded = flux::compositor::loadConfigWithMetadata();
  auto const& config = loaded.config;
  CHECK(config.backgroundColor.r == doctest::Approx(0.20f));
  CHECK(config.backgroundColor.g == doctest::Approx(0.50f));
  CHECK(config.backgroundColor.b == doctest::Approx(0.95f));
  CHECK_FALSE(config.backgroundGradientEnd);
  CHECK(config.wallpaperMode == flux::ImageFillMode::Cover);
  CHECK_FALSE(config.cursorTheme);
  CHECK(config.cursorSize == 0);
  CHECK(config.scale == doctest::Approx(2.0f));
  CHECK(config.animationsEnabled);
  CHECK(config.hardwareCursorEnabled);

  auto snapLeft = std::find_if(config.shortcutBindings.begin(), config.shortcutBindings.end(), [](auto const& binding) {
    return binding.action == flux::compositor::WaylandServer::ShortcutAction::SnapLeft;
  });
  REQUIRE(snapLeft != config.shortcutBindings.end());
  CHECK(snapLeft->meta);
  CHECK_FALSE(snapLeft->ctrl);
  CHECK(snapLeft->key == KEY_LEFT);

  std::filesystem::remove(path);
}

TEST_CASE("compositor config supports shortcut aliases and replacement") {
  ScopedEnv configEnv("FLUX_COMPOSITOR_CONFIG");
  ScopedEnv outputEnv("FLUX_COMPOSITOR_OUTPUT");
  unsetenv("FLUX_COMPOSITOR_OUTPUT");
  auto const path = tempConfigPath();
  std::ofstream file(path);
  file << "[keybindings]\n";
  file << "close_focused = \"shift+super+q\"\n";
  file << "cycle = \"alt+tab\"\n";
  file << "run = \"ctrl+space\"\n";
  file << "quit = \"ctrl+alt+delete\"\n";
  file.close();
  setenv("FLUX_COMPOSITOR_CONFIG", path.c_str(), 1);

  auto const config = flux::compositor::loadConfigWithMetadata().config;
  auto findAction = [&](flux::compositor::WaylandServer::ShortcutAction action) {
    return std::find_if(config.shortcutBindings.begin(), config.shortcutBindings.end(), [&](auto const& binding) {
      return binding.action == action;
    });
  };

  auto close = findAction(flux::compositor::WaylandServer::ShortcutAction::CloseFocused);
  REQUIRE(close != config.shortcutBindings.end());
  CHECK(close->meta);
  CHECK(close->shift);
  CHECK(close->key == KEY_Q);

  auto cycle = findAction(flux::compositor::WaylandServer::ShortcutAction::CycleFocus);
  REQUIRE(cycle != config.shortcutBindings.end());
  CHECK(cycle->alt);
  CHECK_FALSE(cycle->meta);
  CHECK(cycle->key == KEY_TAB);

  auto launch = findAction(flux::compositor::WaylandServer::ShortcutAction::LaunchCommand);
  REQUIRE(launch != config.shortcutBindings.end());
  CHECK(launch->ctrl);
  CHECK_FALSE(launch->meta);
  CHECK(launch->key == KEY_SPACE);

  auto terminate = findAction(flux::compositor::WaylandServer::ShortcutAction::Terminate);
  REQUIRE(terminate != config.shortcutBindings.end());
  CHECK(terminate->ctrl);
  CHECK(terminate->alt);
  CHECK(terminate->key == KEY_DELETE);

  std::filesystem::remove(path);
}

TEST_CASE("compositor config allows output environment override") {
  ScopedEnv configEnv("FLUX_COMPOSITOR_CONFIG");
  ScopedEnv outputEnv("FLUX_COMPOSITOR_OUTPUT");
  auto const path = tempConfigPath();
  std::ofstream file(path);
  file << "output = \"HDMI-A-1\"\n";
  file.close();
  setenv("FLUX_COMPOSITOR_CONFIG", path.c_str(), 1);
  setenv("FLUX_COMPOSITOR_OUTPUT", "secondary", 1);

  auto const config = flux::compositor::loadConfigWithMetadata().config;
  REQUIRE(config.outputSelector);
  CHECK(*config.outputSelector == "secondary");

  std::filesystem::remove(path);
}

TEST_CASE("compositor config applies output environment override when config is invalid") {
  ScopedEnv configEnv("FLUX_COMPOSITOR_CONFIG");
  ScopedEnv outputEnv("FLUX_COMPOSITOR_OUTPUT");
  auto const path = tempConfigPath();
  std::ofstream file(path);
  file << "scale = nope\n";
  file.close();
  setenv("FLUX_COMPOSITOR_CONFIG", path.c_str(), 1);
  setenv("FLUX_COMPOSITOR_OUTPUT", "DP-1", 1);

  auto const config = flux::compositor::loadConfigWithMetadata().config;
  REQUIRE(config.outputSelector);
  CHECK(*config.outputSelector == "DP-1");

  std::filesystem::remove(path);
}
