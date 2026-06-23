#pragma once

/// \file Lambda/UI/Views/Svg.hpp
///
/// Static SVG-like vector document view.

#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Graphics/Styles.hpp>
#include <Lambda/UI/Detail/PrimitiveForwards.hpp>
#include <Lambda/Layout/LayoutEngine.hpp>
#include <Lambda/UI/ViewModifiers.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lambdaui {

class MountContext;
namespace scenegraph {
class SceneNode;
}
namespace detail {
struct SvgAccess;
}

enum class SvgPreserveAspectRatio : std::uint8_t {
  Meet,
  Slice,
  Stretch,
};

class SvgNode {
public:
  SvgNode();
  SvgNode(SvgNode const&);
  SvgNode(SvgNode&&) noexcept;
  SvgNode& operator=(SvgNode const&);
  SvgNode& operator=(SvgNode&&) noexcept;
  ~SvgNode();

  bool operator==(SvgNode const& other) const;

private:
  struct Impl;

  explicit SvgNode(std::unique_ptr<Impl> impl);

  std::unique_ptr<Impl> impl_;

  friend struct detail::SvgAccess;
};

namespace svg {

SvgNode path(std::string d, FillStyle fill = FillStyle::none(),
             StrokeStyle stroke = StrokeStyle::none(), float opacity = 1.f);
SvgNode rect(float x, float y, float width, float height,
             CornerRadius cornerRadius = {}, FillStyle fill = FillStyle::none(),
             StrokeStyle stroke = StrokeStyle::none(), float opacity = 1.f);
SvgNode circle(float cx, float cy, float r, FillStyle fill = FillStyle::none(),
               StrokeStyle stroke = StrokeStyle::none(), float opacity = 1.f);
SvgNode ellipse(float cx, float cy, float rx, float ry, FillStyle fill = FillStyle::none(),
                StrokeStyle stroke = StrokeStyle::none(), float opacity = 1.f);
SvgNode line(float x1, float y1, float x2, float y2, StrokeStyle stroke,
             float opacity = 1.f);
SvgNode polygon(std::vector<Point> points, FillStyle fill = FillStyle::none(),
                StrokeStyle stroke = StrokeStyle::none(), float opacity = 1.f);
SvgNode polyline(std::vector<Point> points, FillStyle fill = FillStyle::none(),
                 StrokeStyle stroke = StrokeStyle::none(), float opacity = 1.f);
SvgNode group(Mat3 transform, std::vector<SvgNode> children, float opacity = 1.f);
SvgNode translated(float x, float y, std::vector<SvgNode> children);
SvgNode rotated(float radians, Point pivot, std::vector<SvgNode> children);
SvgNode scaled(float sx, float sy, std::vector<SvgNode> children);

} // namespace svg

struct Svg : ViewModifiers<Svg> {
  Rect viewBox{0.f, 0.f, 0.f, 0.f};
  SvgPreserveAspectRatio preserveAspectRatio = SvgPreserveAspectRatio::Meet;
  Size intrinsicSize{0.f, 0.f};
  std::vector<SvgNode> children;

  Size measure(MeasureContext&, LayoutConstraints const&, LayoutHints const&, TextSystem&) const;
  std::unique_ptr<scenegraph::SceneNode> mount(MountContext&) const;

  bool operator==(Svg const&) const = default;
};

} // namespace lambdaui
