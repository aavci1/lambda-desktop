#include "Compositor/Config/CompositorConfig.hpp"
#include "Compositor/Chrome/ChromeMetrics.hpp"

#include <Lambda/Graphics/ImageFillMode.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
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
              ("lambda-window-manager-config-test-" + std::to_string(static_cast<unsigned long long>(getpid())) + ".toml");
  std::filesystem::remove(path);
  return path;
}

} // namespace

TEST_CASE("compositor config creates a default file when missing") {
  ScopedEnv configEnv("LAMBDA_WINDOW_MANAGER_CONFIG");
  ScopedEnv outputEnv("LAMBDA_WINDOW_MANAGER_OUTPUT");
  unsetenv("LAMBDA_WINDOW_MANAGER_OUTPUT");
  auto const path = std::filesystem::temp_directory_path() /
                    ("lambda-window-manager-config-test-dir-" +
                     std::to_string(static_cast<unsigned long long>(getpid()))) /
                    "config.toml";
  std::filesystem::remove_all(path.parent_path());
  setenv("LAMBDA_WINDOW_MANAGER_CONFIG", path.c_str(), 1);

  auto loaded = lambda::compositor::loadConfigWithMetadata();

  CHECK(loaded.path == path.string());
  CHECK(loaded.hasModifiedAt);
  CHECK(std::filesystem::exists(path));
  CHECK(loaded.config.scale == doctest::Approx(2.0f));
  CHECK(loaded.config.backgroundColor.r == doctest::Approx(51.f / 255.f));
  CHECK(loaded.config.backgroundColor.g == doctest::Approx(128.f / 255.f));
  CHECK(loaded.config.backgroundColor.b == doctest::Approx(242.f / 255.f));
  CHECK(loaded.config.chrome.titleBarHeight == 28);
  CHECK(loaded.config.chrome.controlsWidth == 84);
  CHECK(loaded.config.rendering.backdropBlurBaseDownsample == 2);
  CHECK(loaded.config.popupGrabs);
  CHECK(loaded.config.keyboard.layout.empty());
  CHECK(loaded.config.keyboard.repeatRate == 25);
  CHECK(loaded.config.keyboard.repeatDelayMs == 600);

  std::ifstream generated(path);
  std::string const text((std::istreambuf_iterator<char>(generated)), std::istreambuf_iterator<char>());
  CHECK(text.find("[input]") != std::string::npos);
  CHECK(text.find("[rendering.backdrop_blur]") != std::string::npos);
  CHECK(text.find("base_downsample = 2") != std::string::npos);
  CHECK(text.find("popup_grabs = true") != std::string::npos);
  CHECK(text.find("screenshot = [\"super+shift+3\", \"printscreen\", \"sysrq\"]") != std::string::npos);
  CHECK(text.find("screenshot_region = \"super+shift+4\"") != std::string::npos);
  CHECK(text.find("screenshot_active_window = [\"super+shift+5\", \"alt+printscreen\", \"alt+sysrq\"]") !=
        std::string::npos);

  std::filesystem::remove_all(path.parent_path());
}

TEST_CASE("compositor config parses colors, wallpaper, and keybindings") {
  ScopedEnv configEnv("LAMBDA_WINDOW_MANAGER_CONFIG");
  ScopedEnv outputEnv("LAMBDA_WINDOW_MANAGER_OUTPUT");
  unsetenv("LAMBDA_WINDOW_MANAGER_OUTPUT");
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
  setenv("LAMBDA_WINDOW_MANAGER_CONFIG", path.c_str(), 1);

  auto loaded = lambda::compositor::loadConfigWithMetadata();
  auto const& config = loaded.config;
  CHECK(loaded.path == path.string());
  CHECK(loaded.hasModifiedAt);
  CHECK(config.backgroundColor.r == doctest::Approx(17.f / 255.f));
  CHECK(config.backgroundColor.g == doctest::Approx(34.f / 255.f));
  CHECK(config.backgroundColor.b == doctest::Approx(51.f / 255.f));
  CHECK(config.backgroundColor.a == doctest::Approx(1.f));
  CHECK(config.wallpaperPath == "/tmp/wallpaper.png");
  CHECK(config.wallpaperMode == lambda::ImageFillMode::Fit);
  CHECK(config.scale == doctest::Approx(1.5f));
  CHECK_FALSE(config.animationsEnabled);
  CHECK_FALSE(config.hardwareCursorEnabled);

  auto snapLeft = std::find_if(config.shortcutBindings.begin(), config.shortcutBindings.end(), [](auto const& binding) {
    return binding.action == lambda::compositor::WaylandServer::ShortcutAction::SnapLeft;
  });
  REQUIRE(snapLeft != config.shortcutBindings.end());
  CHECK(snapLeft->ctrl);
  CHECK(snapLeft->alt);
  CHECK_FALSE(snapLeft->meta);

  std::filesystem::remove(path);
}

TEST_CASE("compositor config resolves wallpaper paths from home and config directory") {
  ScopedEnv configEnv("LAMBDA_WINDOW_MANAGER_CONFIG");
  ScopedEnv outputEnv("LAMBDA_WINDOW_MANAGER_OUTPUT");
  ScopedEnv homeEnv("HOME");
  unsetenv("LAMBDA_WINDOW_MANAGER_OUTPUT");

  auto const root = std::filesystem::temp_directory_path() /
                    ("lambda-window-manager-wallpaper-test-" +
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
  setenv("LAMBDA_WINDOW_MANAGER_CONFIG", path.c_str(), 1);
  auto homeLoaded = lambda::compositor::loadConfigWithMetadata();
  REQUIRE(homeLoaded.config.wallpaperPath);
  CHECK(*homeLoaded.config.wallpaperPath == (homeDir / "Pictures/wallpaper.png").lexically_normal().string());

  {
    std::ofstream file(path);
    file << "wallpaper = \"images/wallpaper.png\"\n";
  }
  auto relativeLoaded = lambda::compositor::loadConfigWithMetadata();
  REQUIRE(relativeLoaded.config.wallpaperPath);
  CHECK(*relativeLoaded.config.wallpaperPath == (configDir / "images/wallpaper.png").lexically_normal().string());

  std::filesystem::remove_all(root);
}

TEST_CASE("compositor config parses shorthand and alpha colors") {
  ScopedEnv configEnv("LAMBDA_WINDOW_MANAGER_CONFIG");
  ScopedEnv outputEnv("LAMBDA_WINDOW_MANAGER_OUTPUT");
  unsetenv("LAMBDA_WINDOW_MANAGER_OUTPUT");
  auto const path = tempConfigPath();
  std::ofstream file(path);
  file << "background_gradient = \"#1234 #01020380\"\n";
  file.close();
  setenv("LAMBDA_WINDOW_MANAGER_CONFIG", path.c_str(), 1);

  auto loaded = lambda::compositor::loadConfigWithMetadata();
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
  ScopedEnv configEnv("LAMBDA_WINDOW_MANAGER_CONFIG");
  ScopedEnv outputEnv("LAMBDA_WINDOW_MANAGER_OUTPUT");
  unsetenv("LAMBDA_WINDOW_MANAGER_OUTPUT");
  auto const path = tempConfigPath();
  std::ofstream file(path);
  file << "[chrome]\n";
  file << "title_bar_height = 44\n";
  file << "controls_width = 96\n";
  file << "button_radius = 8.5\n";
  file << "resize_grip_size = 3\n";
  file << "window_border_color = \"#10203040\"\n";
  file << "window_border_width = 1.5\n";
  file << "close_hover_background = \"#ff0000\"\n";
  file << "[chrome.glass]\n";
  file << "opacity = 0.05\n";
  file << "tint_color = \"#ffffff80\"\n";
  file << "border_color = \"#ffffff90\"\n";
  file << "[chrome.window_corner_radius]\n";
  file << "top_left = 6\n";
  file << "top_right = 7\n";
  file << "bottom_right = 8\n";
  file << "bottom_left = 9\n";
  file << "[chrome.dark]\n";
  file << "title_text_color = \"#e6ecf7\"\n";
  file.close();
  setenv("LAMBDA_WINDOW_MANAGER_CONFIG", path.c_str(), 1);

  auto loaded = lambda::compositor::loadConfigWithMetadata();
  auto const& chrome = loaded.config.chrome;
  CHECK(chrome.titleBarHeight == 44);
  CHECK(chrome.controlsWidth == 96);
  CHECK(chrome.buttonRadius == doctest::Approx(8.5f));
  CHECK(chrome.resizeGripSize == 3);
  CHECK(chrome.glass.opacity == doctest::Approx(0.05f));
  CHECK(chrome.windowBorderColor.a == doctest::Approx(64.f / 255.f));
  CHECK(chrome.glass.borderColor.a == doctest::Approx(144.f / 255.f));
  CHECK(chrome.windowBorderWidth == doctest::Approx(1.5f));
  CHECK(chrome.windowCornerRadius.topLeft == doctest::Approx(6.f));
  CHECK(chrome.windowCornerRadius.topRight == doctest::Approx(7.f));
  CHECK(chrome.windowCornerRadius.bottomRight == doctest::Approx(8.f));
  CHECK(chrome.windowCornerRadius.bottomLeft == doctest::Approx(9.f));
  CHECK(chrome.glass.tintColor.a == doctest::Approx(128.f / 255.f));
  CHECK(chrome.closeHoverBackground.r == doctest::Approx(1.f));
  REQUIRE(loaded.config.darkChrome);
  CHECK(loaded.config.darkChrome->titleTextColor.r == doctest::Approx(230.f / 255.f));

  std::filesystem::remove(path);
}

TEST_CASE("chrome controls scale and center with title bar height") {
  lambda::compositor::ChromeConfig chrome;
  chrome.titleBarHeight = 56;

  auto const metrics = lambda::compositor::chromeControlsMetrics(chrome);
  CHECK(metrics.buttonSize == doctest::Approx(32.f));
  CHECK(metrics.controlsWidth == doctest::Approx(168.f));
  CHECK(metrics.controlWidth == doctest::Approx(56.f));
  CHECK(metrics.insetTop == doctest::Approx(12.f));

  auto const rects = lambda::compositor::chromeControlRects(chrome, 10.f, 20.f, 300.f, 56.f);
  CHECK(rects.controls.x == doctest::Approx(142.f));
  CHECK(rects.minimizeButton.x == doctest::Approx(142.f));
  CHECK(rects.maximizeButton.x == doctest::Approx(198.f));
  CHECK(rects.closeButton.x == doctest::Approx(254.f));
  CHECK(rects.closeButton.y == doctest::Approx(20.f));
  CHECK(rects.closeButton.height == doctest::Approx(56.f));
  CHECK(rects.minimizeButton.height == doctest::Approx(56.f));
  CHECK(rects.maximizeButton.height == doctest::Approx(56.f));
  CHECK(rects.minimizeButton.x + rects.minimizeButton.width == doctest::Approx(rects.maximizeButton.x));
  CHECK(rects.maximizeButton.x + rects.maximizeButton.width == doctest::Approx(rects.closeButton.x));
}

TEST_CASE("compositor config parses rendering backdrop blur settings") {
  ScopedEnv configEnv("LAMBDA_WINDOW_MANAGER_CONFIG");
  ScopedEnv outputEnv("LAMBDA_WINDOW_MANAGER_OUTPUT");
  unsetenv("LAMBDA_WINDOW_MANAGER_OUTPUT");
  auto const path = tempConfigPath();
  std::ofstream file(path);
  file << "[rendering.backdrop_blur]\n";
  file << "base_downsample = 3\n";
  file.close();
  setenv("LAMBDA_WINDOW_MANAGER_CONFIG", path.c_str(), 1);

  auto loaded = lambda::compositor::loadConfigWithMetadata();

  CHECK(loaded.config.rendering.backdropBlurBaseDownsample == 3);

  std::filesystem::remove(path);
}

TEST_CASE("compositor config reports file changes") {
  ScopedEnv configEnv("LAMBDA_WINDOW_MANAGER_CONFIG");
  ScopedEnv outputEnv("LAMBDA_WINDOW_MANAGER_OUTPUT");
  unsetenv("LAMBDA_WINDOW_MANAGER_OUTPUT");
  auto const path = tempConfigPath();
  {
    std::ofstream file(path);
    file << "background = \"#223344\"\n";
  }
  setenv("LAMBDA_WINDOW_MANAGER_CONFIG", path.c_str(), 1);

  auto loaded = lambda::compositor::loadConfigWithMetadata();
  CHECK_FALSE(lambda::compositor::configChanged(loaded));
  std::filesystem::remove(path);
  CHECK(lambda::compositor::configChanged(loaded));
}

TEST_CASE("compositor config parses gradient aliases and scale aliases") {
  ScopedEnv configEnv("LAMBDA_WINDOW_MANAGER_CONFIG");
  ScopedEnv outputEnv("LAMBDA_WINDOW_MANAGER_OUTPUT");
  unsetenv("LAMBDA_WINDOW_MANAGER_OUTPUT");
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
  setenv("LAMBDA_WINDOW_MANAGER_CONFIG", path.c_str(), 1);

  auto loaded = lambda::compositor::loadConfigWithMetadata();
  auto const& config = loaded.config;
  CHECK(config.backgroundColor.r == doctest::Approx(1.f / 255.f));
  CHECK(config.backgroundColor.g == doctest::Approx(2.f / 255.f));
  CHECK(config.backgroundColor.b == doctest::Approx(3.f / 255.f));
  REQUIRE(config.backgroundGradientEnd);
  CHECK(config.backgroundGradientEnd->r == doctest::Approx(160.f / 255.f));
  CHECK(config.backgroundGradientEnd->g == doctest::Approx(176.f / 255.f));
  CHECK(config.backgroundGradientEnd->b == doctest::Approx(192.f / 255.f));
  CHECK(config.wallpaperMode == lambda::ImageFillMode::Tile);
  REQUIRE(config.cursorTheme);
  CHECK(*config.cursorTheme == "Adwaita");
  CHECK(config.cursorSize == 32);
  REQUIRE(config.outputSelector);
  CHECK(*config.outputSelector == "HDMI-A-1");
  CHECK(config.scale == doctest::Approx(1.25f));
  CHECK_FALSE(config.animationsEnabled);
  CHECK(config.hardwareCursorEnabled);
  CHECK(config.keyboard.layout.empty());
  CHECK(config.keyboard.repeatRate == 25);
  CHECK(config.keyboard.repeatDelayMs == 600);

  std::filesystem::remove(path);
}

TEST_CASE("compositor config supports per-output scale overrides") {
  ScopedEnv configEnv("LAMBDA_WINDOW_MANAGER_CONFIG");
  ScopedEnv outputEnv("LAMBDA_WINDOW_MANAGER_OUTPUT");
  unsetenv("LAMBDA_WINDOW_MANAGER_OUTPUT");
  auto const path = tempConfigPath();
  std::ofstream file(path);
  file << "scale = 1.0\n";
  file << "[outputs.\"eDP-1\"]\n";
  file << "scale = 1.25\n";
  file << "[outputs.\"DP-1\"]\n";
  file << "scale = 2.0\n";
  file.close();
  setenv("LAMBDA_WINDOW_MANAGER_CONFIG", path.c_str(), 1);

  auto loaded = lambda::compositor::loadConfigWithMetadata();
  auto const& config = loaded.config;
  CHECK(config.scale == doctest::Approx(1.0f));
  REQUIRE(config.outputScales.contains("eDP-1"));
  REQUIRE(config.outputScales.contains("DP-1"));
  CHECK(config.outputScales.at("eDP-1") == doctest::Approx(1.25f));
  CHECK(config.outputScales.at("DP-1") == doctest::Approx(2.0f));
  CHECK(lambda::compositor::scaleForOutput(config, "eDP-1") == doctest::Approx(1.25f));
  CHECK(lambda::compositor::scaleForOutput(config, "DP-1") == doctest::Approx(2.0f));
  CHECK(lambda::compositor::scaleForOutput(config, "HDMI-A-1") == doctest::Approx(1.0f));

  std::filesystem::remove(path);
}

TEST_CASE("compositor config ignores invalid values and preserves defaults") {
  ScopedEnv configEnv("LAMBDA_WINDOW_MANAGER_CONFIG");
  ScopedEnv outputEnv("LAMBDA_WINDOW_MANAGER_OUTPUT");
  unsetenv("LAMBDA_WINDOW_MANAGER_OUTPUT");
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
  file << "[rendering.backdrop_blur]\n";
  file << "base_downsample = 0\n";
  file << "[input.keyboard]\n";
  file << "layout = 3\n";
  file << "repeat_rate = 500\n";
  file << "repeat_delay_ms = 10\n";
  file << "[keybindings]\n";
  file << "snap_left = \"super+left+right\"\n";
  file << "snap_right = 42\n";
  file.close();
  setenv("LAMBDA_WINDOW_MANAGER_CONFIG", path.c_str(), 1);

  auto loaded = lambda::compositor::loadConfigWithMetadata();
  auto const& config = loaded.config;
  CHECK(config.backgroundColor.r == doctest::Approx(0.20f));
  CHECK(config.backgroundColor.g == doctest::Approx(0.50f));
  CHECK(config.backgroundColor.b == doctest::Approx(0.95f));
  CHECK_FALSE(config.backgroundGradientEnd);
  CHECK(config.wallpaperMode == lambda::ImageFillMode::Cover);
  CHECK_FALSE(config.cursorTheme);
  CHECK(config.cursorSize == 0);
  CHECK(config.scale == doctest::Approx(2.0f));
  CHECK(config.animationsEnabled);
  CHECK(config.hardwareCursorEnabled);
  CHECK(config.rendering.backdropBlurBaseDownsample == 2);
  CHECK(config.keyboard.layout.empty());
  CHECK(config.keyboard.repeatRate == 25);
  CHECK(config.keyboard.repeatDelayMs == 600);

  auto snapLeft = std::find_if(config.shortcutBindings.begin(), config.shortcutBindings.end(), [](auto const& binding) {
    return binding.action == lambda::compositor::WaylandServer::ShortcutAction::SnapLeft;
  });
  REQUIRE(snapLeft != config.shortcutBindings.end());
  CHECK(snapLeft->meta);
  CHECK_FALSE(snapLeft->ctrl);
  CHECK(snapLeft->key == KEY_LEFT);

  std::filesystem::remove(path);
}

TEST_CASE("compositor config parses keyboard input settings") {
  ScopedEnv configEnv("LAMBDA_WINDOW_MANAGER_CONFIG");
  ScopedEnv outputEnv("LAMBDA_WINDOW_MANAGER_OUTPUT");
  unsetenv("LAMBDA_WINDOW_MANAGER_OUTPUT");
  auto const path = tempConfigPath();
  std::ofstream file(path);
  file << "[input]\n";
  file << "popup_grabs = true\n";
  file << "[input.keyboard]\n";
  file << "layout = \"tr\"\n";
  file << "variant = \"f\"\n";
  file << "model = \"pc105\"\n";
  file << "options = \"caps:escape\"\n";
  file << "repeat_rate = 42\n";
  file << "repeat_delay_ms = 320\n";
  file.close();
  setenv("LAMBDA_WINDOW_MANAGER_CONFIG", path.c_str(), 1);

  auto const config = lambda::compositor::loadConfigWithMetadata().config;
  CHECK(config.popupGrabs);
  CHECK(config.keyboard.layout == "tr");
  CHECK(config.keyboard.variant == "f");
  CHECK(config.keyboard.model == "pc105");
  CHECK(config.keyboard.options == "caps:escape");
  CHECK(config.keyboard.repeatRate == 42);
  CHECK(config.keyboard.repeatDelayMs == 320);

  std::filesystem::remove(path);
}

TEST_CASE("compositor config supports shortcut aliases and replacement") {
  ScopedEnv configEnv("LAMBDA_WINDOW_MANAGER_CONFIG");
  ScopedEnv outputEnv("LAMBDA_WINDOW_MANAGER_OUTPUT");
  unsetenv("LAMBDA_WINDOW_MANAGER_OUTPUT");
  auto const path = tempConfigPath();
  std::ofstream file(path);
  file << "[keybindings]\n";
  file << "close_focused = \"shift+super+q\"\n";
  file << "cycle = \"alt+tab\"\n";
  file << "run = \"ctrl+space\"\n";
  file << "screenshot = \"super+shift+3\"\n";
  file << "screenshot_region = \"super+shift+4\"\n";
  file << "screenshot_active_window = \"alt+printscreen\"\n";
  file << "quit = \"ctrl+alt+delete\"\n";
  file.close();
  setenv("LAMBDA_WINDOW_MANAGER_CONFIG", path.c_str(), 1);

  auto const config = lambda::compositor::loadConfigWithMetadata().config;
  auto findAction = [&](lambda::compositor::WaylandServer::ShortcutAction action) {
    return std::find_if(config.shortcutBindings.begin(), config.shortcutBindings.end(), [&](auto const& binding) {
      return binding.action == action;
    });
  };

  auto close = findAction(lambda::compositor::WaylandServer::ShortcutAction::CloseFocused);
  REQUIRE(close != config.shortcutBindings.end());
  CHECK(close->meta);
  CHECK(close->shift);
  CHECK(close->key == KEY_Q);

  auto cycle = findAction(lambda::compositor::WaylandServer::ShortcutAction::CycleFocus);
  REQUIRE(cycle != config.shortcutBindings.end());
  CHECK(cycle->alt);
  CHECK_FALSE(cycle->meta);
  CHECK(cycle->key == KEY_TAB);

  auto launch = findAction(lambda::compositor::WaylandServer::ShortcutAction::LaunchCommand);
  REQUIRE(launch != config.shortcutBindings.end());
  CHECK(launch->ctrl);
  CHECK_FALSE(launch->meta);
  CHECK(launch->key == KEY_SPACE);

  auto terminate = findAction(lambda::compositor::WaylandServer::ShortcutAction::Terminate);
  REQUIRE(terminate != config.shortcutBindings.end());
  CHECK(terminate->ctrl);
  CHECK(terminate->alt);
  CHECK(terminate->key == KEY_DELETE);

  auto screenshot = findAction(lambda::compositor::WaylandServer::ShortcutAction::Screenshot);
  REQUIRE(screenshot != config.shortcutBindings.end());
  CHECK(screenshot->meta);
  CHECK(screenshot->shift);
  CHECK(screenshot->key == KEY_3);

  auto screenshotRegion = findAction(lambda::compositor::WaylandServer::ShortcutAction::ScreenshotRegion);
  REQUIRE(screenshotRegion != config.shortcutBindings.end());
  CHECK(screenshotRegion->meta);
  CHECK(screenshotRegion->shift);
  CHECK(screenshotRegion->key == KEY_4);

  auto screenshotActiveWindow = findAction(lambda::compositor::WaylandServer::ShortcutAction::ScreenshotActiveWindow);
  REQUIRE(screenshotActiveWindow != config.shortcutBindings.end());
  CHECK(screenshotActiveWindow->alt);
  CHECK_FALSE(screenshotActiveWindow->meta);
  CHECK(screenshotActiveWindow->key == KEY_PRINT);

  std::filesystem::remove(path);
}

TEST_CASE("compositor config supports multiple shortcuts per action") {
  ScopedEnv configEnv("LAMBDA_WINDOW_MANAGER_CONFIG");
  ScopedEnv outputEnv("LAMBDA_WINDOW_MANAGER_OUTPUT");
  unsetenv("LAMBDA_WINDOW_MANAGER_OUTPUT");
  auto const path = tempConfigPath();
  std::ofstream file(path);
  file << "[keybindings]\n";
  file << "screenshot = [\"super+shift+3\", \"sysrq\"]\n";
  file << "screenshot_active_window = [\"super+shift+5\", \"alt+printscreen\"]\n";
  file.close();
  setenv("LAMBDA_WINDOW_MANAGER_CONFIG", path.c_str(), 1);

  auto const config = lambda::compositor::loadConfigWithMetadata().config;
  auto countAction = [&](lambda::compositor::WaylandServer::ShortcutAction action) {
    return std::count_if(config.shortcutBindings.begin(), config.shortcutBindings.end(), [&](auto const& binding) {
      return binding.action == action;
    });
  };
  CHECK(countAction(lambda::compositor::WaylandServer::ShortcutAction::Screenshot) == 2);
  CHECK(countAction(lambda::compositor::WaylandServer::ShortcutAction::ScreenshotActiveWindow) == 2);

  auto const hasSysRqScreenshot =
      std::any_of(config.shortcutBindings.begin(), config.shortcutBindings.end(), [](auto const& binding) {
        return binding.action == lambda::compositor::WaylandServer::ShortcutAction::Screenshot &&
               binding.key == KEY_SYSRQ &&
               !binding.meta &&
               !binding.alt &&
               !binding.shift;
      });
  CHECK(hasSysRqScreenshot);

  auto const hasAltPrintWindow =
      std::any_of(config.shortcutBindings.begin(), config.shortcutBindings.end(), [](auto const& binding) {
        return binding.action == lambda::compositor::WaylandServer::ShortcutAction::ScreenshotActiveWindow &&
               binding.key == KEY_PRINT &&
               binding.alt &&
               !binding.meta;
      });
  CHECK(hasAltPrintWindow);

  std::filesystem::remove(path);
}

TEST_CASE("compositor config allows output environment override") {
  ScopedEnv configEnv("LAMBDA_WINDOW_MANAGER_CONFIG");
  ScopedEnv outputEnv("LAMBDA_WINDOW_MANAGER_OUTPUT");
  auto const path = tempConfigPath();
  std::ofstream file(path);
  file << "output = \"HDMI-A-1\"\n";
  file.close();
  setenv("LAMBDA_WINDOW_MANAGER_CONFIG", path.c_str(), 1);
  setenv("LAMBDA_WINDOW_MANAGER_OUTPUT", "secondary", 1);

  auto const config = lambda::compositor::loadConfigWithMetadata().config;
  REQUIRE(config.outputSelector);
  CHECK(*config.outputSelector == "secondary");

  std::filesystem::remove(path);
}

TEST_CASE("compositor config applies output environment override when config is invalid") {
  ScopedEnv configEnv("LAMBDA_WINDOW_MANAGER_CONFIG");
  ScopedEnv outputEnv("LAMBDA_WINDOW_MANAGER_OUTPUT");
  auto const path = tempConfigPath();
  std::ofstream file(path);
  file << "scale = nope\n";
  file.close();
  setenv("LAMBDA_WINDOW_MANAGER_CONFIG", path.c_str(), 1);
  setenv("LAMBDA_WINDOW_MANAGER_OUTPUT", "DP-1", 1);

  auto const config = lambda::compositor::loadConfigWithMetadata().config;
  REQUIRE(config.outputSelector);
  CHECK(*config.outputSelector == "DP-1");

  std::filesystem::remove(path);
}
