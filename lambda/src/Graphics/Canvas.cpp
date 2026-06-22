#include <Lambda/Graphics/Canvas.hpp>
#include <Lambda/Graphics/Image.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>

namespace lambda {

namespace {

Rect imageSourceRect(Size imageSize, Rect const& dst, ImageFillMode mode) {
  if (imageSize.width <= 0.f || imageSize.height <= 0.f || dst.width <= 0.f || dst.height <= 0.f) {
    return Rect::sharp(0.f, 0.f, 0.f, 0.f);
  }

  switch (mode) {
  case ImageFillMode::Stretch:
    return Rect::sharp(0.f, 0.f, imageSize.width, imageSize.height);

  case ImageFillMode::Fit: {
    float const scale =
        std::min(dst.width / imageSize.width, dst.height / imageSize.height);
    if (scale <= 0.f) {
      return Rect::sharp(0.f, 0.f, 0.f, 0.f);
    }
    float const sw = dst.width / scale;
    float const sh = dst.height / scale;
    float const ox = (imageSize.width - sw) * 0.5f;
    float const oy = (imageSize.height - sh) * 0.5f;
    return Rect::sharp(ox, oy, sw, sh);
  }

  case ImageFillMode::Cover: {
    float const scale =
        std::max(dst.width / imageSize.width, dst.height / imageSize.height);
    if (scale <= 0.f) {
      return Rect::sharp(0.f, 0.f, 0.f, 0.f);
    }
    float const sw = dst.width / scale;
    float const sh = dst.height / scale;
    float const ox = (imageSize.width - sw) * 0.5f;
    float const oy = (imageSize.height - sh) * 0.5f;
    return Rect::sharp(ox, oy, sw, sh);
  }

  case ImageFillMode::Center: {
    float const sx = std::clamp(dst.width, 0.f, imageSize.width);
    float const sy = std::clamp(dst.height, 0.f, imageSize.height);
    float const ox = (imageSize.width - sx) * 0.5f;
    float const oy = (imageSize.height - sy) * 0.5f;
    return Rect::sharp(ox, oy, sx, sy);
  }

  case ImageFillMode::Tile:
    assert(false && "Tile must be handled via drawImageTiled before imageSourceRect");
    return Rect::sharp(0.f, 0.f, 0.f, 0.f);
  }
  return Rect::sharp(0.f, 0.f, 0.f, 0.f);
}

} // namespace

Canvas::~Canvas() = default;

void Canvas::drawImage(Image const& image, Rect const& dst, ImageFillMode fillMode,
                       CornerRadius const& corners, float opacity) {
  if (fillMode == ImageFillMode::Tile) {
    drawImageTiled(image, dst, corners, opacity);
    return;
  }
  Rect const src = imageSourceRect(image.size(), dst, fillMode);
  drawImage(image, src, dst, corners, opacity);
}

} // namespace lambda
