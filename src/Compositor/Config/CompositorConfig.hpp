#pragma once

#include "Compositor/WaylandServer.hpp"

#include <Flux/Core/Color.hpp>
#include <Flux/Graphics/ImageFillMode.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace flux::compositor {

struct CompositorConfig {
  Color backgroundColor{0.20f, 0.50f, 0.95f, 1.0f};
  std::optional<Color> backgroundGradientEnd;
  std::optional<std::string> wallpaperPath;
  ImageFillMode wallpaperMode = ImageFillMode::Cover;
  std::optional<std::string> cursorTheme;
  int cursorSize = 0;
  std::optional<std::string> outputSelector;
  float scale = 2.0f;
  std::unordered_map<std::string, float> outputScales;
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
[[nodiscard]] float scaleForOutput(CompositorConfig const& config, std::string const& outputName);

} // namespace flux::compositor
