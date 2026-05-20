#include "Compositor/Config/CompositorConfig.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <linux/input-event-codes.h>
#include <optional>
#include <charconv>
#include <string>
#include <string_view>
#include <unordered_map>

#include <toml++/toml.hpp>

namespace flux::compositor {
namespace {

std::string trim(std::string_view value) {
  std::size_t begin = 0;
  while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
  std::size_t end = value.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1u]))) --end;
  return std::string(value.substr(begin, end - begin));
}

std::optional<unsigned int> hexDigit(char c) {
  if (c >= '0' && c <= '9') return static_cast<unsigned int>(c - '0');
  if (c >= 'a' && c <= 'f') return static_cast<unsigned int>(c - 'a' + 10);
  if (c >= 'A' && c <= 'F') return static_cast<unsigned int>(c - 'A' + 10);
  return std::nullopt;
}

std::optional<unsigned int> hexByte(std::string_view value, std::size_t offset) {
  auto hi = hexDigit(value[offset]);
  auto lo = hexDigit(value[offset + 1u]);
  if (!hi || !lo) return std::nullopt;
  return (*hi << 4u) | *lo;
}

std::optional<Color> parseHexColor(std::string_view value) {
  std::string text = trim(value);
  if (text.empty() || text[0] != '#') return std::nullopt;
  std::optional<unsigned int> red;
  std::optional<unsigned int> green;
  std::optional<unsigned int> blue;
  std::optional<unsigned int> alpha = 255u;
  if (text.size() == 4 || text.size() == 5) {
    red = hexDigit(text[1]);
    green = hexDigit(text[2]);
    blue = hexDigit(text[3]);
    if (text.size() == 5) alpha = hexDigit(text[4]);
    if (!red || !green || !blue || !alpha) return std::nullopt;
    red = (*red << 4u) | *red;
    green = (*green << 4u) | *green;
    blue = (*blue << 4u) | *blue;
    alpha = (*alpha << 4u) | *alpha;
  } else if (text.size() == 7 || text.size() == 9) {
    red = hexByte(text, 1);
    green = hexByte(text, 3);
    blue = hexByte(text, 5);
    if (text.size() == 9) alpha = hexByte(text, 7);
    if (!red || !green || !blue || !alpha) return std::nullopt;
  } else {
    return std::nullopt;
  }
  return Color{
      static_cast<float>(*red) / 255.f,
      static_cast<float>(*green) / 255.f,
      static_cast<float>(*blue) / 255.f,
      static_cast<float>(*alpha) / 255.f,
  };
}

std::optional<std::pair<Color, Color>> parseLinearGradient(std::string_view value) {
  std::string text = trim(value);
  std::replace(text.begin(), text.end(), ',', ' ');
  std::size_t const split = text.find_first_of(" \t");
  if (split == std::string::npos) return std::nullopt;
  std::string first = trim(std::string_view(text).substr(0, split));
  std::string second = trim(std::string_view(text).substr(split + 1u));
  if (first.empty() || second.empty()) return std::nullopt;
  auto from = parseHexColor(first);
  auto to = parseHexColor(second);
  if (!from || !to) return std::nullopt;
  return std::pair{*from, *to};
}

std::optional<bool> parseBool(std::string_view value) {
  std::string text = trim(value);
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  if (text == "true" || text == "yes" || text == "on" || text == "1") return true;
  if (text == "false" || text == "no" || text == "off" || text == "0") return false;
  return std::nullopt;
}

std::optional<float> parseScale(std::string_view value) {
  std::string text = trim(value);
  float scale = 1.f;
  auto const* begin = text.data();
  auto const* end = text.data() + text.size();
  auto [ptr, error] = std::from_chars(begin, end, scale);
  if (error != std::errc{} || ptr != end || scale < 0.5f || scale > 4.f) return std::nullopt;
  return scale;
}

std::optional<float> parseFloat(std::string_view value) {
  std::string text = trim(value);
  float result = 0.f;
  auto const* begin = text.data();
  auto const* end = text.data() + text.size();
  auto [ptr, error] = std::from_chars(begin, end, result);
  if (error != std::errc{} || ptr != end) return std::nullopt;
  return result;
}

std::optional<int> parseInteger(std::string_view value) {
  std::string text = trim(value);
  int result = 0;
  auto const* begin = text.data();
  auto const* end = text.data() + text.size();
  auto [ptr, error] = std::from_chars(begin, end, result);
  if (error != std::errc{} || ptr != end) return std::nullopt;
  return result;
}

std::string lowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

std::optional<ImageFillMode> parseImageFillMode(std::string_view value) {
  std::string text = lowerAscii(trim(value));
  if (text == "stretch" || text == "fill") return ImageFillMode::Stretch;
  if (text == "fit" || text == "contain") return ImageFillMode::Fit;
  if (text == "cover") return ImageFillMode::Cover;
  if (text == "center" || text == "none") return ImageFillMode::Center;
  if (text == "tile" || text == "repeat") return ImageFillMode::Tile;
  return std::nullopt;
}

std::optional<bool> configBool(toml::table const& table, char const* key) {
  if (auto value = table[key].value<bool>()) return *value;
  if (auto value = table[key].value<std::string>()) return parseBool(*value);
  return std::nullopt;
}

std::optional<std::string> configString(toml::table const& table, char const* key) {
  if (auto value = table[key].value<std::string>()) return *value;
  return std::nullopt;
}

std::optional<float> configFloat(toml::table const& table, char const* key) {
  if (auto value = table[key].value<double>()) return static_cast<float>(*value);
  if (auto value = table[key].value<int64_t>()) return static_cast<float>(*value);
  if (auto value = table[key].value<std::string>()) return parseScale(*value);
  return std::nullopt;
}

std::optional<float> configNumber(toml::table const& table, char const* key) {
  if (auto value = table[key].value<double>()) return static_cast<float>(*value);
  if (auto value = table[key].value<int64_t>()) return static_cast<float>(*value);
  if (auto value = table[key].value<std::string>()) return parseFloat(*value);
  return std::nullopt;
}

std::optional<int> configInt(toml::table const& table, char const* key) {
  if (auto value = table[key].value<int64_t>()) return static_cast<int>(*value);
  if (auto value = table[key].value<std::string>()) return parseInteger(*value);
  return std::nullopt;
}

std::vector<WaylandServer::ShortcutBinding> defaultShortcutBindings() {
  using Action = WaylandServer::ShortcutAction;
  return {
      {.action = Action::CloseFocused, .key = KEY_Q, .meta = true},
      {.action = Action::CycleFocus, .key = KEY_TAB, .meta = true},
      {.action = Action::SnapLeft, .key = KEY_LEFT, .meta = true},
      {.action = Action::SnapRight, .key = KEY_RIGHT, .meta = true},
      {.action = Action::Maximize, .key = KEY_UP, .meta = true},
      {.action = Action::Restore, .key = KEY_DOWN, .meta = true},
      {.action = Action::LaunchCommand, .key = KEY_SPACE, .meta = true},
      {.action = Action::Terminate, .key = KEY_BACKSPACE, .ctrl = true, .alt = true},
  };
}

std::optional<std::uint32_t> keyCodeForName(std::string const& token) {
  static std::unordered_map<std::string, std::uint32_t> const keyCodes{
      {"a", KEY_A},
      {"backspace", KEY_BACKSPACE},
      {"b", KEY_B},
      {"c", KEY_C},
      {"d", KEY_D},
      {"delete", KEY_DELETE},
      {"down", KEY_DOWN},
      {"e", KEY_E},
      {"enter", KEY_ENTER},
      {"escape", KEY_ESC},
      {"esc", KEY_ESC},
      {"f", KEY_F},
      {"g", KEY_G},
      {"h", KEY_H},
      {"i", KEY_I},
      {"j", KEY_J},
      {"k", KEY_K},
      {"l", KEY_L},
      {"left", KEY_LEFT},
      {"m", KEY_M},
      {"n", KEY_N},
      {"o", KEY_O},
      {"p", KEY_P},
      {"q", KEY_Q},
      {"r", KEY_R},
      {"right", KEY_RIGHT},
      {"s", KEY_S},
      {"space", KEY_SPACE},
      {"t", KEY_T},
      {"tab", KEY_TAB},
      {"u", KEY_U},
      {"up", KEY_UP},
      {"v", KEY_V},
      {"w", KEY_W},
      {"x", KEY_X},
      {"y", KEY_Y},
      {"z", KEY_Z},
  };
  auto found = keyCodes.find(token);
  if (found == keyCodes.end()) return std::nullopt;
  return found->second;
}

std::optional<WaylandServer::ShortcutBinding> parseShortcut(WaylandServer::ShortcutAction action,
                                                            std::string_view value) {
  auto text = lowerAscii(trim(value));
  WaylandServer::ShortcutBinding binding{.action = action};
  std::size_t start = 0;
  while (start <= text.size()) {
    std::size_t const end = text.find('+', start);
    std::string token = trim(std::string_view(text).substr(start, end == std::string::npos ? end : end - start));
    if (token.empty()) return std::nullopt;
    if (token == "super" || token == "meta" || token == "logo" || token == "mod4") {
      binding.meta = true;
    } else if (token == "ctrl" || token == "control") {
      binding.ctrl = true;
    } else if (token == "alt") {
      binding.alt = true;
    } else if (token == "shift") {
      binding.shift = true;
    } else if (auto key = keyCodeForName(token)) {
      if (binding.key != 0) return std::nullopt;
      binding.key = *key;
    } else {
      return std::nullopt;
    }
    if (end == std::string::npos) break;
    start = end + 1u;
  }
  if (binding.key == 0) return std::nullopt;
  return binding;
}

std::optional<WaylandServer::ShortcutAction> shortcutActionForKey(std::string const& key) {
  using Action = WaylandServer::ShortcutAction;
  static std::unordered_map<std::string, Action> const actions{
      {"close", Action::CloseFocused},
      {"close_focused", Action::CloseFocused},
      {"cycle", Action::CycleFocus},
      {"cycle_focus", Action::CycleFocus},
      {"maximize", Action::Maximize},
      {"quit", Action::Terminate},
      {"restore", Action::Restore},
      {"launch", Action::LaunchCommand},
      {"launch_command", Action::LaunchCommand},
      {"run", Action::LaunchCommand},
      {"snap_left", Action::SnapLeft},
      {"snap_right", Action::SnapRight},
      {"terminate", Action::Terminate},
  };
  auto found = actions.find(key);
  if (found == actions.end()) return std::nullopt;
  return found->second;
}

void replaceShortcutBinding(std::vector<WaylandServer::ShortcutBinding>& bindings,
                            WaylandServer::ShortcutBinding binding) {
  auto found = std::find_if(bindings.begin(), bindings.end(), [&](auto const& existing) {
    return existing.action == binding.action;
  });
  if (found == bindings.end()) {
    bindings.push_back(binding);
  } else {
    *found = binding;
  }
}

void parseChromeConfig(toml::table const& table, ChromeConfig& chrome, char const* path) {
  auto parseIntField = [&](char const* key, std::int32_t& field, std::int32_t minValue, std::int32_t maxValue) {
    if (!table.contains(key)) return;
    if (auto value = configInt(table, key); value && *value >= minValue && *value <= maxValue) {
      field = *value;
    } else {
      std::fprintf(stderr, "flux-compositor: ignoring invalid chrome.%s value in %s\n", key, path);
    }
  };
  auto parseFloatField = [&](char const* key, float& field, float minValue, float maxValue) {
    if (!table.contains(key)) return;
    if (auto value = configNumber(table, key); value && *value >= minValue && *value <= maxValue) {
      field = *value;
    } else {
      std::fprintf(stderr, "flux-compositor: ignoring invalid chrome.%s value in %s\n", key, path);
    }
  };
  auto parseColorField = [&](char const* key, Color& field) {
    if (!table.contains(key)) return;
    if (auto value = configString(table, key); value) {
      if (auto color = parseHexColor(*value)) {
        field = *color;
      } else {
        std::fprintf(stderr, "flux-compositor: ignoring invalid chrome.%s color in %s\n", key, path);
      }
    } else {
      std::fprintf(stderr, "flux-compositor: ignoring non-string chrome.%s color in %s\n", key, path);
    }
  };

  parseIntField("title_bar_height", chrome.titleBarHeight, 16, 120);
  parseIntField("controls_width", chrome.controlsWidth, 32, 240);
  parseIntField("controls_inset_right", chrome.controlsInsetRight, 0, 120);
  parseIntField("controls_inset_top", chrome.controlsInsetTop, 0, 80);
  parseIntField("button_size", chrome.buttonSize, 12, 80);
  parseFloatField("button_radius", chrome.buttonRadius, 0.f, 40.f);
  parseIntField("button_gap", chrome.buttonGap, 0, 80);
  parseColorField("close_glyph_color", chrome.closeGlyphColor);
  parseColorField("close_glyph_hover_color", chrome.closeGlyphHoverColor);
  parseColorField("close_hover_background", chrome.closeHoverBackground);
  parseColorField("minimize_glyph_color", chrome.minimizeGlyphColor);
  parseColorField("minimize_glyph_hover_color", chrome.minimizeGlyphHoverColor);
  parseColorField("minimize_hover_background", chrome.minimizeHoverBackground);
  parseColorField("title_text_color", chrome.titleTextColor);
  parseFloatField("title_text_font_size", chrome.titleTextFontSize, 6.f, 40.f);
  parseFloatField("title_text_font_weight", chrome.titleTextFontWeight, 100.f, 1000.f);
  parseFloatField("window_corner_radius", chrome.windowCornerRadius, 0.f, 48.f);
  parseColorField("glass_tint", chrome.glassTint);
  parseFloatField("glass_blur_radius", chrome.glassBlurRadius, 0.f, 120.f);
  parseColorField("border_line_color", chrome.borderLineColor);
  parseColorField("inset_highlight_color", chrome.insetHighlightColor);
  parseColorField("focused_shadow_color", chrome.focusedShadowColor);
  parseColorField("unfocused_shadow_color", chrome.unfocusedShadowColor);
}

std::optional<std::string> configPath() {
  if (char const* explicitPath = std::getenv("FLUX_COMPOSITOR_CONFIG"); explicitPath && *explicitPath) {
    return std::string(explicitPath);
  }
  if (char const* configHome = std::getenv("XDG_CONFIG_HOME"); configHome && *configHome) {
    return std::string(configHome) + "/flux-compositor/config.toml";
  }
  if (char const* home = std::getenv("HOME"); home && *home) {
    return std::string(home) + "/.config/flux-compositor/config.toml";
  }
  return std::nullopt;
}

void applyEnvironmentOverrides(CompositorConfig& config) {
  if (char const* outputEnv = std::getenv("FLUX_COMPOSITOR_OUTPUT"); outputEnv && *outputEnv) {
    config.outputSelector = trim(outputEnv);
  }
}

std::string resolveConfigPathValue(std::string_view value, std::string const& configFilePath) {
  std::string text = trim(value);
  if (text.empty()) return text;
  if (text == "~") {
    if (char const* home = std::getenv("HOME"); home && *home) return std::string(home);
    return text;
  }
  if (text.rfind("~/", 0) == 0) {
    if (char const* home = std::getenv("HOME"); home && *home) {
      return (std::filesystem::path(home) / text.substr(2)).lexically_normal().string();
    }
    return text;
  }

  std::filesystem::path resolved(text);
  if (resolved.is_absolute()) return resolved.lexically_normal().string();

  std::filesystem::path const configPath(configFilePath);
  std::filesystem::path const base = configPath.has_parent_path()
                                         ? configPath.parent_path()
                                         : std::filesystem::current_path();
  return (base / resolved).lexically_normal().string();
}

std::string defaultConfigToml() {
  return R"(# Flux compositor configuration.
#
# The compositor reloads this file when it changes. Scale is compositor-level:
# clients see the corresponding logical output size and render into that space.

background = "#3380f2"
# background_gradient = "#203040 #405060"
# wallpaper = "/path/to/wallpaper.jpg"
# wallpaper_mode = "cover" # cover, contain, stretch, center, tile
# cursor_theme = "Adwaita" # unset uses XCURSOR_THEME or system default
# cursor_size = 24 # unset uses XCURSOR_SIZE or 24
# output = "HDMI-A-1" # connector name, 0-based index, "primary", or "secondary"

scale = 2.0 # fallback scale for outputs without an override
#
# Per-output scale overrides use connector names:
# [outputs."eDP-1"]
# scale = 1.25
#
# [outputs."DP-1"]
# scale = 2.0

animations = true
hardware_cursor = true

[chrome]
title_bar_height = 42
controls_width = 90
controls_inset_right = 10
controls_inset_top = 8
button_size = 26
button_radius = 7
button_gap = 4
close_glyph_color = "#5b6781"
close_glyph_hover_color = "#ffffff"
close_hover_background = "#e25555"
minimize_glyph_color = "#5b6781"
minimize_glyph_hover_color = "#16203a"
minimize_hover_background = "#00000012"
title_text_color = "#16203a"
title_text_font_size = 12.5
title_text_font_weight = 600
window_corner_radius = 14
glass_tint = "#ffffffcc"
glass_blur_radius = 32
border_line_color = "#141e3c14"

[keybindings]
close = "super+q"
cycle_focus = "super+tab"
snap_left = "super+left"
snap_right = "super+right"
maximize = "super+up"
restore = "super+down"
launch_command = "super+space"
terminate = "ctrl+alt+backspace"
)";
}

void ensureDefaultConfigFile(std::string const& path) {
  std::error_code error;
  if (std::filesystem::exists(path, error)) return;
  if (error) {
    std::fprintf(stderr, "flux-compositor: cannot check config %s: %s\n", path.c_str(), error.message().c_str());
    return;
  }

  std::filesystem::path const configPath(path);
  auto const parent = configPath.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, error);
    if (error) {
      std::fprintf(stderr,
                   "flux-compositor: cannot create config directory %s: %s\n",
                   parent.string().c_str(),
                   error.message().c_str());
      return;
    }
  }

  std::ofstream file(path);
  if (!file) {
    std::fprintf(stderr, "flux-compositor: cannot create config %s\n", path.c_str());
    return;
  }
  file << defaultConfigToml();
  file.close();
  if (!file) {
    std::fprintf(stderr, "flux-compositor: failed while writing config %s\n", path.c_str());
    return;
  }
  std::fprintf(stderr, "flux-compositor: created default config %s\n", path.c_str());
}

CompositorConfig loadConfig() {
  CompositorConfig config;
  config.shortcutBindings = defaultShortcutBindings();
  auto path = configPath();
  if (!path) {
    applyEnvironmentOverrides(config);
    return config;
  }

  toml::table table;
  try {
    table = toml::parse_file(*path);
  } catch (toml::parse_error const& error) {
    auto const message = std::string(error.description());
    std::fprintf(stderr,
                 "flux-compositor: ignoring invalid config %s: %s\n",
                 path->c_str(),
                 message.c_str());
    applyEnvironmentOverrides(config);
    return config;
  }

  if (auto* keybindings = table["keybindings"].as_table()) {
    for (auto&& [key, node] : *keybindings) {
      std::string actionName = lowerAscii(std::string(key.str()));
      if (auto action = shortcutActionForKey(actionName)) {
        if (auto value = node.value<std::string>(); value) {
          if (auto binding = parseShortcut(*action, *value)) {
            replaceShortcutBinding(config.shortcutBindings, *binding);
          } else {
            std::fprintf(stderr,
                         "flux-compositor: ignoring invalid keybinding %s in %s\n",
                         actionName.c_str(),
                         path->c_str());
          }
        } else {
          std::fprintf(stderr,
                       "flux-compositor: ignoring non-string keybinding %s in %s\n",
                       actionName.c_str(),
                       path->c_str());
        }
      }
    }
  }

  auto parseColorKey = [&](char const* key) -> std::optional<Color> {
    if (auto value = configString(table, key)) return parseHexColor(*value);
    return std::nullopt;
  };

  if (auto color = parseColorKey("background")) {
    config.backgroundColor = *color;
    config.backgroundGradientEnd.reset();
  } else if (auto color = parseColorKey("background_color")) {
    config.backgroundColor = *color;
    config.backgroundGradientEnd.reset();
  } else if (table.contains("background") || table.contains("background_color")) {
    std::fprintf(stderr, "flux-compositor: ignoring invalid background color in %s\n", path->c_str());
  }

  if (auto value = configString(table, "background_gradient")) {
    if (auto gradient = parseLinearGradient(*value)) {
      config.backgroundColor = gradient->first;
      config.backgroundGradientEnd = gradient->second;
    } else {
      std::fprintf(stderr, "flux-compositor: ignoring invalid background gradient in %s\n", path->c_str());
    }
  }

  auto parseWallpaper = [&](char const* key) -> bool {
    if (auto wallpaper = configString(table, key); wallpaper && !wallpaper->empty()) {
      config.wallpaperPath = resolveConfigPathValue(*wallpaper, *path);
      return true;
    }
    return false;
  };
  if (!parseWallpaper("wallpaper")) parseWallpaper("wallpaper_path");

  auto parseWallpaperMode = [&](char const* key) -> bool {
    if (auto value = configString(table, key)) {
      if (auto mode = parseImageFillMode(*value)) {
        config.wallpaperMode = *mode;
        return true;
      }
      std::fprintf(stderr, "flux-compositor: ignoring invalid wallpaper mode in %s\n", path->c_str());
      return true;
    }
    return false;
  };
  if (!parseWallpaperMode("wallpaper_mode")) parseWallpaperMode("wallpaper_fit");

  if (auto cursorTheme = configString(table, "cursor_theme"); cursorTheme && !cursorTheme->empty()) {
    config.cursorTheme = *cursorTheme;
  }
  if (table.contains("cursor_size")) {
    if (auto cursorSize = configInt(table, "cursor_size"); cursorSize && *cursorSize >= 8 && *cursorSize <= 256) {
      config.cursorSize = *cursorSize;
    } else {
      std::fprintf(stderr, "flux-compositor: ignoring invalid cursor_size value in %s\n", path->c_str());
    }
  }

  auto parseOutputSelector = [&](char const* key) -> bool {
    if (auto selector = configString(table, key)) {
      selector = trim(*selector);
      if (!selector->empty()) {
        config.outputSelector = *selector;
        return true;
      }
      std::fprintf(stderr, "flux-compositor: ignoring empty output selector in %s\n", path->c_str());
      return true;
    }
    return false;
  };
  if (!parseOutputSelector("output") && !parseOutputSelector("output_name")) parseOutputSelector("kms_output");

  auto parseScaleKey = [&](char const* key) -> bool {
    if (!table.contains(key)) return false;
    if (auto scale = configFloat(table, key); scale && *scale >= 0.5f && *scale <= 4.f) {
      config.scale = *scale;
    } else {
      std::fprintf(stderr, "flux-compositor: ignoring invalid scale value in %s\n", path->c_str());
    }
    return true;
  };
  if (!parseScaleKey("scale") && !parseScaleKey("output_scale")) parseScaleKey("fractional_scale");

  auto parseOutputScaleEntry = [&](std::string outputName, toml::table const& outputTable) {
    outputName = trim(outputName);
    if (outputName.empty()) {
      std::fprintf(stderr, "flux-compositor: ignoring output scale with empty output name in %s\n", path->c_str());
      return;
    }
    auto parseNamedScaleKey = [&](char const* key) -> bool {
      if (!outputTable.contains(key)) return false;
      if (auto scale = configFloat(outputTable, key); scale && *scale >= 0.5f && *scale <= 4.f) {
        config.outputScales[outputName] = *scale;
      } else {
        std::fprintf(stderr,
                     "flux-compositor: ignoring invalid scale for output %s in %s\n",
                     outputName.c_str(),
                     path->c_str());
      }
      return true;
    };
    if (!parseNamedScaleKey("scale") && !parseNamedScaleKey("output_scale")) parseNamedScaleKey("fractional_scale");
  };

  if (auto* outputsTable = table["outputs"].as_table()) {
    for (auto&& [key, node] : *outputsTable) {
      if (auto* outputTable = node.as_table()) {
        parseOutputScaleEntry(std::string(key.str()), *outputTable);
      } else {
        std::fprintf(stderr,
                     "flux-compositor: ignoring non-table output entry %s in %s\n",
                     std::string(key.str()).c_str(),
                     path->c_str());
      }
    }
  } else if (auto* outputsArray = table["outputs"].as_array()) {
    for (auto&& node : *outputsArray) {
      if (auto* outputTable = node.as_table()) {
        auto outputName = configString(*outputTable, "name");
        if (!outputName) outputName = configString(*outputTable, "output");
        if (outputName) {
          parseOutputScaleEntry(*outputName, *outputTable);
        } else {
          std::fprintf(stderr, "flux-compositor: ignoring output scale without name in %s\n", path->c_str());
        }
      }
    }
  }

  if (table.contains("animations")) {
    if (auto enabled = configBool(table, "animations")) {
      config.animationsEnabled = *enabled;
    } else {
      std::fprintf(stderr, "flux-compositor: ignoring invalid animations value in %s\n", path->c_str());
    }
  }

  if (table.contains("hardware_cursor")) {
    if (auto enabled = configBool(table, "hardware_cursor")) {
      config.hardwareCursorEnabled = *enabled;
    } else {
      std::fprintf(stderr, "flux-compositor: ignoring invalid hardware_cursor value in %s\n", path->c_str());
    }
  }

  if (auto* chromeTable = table["chrome"].as_table()) {
    parseChromeConfig(*chromeTable, config.chrome, path->c_str());
    if (auto* darkTable = (*chromeTable)["dark"].as_table()) {
      ChromeConfig darkChrome = config.chrome;
      parseChromeConfig(*darkTable, darkChrome, path->c_str());
      config.darkChrome = darkChrome;
    }
  } else if (table.contains("chrome")) {
    std::fprintf(stderr, "flux-compositor: ignoring non-table chrome section in %s\n", path->c_str());
  }

  applyEnvironmentOverrides(config);

  std::fprintf(stderr, "flux-compositor: loaded config %s\n", path->c_str());
  return config;
}

} // namespace

LoadedCompositorConfig loadConfigWithMetadata() {
  auto path = configPath();
  if (path) {
    ensureDefaultConfigFile(*path);
  }
  LoadedCompositorConfig loaded{
      .config = loadConfig(),
      .path = path,
  };
  if (loaded.path) {
    std::error_code error;
    auto modifiedAt = std::filesystem::last_write_time(*loaded.path, error);
    if (!error) {
      loaded.modifiedAt = modifiedAt;
      loaded.hasModifiedAt = true;
    }
  }
  return loaded;
}

bool configChanged(LoadedCompositorConfig const& loaded) {
  if (!loaded.path) return false;
  std::error_code error;
  auto modifiedAt = std::filesystem::last_write_time(*loaded.path, error);
  if (error) return loaded.hasModifiedAt;
  return !loaded.hasModifiedAt || modifiedAt != loaded.modifiedAt;
}

float scaleForOutput(CompositorConfig const& config, std::string const& outputName) {
  auto found = config.outputScales.find(outputName);
  return found == config.outputScales.end() ? config.scale : found->second;
}

} // namespace flux::compositor
