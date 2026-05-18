#include <Flux/Core/Color.hpp>
#include <Flux/Core/Geometry.hpp>
#include <Flux/Graphics/Canvas.hpp>
#include <Flux/Graphics/Font.hpp>
#include <Flux/Graphics/Image.hpp>
#include <Flux/Graphics/TextLayoutOptions.hpp>
#include <Flux/Graphics/VulkanContext.hpp>
#include <Flux/Platform/Linux/KmsOutput.hpp>

#include "Compositor/WaylandServer.hpp"
#include "Graphics/Linux/FreeTypeTextSystem.hpp"
#include "Graphics/Vulkan/VulkanCanvas.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include <unistd.h>
#include <linux/input-event-codes.h>
#include <vulkan/vulkan.h>

namespace {

std::atomic<bool> gRunning{true};

void onSignal(int) {
  gRunning.store(false, std::memory_order_relaxed);
}

struct CachedClientImage {
  std::uint64_t id = 0;
  std::uint64_t serial = 0;
  std::shared_ptr<flux::Image> image;
  bool logged = false;
};

struct SurfaceVisualState {
  std::chrono::steady_clock::time_point firstSeen{};
  flux::compositor::CommittedSurfaceSnapshot lastSnapshot{};
  bool hasLastSnapshot = false;
};

struct ClosingSurfaceVisual {
  flux::compositor::CommittedSurfaceSnapshot snapshot{};
  std::shared_ptr<flux::Image> image;
  std::chrono::steady_clock::time_point closedAt{};
};

struct CompositorConfig {
  flux::Color backgroundColor{0.20f, 0.50f, 0.95f, 1.0f};
  std::optional<flux::Color> backgroundGradientEnd;
  std::optional<std::string> wallpaperPath;
  flux::ImageFillMode wallpaperMode = flux::ImageFillMode::Cover;
  bool animationsEnabled = true;
  bool hardwareCursorEnabled = true;
  std::vector<flux::compositor::WaylandServer::ShortcutBinding> shortcutBindings;
};

struct LoadedCompositorConfig {
  CompositorConfig config;
  std::optional<std::string> path;
  std::filesystem::file_time_type modifiedAt{};
  bool hasModifiedAt = false;
};

float clamp01(float value) {
  return std::clamp(value, 0.f, 1.f);
}

float easeOutCubic(float value) {
  float const t = clamp01(value);
  float const inverse = 1.f - t;
  return 1.f - inverse * inverse * inverse;
}

bool resizeTraceEnabled() {
  static bool const enabled = [] {
    char const* value = std::getenv("FLUX_COMPOSITOR_RESIZE_TRACE");
    return value && value[0] != '\0' && std::strcmp(value, "0") != 0;
  }();
  return enabled;
}

bool shouldTraceRenderSnapshot(flux::compositor::CommittedSurfaceSnapshot const& current,
                               SurfaceVisualState const& visual) {
  if (!resizeTraceEnabled()) return false;
  if (!visual.hasLastSnapshot) return true;
  auto const& previous = visual.lastSnapshot;
  return current.x != previous.x || current.y != previous.y ||
         current.width != previous.width || current.height != previous.height ||
         current.bufferWidth != previous.bufferWidth || current.bufferHeight != previous.bufferHeight ||
         current.activeSizing != previous.activeSizing ||
         current.serial != previous.serial ||
         current.sourceX != previous.sourceX || current.sourceY != previous.sourceY ||
         current.sourceWidth != previous.sourceWidth || current.sourceHeight != previous.sourceHeight;
}

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

std::optional<flux::Color> parseHexColor(std::string_view value) {
  std::string text = unquote(value);
  if (text.size() != 7 || text[0] != '#') return std::nullopt;
  auto red = hexByte(text, 1);
  auto green = hexByte(text, 3);
  auto blue = hexByte(text, 5);
  if (!red || !green || !blue) return std::nullopt;
  return flux::Color{
      static_cast<float>(*red) / 255.f,
      static_cast<float>(*green) / 255.f,
      static_cast<float>(*blue) / 255.f,
      1.f,
  };
}

std::optional<std::pair<flux::Color, flux::Color>> parseLinearGradient(std::string_view value) {
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

std::optional<flux::ImageFillMode> parseImageFillMode(std::string_view value) {
  std::string text = lowerAscii(unquote(value));
  if (text == "stretch" || text == "fill") return flux::ImageFillMode::Stretch;
  if (text == "fit" || text == "contain") return flux::ImageFillMode::Fit;
  if (text == "cover") return flux::ImageFillMode::Cover;
  if (text == "center" || text == "none") return flux::ImageFillMode::Center;
  if (text == "tile" || text == "repeat") return flux::ImageFillMode::Tile;
  return std::nullopt;
}

std::vector<flux::compositor::WaylandServer::ShortcutBinding> defaultShortcutBindings() {
  using Action = flux::compositor::WaylandServer::ShortcutAction;
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

std::optional<flux::compositor::WaylandServer::ShortcutBinding> parseShortcut(
    flux::compositor::WaylandServer::ShortcutAction action,
    std::string_view value) {
  auto text = lowerAscii(unquote(value));
  flux::compositor::WaylandServer::ShortcutBinding binding{.action = action};
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

std::optional<flux::compositor::WaylandServer::ShortcutAction> shortcutActionForKey(std::string const& key) {
  using Action = flux::compositor::WaylandServer::ShortcutAction;
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

void replaceShortcutBinding(std::vector<flux::compositor::WaylandServer::ShortcutBinding>& bindings,
                            flux::compositor::WaylandServer::ShortcutBinding binding) {
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

void drawSurfaceImage(flux::Canvas& canvas,
                      flux::compositor::CommittedSurfaceSnapshot const& surface,
                      flux::Image& image,
                      float opacity,
                      float scale) {
  float const windowX = static_cast<float>(surface.x);
  float const windowY = static_cast<float>(surface.y);
  float const windowWidth = static_cast<float>(surface.width);
  float const windowHeight = static_cast<float>(surface.height);
  float const titleBarHeight = static_cast<float>(surface.titleBarHeight);
  float const outerHeight = windowHeight + titleBarHeight;
  flux::Point const pivot{windowX + windowWidth * 0.5f, windowY - titleBarHeight + outerHeight * 0.5f};
  canvas.save();
  canvas.setOpacity(canvas.opacity() * opacity);
  if (scale != 1.f) {
    canvas.translate(pivot.x, pivot.y);
    canvas.scale(scale);
    canvas.translate(-pivot.x, -pivot.y);
  }
  float const sourceWidth = surface.sourceWidth > 0.f
                                ? surface.sourceWidth
                                : static_cast<float>(image.size().width);
  float const sourceHeight = surface.sourceHeight > 0.f
                                 ? surface.sourceHeight
                                 : static_cast<float>(image.size().height);
  canvas.drawImage(image,
                   flux::Rect::sharp(surface.sourceX,
                                     surface.sourceY,
                                     sourceWidth,
                                     sourceHeight),
                   flux::Rect::sharp(windowX,
                                     windowY,
                                     windowWidth,
                                     windowHeight));
  canvas.restore();
}

void updateCachedImage(flux::compositor::WaylandServer& wayland,
                       flux::Canvas& canvas,
                       flux::compositor::CommittedSurfaceSnapshot const& surface,
                       CachedClientImage& cached) {
  if (cached.image && cached.id == surface.id && cached.serial == surface.serial) return;

  cached.id = surface.id;
  cached.serial = surface.serial;
  cached.image.reset();
  cached.logged = false;
  std::int32_t const bufferWidth = surface.bufferWidth > 0 ? surface.bufferWidth : surface.width;
  std::int32_t const bufferHeight = surface.bufferHeight > 0 ? surface.bufferHeight : surface.height;
  if (!surface.rgbaPixels.empty()) {
    cached.image = flux::Image::fromRgbaPixels(static_cast<std::uint32_t>(bufferWidth),
                                               static_cast<std::uint32_t>(bufferHeight),
                                               surface.rgbaPixels,
                                               canvas.gpuDevice());
  } else if (!surface.dmabufPlanes.empty()) {
    std::vector<std::uint8_t> fallbackPixels;
    std::vector<int> fds = wayland.duplicateDmabufFds(surface.id);
    if (fds.size() == surface.dmabufPlanes.size()) {
      std::vector<flux::Image::DmabufPlane> planes;
      planes.reserve(surface.dmabufPlanes.size());
      for (std::size_t i = 0; i < surface.dmabufPlanes.size(); ++i) {
        planes.push_back({
            .fd = fds[i],
            .offset = surface.dmabufPlanes[i].offset,
            .stride = surface.dmabufPlanes[i].stride,
            .modifier = surface.dmabufPlanes[i].modifier,
        });
      }
      try {
        cached.image = flux::Image::fromDmabuf({
            .width = static_cast<std::uint32_t>(bufferWidth),
            .height = static_cast<std::uint32_t>(bufferHeight),
            .drmFormat = surface.dmabufFormat,
            .planes = planes,
        });
        if (cached.image && !cached.logged) {
          std::fprintf(stderr, "flux-compositor: imported DMABUF as Vulkan image\n");
        }
      } catch (std::exception const& e) {
        std::fprintf(stderr, "flux-compositor: dmabuf import failed: %s\n", e.what());
      }
    } else {
      for (int fd : fds) close(fd);
    }
    if (wayland.copyDmabufToRgba(surface.id, fallbackPixels)) {
      cached.image = flux::Image::fromRgbaPixels(static_cast<std::uint32_t>(bufferWidth),
                                                 static_cast<std::uint32_t>(bufferHeight),
                                                 fallbackPixels,
                                                 canvas.gpuDevice());
      if (cached.image && !cached.logged) {
        std::fprintf(stderr, "flux-compositor: displaying readable DMABUF contents\n");
        cached.logged = true;
      }
    }
  }
}

std::uint32_t monotonicMilliseconds() {
  using Clock = std::chrono::steady_clock;
  auto const now = std::chrono::duration_cast<std::chrono::milliseconds>(
      Clock::now().time_since_epoch());
  return static_cast<std::uint32_t>(now.count());
}

bool debugCompositorInput() {
  char const* value = std::getenv("FLUX_DEBUG_COMPOSITOR_INPUT");
  return value && *value && std::strcmp(value, "0") != 0;
}

void drawArrowCursor(flux::Canvas& canvas, float cursorX, float cursorY) {
  flux::Path cursor;
  cursor.moveTo({cursorX, cursorY});
  cursor.lineTo({cursorX + 2.f, cursorY + 22.f});
  cursor.lineTo({cursorX + 8.f, cursorY + 16.f});
  cursor.lineTo({cursorX + 14.f, cursorY + 30.f});
  cursor.lineTo({cursorX + 19.f, cursorY + 28.f});
  cursor.lineTo({cursorX + 13.f, cursorY + 14.f});
  cursor.lineTo({cursorX + 21.f, cursorY + 14.f});
  cursor.close();
  canvas.drawPath(cursor,
                  flux::FillStyle::solid(flux::Colors::white),
                  flux::StrokeStyle::solid(flux::Colors::black, 1.f));
}

std::uint32_t premulArgb(std::uint8_t a, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
  auto premul = [a](std::uint8_t c) {
    return static_cast<std::uint8_t>((static_cast<unsigned int>(c) * static_cast<unsigned int>(a)) / 255u);
  };
  return (static_cast<std::uint32_t>(a) << 24u) |
         (static_cast<std::uint32_t>(premul(r)) << 16u) |
         (static_cast<std::uint32_t>(premul(g)) << 8u) |
         static_cast<std::uint32_t>(premul(b));
}

std::vector<std::uint32_t> makeHardwareArrowCursor(std::uint32_t width, std::uint32_t height) {
  std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0u);
  std::uint32_t const black = premulArgb(255, 0, 0, 0);
  std::uint32_t const white = premulArgb(255, 255, 255, 255);
  int const cursorH = std::min<int>(static_cast<int>(height), 28);
  int const cursorW = std::min<int>(static_cast<int>(width), 20);
  auto put = [&](int x, int y, std::uint32_t color) {
    if (x >= 0 && y >= 0 && x < static_cast<int>(width) && y < static_cast<int>(height)) {
      pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] = color;
    }
  };
  for (int y = 0; y < cursorH; ++y) {
    int right = std::min(y / 2 + 2, cursorW - 1);
    for (int x = 0; x <= right; ++x) put(x, y, white);
  }
  for (int y = 0; y < cursorH; ++y) {
    int right = std::min(y / 2 + 2, cursorW - 1);
    put(0, y, black);
    put(right, y, black);
  }
  for (int x = 0; x < std::min(cursorW, 8); ++x) put(x, cursorH - 1, black);
  for (int y = 13; y < std::min<int>(cursorH, 26); ++y) {
    for (int x = 7; x < 12; ++x) put(x, y, white);
    put(7, y, black);
    put(12, y, black);
  }
  return pixels;
}

void drawLineCursor(flux::Canvas& canvas, flux::Point from, flux::Point to, float width = 2.f) {
  canvas.drawLine({from.x + 1.f, from.y + 1.f},
                  {to.x + 1.f, to.y + 1.f},
                  flux::StrokeStyle::solid(flux::Colors::black, width + 1.f));
  canvas.drawLine(from, to, flux::StrokeStyle::solid(flux::Colors::white, width));
}

void drawFallbackCursor(flux::Canvas& canvas, flux::compositor::CursorShape shape, float cursorX, float cursorY) {
  switch (shape) {
  case flux::compositor::CursorShape::IBeam:
    drawLineCursor(canvas, {cursorX, cursorY}, {cursorX, cursorY + 24.f}, 2.f);
    drawLineCursor(canvas, {cursorX - 5.f, cursorY}, {cursorX + 5.f, cursorY}, 2.f);
    drawLineCursor(canvas, {cursorX - 5.f, cursorY + 24.f}, {cursorX + 5.f, cursorY + 24.f}, 2.f);
    return;
  case flux::compositor::CursorShape::Crosshair:
    drawLineCursor(canvas, {cursorX - 12.f, cursorY}, {cursorX + 12.f, cursorY}, 2.f);
    drawLineCursor(canvas, {cursorX, cursorY - 12.f}, {cursorX, cursorY + 12.f}, 2.f);
    return;
  case flux::compositor::CursorShape::Hand:
    canvas.drawCircle({cursorX + 7.f, cursorY + 8.f},
                      7.f,
                      flux::FillStyle::solid(flux::Colors::white),
                      flux::StrokeStyle::solid(flux::Colors::black, 1.f));
    drawLineCursor(canvas, {cursorX + 7.f, cursorY + 8.f}, {cursorX + 7.f, cursorY + 25.f}, 3.f);
    return;
  case flux::compositor::CursorShape::ResizeEW:
    drawLineCursor(canvas, {cursorX - 12.f, cursorY}, {cursorX + 12.f, cursorY}, 2.f);
    drawLineCursor(canvas, {cursorX - 12.f, cursorY}, {cursorX - 6.f, cursorY - 6.f}, 2.f);
    drawLineCursor(canvas, {cursorX - 12.f, cursorY}, {cursorX - 6.f, cursorY + 6.f}, 2.f);
    drawLineCursor(canvas, {cursorX + 12.f, cursorY}, {cursorX + 6.f, cursorY - 6.f}, 2.f);
    drawLineCursor(canvas, {cursorX + 12.f, cursorY}, {cursorX + 6.f, cursorY + 6.f}, 2.f);
    return;
  case flux::compositor::CursorShape::ResizeNS:
    drawLineCursor(canvas, {cursorX, cursorY - 12.f}, {cursorX, cursorY + 12.f}, 2.f);
    drawLineCursor(canvas, {cursorX, cursorY - 12.f}, {cursorX - 6.f, cursorY - 6.f}, 2.f);
    drawLineCursor(canvas, {cursorX, cursorY - 12.f}, {cursorX + 6.f, cursorY - 6.f}, 2.f);
    drawLineCursor(canvas, {cursorX, cursorY + 12.f}, {cursorX - 6.f, cursorY + 6.f}, 2.f);
    drawLineCursor(canvas, {cursorX, cursorY + 12.f}, {cursorX + 6.f, cursorY + 6.f}, 2.f);
    return;
  case flux::compositor::CursorShape::ResizeNESW:
    drawLineCursor(canvas, {cursorX - 10.f, cursorY + 10.f}, {cursorX + 10.f, cursorY - 10.f}, 2.f);
    return;
  case flux::compositor::CursorShape::ResizeNWSE:
    drawLineCursor(canvas, {cursorX - 10.f, cursorY - 10.f}, {cursorX + 10.f, cursorY + 10.f}, 2.f);
    return;
  case flux::compositor::CursorShape::ResizeAll:
    drawLineCursor(canvas, {cursorX - 12.f, cursorY}, {cursorX + 12.f, cursorY}, 2.f);
    drawLineCursor(canvas, {cursorX, cursorY - 12.f}, {cursorX, cursorY + 12.f}, 2.f);
    return;
  case flux::compositor::CursorShape::NotAllowed:
    canvas.drawCircle({cursorX + 8.f, cursorY + 8.f},
                      9.f,
                      flux::FillStyle::none(),
                      flux::StrokeStyle::solid(flux::Colors::white, 4.f));
    canvas.drawLine({cursorX + 2.f, cursorY + 14.f},
                    {cursorX + 14.f, cursorY + 2.f},
                    flux::StrokeStyle::solid(flux::Colors::white, 4.f));
    canvas.drawCircle({cursorX + 8.f, cursorY + 8.f},
                      9.f,
                      flux::FillStyle::none(),
                      flux::StrokeStyle::solid(flux::Colors::black, 1.f));
    canvas.drawLine({cursorX + 2.f, cursorY + 14.f},
                    {cursorX + 14.f, cursorY + 2.f},
                    flux::StrokeStyle::solid(flux::Colors::black, 1.f));
    return;
  case flux::compositor::CursorShape::Arrow:
    drawArrowCursor(canvas, cursorX, cursorY);
    return;
  }
}

} // namespace

int main(int, char**) {
  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);

  try {
    auto device = flux::platform::KmsDevice::open();
    auto outputs = device->outputs();
    if (outputs.empty()) {
      std::fprintf(stderr, "flux-compositor: no connected KMS outputs\n");
      return 1;
    }

    flux::platform::KmsOutput const& output = outputs.front();
    flux::compositor::WaylandServer wayland({
        .name = output.name(),
        .width = static_cast<std::int32_t>(output.width()),
        .height = static_cast<std::int32_t>(output.height()),
        .refreshMilliHz = static_cast<std::int32_t>(output.refreshRateMilliHz()),
    });
    device->setInputHandler([&wayland](flux::platform::KmsInputEvent const& event) {
      if (debugCompositorInput()) {
        std::fprintf(stderr,
                     "flux-compositor: input kind=%u dx=%.2f dy=%.2f x=%.1f y=%.1f button=%u pressed=%d key=%u\n",
                     static_cast<unsigned int>(event.kind),
                     event.dx,
                     event.dy,
                     event.x,
                     event.y,
                     event.button,
                     event.pressed,
                     event.key);
      }
      switch (event.kind) {
      case flux::platform::KmsInputEvent::Kind::PointerMotion:
        wayland.handlePointerMotion(event.dx, event.dy, event.timeMs);
        break;
      case flux::platform::KmsInputEvent::Kind::PointerPosition:
        wayland.handlePointerPosition(event.x, event.y, event.timeMs);
        break;
      case flux::platform::KmsInputEvent::Kind::PointerButton:
        wayland.handlePointerButton(event.button, event.pressed, event.timeMs);
        break;
      case flux::platform::KmsInputEvent::Kind::PointerAxis:
        wayland.handlePointerAxis(event.dx, event.dy, event.timeMs);
        break;
      case flux::platform::KmsInputEvent::Kind::Key:
        wayland.handleKeyboardKey(event.key, event.pressed, event.timeMs);
        break;
      }
    });

    flux::configureVulkanCanvasRuntime(device->requiredVulkanInstanceExtensions(), device->cacheDir());
    auto& vulkan = flux::VulkanContext::instance();
    vulkan.addRequiredDeviceExtension(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
    vulkan.addRequiredDeviceExtension(VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME);
    vulkan.addRequiredDeviceExtension(VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME);
    VkInstance instance = flux::ensureSharedVulkanInstance();
    VkSurfaceKHR surface = output.createVulkanSurface(instance);

    static flux::FreeTypeTextSystem textSystem;
    std::unique_ptr<flux::Canvas> canvas = flux::createVulkanCanvas(surface, 1u, textSystem);
    canvas->updateDpiScale(1.f, 1.f);
    canvas->resize(static_cast<int>(output.width()), static_cast<int>(output.height()));

    std::fprintf(stderr,
                 "flux-compositor: presenting %ux%u on %s\n",
                 output.width(),
                 output.height(),
                 output.name().c_str());

    LoadedCompositorConfig loadedConfig = loadConfigWithMetadata();
    CompositorConfig config = loadedConfig.config;
    wayland.setShortcutBindings(config.shortcutBindings);
    flux::Color clearColor = config.backgroundColor;
    flux::FillStyle backgroundFill =
        config.backgroundGradientEnd
            ? flux::FillStyle::linearGradient(config.backgroundColor, *config.backgroundGradientEnd, {0.f, 0.f}, {1.f, 1.f})
            : flux::FillStyle::solid(config.backgroundColor);
    std::shared_ptr<flux::Image> wallpaperImage;
    auto applyConfig = [&] {
      config = loadedConfig.config;
      wayland.setShortcutBindings(config.shortcutBindings);
      clearColor = config.backgroundColor;
      backgroundFill =
          config.backgroundGradientEnd
              ? flux::FillStyle::linearGradient(config.backgroundColor, *config.backgroundGradientEnd, {0.f, 0.f}, {1.f, 1.f})
              : flux::FillStyle::solid(config.backgroundColor);
      wallpaperImage.reset();
      if (config.wallpaperPath) {
        wallpaperImage = flux::loadImageFromFile(*config.wallpaperPath, canvas->gpuDevice());
        if (!wallpaperImage) {
          std::fprintf(stderr, "flux-compositor: failed to load wallpaper %s\n", config.wallpaperPath->c_str());
        }
      }
    };
    applyConfig();
    std::unordered_map<std::uint64_t, CachedClientImage> clientImages;
    std::unordered_map<std::uint64_t, SurfaceVisualState> surfaceVisuals;
    std::unordered_map<std::uint64_t, ClosingSurfaceVisual> closingSurfaces;
    CachedClientImage cursorImage;
    bool hardwareArrowCursor = false;
    std::uint32_t const hardwareCursorWidth = output.cursorWidth();
    std::uint32_t const hardwareCursorHeight = output.cursorHeight();
    std::vector<std::uint32_t> hardwareCursorPixels;
    if (config.hardwareCursorEnabled && hardwareCursorWidth > 0 && hardwareCursorHeight > 0) {
      hardwareCursorPixels = makeHardwareArrowCursor(hardwareCursorWidth, hardwareCursorHeight);
      hardwareArrowCursor = output.setCursorImage(hardwareCursorPixels, hardwareCursorWidth, hardwareCursorHeight);
      if (!hardwareArrowCursor) {
        std::fprintf(stderr, "flux-compositor: hardware cursor unavailable; using software cursor\n");
      }
    }
    while (gRunning.load(std::memory_order_relaxed) && !device->shouldTerminate()) {
      device->pollEvents(0);
      wayland.dispatch();
      if (configChanged(loadedConfig)) {
        loadedConfig = loadConfigWithMetadata();
        applyConfig();
      }
      if (!device->isVtForeground()) {
        device->pollEvents(250);
        wayland.dispatch();
        continue;
      }

      output.waitForVblank();
      device->pollEvents(0);
      wayland.dispatch();
      if (!device->isVtForeground()) continue;
      auto const frameTime = std::chrono::steady_clock::now();
      wayland.updateAnimations(monotonicMilliseconds(), config.animationsEnabled);

      canvas->beginFrame();
      canvas->clear(clearColor);
      if (config.backgroundGradientEnd) {
        canvas->drawRect(flux::Rect::sharp(0.f, 0.f, static_cast<float>(output.width()), static_cast<float>(output.height())),
                         flux::CornerRadius{0.f},
                         backgroundFill,
                         flux::StrokeStyle::none(),
                         flux::ShadowStyle::none());
      }
      if (wallpaperImage) {
        canvas->drawImage(*wallpaperImage,
                          flux::Rect::sharp(0.f,
                                            0.f,
                                            static_cast<float>(output.width()),
                                            static_cast<float>(output.height())),
                          config.wallpaperMode);
      }
      auto committedSurfaces = wayland.committedSurfaces();
      std::unordered_set<std::uint64_t> liveSurfaceIds;
      liveSurfaceIds.reserve(committedSurfaces.size());
      for (auto const& clientSurface : committedSurfaces) {
        liveSurfaceIds.insert(clientSurface.id);
        auto& visual = surfaceVisuals[clientSurface.id];
        if (visual.firstSeen.time_since_epoch().count() == 0) visual.firstSeen = frameTime;
        auto& cached = clientImages[clientSurface.id];
        updateCachedImage(wayland, *canvas, clientSurface, cached);
        if (!cached.image) continue;
        bool const traceSnapshot = shouldTraceRenderSnapshot(clientSurface, visual);
        if (traceSnapshot) {
          auto const imageSize = cached.image->size();
          std::fprintf(stderr,
                       "resize-trace: render-snapshot surface=%llu window=%d,%d frame=%dx%d buffer=%dx%d "
                       "image=%dx%d source=%.1f,%.1f %.1fx%.1f serial=%llu\n",
                       static_cast<unsigned long long>(clientSurface.id),
                       clientSurface.x,
                       clientSurface.y,
                       clientSurface.width,
                       clientSurface.height,
                       clientSurface.bufferWidth,
                       clientSurface.bufferHeight,
                       static_cast<int>(imageSize.width),
                       static_cast<int>(imageSize.height),
                       clientSurface.sourceX,
                       clientSurface.sourceY,
                       clientSurface.sourceWidth,
                       clientSurface.sourceHeight,
                       static_cast<unsigned long long>(clientSurface.serial));
        }
        visual.lastSnapshot = clientSurface;
        visual.hasLastSnapshot = true;

        float const windowX = static_cast<float>(clientSurface.x);
        float const windowY = static_cast<float>(clientSurface.y);
        float const windowWidth = static_cast<float>(clientSurface.width);
        float const windowHeight = static_cast<float>(clientSurface.height);
        float const titleBarHeight = static_cast<float>(clientSurface.titleBarHeight);
        float const animationMs = static_cast<float>(
            std::chrono::duration_cast<std::chrono::milliseconds>(frameTime - visual.firstSeen).count());
        float const openProgress = config.animationsEnabled ? easeOutCubic(animationMs / 140.f) : 1.f;
        float const openScale = 0.965f + 0.035f * openProgress;
        float const openOpacity = openProgress;
        float const outerHeight = windowHeight + titleBarHeight;
        flux::Point const pivot{windowX + windowWidth * 0.5f, windowY - titleBarHeight + outerHeight * 0.5f};
        canvas->save();
        canvas->setOpacity(canvas->opacity() * openOpacity);
        if (openScale < 1.f) {
          canvas->translate(pivot.x, pivot.y);
          canvas->scale(openScale);
          canvas->translate(-pivot.x, -pivot.y);
        }
        if (titleBarHeight > 0.f) {
          flux::Color const titleFill =
              clientSurface.focused ? flux::Color{0.10f, 0.12f, 0.14f, 1.f}
                                    : flux::Color{0.30f, 0.32f, 0.36f, 1.f};
          flux::Color const borderColor =
              clientSurface.focused ? flux::Color{0.02f, 0.03f, 0.04f, 1.f}
                                    : flux::Color{0.18f, 0.19f, 0.21f, 1.f};
          canvas->drawRect(flux::Rect::sharp(windowX, windowY - titleBarHeight, windowWidth, titleBarHeight),
                           flux::CornerRadius{0.f},
                           flux::FillStyle::solid(titleFill),
                           flux::StrokeStyle::none(),
                           flux::ShadowStyle::none());
          canvas->drawRect(flux::Rect::sharp(windowX - 1.f,
                                             windowY - titleBarHeight - 1.f,
                                             windowWidth + 2.f,
                                             windowHeight + titleBarHeight + 2.f),
                           flux::CornerRadius{0.f},
                           flux::FillStyle::none(),
                           flux::StrokeStyle::solid(borderColor, 1.f),
                           flux::ShadowStyle::none());
          float constexpr closeSize = 18.f;
          float constexpr closeInset = 5.f;
          float const closeX = windowX + windowWidth - closeInset - closeSize;
          float const closeY = windowY - titleBarHeight + closeInset;
          flux::Color const closeFill =
              clientSurface.focused ? flux::Color{0.86f, 0.20f, 0.22f, 1.f}
                                    : flux::Color{0.48f, 0.20f, 0.22f, 1.f};
          flux::Color const closeStroke =
              clientSurface.focused ? flux::Color{0.98f, 0.88f, 0.88f, 1.f}
                                    : flux::Color{0.68f, 0.58f, 0.58f, 1.f};
          canvas->drawCircle({closeX + closeSize * 0.5f, closeY + closeSize * 0.5f},
                             closeSize * 0.5f,
                             flux::FillStyle::solid(closeFill),
                             flux::StrokeStyle::none());
          canvas->drawLine({closeX + 6.f, closeY + 6.f},
                           {closeX + closeSize - 6.f, closeY + closeSize - 6.f},
                           flux::StrokeStyle::solid(closeStroke, 1.5f));
          canvas->drawLine({closeX + closeSize - 6.f, closeY + 6.f},
                           {closeX + 6.f, closeY + closeSize - 6.f},
                           flux::StrokeStyle::solid(closeStroke, 1.5f));
          float const titleLeft = windowX + 10.f;
          float const titleWidth = std::max(0.f, closeX - titleLeft - 8.f);
          if (titleWidth > 0.f && !clientSurface.title.empty()) {
            flux::Font titleFont{};
            titleFont.size = 13.f;
            titleFont.weight = 500.f;
            flux::TextLayoutOptions titleOptions{
                .verticalAlignment = flux::VerticalAlignment::Center,
                .wrapping = flux::TextWrapping::NoWrap,
                .maxLines = 1,
            };
            flux::Color const titleColor =
                clientSurface.focused ? flux::Color{0.94f, 0.96f, 0.98f, 1.f}
                                      : flux::Color{0.72f, 0.75f, 0.80f, 1.f};
            flux::TextSystem& compositorTextSystem = textSystem;
            auto titleLayout =
                compositorTextSystem.layout(clientSurface.title,
                                            titleFont,
                                            titleColor,
                                            flux::Rect::sharp(titleLeft,
                                                              windowY - titleBarHeight,
                                                              titleWidth,
                                                              titleBarHeight),
                                            titleOptions);
            if (titleLayout) {
              canvas->save();
              canvas->clipRect(flux::Rect::sharp(titleLeft,
                                                 windowY - titleBarHeight,
                                                 titleWidth,
                                                 titleBarHeight));
              canvas->drawTextLayout(*titleLayout, {0.f, 0.f});
              canvas->restore();
            }
          }
          flux::Color const gripColor =
              clientSurface.focused ? flux::Color{0.78f, 0.82f, 0.88f, 1.f}
                                    : flux::Color{0.55f, 0.58f, 0.64f, 1.f};
          float constexpr grip = 10.f;
          float constexpr gripStroke = 2.f;
          auto drawGrip = [&](float x, float y) {
            canvas->drawRect(flux::Rect::sharp(x, y, grip, gripStroke),
                             flux::CornerRadius{0.f},
                             flux::FillStyle::solid(gripColor),
                             flux::StrokeStyle::none(),
                             flux::ShadowStyle::none());
            canvas->drawRect(flux::Rect::sharp(x, y, gripStroke, grip),
                             flux::CornerRadius{0.f},
                             flux::FillStyle::solid(gripColor),
                             flux::StrokeStyle::none(),
                             flux::ShadowStyle::none());
          };
          drawGrip(windowX + 3.f, windowY + 3.f);
          drawGrip(windowX + windowWidth - grip - 3.f, windowY + 3.f);
          drawGrip(windowX + 3.f, windowY + windowHeight - grip - 3.f);
          drawGrip(windowX + windowWidth - grip - 3.f, windowY + windowHeight - grip - 3.f);
        }
        float const sourceWidth = clientSurface.sourceWidth > 0.f
                                      ? clientSurface.sourceWidth
                                      : static_cast<float>(cached.image->size().width);
        float const sourceHeight = clientSurface.sourceHeight > 0.f
                                       ? clientSurface.sourceHeight
                                       : static_cast<float>(cached.image->size().height);
        float const contentWidth = clientSurface.activeSizing ? sourceWidth : windowWidth;
        float const contentHeight = clientSurface.activeSizing ? sourceHeight : windowHeight;
        canvas->save();
        canvas->clipRect(flux::Rect::sharp(windowX, windowY, windowWidth, windowHeight));
        canvas->drawImage(*cached.image,
                          flux::Rect::sharp(clientSurface.sourceX,
                                            clientSurface.sourceY,
                                            sourceWidth,
                                            sourceHeight),
                          flux::Rect::sharp(windowX,
                                            windowY,
                                            contentWidth,
                                            contentHeight));
        canvas->restore();
        canvas->restore();
      }
      for (auto const& [surfaceId, visual] : surfaceVisuals) {
        if (liveSurfaceIds.contains(surfaceId)) continue;
        auto cached = clientImages.find(surfaceId);
        if (!config.animationsEnabled || !visual.hasLastSnapshot || cached == clientImages.end() ||
            !cached->second.image) {
          continue;
        }
        closingSurfaces[surfaceId] = ClosingSurfaceVisual{
            .snapshot = visual.lastSnapshot,
            .image = cached->second.image,
            .closedAt = frameTime,
        };
      }
      for (auto it = closingSurfaces.begin(); it != closingSurfaces.end();) {
        float const closeMs = static_cast<float>(
            std::chrono::duration_cast<std::chrono::milliseconds>(frameTime - it->second.closedAt).count());
        float const progress = clamp01(closeMs / 120.f);
        if (progress >= 1.f || !it->second.image) {
          it = closingSurfaces.erase(it);
          continue;
        }
        float const eased = easeOutCubic(progress);
        drawSurfaceImage(*canvas, it->second.snapshot, *it->second.image, 1.f - eased, 1.f - 0.025f * eased);
        ++it;
      }
      if (auto snapPreview = wayland.snapPreview()) {
        flux::Rect const previewRect = flux::Rect::sharp(static_cast<float>(snapPreview->x),
                                                         static_cast<float>(snapPreview->y),
                                                         static_cast<float>(snapPreview->width),
                                                         static_cast<float>(snapPreview->height));
        canvas->drawRect(previewRect,
                         flux::CornerRadius{0.f},
                         flux::FillStyle::solid(flux::Color{0.86f, 0.93f, 1.0f, 0.22f}),
                         flux::StrokeStyle::solid(flux::Color{0.92f, 0.97f, 1.0f, 0.82f}, 2.f),
                         flux::ShadowStyle::none());
      }
      if (auto cursorSurface = wayland.cursorSurface()) {
        if (hardwareArrowCursor) output.hideCursor();
        updateCachedImage(wayland, *canvas, *cursorSurface, cursorImage);
        if (cursorImage.image) {
          float const cursorSourceWidth = cursorSurface->sourceWidth > 0.f
                                              ? cursorSurface->sourceWidth
                                              : static_cast<float>(cursorImage.image->size().width);
          float const cursorSourceHeight = cursorSurface->sourceHeight > 0.f
                                               ? cursorSurface->sourceHeight
                                               : static_cast<float>(cursorImage.image->size().height);
          canvas->drawImage(*cursorImage.image,
                            flux::Rect::sharp(cursorSurface->sourceX,
                                              cursorSurface->sourceY,
                                              cursorSourceWidth,
                                              cursorSourceHeight),
                            flux::Rect::sharp(static_cast<float>(cursorSurface->x),
                                              static_cast<float>(cursorSurface->y),
                                              static_cast<float>(cursorSurface->width),
                                              static_cast<float>(cursorSurface->height)));
        }
      } else {
        cursorImage = {};
        float const cursorX = wayland.pointerX();
        float const cursorY = wayland.pointerY();
        if (hardwareArrowCursor && wayland.cursorShape() == flux::compositor::CursorShape::Arrow) {
          std::int32_t const cursorXi = static_cast<std::int32_t>(std::lround(cursorX));
          std::int32_t const cursorYi = static_cast<std::int32_t>(std::lround(cursorY));
          if (!output.moveCursor(cursorXi, cursorYi)) {
            (void)output.setCursorImage(hardwareCursorPixels, hardwareCursorWidth, hardwareCursorHeight);
            (void)output.moveCursor(cursorXi, cursorYi);
          }
        } else {
          if (hardwareArrowCursor) output.hideCursor();
          drawFallbackCursor(*canvas, wayland.cursorShape(), cursorX, cursorY);
        }
      }
      for (auto it = clientImages.begin(); it != clientImages.end();) {
        if (liveSurfaceIds.contains(it->first)) {
          ++it;
        } else {
          it = clientImages.erase(it);
        }
      }
      for (auto it = surfaceVisuals.begin(); it != surfaceVisuals.end();) {
        if (liveSurfaceIds.contains(it->first)) {
          ++it;
        } else {
          it = surfaceVisuals.erase(it);
        }
      }
      canvas->present();
      wayland.sendFrameCallbacks(monotonicMilliseconds());
    }

    output.hideCursor();
    return 0;
  } catch (std::exception const& e) {
    std::fprintf(stderr, "flux-compositor: %s\n", e.what());
    return 1;
  }
}
