#pragma once

#include <Lambda/Graphics/Image.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

namespace lambdaui::compositor {

[[nodiscard]] std::filesystem::path wallpaperCacheDirectory(std::filesystem::path const& cacheRoot);

[[nodiscard]] std::optional<lambdaui::DecodedImageRgba>
readWallpaperCache(std::filesystem::path const& sourcePath,
                   std::uint32_t maxLongEdge,
                   std::filesystem::path const& cacheRoot);

[[nodiscard]] bool writeWallpaperCache(std::filesystem::path const& sourcePath,
                                       std::uint32_t maxLongEdge,
                                       std::filesystem::path const& cacheRoot,
                                       lambdaui::DecodedImageRgba const& image);

} // namespace lambdaui::compositor
