#include "Compositor/Config/WallpaperCache.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <system_error>

namespace lambdaui::compositor {
namespace {

constexpr char kCacheMagic[4] = {'L', 'M', 'W', 'P'};
constexpr std::uint32_t kCacheVersion = 1u;

struct CacheHeader {
  char magic[4];
  std::uint32_t version = 0;
  std::uint64_t sourceMtimeNs = 0;
  std::uint64_t sourceSize = 0;
  std::uint32_t maxLongEdge = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

std::uint64_t fnv1a64(std::string_view text) {
  std::uint64_t hash = 1469598103934665603ull;
  for (unsigned char c : text) {
    hash ^= static_cast<std::uint64_t>(c);
    hash *= 1099511628211ull;
  }
  return hash;
}

std::optional<std::pair<std::uint64_t, std::uint64_t>> sourceFileIdentity(std::filesystem::path const& path) {
  std::error_code ec;
  if (!std::filesystem::exists(path, ec) || ec) {
    return std::nullopt;
  }
  auto const size = std::filesystem::file_size(path, ec);
  if (ec) {
    return std::nullopt;
  }
  auto const mtime = std::filesystem::last_write_time(path, ec);
  if (ec) {
    return std::nullopt;
  }
  auto const ns = std::chrono::duration_cast<std::chrono::nanoseconds>(mtime.time_since_epoch()).count();
  return std::pair{static_cast<std::uint64_t>(ns), static_cast<std::uint64_t>(size)};
}

std::filesystem::path cacheFilePath(std::filesystem::path const& sourcePath,
                                    std::uint32_t maxLongEdge,
                                    std::filesystem::path const& cacheRoot) {
  std::string const canonical = std::filesystem::weakly_canonical(sourcePath).string();
  std::uint64_t const hash = fnv1a64(canonical);
  char name[64];
  std::snprintf(name, sizeof(name), "%016llx_%u.wp", static_cast<unsigned long long>(hash),
                static_cast<unsigned>(maxLongEdge));
  return cacheRoot / name;
}

} // namespace

std::filesystem::path wallpaperCacheDirectory(std::filesystem::path const& cacheRoot) {
  std::filesystem::path dir = cacheRoot / "wallpaper";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  return dir;
}

std::optional<lambdaui::DecodedImageRgba> readWallpaperCache(std::filesystem::path const& sourcePath,
                                                          std::uint32_t maxLongEdge,
                                                          std::filesystem::path const& cacheRoot) {
  auto const identity = sourceFileIdentity(sourcePath);
  if (!identity) {
    return std::nullopt;
  }

  std::filesystem::path const file = cacheFilePath(sourcePath, maxLongEdge, cacheRoot);
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    return std::nullopt;
  }

  CacheHeader header{};
  in.read(reinterpret_cast<char*>(&header), sizeof(header));
  if (!in || std::memcmp(header.magic, kCacheMagic, sizeof(kCacheMagic)) != 0 ||
      header.version != kCacheVersion || header.maxLongEdge != maxLongEdge ||
      header.sourceMtimeNs != identity->first || header.sourceSize != identity->second ||
      header.width == 0 || header.height == 0) {
    return std::nullopt;
  }

  std::size_t const pixelBytes =
      static_cast<std::size_t>(header.width) * static_cast<std::size_t>(header.height) * 4u;
  lambdaui::DecodedImageRgba decoded{
      .width = header.width,
      .height = header.height,
      .pixels = {},
  };
  decoded.pixels.resize(pixelBytes);
  in.read(reinterpret_cast<char*>(decoded.pixels.data()), static_cast<std::streamsize>(pixelBytes));
  if (!in) {
    return std::nullopt;
  }
  return decoded;
}

bool writeWallpaperCache(std::filesystem::path const& sourcePath,
                         std::uint32_t maxLongEdge,
                         std::filesystem::path const& cacheRoot,
                         lambdaui::DecodedImageRgba const& image) {
  auto const identity = sourceFileIdentity(sourcePath);
  if (!identity || image.width == 0 || image.height == 0 ||
      image.pixels.size() != static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4u) {
    return false;
  }

  std::error_code ec;
  std::filesystem::create_directories(cacheRoot, ec);

  std::filesystem::path const file = cacheFilePath(sourcePath, maxLongEdge, cacheRoot);
  std::filesystem::path const temp = file.string() + ".tmp";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) {
      return false;
    }
    CacheHeader header{};
    std::memcpy(header.magic, kCacheMagic, sizeof(kCacheMagic));
    header.version = kCacheVersion;
    header.sourceMtimeNs = identity->first;
    header.sourceSize = identity->second;
    header.maxLongEdge = maxLongEdge;
    header.width = image.width;
    header.height = image.height;
    out.write(reinterpret_cast<char const*>(&header), sizeof(header));
    out.write(reinterpret_cast<char const*>(image.pixels.data()),
              static_cast<std::streamsize>(image.pixels.size()));
    if (!out) {
      return false;
    }
  }
  std::filesystem::rename(temp, file, ec);
  if (ec) {
    std::filesystem::remove(temp, ec);
    return false;
  }
  return true;
}

} // namespace lambdaui::compositor
