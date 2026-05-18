#include "Compositor/Config/CompositorConfig.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <linux/input-event-codes.h>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace flux::compositor {
namespace {

std::string trim(std::string_view value) {
  std::size_t begin = 0;
  while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
  std::size_t end = value.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1u]))) --end;
  return std::string(value.substr(begin, end - begin));
}

std::string stripTomlComment(std::string_view line) {
  bool inString = false;
  for (std::size_t i = 0; i < line.size(); ++i) {
    char const c = line[i];
    if (c == '"' && (i == 0 || line[i - 1u] != '\\')) inString = !inString;
    if (c == '#' && !inString) return std::string(line.substr(0, i));
  }
  return std::string(line);
}

std::string unquote(std::string_view value) {
  std::string trimmed = trim(value);
  if (trimmed.size() >= 2 && trimmed.front() == '"' && trimmed.back() == '"') {
    return trimmed.substr(1, trimmed.size() - 2u);
  }
  return trimmed;
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
  std::string text = unquote(value);
  if (text.size() != 7 || text[0] != '#') return std::nullopt;
  auto red = hexByte(text, 1);
  auto green = hexByte(text, 3);
  auto blue = hexByte(text, 5);
  if (!red || !green || !blue) return std::nullopt;
  return Color{
      static_cast<float>(*red) / 255.f,
      static_cast<float>(*green) / 255.f,
      static_cast<float>(*blue) / 255.f,
      1.f,
  };
}

std::optional<std::pair<Color, Color>> parseLinearGradient(std::string_view value) {
  std::string text = unquote(value);
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
  std::string text = unquote(value);
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  if (text == "true" || text == "yes" || text == "on" || text == "1") return true;
  if (text == "false" || text == "no" || text == "off" || text == "0") return false;
  return std::nullopt;
}

std::string lowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

std::optional<ImageFillMode> parseImageFillMode(std::string_view value) {
  std::string text = lowerAscii(unquote(value));
  if (text == "stretch" || text == "fill") return ImageFillMode::Stretch;
  if (text == "fit" || text == "contain") return ImageFillMode::Fit;
  if (text == "cover") return ImageFillMode::Cover;
  if (text == "center" || text == "none") return ImageFillMode::Center;
  if (text == "tile" || text == "repeat") return ImageFillMode::Tile;
  return std::nullopt;
}

std::vector<WaylandServer::ShortcutBinding> defaultShortcutBindings() {
  using Action = WaylandServer::ShortcutAction;
  return {
      {.action = Action::CloseFocused, .key = KEY_Q, .meta = true},
      {.action = Action::CycleFocus, .key = KEY_TAB, .meta = true},
      {.action = Action::SnapLeft, .key = KEY_LEFT, .meta = true},
      {.action = Action::SnapRight, .key = KEY_RIGHT, .meta = true},
      {.action = Action::Terminate, .key = KEY_BACKSPACE, .ctrl = true, .alt = true},
  };
}

std::optional<std::uint32_t> keyCodeForName(std::string const& token) {
  static std::unordered_map<std::string, std::uint32_t> const keyCodes{
      {"backspace", KEY_BACKSPACE},
      {"delete", KEY_DELETE},
      {"down", KEY_DOWN},
      {"enter", KEY_ENTER},
      {"escape", KEY_ESC},
      {"esc", KEY_ESC},
      {"left", KEY_LEFT},
      {"q", KEY_Q},
      {"right", KEY_RIGHT},
      {"space", KEY_SPACE},
      {"tab", KEY_TAB},
      {"up", KEY_UP},
  };
  if (token.size() == 1 && token[0] >= 'a' && token[0] <= 'z') {
    return static_cast<std::uint32_t>(KEY_A + (token[0] - 'a'));
  }
  auto found = keyCodes.find(token);
  if (found == keyCodes.end()) return std::nullopt;
  return found->second;
}

std::optional<WaylandServer::ShortcutBinding> parseShortcut(WaylandServer::ShortcutAction action,
                                                            std::string_view value) {
  auto text = lowerAscii(unquote(value));
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
      {"quit", Action::Terminate},
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

CompositorConfig loadConfig() {
  CompositorConfig config;
  config.shortcutBindings = defaultShortcutBindings();
  auto path = configPath();
  if (!path) return config;
  std::ifstream file(*path);
  if (!file) return config;

  std::string line;
  std::string section;
  unsigned int lineNumber = 0;
  while (std::getline(file, line)) {
    ++lineNumber;
    std::string clean = trim(stripTomlComment(line));
    if (clean.empty()) continue;
    if (clean.front() == '[') {
      if (clean.size() >= 2 && clean.back() == ']') {
        section = lowerAscii(trim(std::string_view(clean).substr(1, clean.size() - 2u)));
      }
      continue;
    }
    std::size_t const equals = clean.find('=');
    if (equals == std::string::npos) continue;
    std::string key = lowerAscii(trim(std::string_view(clean).substr(0, equals)));
    std::string value = trim(std::string_view(clean).substr(equals + 1u));
    if (section == "keybindings") {
      if (auto action = shortcutActionForKey(key)) {
        if (auto binding = parseShortcut(*action, value)) {
          replaceShortcutBinding(config.shortcutBindings, *binding);
        } else {
          std::fprintf(stderr,
                       "flux-compositor: ignoring invalid keybinding in %s:%u\n",
                       path->c_str(),
                       lineNumber);
        }
      }
    } else if (key == "background" || key == "background_color") {
      if (auto color = parseHexColor(value)) {
        config.backgroundColor = *color;
        config.backgroundGradientEnd.reset();
      } else {
        std::fprintf(stderr,
                     "flux-compositor: ignoring invalid background color in %s:%u\n",
                     path->c_str(),
                     lineNumber);
      }
    } else if (key == "background_gradient") {
      if (auto gradient = parseLinearGradient(value)) {
        config.backgroundColor = gradient->first;
        config.backgroundGradientEnd = gradient->second;
      } else {
        std::fprintf(stderr,
                     "flux-compositor: ignoring invalid background gradient in %s:%u\n",
                     path->c_str(),
                     lineNumber);
      }
    } else if (key == "wallpaper" || key == "wallpaper_path") {
      std::string wallpaper = unquote(value);
      if (!wallpaper.empty()) {
        config.wallpaperPath = wallpaper;
      }
    } else if (key == "wallpaper_mode" || key == "wallpaper_fit") {
      if (auto mode = parseImageFillMode(value)) {
        config.wallpaperMode = *mode;
      } else {
        std::fprintf(stderr,
                     "flux-compositor: ignoring invalid wallpaper mode in %s:%u\n",
                     path->c_str(),
                     lineNumber);
      }
    } else if (key == "animations") {
      if (auto enabled = parseBool(value)) {
        config.animationsEnabled = *enabled;
      } else {
        std::fprintf(stderr,
                     "flux-compositor: ignoring invalid animations value in %s:%u\n",
                     path->c_str(),
                     lineNumber);
      }
    } else if (key == "hardware_cursor") {
      if (auto enabled = parseBool(value)) {
        config.hardwareCursorEnabled = *enabled;
      } else {
        std::fprintf(stderr,
                     "flux-compositor: ignoring invalid hardware_cursor value in %s:%u\n",
                     path->c_str(),
                     lineNumber);
      }
    }
  }
  std::fprintf(stderr, "flux-compositor: loaded config %s\n", path->c_str());
  return config;
}

} // namespace

LoadedCompositorConfig loadConfigWithMetadata() {
  LoadedCompositorConfig loaded{
      .config = loadConfig(),
      .path = configPath(),
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

} // namespace flux::compositor
