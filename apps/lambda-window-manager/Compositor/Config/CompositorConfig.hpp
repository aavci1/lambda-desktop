#pragma once

#include "Compositor/Chrome/ChromeConfig.hpp"
#include "Compositor/WaylandServer.hpp"

#include <Lambda/Core/Color.hpp>
#include <Lambda/Graphics/ImageFillMode.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace lambda::compositor {

struct CompositorRenderingConfig {
  std::uint32_t backdropBlurBaseDownsample = 2;
};

struct CompositorConfig {
  Color backgroundColor{0.20f, 0.50f, 0.95f, 1.0f};
  std::optional<Color> backgroundGradientEnd;
  bool backgroundConfigured = false;
  std::optional<std::string> wallpaperPath;
  ImageFillMode wallpaperMode = ImageFillMode::Cover;
  std::optional<std::string> cursorTheme;
  int cursorSize = 0;
  std::optional<std::string> outputSelector;
  float scale = 2.0f;
  std::unordered_map<std::string, float> outputScales;
  bool animationsEnabled = true;
  bool hardwareCursorEnabled = true;
  int idleBlankTimeoutSeconds = 0;
  CompositorRenderingConfig rendering;
  ChromeConfig chrome;
  std::optional<ChromeConfig> darkChrome;
  CompositorKeyboardConfig keyboard;
  std::vector<WaylandServer::ShortcutBinding> shortcutBindings;
  bool popupGrabs = true;
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

} // namespace lambda::compositor
