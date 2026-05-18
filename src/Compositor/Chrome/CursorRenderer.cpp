#include "Compositor/Chrome/CursorRenderer.hpp"

#include "Compositor/Surface/SurfaceRenderer.hpp"

#include <Flux/Core/Geometry.hpp>

#include <algorithm>
#include <cmath>

namespace flux::compositor {
namespace {

void drawArrowCursor(Canvas& canvas, float cursorX, float cursorY) {
  Path cursor;
  cursor.moveTo({cursorX, cursorY});
  cursor.lineTo({cursorX + 2.f, cursorY + 22.f});
  cursor.lineTo({cursorX + 8.f, cursorY + 16.f});
  cursor.lineTo({cursorX + 14.f, cursorY + 30.f});
  cursor.lineTo({cursorX + 19.f, cursorY + 28.f});
  cursor.lineTo({cursorX + 13.f, cursorY + 14.f});
  cursor.lineTo({cursorX + 21.f, cursorY + 14.f});
  cursor.close();
  canvas.drawPath(cursor,
                  FillStyle::solid(Colors::white),
                  StrokeStyle::solid(Colors::black, 1.f));
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

void drawLineCursor(Canvas& canvas, Point from, Point to, float width = 2.f) {
  canvas.drawLine({from.x + 1.f, from.y + 1.f},
                  {to.x + 1.f, to.y + 1.f},
                  StrokeStyle::solid(Colors::black, width + 1.f));
  canvas.drawLine(from, to, StrokeStyle::solid(Colors::white, width));
}

} // namespace

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

void drawFallbackCursor(Canvas& canvas, CursorShape shape, float cursorX, float cursorY) {
  switch (shape) {
  case CursorShape::IBeam:
    drawLineCursor(canvas, {cursorX, cursorY}, {cursorX, cursorY + 24.f}, 2.f);
    drawLineCursor(canvas, {cursorX - 5.f, cursorY}, {cursorX + 5.f, cursorY}, 2.f);
    drawLineCursor(canvas, {cursorX - 5.f, cursorY + 24.f}, {cursorX + 5.f, cursorY + 24.f}, 2.f);
    return;
  case CursorShape::Crosshair:
    drawLineCursor(canvas, {cursorX - 12.f, cursorY}, {cursorX + 12.f, cursorY}, 2.f);
    drawLineCursor(canvas, {cursorX, cursorY - 12.f}, {cursorX, cursorY + 12.f}, 2.f);
    return;
  case CursorShape::Hand:
    canvas.drawCircle({cursorX + 7.f, cursorY + 8.f},
                      7.f,
                      FillStyle::solid(Colors::white),
                      StrokeStyle::solid(Colors::black, 1.f));
    drawLineCursor(canvas, {cursorX + 7.f, cursorY + 8.f}, {cursorX + 7.f, cursorY + 25.f}, 3.f);
    return;
  case CursorShape::ResizeEW:
    drawLineCursor(canvas, {cursorX - 12.f, cursorY}, {cursorX + 12.f, cursorY}, 2.f);
    drawLineCursor(canvas, {cursorX - 12.f, cursorY}, {cursorX - 6.f, cursorY - 6.f}, 2.f);
    drawLineCursor(canvas, {cursorX - 12.f, cursorY}, {cursorX - 6.f, cursorY + 6.f}, 2.f);
    drawLineCursor(canvas, {cursorX + 12.f, cursorY}, {cursorX + 6.f, cursorY - 6.f}, 2.f);
    drawLineCursor(canvas, {cursorX + 12.f, cursorY}, {cursorX + 6.f, cursorY + 6.f}, 2.f);
    return;
  case CursorShape::ResizeNS:
    drawLineCursor(canvas, {cursorX, cursorY - 12.f}, {cursorX, cursorY + 12.f}, 2.f);
    drawLineCursor(canvas, {cursorX, cursorY - 12.f}, {cursorX - 6.f, cursorY - 6.f}, 2.f);
    drawLineCursor(canvas, {cursorX, cursorY - 12.f}, {cursorX + 6.f, cursorY - 6.f}, 2.f);
    drawLineCursor(canvas, {cursorX, cursorY + 12.f}, {cursorX - 6.f, cursorY + 6.f}, 2.f);
    drawLineCursor(canvas, {cursorX, cursorY + 12.f}, {cursorX + 6.f, cursorY + 6.f}, 2.f);
    return;
  case CursorShape::ResizeNESW:
    drawLineCursor(canvas, {cursorX - 10.f, cursorY + 10.f}, {cursorX + 10.f, cursorY - 10.f}, 2.f);
    return;
  case CursorShape::ResizeNWSE:
    drawLineCursor(canvas, {cursorX - 10.f, cursorY - 10.f}, {cursorX + 10.f, cursorY + 10.f}, 2.f);
    return;
  case CursorShape::ResizeAll:
    drawLineCursor(canvas, {cursorX - 12.f, cursorY}, {cursorX + 12.f, cursorY}, 2.f);
    drawLineCursor(canvas, {cursorX, cursorY - 12.f}, {cursorX, cursorY + 12.f}, 2.f);
    return;
  case CursorShape::NotAllowed:
    canvas.drawCircle({cursorX + 8.f, cursorY + 8.f},
                      9.f,
                      FillStyle::none(),
                      StrokeStyle::solid(Colors::white, 4.f));
    canvas.drawLine({cursorX + 2.f, cursorY + 14.f},
                    {cursorX + 14.f, cursorY + 2.f},
                    StrokeStyle::solid(Colors::white, 4.f));
    canvas.drawCircle({cursorX + 8.f, cursorY + 8.f},
                      9.f,
                      FillStyle::none(),
                      StrokeStyle::solid(Colors::black, 1.f));
    canvas.drawLine({cursorX + 2.f, cursorY + 14.f},
                    {cursorX + 14.f, cursorY + 2.f},
                    StrokeStyle::solid(Colors::black, 1.f));
    return;
  case CursorShape::Arrow:
    drawArrowCursor(canvas, cursorX, cursorY);
    return;
  }
}

void drawCompositorCursor(WaylandServer& wayland,
                          Canvas& canvas,
                          platform::KmsOutput const& output,
                          CachedClientImage& cursorImage,
                          bool hardwareArrowCursor,
                          std::vector<std::uint32_t> const& hardwareCursorPixels,
                          std::uint32_t hardwareCursorWidth,
                          std::uint32_t hardwareCursorHeight) {
  if (auto cursorSurface = wayland.cursorSurface()) {
    if (hardwareArrowCursor) output.hideCursor();
    updateCachedImage(wayland, canvas, *cursorSurface, cursorImage);
    if (cursorImage.image) {
      float const cursorSourceWidth = cursorSurface->sourceWidth > 0.f
                                          ? cursorSurface->sourceWidth
                                          : static_cast<float>(cursorImage.image->size().width);
      float const cursorSourceHeight = cursorSurface->sourceHeight > 0.f
                                           ? cursorSurface->sourceHeight
                                           : static_cast<float>(cursorImage.image->size().height);
      canvas.drawImage(*cursorImage.image,
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

  cursorImage = {};
  float const cursorX = wayland.pointerX();
  float const cursorY = wayland.pointerY();
  if (hardwareArrowCursor && wayland.cursorShape() == CursorShape::Arrow) {
    std::int32_t const cursorXi = static_cast<std::int32_t>(std::lround(cursorX));
    std::int32_t const cursorYi = static_cast<std::int32_t>(std::lround(cursorY));
    if (!output.moveCursor(cursorXi, cursorYi)) {
      (void)output.setCursorImage(hardwareCursorPixels, hardwareCursorWidth, hardwareCursorHeight);
      (void)output.moveCursor(cursorXi, cursorYi);
    }
  } else {
    if (hardwareArrowCursor) output.hideCursor();
    drawFallbackCursor(canvas, wayland.cursorShape(), cursorX, cursorY);
  }
}

} // namespace flux::compositor
