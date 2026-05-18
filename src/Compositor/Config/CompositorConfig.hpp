#pragma once

#include "Compositor/WaylandServer.hpp"

#include <Flux/Core/Color.hpp>
#include <Flux/Graphics/ImageFillMode.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace flux::compositor {

struct CompositorConfig {
  Color backgroundColor{0.20f, 0.50f, 0.95f, 1.0f};
  std::optional<Color> backgroundGradientEnd;
  std::optional<std::string> wallpaperPath;
  ImageFillMode wallpaperMode = ImageFillMode::Cover;
  std::optional<std::string> cursorTheme;
  int cursorSize = 0;
  float scale = 2.0f;
  bool animationsEnabled = true;
  bool hardwareCursorEnabled = true;
  std::vector<WaylandServer::ShortcutBinding> shortcutBindings;
};

struct LoadedCompositorConfig {
  CompositorConfig config;
  std::optional<std::string> path;
  std::filesystem::file_time_type modifiedAt{};
  bool hasModifiedAt = false;
};

[[nodiscard]] LoadedCompositorConfig loadConfigWithMetadata();
[[nodiscard]] bool configChanged(LoadedCompositorConfig const& loaded);

} // namespace flux::compositor
