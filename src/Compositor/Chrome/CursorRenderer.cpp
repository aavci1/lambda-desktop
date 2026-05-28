#include "Compositor/Chrome/CursorRenderer.hpp"

#include <Flux/Core/Geometry.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>

namespace flux::compositor {
namespace {

struct ThemeCursor {
  std::vector<std::uint8_t> rgbaPixels;
  std::vector<std::uint32_t> premultipliedArgbPixels;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::int32_t hotspotX = 0;
  std::int32_t hotspotY = 0;
};

struct HardwareCursorPixels {
  std::vector<std::uint32_t> pixels;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

constexpr std::uint32_t kXcursorMagic = 0x72756358u;
constexpr std::uint32_t kXcursorImageType = 0xfffd0002u;
constexpr std::uint32_t kXcursorImageVersion = 1u;

std::optional<std::uint32_t> readU32(std::ifstream& file) {
  unsigned char bytes[4]{};
  file.read(reinterpret_cast<char*>(bytes), sizeof(bytes));
  if (!file) return std::nullopt;
  return static_cast<std::uint32_t>(bytes[0]) |
         (static_cast<std::uint32_t>(bytes[1]) << 8u) |
         (static_cast<std::uint32_t>(bytes[2]) << 16u) |
         (static_cast<std::uint32_t>(bytes[3]) << 24u);
}

std::vector<std::string> splitCommaList(std::string const& value) {
  std::vector<std::string> result;
  std::stringstream stream(value);
  std::string item;
  while (std::getline(stream, item, ',')) {
    item.erase(item.begin(), std::find_if(item.begin(), item.end(), [](unsigned char c) {
      return !std::isspace(c);
    }));
    item.erase(std::find_if(item.rbegin(), item.rend(), [](unsigned char c) {
      return !std::isspace(c);
    }).base(), item.end());
    if (!item.empty()) result.push_back(std::move(item));
  }
  return result;
}

std::vector<std::filesystem::path> iconSearchPaths() {
  std::vector<std::filesystem::path> paths;
  if (char const* xcursorPath = std::getenv("XCURSOR_PATH"); xcursorPath && *xcursorPath) {
    std::stringstream stream(xcursorPath);
    std::string item;
    while (std::getline(stream, item, ':')) {
      if (!item.empty()) paths.emplace_back(item);
    }
  }
  if (char const* home = std::getenv("HOME"); home && *home) {
    paths.emplace_back(std::filesystem::path(home) / ".icons");
    paths.emplace_back(std::filesystem::path(home) / ".local/share/icons");
  }
  paths.emplace_back("/usr/local/share/icons");
  paths.emplace_back("/usr/share/icons");
  paths.emplace_back("/usr/share/pixmaps");
  return paths;
}

std::vector<std::string> inheritedThemes(std::string const& themeName,
                                         std::vector<std::filesystem::path> const& searchPaths) {
  for (auto const& base : searchPaths) {
    std::ifstream file(base / themeName / "index.theme");
    if (!file) continue;
    std::string line;
    while (std::getline(file, line)) {
      if (line.rfind("Inherits=", 0) == 0) return splitCommaList(line.substr(9));
    }
  }
  return {};
}

std::optional<std::filesystem::path> findCursorFile(std::string const& cursorName,
                                                    std::string const& themeName,
                                                    std::vector<std::filesystem::path> const& searchPaths,
                                                    std::unordered_set<std::string>& visited) {
  if (themeName.empty() || !visited.insert(themeName).second) return std::nullopt;
  for (auto const& base : searchPaths) {
    std::filesystem::path candidate = base / themeName / "cursors" / cursorName;
    if (std::filesystem::is_regular_file(candidate)) return candidate;
  }
  for (std::string const& inherited : inheritedThemes(themeName, searchPaths)) {
    if (auto path = findCursorFile(cursorName, inherited, searchPaths, visited)) return path;
  }
  return std::nullopt;
}

std::optional<std::filesystem::path> findCursorFile(std::string const& cursorName,
                                                    std::string const& themeName) {
  std::vector<std::filesystem::path> const paths = iconSearchPaths();
  std::vector<std::string> themes;
  if (!themeName.empty()) themes.push_back(themeName);
  themes.push_back("default");
  themes.push_back("hicolor");
  for (std::string const& theme : themes) {
    std::unordered_set<std::string> visited;
    if (auto path = findCursorFile(cursorName, theme, paths, visited)) return path;
  }
  return std::nullopt;
}

char const* const* cursorNames(CursorShape cursor) {
  static char const* const arrow[] = {"default", "left_ptr", nullptr};
  static char const* const ibeam[] = {"text", "xterm", nullptr};
  static char const* const hand[] = {"pointer", "hand2", "hand1", nullptr};
  static char const* const resizeEW[] = {"ew-resize", "col-resize", "sb_h_double_arrow", nullptr};
  static char const* const resizeNS[] = {"ns-resize", "row-resize", "sb_v_double_arrow", nullptr};
  static char const* const resizeNESW[] = {"nesw-resize", "fd_double_arrow", nullptr};
  static char const* const resizeNWSE[] = {"nwse-resize", "bd_double_arrow", nullptr};
  static char const* const resizeAll[] = {"all-scroll", "move", "fleur", nullptr};
  static char const* const crosshair[] = {"crosshair", "cross", nullptr};
  static char const* const notAllowed[] = {"not-allowed", "crossed_circle", nullptr};

  switch (cursor) {
  case CursorShape::Arrow: return arrow;
  case CursorShape::IBeam: return ibeam;
  case CursorShape::Hand: return hand;
  case CursorShape::ResizeEW: return resizeEW;
  case CursorShape::ResizeNS: return resizeNS;
  case CursorShape::ResizeNESW: return resizeNESW;
  case CursorShape::ResizeNWSE: return resizeNWSE;
  case CursorShape::ResizeAll: return resizeAll;
  case CursorShape::Crosshair: return crosshair;
  case CursorShape::NotAllowed: return notAllowed;
  }
  return arrow;
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

std::optional<int> parseCursorSize(std::string_view value) {
  int result = 0;
  auto const* begin = value.data();
  auto const* end = value.data() + value.size();
  auto [ptr, error] = std::from_chars(begin, end, result);
  if (error != std::errc{} || ptr != end || result < 8 || result > 256) return std::nullopt;
  return result;
}

std::string resolvedCursorTheme(std::optional<std::string> const& configuredTheme) {
  if (configuredTheme && !configuredTheme->empty()) return *configuredTheme;
  char const* envTheme = std::getenv("XCURSOR_THEME");
  return envTheme && *envTheme ? std::string(envTheme) : std::string{};
}

int resolvedCursorBaseSize(std::int32_t configuredSize) {
  if (configuredSize >= 8 && configuredSize <= 256) return configuredSize;
  if (char const* envSize = std::getenv("XCURSOR_SIZE"); envSize && *envSize) {
    if (auto size = parseCursorSize(envSize)) return *size;
  }
  return 24;
}

std::optional<ThemeCursor> loadThemeCursor(CursorShape shape,
                                           float scale,
                                           std::string const& themeName,
                                           int baseSize) {
  int const size = std::max(16, static_cast<int>(std::lround(static_cast<float>(baseSize) *
                                                            std::max(0.5f, scale))));

  for (char const* const* name = cursorNames(shape); *name; ++name) {
    auto path = findCursorFile(*name, themeName);
    if (!path) continue;
    std::ifstream file(*path, std::ios::binary);
    if (!file) continue;

    auto const magic = readU32(file);
    auto const headerSize = readU32(file);
    (void)readU32(file); // file version
    auto const tocCount = readU32(file);
    if (!magic || *magic != kXcursorMagic || !headerSize || *headerSize < 16u || !tocCount) continue;
    file.seekg(static_cast<std::streamoff>(*headerSize), std::ios::beg);
    if (!file) continue;

    struct TocEntry {
      std::uint32_t type = 0;
      std::uint32_t subtype = 0;
      std::uint32_t position = 0;
    };
    std::vector<TocEntry> entries;
    entries.reserve(*tocCount);
    for (std::uint32_t i = 0; i < *tocCount; ++i) {
      auto type = readU32(file);
      auto subtype = readU32(file);
      auto position = readU32(file);
      if (!type || !subtype || !position) break;
      if (*type == kXcursorImageType) entries.push_back({.type = *type, .subtype = *subtype, .position = *position});
    }
    if (entries.empty()) continue;
    std::sort(entries.begin(), entries.end(), [size](TocEntry const& lhs, TocEntry const& rhs) {
      return std::abs(static_cast<int>(lhs.subtype) - size) < std::abs(static_cast<int>(rhs.subtype) - size);
    });

    for (TocEntry const& entry : entries) {
      file.clear();
      file.seekg(static_cast<std::streamoff>(entry.position), std::ios::beg);
      auto const chunkHeader = readU32(file);
      auto const chunkType = readU32(file);
      auto const chunkSubtype = readU32(file);
      auto const chunkVersion = readU32(file);
      auto const width = readU32(file);
      auto const height = readU32(file);
      auto const hotspotX = readU32(file);
      auto const hotspotY = readU32(file);
      (void)readU32(file); // delay
      if (!chunkHeader || !chunkType || !chunkSubtype || !chunkVersion || !width || !height ||
          !hotspotX || !hotspotY) {
        continue;
      }
      if (*chunkType != kXcursorImageType || *chunkVersion != kXcursorImageVersion ||
          *width == 0 || *height == 0 || *width > 1024u || *height > 1024u) {
        continue;
      }
      std::size_t const count = static_cast<std::size_t>(*width) * static_cast<std::size_t>(*height);
      ThemeCursor cursor{
          .rgbaPixels = {},
          .premultipliedArgbPixels = {},
          .width = *width,
          .height = *height,
          .hotspotX = static_cast<std::int32_t>(*hotspotX),
          .hotspotY = static_cast<std::int32_t>(*hotspotY),
      };
      cursor.rgbaPixels.resize(count * 4u);
      cursor.premultipliedArgbPixels.resize(count);
      bool ok = true;
      for (std::size_t i = 0; i < count; ++i) {
        auto pixelValue = readU32(file);
        if (!pixelValue) {
          ok = false;
          break;
        }
        std::uint32_t const pixel = *pixelValue;
        auto const a = static_cast<std::uint8_t>((pixel >> 24u) & 0xffu);
        auto const r = static_cast<std::uint8_t>((pixel >> 16u) & 0xffu);
        auto const g = static_cast<std::uint8_t>((pixel >> 8u) & 0xffu);
        auto const b = static_cast<std::uint8_t>(pixel & 0xffu);
      cursor.rgbaPixels[i * 4u + 0u] = r;
      cursor.rgbaPixels[i * 4u + 1u] = g;
      cursor.rgbaPixels[i * 4u + 2u] = b;
      cursor.rgbaPixels[i * 4u + 3u] = a;
      cursor.premultipliedArgbPixels[i] = premulArgb(a, r, g, b);
      }
      if (ok) return cursor;
    }
  }

  if (shape != CursorShape::Arrow) return loadThemeCursor(CursorShape::Arrow, scale, themeName, baseSize);
  return std::nullopt;
}

bool hardwareCursorFits(platform::KmsOutput const& output, std::uint32_t width, std::uint32_t height) {
  std::uint32_t const maxWidth = output.cursorWidth();
  std::uint32_t const maxHeight = output.cursorHeight();
  return width > 0 && height > 0 && maxWidth > 0 && maxHeight > 0 && width <= maxWidth && height <= maxHeight;
}

void hideHardwareCursor(platform::KmsOutput const& output, CursorRenderState& state) {
  if (!state.hardwareVisible) return;
  output.hideCursor();
  state.hardwareVisible = false;
  state.hardwareSerial = 0;
  state.hardwareClientId = 0;
}

std::optional<HardwareCursorPixels> clientCursorPixelsForHardware(CommittedSurfaceSnapshot const& cursorSurface,
                                                                  float outputScale) {
  if (!cursorSurface.rgbaPixels || cursorSurface.rgbaPixels->empty() ||
      cursorSurface.bufferWidth <= 0 || cursorSurface.bufferHeight <= 0) {
    return std::nullopt;
  }
  auto const& rgbaPixels = *cursorSurface.rgbaPixels;
  std::size_t const count = static_cast<std::size_t>(cursorSurface.bufferWidth) *
                            static_cast<std::size_t>(cursorSurface.bufferHeight);
  if (rgbaPixels.size() != count * 4u) return std::nullopt;

  float const logicalWidth =
      cursorSurface.width > 0 ? static_cast<float>(cursorSurface.width) : static_cast<float>(cursorSurface.bufferWidth);
  float const logicalHeight =
      cursorSurface.height > 0 ? static_cast<float>(cursorSurface.height) : static_cast<float>(cursorSurface.bufferHeight);
  auto const targetWidth =
      static_cast<std::uint32_t>(std::max(1, static_cast<int>(std::lround(logicalWidth * outputScale))));
  auto const targetHeight =
      static_cast<std::uint32_t>(std::max(1, static_cast<int>(std::lround(logicalHeight * outputScale))));

  float const sourceX = cursorSurface.sourceX;
  float const sourceY = cursorSurface.sourceY;
  float const sourceWidth =
      cursorSurface.sourceWidth > 0.f ? cursorSurface.sourceWidth : static_cast<float>(cursorSurface.bufferWidth);
  float const sourceHeight =
      cursorSurface.sourceHeight > 0.f ? cursorSurface.sourceHeight : static_cast<float>(cursorSurface.bufferHeight);
  if (!std::isfinite(sourceX) || !std::isfinite(sourceY) ||
      !std::isfinite(sourceWidth) || !std::isfinite(sourceHeight) ||
      sourceWidth <= 0.f || sourceHeight <= 0.f ||
      sourceX < 0.f || sourceY < 0.f ||
      sourceX + sourceWidth > static_cast<float>(cursorSurface.bufferWidth) + 0.5f ||
      sourceY + sourceHeight > static_cast<float>(cursorSurface.bufferHeight) + 0.5f) {
    return std::nullopt;
  }

  std::vector<std::uint32_t> pixels(static_cast<std::size_t>(targetWidth) * targetHeight);
  for (std::uint32_t y = 0; y < targetHeight; ++y) {
    float const srcY = sourceY + (static_cast<float>(y) + 0.5f) * sourceHeight / static_cast<float>(targetHeight);
    auto const sampleY = static_cast<std::uint32_t>(
        std::clamp(static_cast<int>(std::floor(srcY)), 0, cursorSurface.bufferHeight - 1));
    for (std::uint32_t x = 0; x < targetWidth; ++x) {
      float const srcX = sourceX + (static_cast<float>(x) + 0.5f) * sourceWidth / static_cast<float>(targetWidth);
      auto const sampleX = static_cast<std::uint32_t>(
          std::clamp(static_cast<int>(std::floor(srcX)), 0, cursorSurface.bufferWidth - 1));
      std::size_t const srcIndex = static_cast<std::size_t>(sampleY) * cursorSurface.bufferWidth + sampleX;
      std::size_t const dstIndex = static_cast<std::size_t>(y) * targetWidth + x;
      auto const r = cursorSurface.pixelFormat == Image::PixelFormat::Bgra8888
                         ? rgbaPixels[srcIndex * 4u + 2u]
                         : rgbaPixels[srcIndex * 4u + 0u];
      auto const g = rgbaPixels[srcIndex * 4u + 1u];
      auto const b = cursorSurface.pixelFormat == Image::PixelFormat::Bgra8888
                         ? rgbaPixels[srcIndex * 4u + 0u]
                         : rgbaPixels[srcIndex * 4u + 2u];
      auto const a = rgbaPixels[srcIndex * 4u + 3u];
      pixels[dstIndex] = premulArgb(a, r, g, b);
    }
  }
  return HardwareCursorPixels{.pixels = std::move(pixels), .width = targetWidth, .height = targetHeight};
}

std::uint64_t themeSerial(CursorShape shape, float scale) {
  return (static_cast<std::uint64_t>(static_cast<std::uint8_t>(shape)) << 32u) ^
         static_cast<std::uint64_t>(std::lround(scale * 100.f));
}

} // namespace

void drawCompositorCursor(WaylandServer& wayland,
                          Canvas& canvas,
                          platform::KmsOutput const& output,
                          CursorRenderState& cursorState,
                          std::optional<std::string> const& cursorTheme,
                          std::int32_t cursorSize,
                          bool hardwareCursorEnabled) {
  if (auto cursorSurface = wayland.cursorSurface()) {
    float const outputScale = wayland.preferredScale();
    if (hardwareCursorEnabled) {
      std::optional<HardwareCursorPixels> hardwarePixels = clientCursorPixelsForHardware(*cursorSurface, outputScale);
      if (hardwarePixels && hardwareCursorFits(output, hardwarePixels->width, hardwarePixels->height)) {
        bool const imageChanged = !cursorState.hardwareVisible ||
                                  !cursorState.hardwareClient ||
                                  cursorState.hardwareClientId != cursorSurface->id ||
                                  cursorState.hardwareSerial != cursorSurface->serial;
        if (imageChanged) {
          cursorState.hardwareVisible =
              output.setCursorImage(hardwarePixels->pixels,
                                    hardwarePixels->width,
                                    hardwarePixels->height);
          cursorState.hardwareClient = cursorState.hardwareVisible;
          cursorState.hardwareClientId = cursorSurface->id;
          cursorState.hardwareSerial = cursorSurface->serial;
        }
        if (cursorState.hardwareVisible) {
          (void)output.moveCursor(static_cast<std::int32_t>(std::lround(static_cast<float>(cursorSurface->x) * outputScale)),
                                  static_cast<std::int32_t>(std::lround(static_cast<float>(cursorSurface->y) * outputScale)));
          cursorState.clientImage = {};
          return;
        }
      }
      CursorShape const fallbackShape = wayland.cursorShape();
      std::string const themeName = resolvedCursorTheme(cursorTheme);
      int const themeBaseSize = resolvedCursorBaseSize(cursorSize);
      std::uint64_t const serial = themeSerial(fallbackShape, outputScale);
      if (!cursorState.hardwareVisible ||
          cursorState.hardwareClient ||
          cursorState.hardwareShape != fallbackShape ||
          cursorState.hardwareSerial != serial ||
          cursorState.themeName != themeName ||
          cursorState.themeBaseSize != themeBaseSize ||
          std::abs(cursorState.themeScale - outputScale) > 0.01f) {
        if (auto themeCursor = loadThemeCursor(fallbackShape, outputScale, themeName, themeBaseSize);
            themeCursor && hardwareCursorFits(output, themeCursor->width, themeCursor->height)) {
          cursorState.hardwareVisible = output.setCursorImage(themeCursor->premultipliedArgbPixels,
                                                              themeCursor->width,
                                                              themeCursor->height,
                                                              themeCursor->hotspotX,
                                                              themeCursor->hotspotY);
          cursorState.hardwareClient = false;
          cursorState.hardwareClientId = 0;
          cursorState.hardwareSerial = serial;
          cursorState.hardwareShape = fallbackShape;
          cursorState.themeName = themeName;
          cursorState.themeScale = outputScale;
          cursorState.themeBaseSize = themeBaseSize;
        }
      }
      if (cursorState.hardwareVisible && !cursorState.hardwareClient) {
        (void)output.moveCursor(static_cast<std::int32_t>(std::lround(wayland.pointerX() * outputScale)),
                                static_cast<std::int32_t>(std::lround(wayland.pointerY() * outputScale)));
        cursorState.clientImage = {};
        return;
      }
    }

    hideHardwareCursor(output, cursorState);
    updateCachedImage(wayland, canvas, *cursorSurface, cursorState.clientImage);
    if (cursorState.clientImage.image) {
      float const cursorSourceWidth = cursorSurface->sourceWidth > 0.f
                                          ? cursorSurface->sourceWidth
                                          : static_cast<float>(cursorState.clientImage.image->size().width);
      float const cursorSourceHeight = cursorSurface->sourceHeight > 0.f
                                           ? cursorSurface->sourceHeight
                                           : static_cast<float>(cursorState.clientImage.image->size().height);
      canvas.drawImage(*cursorState.clientImage.image,
                       Rect::sharp(cursorSurface->sourceX,
                                   cursorSurface->sourceY,
                                   cursorSourceWidth,
                                   cursorSourceHeight),
                       Rect::sharp(static_cast<float>(cursorSurface->x),
                                   static_cast<float>(cursorSurface->y),
                                   static_cast<float>(cursorSurface->width),
                                   static_cast<float>(cursorSurface->height)));
    }
    return;
  }

  cursorState.clientImage = {};
  float const cursorX = wayland.pointerX();
  float const cursorY = wayland.pointerY();
  CursorShape const shape = wayland.cursorShape();
  float const outputScale = wayland.preferredScale();
  std::string const themeName = resolvedCursorTheme(cursorTheme);
  int const themeBaseSize = resolvedCursorBaseSize(cursorSize);
  std::uint64_t const serial = themeSerial(shape, outputScale);

  if (!cursorState.themeImage.image ||
      cursorState.themeShape != shape ||
      cursorState.themeName != themeName ||
      cursorState.themeBaseSize != themeBaseSize ||
      std::abs(cursorState.themeScale - outputScale) > 0.01f) {
    cursorState.themeImage = {};
    cursorState.themeShape = shape;
    cursorState.themeName = themeName;
    cursorState.themeScale = outputScale;
    cursorState.themeBaseSize = themeBaseSize;
    cursorState.themeSerial = serial;
    cursorState.themeHotspotX = 0;
    cursorState.themeHotspotY = 0;
    if (auto themeCursor = loadThemeCursor(shape, outputScale, themeName, themeBaseSize)) {
      cursorState.themeImage.id = static_cast<std::uint64_t>(static_cast<std::uint8_t>(shape)) + 1u;
      cursorState.themeImage.serial = serial;
      cursorState.themeImage.image = Image::fromRgbaPixels(themeCursor->width,
                                                           themeCursor->height,
                                                           themeCursor->rgbaPixels,
                                                           canvas.gpuDevice());
      cursorState.themeImage.logged = true;
      cursorState.themeHotspotX = themeCursor->hotspotX;
      cursorState.themeHotspotY = themeCursor->hotspotY;
      if (hardwareCursorEnabled && hardwareCursorFits(output, themeCursor->width, themeCursor->height)) {
        cursorState.hardwareVisible = output.setCursorImage(themeCursor->premultipliedArgbPixels,
                                                            themeCursor->width,
                                                            themeCursor->height,
                                                            themeCursor->hotspotX,
                                                            themeCursor->hotspotY);
        cursorState.hardwareClient = false;
        cursorState.hardwareClientId = 0;
        cursorState.hardwareSerial = serial;
        cursorState.hardwareShape = shape;
      } else {
        hideHardwareCursor(output, cursorState);
      }
    } else {
      hideHardwareCursor(output, cursorState);
    }
  }

  if (hardwareCursorEnabled &&
      cursorState.hardwareVisible &&
      !cursorState.hardwareClient &&
      cursorState.hardwareShape == shape &&
      cursorState.hardwareSerial == serial) {
    (void)output.moveCursor(static_cast<std::int32_t>(std::lround(cursorX * outputScale)),
                            static_cast<std::int32_t>(std::lround(cursorY * outputScale)));
    return;
  }

  hideHardwareCursor(output, cursorState);
  if (!cursorState.themeImage.image) return;

  float const logicalWidth =
      static_cast<float>(cursorState.themeImage.image->size().width) / std::max(0.5f, outputScale);
  float const logicalHeight =
      static_cast<float>(cursorState.themeImage.image->size().height) / std::max(0.5f, outputScale);
  canvas.drawImage(*cursorState.themeImage.image,
                   Rect::sharp(0.f,
                               0.f,
                               static_cast<float>(cursorState.themeImage.image->size().width),
                               static_cast<float>(cursorState.themeImage.image->size().height)),
                   Rect::sharp(cursorX - static_cast<float>(cursorState.themeHotspotX) / outputScale,
                               cursorY - static_cast<float>(cursorState.themeHotspotY) / outputScale,
                               logicalWidth,
                               logicalHeight));
}

bool moveCurrentHardwareCursor(WaylandServer& wayland,
                               platform::KmsOutput const& output,
                               CursorRenderState const& cursorState,
                               bool hardwareCursorEnabled) {
  if (!hardwareCursorEnabled || !cursorState.hardwareVisible) return false;

  float const outputScale = wayland.preferredScale();
  if (cursorState.hardwareClient) {
    auto cursorSurface = wayland.cursorSurface();
    if (!cursorSurface ||
        cursorState.hardwareClientId != cursorSurface->id ||
        cursorState.hardwareSerial != cursorSurface->serial) {
      return false;
    }
    return output.moveCursor(
        static_cast<std::int32_t>(std::lround(static_cast<float>(cursorSurface->x) * outputScale)),
        static_cast<std::int32_t>(std::lround(static_cast<float>(cursorSurface->y) * outputScale)));
  }

  CursorShape const shape = wayland.cursorShape();
  if (cursorState.hardwareShape != shape ||
      cursorState.hardwareSerial != themeSerial(shape, outputScale)) {
    return false;
  }
  return output.moveCursor(static_cast<std::int32_t>(std::lround(wayland.pointerX() * outputScale)),
                           static_cast<std::int32_t>(std::lround(wayland.pointerY() * outputScale)));
}

} // namespace flux::compositor
