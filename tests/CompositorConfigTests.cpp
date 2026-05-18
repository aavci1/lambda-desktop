#include "Compositor/Config/CompositorConfig.hpp"

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

  std::filesystem::remove_all(path.parent_path());
}

TEST_CASE("compositor config parses colors, wallpaper, and keybindings") {
  ScopedEnv configEnv("FLUX_COMPOSITOR_CONFIG");
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

TEST_CASE("compositor config reports file changes") {
  ScopedEnv configEnv("FLUX_COMPOSITOR_CONFIG");
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
  auto const path = tempConfigPath();
  std::ofstream file(path);
  file << "background_gradient = \"#010203,#a0b0c0\"\n";
  file << "wallpaper_mode = \"tile\"\n";
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
  CHECK(config.scale == doctest::Approx(1.25f));
  CHECK_FALSE(config.animationsEnabled);
  CHECK(config.hardwareCursorEnabled);

  std::filesystem::remove(path);
}

TEST_CASE("compositor config ignores invalid values and preserves defaults") {
  ScopedEnv configEnv("FLUX_COMPOSITOR_CONFIG");
  auto const path = tempConfigPath();
  std::ofstream file(path);
  file << "background = \"nope\"\n";
  file << "background_gradient = \"#112233 nope\"\n";
  file << "wallpaper_mode = \"invalid\"\n";
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
  auto const path = tempConfigPath();
  std::ofstream file(path);
  file << "[keybindings]\n";
  file << "close_focused = \"shift+super+q\"\n";
  file << "cycle = \"alt+tab\"\n";
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

  auto terminate = findAction(flux::compositor::WaylandServer::ShortcutAction::Terminate);
  REQUIRE(terminate != config.shortcutBindings.end());
  CHECK(terminate->ctrl);
  CHECK(terminate->alt);
  CHECK(terminate->key == KEY_DELETE);

  std::filesystem::remove(path);
}
