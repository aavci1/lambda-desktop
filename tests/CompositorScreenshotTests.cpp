#include "Compositor/Screenshot.hpp"

#include <doctest/doctest.h>

#include <array>
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

std::filesystem::path screenshotTempRoot() {
  auto path = std::filesystem::temp_directory_path() /
              ("lambda-window-manager-screenshot-test-" +
               std::to_string(static_cast<unsigned long long>(getpid())));
  std::filesystem::remove_all(path);
  return path;
}

} // namespace

TEST_CASE("screenshot regions normalize and clamp to bounds") {
  using flux::compositor::ScreenshotRegion;
  auto normalized = flux::compositor::normalizeScreenshotRegion(ScreenshotRegion{.x = 80, .y = 60, .width = -120, .height = -80},
                                                               100,
                                                               70);
  REQUIRE(normalized);
  ScreenshotRegion const expected{.x = 0, .y = 0, .width = 80, .height = 60};
  CHECK(*normalized == expected);

  CHECK_FALSE(flux::compositor::normalizeScreenshotRegion(ScreenshotRegion{.x = 10, .y = 10, .width = 0, .height = 20},
                                                          100,
                                                          70));
  CHECK_FALSE(flux::compositor::normalizeScreenshotRegion(ScreenshotRegion{.x = 120, .y = 10, .width = 20, .height = 20},
                                                          100,
                                                          70));
}

TEST_CASE("screenshot logical regions convert to framebuffer coordinates") {
  using flux::compositor::ScreenshotRegion;
  auto region = flux::compositor::logicalRegionToFramebuffer(ScreenshotRegion{.x = 10, .y = 5, .width = 20, .height = 10},
                                                            100,
                                                            50,
                                                            200,
                                                            150);
  REQUIRE(region);
  ScreenshotRegion const expected{.x = 20, .y = 15, .width = 40, .height = 30};
  CHECK(*region == expected);

  auto fractional = flux::compositor::logicalRegionToFramebuffer(ScreenshotRegion{.x = 1, .y = 1, .width = 1, .height = 1},
                                                                3,
                                                                3,
                                                                10,
                                                                10);
  REQUIRE(fractional);
  ScreenshotRegion const expectedFractional{.x = 3, .y = 3, .width = 4, .height = 4};
  CHECK(*fractional == expectedFractional);
}

TEST_CASE("screenshot selection regions normalize drag direction and reject tiny selections") {
  using flux::compositor::ScreenshotRegion;
  auto region = flux::compositor::screenshotSelectionRegion(80.f, 60.f, 10.f, 15.f, 100, 80);
  REQUIRE(region);
  CHECK(*region == ScreenshotRegion{.x = 10, .y = 15, .width = 70, .height = 45});

  CHECK_FALSE(flux::compositor::screenshotSelectionRegion(10.f, 10.f, 11.f, 11.f, 100, 80));
  CHECK_FALSE(flux::compositor::screenshotSelectionRegion(120.f, 120.f, 140.f, 140.f, 100, 80));
}

TEST_CASE("screenshot request policy sets mode region and cursor inclusion") {
  using flux::compositor::ScreenshotMode;
  using flux::compositor::ScreenshotRegion;

  auto full = flux::compositor::makeScreenshotRequest(ScreenshotMode::FullOutput);
  CHECK(full.mode == ScreenshotMode::FullOutput);
  CHECK_FALSE(full.region);
  CHECK(full.includeCursor);

  auto active = flux::compositor::makeScreenshotRequest(ScreenshotMode::ActiveWindow,
                                                        ScreenshotRegion{.x = 1, .y = 2, .width = 3, .height = 4});
  CHECK(active.mode == ScreenshotMode::ActiveWindow);
  CHECK_FALSE(active.region);
  CHECK(active.includeCursor);

  auto region = flux::compositor::makeScreenshotRequest(ScreenshotMode::Region,
                                                        ScreenshotRegion{.x = 5, .y = 6, .width = 7, .height = 8});
  CHECK(region.mode == ScreenshotMode::Region);
  REQUIRE(region.region);
  CHECK(*region.region == ScreenshotRegion{.x = 5, .y = 6, .width = 7, .height = 8});
  CHECK_FALSE(region.includeCursor);
}

TEST_CASE("screenshot BGRA crop preserves selected pixels") {
  using flux::compositor::ScreenshotRegion;
  std::vector<std::uint8_t> pixels(4u * 3u * 4u);
  for (std::uint32_t y = 0; y < 3u; ++y) {
    for (std::uint32_t x = 0; x < 4u; ++x) {
      std::size_t const offset = (static_cast<std::size_t>(y) * 4u + x) * 4u;
      pixels[offset + 0u] = static_cast<std::uint8_t>(x);
      pixels[offset + 1u] = static_cast<std::uint8_t>(y);
      pixels[offset + 2u] = static_cast<std::uint8_t>(10u + x + y * 4u);
      pixels[offset + 3u] = 255u;
    }
  }

  auto cropped = flux::compositor::cropBgra(pixels, 4, 3, ScreenshotRegion{.x = 1, .y = 1, .width = 2, .height = 2});
  REQUIRE(cropped);
  CHECK(cropped->width == 2u);
  CHECK(cropped->height == 2u);
  REQUIRE(cropped->pixels.size() == 16u);
  CHECK(cropped->pixels[0] == 1u);
  CHECK(cropped->pixels[1] == 1u);
  CHECK(cropped->pixels[2] == 15u);
  CHECK(cropped->pixels[4] == 2u);
  CHECK(cropped->pixels[5] == 1u);
  CHECK(cropped->pixels[6] == 16u);
  CHECK(cropped->pixels[8] == 1u);
  CHECK(cropped->pixels[9] == 2u);
  CHECK(cropped->pixels[10] == 19u);
}

TEST_CASE("screenshot rounded mask clears pixels outside active window corners") {
  std::vector<std::uint8_t> pixels(6u * 6u * 4u, 255u);
  flux::compositor::maskBgraToRoundedRect(pixels, 6, 6, flux::CornerRadius{3.f});

  CHECK(pixels[3u] == 0u);
  std::size_t const topRight = (0u * 6u + 5u) * 4u;
  CHECK(pixels[topRight + 3u] == 0u);
  std::size_t const center = (3u * 6u + 3u) * 4u;
  CHECK(pixels[center + 0u] == 255u);
  CHECK(pixels[center + 1u] == 255u);
  CHECK(pixels[center + 2u] == 255u);
  CHECK(pixels[center + 3u] == 255u);
}

TEST_CASE("screenshot default path uses home screenshots directory and avoids collisions") {
  ScopedEnv home("HOME");
  auto const root = screenshotTempRoot();
  std::filesystem::create_directories(root);
  setenv("HOME", root.c_str(), 1);

  auto const first = flux::compositor::defaultScreenshotPath();
  CHECK(first.parent_path() == root / "Pictures" / "Screenshots");
  CHECK(first.extension() == ".png");

  std::filesystem::create_directories(first.parent_path());
  {
    std::ofstream existing(first);
    existing << "existing";
  }

  auto const second = flux::compositor::defaultScreenshotPath();
  CHECK(second.parent_path() == first.parent_path());
  CHECK(second.extension() == ".png");
  CHECK(second != first);

  std::filesystem::remove_all(root);
}

TEST_CASE("screenshot PNG writer creates output file") {
  auto const root = screenshotTempRoot();
  auto const path = root / "nested" / "screenshot.png";
  std::vector<std::uint8_t> const bgra{30u, 20u, 10u, 255u};

  auto const result = flux::compositor::saveScreenshotPng(path, bgra, 1, 1);
  CHECK(result.error.empty());
  CHECK(result.path == path);
  REQUIRE(std::filesystem::exists(path));

  std::ifstream file(path, std::ios::binary);
  std::array<unsigned char, 8> signature{};
  file.read(reinterpret_cast<char*>(signature.data()), static_cast<std::streamsize>(signature.size()));
  CHECK(signature == std::array<unsigned char, 8>{137u, 80u, 78u, 71u, 13u, 10u, 26u, 10u});

  std::filesystem::remove_all(root);
}
