#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace flux::compositor {

struct ScreenshotSaveResult {
  std::filesystem::path path;
  std::string error;
};

[[nodiscard]] std::filesystem::path defaultScreenshotPath();
[[nodiscard]] ScreenshotSaveResult saveScreenshotPng(std::vector<std::uint8_t> const& bgra,
                                                     std::uint32_t width,
                                                     std::uint32_t height);

} // namespace flux::compositor
