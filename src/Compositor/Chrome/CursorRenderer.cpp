#include "Compositor/Chrome/CursorRenderer.hpp"

#include <Flux/Core/Geometry.hpp>

#include <X11/Xcursor/Xcursor.h>
#ifdef CursorShape
#undef CursorShape
#endif

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

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

struct XcursorImagesDeleter {
  void operator()(XcursorImages* images) const noexcept {
    if (images) XcursorImagesDestroy(images);
  }
};

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
  char const* theme = themeName.empty() ? nullptr : themeName.c_str();

  for (char const* const* name = cursorNames(shape); *name; ++name) {
    std::unique_ptr<XcursorImages, XcursorImagesDeleter> images{
        XcursorLibraryLoadImages(*name, theme, size)};
    if (!images || images->nimage <= 0 || !images->images || !images->images[0]) continue;

    XcursorImage const* image = images->images[0];
    if (image->width == 0 || image->height == 0 || !image->pixels) continue;

    ThemeCursor cursor{
        .rgbaPixels = {},
        .premultipliedArgbPixels = {},
        .width = static_cast<std::uint32_t>(image->width),
        .height = static_cast<std::uint32_t>(image->height),
        .hotspotX = static_cast<std::int32_t>(image->xhot),
        .hotspotY = static_cast<std::int32_t>(image->yhot),
    };
    std::size_t const count = static_cast<std::size_t>(cursor.width) * cursor.height;
    cursor.rgbaPixels.resize(count * 4u);
    cursor.premultipliedArgbPixels.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
      XcursorPixel const pixel = image->pixels[i];
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
    return cursor;
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

std::vector<std::uint32_t> clientCursorPixelsForHardware(CommittedSurfaceSnapshot const& cursorSurface,
                                                         float outputScale) {
  if (cursorSurface.rgbaPixels.empty() || cursorSurface.bufferWidth <= 0 || cursorSurface.bufferHeight <= 0) {
    return {};
  }
  std::size_t const count = static_cast<std::size_t>(cursorSurface.bufferWidth) *
                            static_cast<std::size_t>(cursorSurface.bufferHeight);
  if (cursorSurface.rgbaPixels.size() != count * 4u) return {};

  float const cursorScaleX = cursorSurface.width > 0
                                 ? static_cast<float>(cursorSurface.bufferWidth) /
                                       static_cast<float>(cursorSurface.width)
                                 : 0.f;
  float const cursorScaleY = cursorSurface.height > 0
                                 ? static_cast<float>(cursorSurface.bufferHeight) /
                                       static_cast<float>(cursorSurface.height)
                                 : 0.f;
  if (std::abs(cursorScaleX - outputScale) > 0.01f || std::abs(cursorScaleY - outputScale) > 0.01f) {
    return {};
  }

  std::vector<std::uint32_t> pixels(count);
  for (std::size_t i = 0; i < count; ++i) {
    auto const r = cursorSurface.rgbaPixels[i * 4u + 0u];
    auto const g = cursorSurface.rgbaPixels[i * 4u + 1u];
    auto const b = cursorSurface.rgbaPixels[i * 4u + 2u];
    auto const a = cursorSurface.rgbaPixels[i * 4u + 3u];
    pixels[i] = premulArgb(a, r, g, b);
  }
  return pixels;
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
    bool const wholeBuffer = cursorSurface->sourceX == 0.f &&
                             cursorSurface->sourceY == 0.f &&
                             cursorSurface->sourceWidth == static_cast<float>(cursorSurface->bufferWidth) &&
                             cursorSurface->sourceHeight == static_cast<float>(cursorSurface->bufferHeight);
    if (hardwareCursorEnabled &&
        wholeBuffer &&
        hardwareCursorFits(output,
                           static_cast<std::uint32_t>(cursorSurface->bufferWidth),
                           static_cast<std::uint32_t>(cursorSurface->bufferHeight))) {
      std::vector<std::uint32_t> pixels = clientCursorPixelsForHardware(*cursorSurface, outputScale);
      if (!pixels.empty()) {
        bool const imageChanged = !cursorState.hardwareVisible ||
                                  !cursorState.hardwareClient ||
                                  cursorState.hardwareClientId != cursorSurface->id ||
                                  cursorState.hardwareSerial != cursorSurface->serial;
        if (imageChanged) {
          cursorState.hardwareVisible =
              output.setCursorImage(pixels,
                                    static_cast<std::uint32_t>(cursorSurface->bufferWidth),
                                    static_cast<std::uint32_t>(cursorSurface->bufferHeight));
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

} // namespace flux::compositor
