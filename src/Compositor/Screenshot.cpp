#include "Compositor/Screenshot.hpp"

#include <zlib.h>

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <stdexcept>

namespace flux::compositor {
namespace {

void writeBigEndian(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 24u) & 0xffu));
  out.push_back(static_cast<std::uint8_t>((value >> 16u) & 0xffu));
  out.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
  out.push_back(static_cast<std::uint8_t>(value & 0xffu));
}

void appendChunk(std::vector<std::uint8_t>& out, char const* type, std::vector<std::uint8_t> const& data) {
  writeBigEndian(out, static_cast<std::uint32_t>(data.size()));
  std::size_t const typeOffset = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), data.begin(), data.end());
  std::uint32_t crc = crc32(0u, nullptr, 0u);
  crc = crc32(crc, out.data() + typeOffset, static_cast<uInt>(4u + data.size()));
  writeBigEndian(out, crc);
}

std::filesystem::path homeDirectory() {
  if (char const* home = std::getenv("HOME"); home && *home) {
    return home;
  }
  return ".";
}

std::string timestampName() {
  auto const now = std::chrono::system_clock::now();
  std::time_t const time = std::chrono::system_clock::to_time_t(now);
  std::tm local{};
  localtime_r(&time, &local);
  char buffer[64]{};
  if (std::strftime(buffer, sizeof(buffer), "Screenshot %Y-%m-%d at %H.%M.%S.png", &local) == 0) {
    return "Screenshot.png";
  }
  return buffer;
}

std::filesystem::path uniquePath(std::filesystem::path path) {
  if (!std::filesystem::exists(path)) {
    return path;
  }
  std::filesystem::path const parent = path.parent_path();
  std::string const stem = path.stem().string();
  std::string const extension = path.extension().string();
  for (int index = 2; index < 1000; ++index) {
    std::filesystem::path candidate = parent / (stem + " " + std::to_string(index) + extension);
    if (!std::filesystem::exists(candidate)) {
      return candidate;
    }
  }
  return path;
}

void writePng(std::filesystem::path const& path,
              std::vector<std::uint8_t> const& bgra,
              std::uint32_t width,
              std::uint32_t height) {
  if (width == 0 || height == 0 || bgra.size() < static_cast<std::size_t>(width) * height * 4u) {
    throw std::runtime_error("invalid screenshot pixels");
  }
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path());
  }

  std::vector<std::uint8_t> scanlines;
  scanlines.reserve(static_cast<std::size_t>(height) * (1u + static_cast<std::size_t>(width) * 4u));
  for (std::uint32_t y = 0; y < height; ++y) {
    scanlines.push_back(0);
    for (std::uint32_t x = 0; x < width; ++x) {
      std::size_t const offset = (static_cast<std::size_t>(y) * width + x) * 4u;
      scanlines.push_back(bgra[offset + 2u]);
      scanlines.push_back(bgra[offset + 1u]);
      scanlines.push_back(bgra[offset + 0u]);
      scanlines.push_back(bgra[offset + 3u]);
    }
  }

  uLongf compressedSize = compressBound(static_cast<uLong>(scanlines.size()));
  std::vector<std::uint8_t> compressed(compressedSize);
  int const result = compress2(compressed.data(),
                               &compressedSize,
                               scanlines.data(),
                               static_cast<uLong>(scanlines.size()),
                               Z_BEST_SPEED);
  if (result != Z_OK) {
    throw std::runtime_error("failed to compress screenshot PNG");
  }
  compressed.resize(compressedSize);

  std::vector<std::uint8_t> png{137, 80, 78, 71, 13, 10, 26, 10};
  std::vector<std::uint8_t> ihdr;
  writeBigEndian(ihdr, width);
  writeBigEndian(ihdr, height);
  ihdr.push_back(8);
  ihdr.push_back(6);
  ihdr.push_back(0);
  ihdr.push_back(0);
  ihdr.push_back(0);
  appendChunk(png, "IHDR", ihdr);
  appendChunk(png, "IDAT", compressed);
  appendChunk(png, "IEND", {});

  std::ofstream output(path, std::ios::binary);
  if (!output) {
    throw std::runtime_error("failed to open screenshot output");
  }
  output.write(reinterpret_cast<char const*>(png.data()), static_cast<std::streamsize>(png.size()));
  if (!output) {
    throw std::runtime_error("failed to write screenshot PNG");
  }
}

} // namespace

std::filesystem::path defaultScreenshotPath() {
  return uniquePath(homeDirectory() / "Pictures" / "Screenshots" / timestampName());
}

ScreenshotSaveResult saveScreenshotPng(std::vector<std::uint8_t> const& bgra,
                                       std::uint32_t width,
                                       std::uint32_t height) {
  ScreenshotSaveResult result{.path = defaultScreenshotPath(), .error = {}};
  try {
    writePng(result.path, bgra, width, height);
    return result;
  } catch (std::exception const& e) {
    result.error = e.what();
    return result;
  } catch (...) {
    result.error = "unknown screenshot error";
    return result;
  }
}

} // namespace flux::compositor
