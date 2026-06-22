#pragma once

/// \file Lambda/Graphics/Canvas.hpp
///
/// Part of the Lambda public API.


#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Core/Color.hpp>
#include <Lambda/Graphics/Path.hpp>
#include <Lambda/Graphics/Styles.hpp>
#include <Lambda/Graphics/ImageFillMode.hpp>
#include <Lambda/Graphics/TextLayout.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string_view>

namespace lambda {

class Image;
class Window;

enum class Backend : std::uint8_t { Metal, Vulkan };

class Canvas {
public:
  virtual ~Canvas();

  Canvas(const Canvas&) = delete;
  Canvas& operator=(const Canvas&) = delete;
  Canvas(Canvas&&) = delete;
  Canvas& operator=(Canvas&&) = delete;

  virtual Backend backend() const noexcept = 0;
  virtual unsigned int windowHandle() const = 0;

  virtual void resize(int width, int height) = 0;
  virtual void updateDpiScale(float scaleX, float scaleY) = 0;
  virtual float dpiScale() const noexcept = 0;

  virtual void beginFrame() = 0;
  virtual void present() = 0;

  // -------------------------------------------------------------------------
  // State stack
  // -------------------------------------------------------------------------

  virtual void save() = 0;
  virtual void restore() = 0;

  // -------------------------------------------------------------------------
  // Transform
  // -------------------------------------------------------------------------

  virtual void setTransform(Mat3 const& m) = 0;
  virtual void transform(Mat3 const& m) = 0;
  virtual void translate(Point offset) = 0;
  virtual void translate(float x, float y) = 0;
  virtual void scale(float sx, float sy) = 0;
  virtual void scale(float s) = 0;
  virtual void rotate(float radians) = 0;
  virtual void rotate(float radians, Point pivot) = 0;
  virtual Mat3 currentTransform() const = 0;

  // -------------------------------------------------------------------------
  // Clip
  // -------------------------------------------------------------------------

  virtual void clipRect(Rect rect, CornerRadius const& cornerRadius = CornerRadius{}, bool antiAlias = false) = 0;

  virtual Rect clipBounds() const = 0;
  virtual bool quickReject(Rect rect) const = 0;

  // -------------------------------------------------------------------------
  // Opacity / blend
  // -------------------------------------------------------------------------

  virtual void setOpacity(float opacity) = 0;
  virtual float opacity() const = 0;
  virtual void setBlendMode(BlendMode mode) = 0;
  virtual BlendMode blendMode() const = 0;

  // -------------------------------------------------------------------------
  // Drawing
  // -------------------------------------------------------------------------

  virtual void drawRect(Rect const& rect, CornerRadius const& cornerRadius, FillStyle const& fill,
                        StrokeStyle const& stroke, ShadowStyle const& shadow = ShadowStyle::none()) = 0;
  virtual void drawLine(Point from, Point to, StrokeStyle const& stroke) = 0;
  virtual void drawPath(Path const& path, FillStyle const& fill, StrokeStyle const& stroke,
                        ShadowStyle const& shadow = ShadowStyle::none()) = 0;
  virtual void drawCircle(Point center, float radius, FillStyle const& fill, StrokeStyle const& stroke) = 0;

  /// Draw laid-out text. `origin` is the layout box top-left (`TextLayout::measuredSize`).
  virtual void drawTextLayout(TextLayout const& layout, Point origin) = 0;

  /// Draw `src` sub-rect of the image (pixel space) into `dst` (logical space). UVs are derived as src/size.
  virtual void drawImage(Image const& image, Rect const& src, Rect const& dst, CornerRadius const& corners = {},
                         float opacity = 1.f) = 0;

  /// Repeat the image across `dst` with a repeat sampler (1 logical pixel ≈ 1 texel).
  virtual void drawImageTiled(Image const& image, Rect const& dst, CornerRadius const& corners = {},
                               float opacity = 1.f) = 0;

  void drawImage(Image const& image, Rect const& dst, ImageFillMode fillMode = ImageFillMode::Cover,
                 CornerRadius const& corners = {}, float opacity = 1.f);

  /// Draw a transparent backdrop-filter region. `radius` is expressed in logical pixels.
  /// Backends sample the scene rendered before this operation, blur it, then draw this
  /// region from that blurred snapshot through the region's bounds and corners. Consecutive
  /// backdrop regions may share one snapshot so multi-rect masks stay visually coherent.
  virtual void drawBackdropBlur(Rect const& rect, float radius, Color tint = Colors::transparent,
                                CornerRadius const& corners = {}) = 0;

  /// Draw a backdrop blur while allowing backends to cache a larger stable source region.
  /// This is useful for animated masks whose destination rect changes every frame but whose
  /// underlying backdrop does not.
  virtual void drawBackdropBlurCached(Rect const& rect, Rect const& cacheRect, float radius,
                                      Color tint = Colors::transparent,
                                      CornerRadius const& corners = {}) {
    (void)cacheRect;
    drawBackdropBlur(rect, radius, tint, corners);
  }

  /// Metal: `id<MTLDevice>` as `void*` (use with `loadImage(path, canvas.gpuDevice())`). Null if unavailable.
  virtual void* gpuDevice() const = 0;

  virtual void clear(Color color = Colors::transparent) = 0;

protected:
  Canvas() = default;
};

using RasterizeDrawCallback = std::function<void(Canvas&, Rect)>;

/// Records \p draw into an offscreen texture and returns it as an Image.
///
/// `logicalSize` is in logical canvas units. `dpiScale` is the target pixels-per-logical-unit scale.
/// The callback receives a temporary target-local canvas with bounds `{0, 0, logicalSize}`.
/// Returns null when the active backend cannot rasterize or the requested size is empty.
std::shared_ptr<Image> rasterizeToImage(Canvas& canvas, Size logicalSize,
                                        RasterizeDrawCallback draw, float dpiScale);

} // namespace lambda
