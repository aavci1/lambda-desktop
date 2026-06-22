#include "Compositor/Config/CompositorConfig.hpp"

#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace {

struct ScopedEnv {
  explicit ScopedEnv(char const* name) : name(name) {
    if (char const* value = std::getenv(name)) {
      hadOriginal = true;
      original = value;
    }
  }
  ~ScopedEnv() {
    if (!hadOriginal) unsetenv(name);
    else setenv(name, original.c_str(), 1);
  }
  char const* name;
  bool hadOriginal = false;
  std::string original;
};

} // namespace

TEST_CASE("compositor config hot reload detects scale and wallpaper edits") {
  ScopedEnv configEnv("LAMBDA_WINDOW_MANAGER_CONFIG");
  ScopedEnv outputEnv("LAMBDA_WINDOW_MANAGER_OUTPUT");
  unsetenv("LAMBDA_WINDOW_MANAGER_OUTPUT");
  auto const path = std::filesystem::temp_directory_path() /
                    ("lambda-wm-hot-reload-" + std::to_string(static_cast<unsigned long long>(getpid())) + ".toml");
  {
    std::ofstream file(path);
    file << "scale = 1.0\n";
    file << "wallpaper = \"/tmp/a.png\"\n";
  }
  setenv("LAMBDA_WINDOW_MANAGER_CONFIG", path.c_str(), 1);

  auto loaded = lambda::compositor::loadConfigWithMetadata();
  CHECK(loaded.config.scale == doctest::Approx(1.0f));
  REQUIRE(loaded.config.wallpaperPath);
  CHECK(*loaded.config.wallpaperPath == "/tmp/a.png");
  CHECK_FALSE(lambda::compositor::configChanged(loaded));

  {
    std::ofstream file(path);
    file << "scale = 1.25\n";
    file << "wallpaper = \"/tmp/b.png\"\n";
  }
  CHECK(lambda::compositor::configChanged(loaded));

  loaded = lambda::compositor::loadConfigWithMetadata();
  CHECK(loaded.config.scale == doctest::Approx(1.25f));
  REQUIRE(loaded.config.wallpaperPath);
  CHECK(*loaded.config.wallpaperPath == "/tmp/b.png");

  std::filesystem::remove(path);
}
