#pragma once

#include <Lambda/Graphics/Image.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

namespace lambda::compositor {

[[nodiscard]] std::filesystem::path wallpaperCacheDirectory(std::filesystem::path const& cacheRoot);

[[nodiscard]] std::optional<lambda::DecodedImageRgba>
readWallpaperCache(std::filesystem::path const& sourcePath,
                   std::uint32_t maxLongEdge,
                   std::filesystem::path const& cacheRoot);

[[nodiscard]] bool writeWallpaperCache(std::filesystem::path const& sourcePath,
                                       std::uint32_t maxLongEdge,
                                       std::filesystem::path const& cacheRoot,
                                       lambda::DecodedImageRgba const& image);

} // namespace lambda::compositor
