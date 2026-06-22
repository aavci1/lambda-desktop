#include <Lambda/UI/Views/Svg.hpp>

#include <Lambda/Graphics/Canvas.hpp>
#include <Lambda/Graphics/SvgPath.hpp>
#include <Lambda/SceneGraph/RasterCacheNode.hpp>
#include <Lambda/SceneGraph/RenderNode.hpp>
#include <Lambda/UI/MeasureContext.hpp>
#include <Lambda/UI/MountContext.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>

namespace lambda {

namespace {

struct SvgPathElement {
  std::string d;
  FillStyle fill{FillStyle::none()};
  StrokeStyle stroke{StrokeStyle::none()};
  float opacity = 1.f;
  bool operator==(SvgPathElement const&) const = default;
};

struct SvgRectElement {
  float x = 0.f;
  float y = 0.f;
  float width = 0.f;
  float height = 0.f;
  CornerRadius cornerRadius{};
  FillStyle fill{FillStyle::none()};
  StrokeStyle stroke{StrokeStyle::none()};
  float opacity = 1.f;
  bool operator==(SvgRectElement const&) const = default;
};

struct SvgCircleElement {
  float cx = 0.f;
  float cy = 0.f;
  float r = 0.f;
  FillStyle fill{FillStyle::none()};
  StrokeStyle stroke{StrokeStyle::none()};
  float opacity = 1.f;
  bool operator==(SvgCircleElement const&) const = default;
};

struct SvgEllipseElement {
  float cx = 0.f;
  float cy = 0.f;
  float rx = 0.f;
  float ry = 0.f;
  FillStyle fill{FillStyle::none()};
  StrokeStyle stroke{StrokeStyle::none()};
  float opacity = 1.f;
  bool operator==(SvgEllipseElement const&) const = default;
};

struct SvgLineElement {
  float x1 = 0.f;
  float y1 = 0.f;
  float x2 = 0.f;
  float y2 = 0.f;
  StrokeStyle stroke{StrokeStyle::none()};
  float opacity = 1.f;
  bool operator==(SvgLineElement const&) const = default;
};

struct SvgPolygonElement {
  std::vector<Point> points;
  FillStyle fill{FillStyle::none()};
  StrokeStyle stroke{StrokeStyle::none()};
  float opacity = 1.f;
  bool operator==(SvgPolygonElement const&) const = default;
};

struct SvgPolylineElement {
  std::vector<Point> points;
  FillStyle fill{FillStyle::none()};
  StrokeStyle stroke{StrokeStyle::none()};
  float opacity = 1.f;
  bool operator==(SvgPolylineElement const&) const = default;
};

struct SvgGroupElement {
  Mat3 transform{Mat3::identity()};
  std::vector<SvgNode> children;
  float opacity = 1.f;
  bool operator==(SvgGroupElement const&) const = default;
};

using SvgNodeStorage = std::variant<SvgPathElement, SvgRectElement, SvgCircleElement,
                                    SvgEllipseElement, SvgLineElement, SvgPolygonElement,
                                    SvgPolylineElement, SvgGroupElement>;

} // namespace

struct SvgNode::Impl {
  SvgNodeStorage storage;

  explicit Impl(SvgNodeStorage value)
      : storage(std::move(value)) {}

  bool operator==(Impl const&) const = default;
};

namespace detail {

struct SvgAccess {
  template <typename T>
  static SvgNode make(T value) {
    return SvgNode{std::make_unique<SvgNode::Impl>(SvgNodeStorage{std::move(value)})};
  }

  static SvgNode::Impl const* impl(SvgNode const& node) noexcept {
    return node.impl_.get();
  }
};

} // namespace detail

SvgNode::SvgNode() = default;

SvgNode::SvgNode(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

SvgNode::SvgNode(SvgNode const& other)
    : impl_(other.impl_ ? std::make_unique<Impl>(*other.impl_) : nullptr) {}

SvgNode::SvgNode(SvgNode&&) noexcept = default;

SvgNode& SvgNode::operator=(SvgNode const& other) {
  if (this != &other) {
    impl_ = other.impl_ ? std::make_unique<Impl>(*other.impl_) : nullptr;
  }
  return *this;
}

SvgNode& SvgNode::operator=(SvgNode&&) noexcept = default;

SvgNode::~SvgNode() = default;

bool SvgNode::operator==(SvgNode const& other) const {
  if (!impl_ || !other.impl_) {
    return impl_ == other.impl_;
  }
  return *impl_ == *other.impl_;
}

namespace svg {

SvgNode path(std::string d, FillStyle fill, StrokeStyle stroke, float opacity) {
  return detail::SvgAccess::make(SvgPathElement{
      .d = std::move(d), .fill = std::move(fill), .stroke = std::move(stroke), .opacity = opacity});
}

SvgNode rect(float x, float y, float width, float height, CornerRadius cornerRadius,
             FillStyle fill, StrokeStyle stroke, float opacity) {
  return detail::SvgAccess::make(SvgRectElement{
      .x = x, .y = y, .width = width, .height = height, .cornerRadius = cornerRadius,
      .fill = std::move(fill), .stroke = std::move(stroke), .opacity = opacity});
}

SvgNode circle(float cx, float cy, float r, FillStyle fill, StrokeStyle stroke, float opacity) {
  return detail::SvgAccess::make(SvgCircleElement{
      .cx = cx, .cy = cy, .r = r, .fill = std::move(fill),
      .stroke = std::move(stroke), .opacity = opacity});
}

SvgNode ellipse(float cx, float cy, float rx, float ry, FillStyle fill,
                StrokeStyle stroke, float opacity) {
  return detail::SvgAccess::make(SvgEllipseElement{
      .cx = cx, .cy = cy, .rx = rx, .ry = ry, .fill = std::move(fill),
      .stroke = std::move(stroke), .opacity = opacity});
}

SvgNode line(float x1, float y1, float x2, float y2, StrokeStyle stroke, float opacity) {
  return detail::SvgAccess::make(SvgLineElement{
      .x1 = x1, .y1 = y1, .x2 = x2, .y2 = y2, .stroke = std::move(stroke),
      .opacity = opacity});
}

SvgNode polygon(std::vector<Point> points, FillStyle fill, StrokeStyle stroke, float opacity) {
  return detail::SvgAccess::make(SvgPolygonElement{
      .points = std::move(points), .fill = std::move(fill), .stroke = std::move(stroke),
      .opacity = opacity});
}

SvgNode polyline(std::vector<Point> points, FillStyle fill, StrokeStyle stroke, float opacity) {
  return detail::SvgAccess::make(SvgPolylineElement{
      .points = std::move(points), .fill = std::move(fill), .stroke = std::move(stroke),
      .opacity = opacity});
}

SvgNode group(Mat3 transform, std::vector<SvgNode> children, float opacity) {
  return detail::SvgAccess::make(SvgGroupElement{
      .transform = transform, .children = std::move(children), .opacity = opacity});
}

SvgNode translated(float x, float y, std::vector<SvgNode> children) {
  return group(Mat3::translate(x, y), std::move(children));
}

SvgNode rotated(float radians, Point pivot, std::vector<SvgNode> children) {
  return group(Mat3::rotate(radians, pivot), std::move(children));
}

SvgNode scaled(float sx, float sy, std::vector<SvgNode> children) {
  return group(Mat3::scale(sx, sy), std::move(children));
}

} // namespace svg

namespace {

Size clampSize(Size size, LayoutConstraints const& constraints) {
  if (std::isfinite(constraints.maxWidth) && constraints.maxWidth >= 0.f) {
    size.width = std::min(size.width, constraints.maxWidth);
  }
  if (std::isfinite(constraints.maxHeight) && constraints.maxHeight >= 0.f) {
    size.height = std::min(size.height, constraints.maxHeight);
  }
  size.width = std::max(size.width, constraints.minWidth);
  size.height = std::max(size.height, constraints.minHeight);
  return Size{std::max(0.f, size.width), std::max(0.f, size.height)};
}

Size assignedSize(LayoutConstraints const& constraints, Size measured) {
  Size size = measured;
  if (std::isfinite(constraints.maxWidth) && constraints.maxWidth > 0.f) {
    size.width = constraints.maxWidth;
  }
  if (std::isfinite(constraints.maxHeight) && constraints.maxHeight > 0.f) {
    size.height = constraints.maxHeight;
  }
  size.width = std::max(size.width, constraints.minWidth);
  size.height = std::max(size.height, constraints.minHeight);
  return Size{std::max(0.f, size.width), std::max(0.f, size.height)};
}

Size preferredSize(Svg const& svg) {
  if (svg.intrinsicSize.width > 0.f && svg.intrinsicSize.height > 0.f) {
    return svg.intrinsicSize;
  }
  if (svg.viewBox.width <= 0.f || svg.viewBox.height <= 0.f) {
    return Size{0.f, 0.f};
  }
  return Size{svg.viewBox.width, svg.viewBox.height};
}

Size measureSvg(Svg const& svg, LayoutConstraints const& constraints) {
  return clampSize(preferredSize(svg), constraints);
}

void withOpacity(Canvas& canvas, float opacity, auto&& draw) {
  float const clamped = std::clamp(opacity, 0.f, 1.f);
  if (clamped <= 0.f) {
    return;
  }
  if (std::abs(clamped - 1.f) <= 0.0001f) {
    draw();
    return;
  }
  canvas.save();
  canvas.setOpacity(canvas.opacity() * clamped);
  draw();
  canvas.restore();
}

Path pathFromPoints(std::vector<Point> const& points, bool close) {
  Path path;
  if (points.empty()) {
    return path;
  }
  path.moveTo(points.front());
  for (std::size_t i = 1; i < points.size(); ++i) {
    path.lineTo(points[i]);
  }
  if (close) {
    path.close();
  }
  return path;
}

Path const& cachedPathFor(std::string const& d, std::unordered_map<std::string, Path>& cache) {
  auto it = cache.find(d);
  if (it == cache.end()) {
    it = cache.emplace(d, parseSvgPath(d)).first;
  }
  return it->second;
}

void drawNode(Canvas& canvas, SvgNode const& node, std::unordered_map<std::string, Path>& pathCache);

void drawChildren(Canvas& canvas, std::vector<SvgNode> const& children,
                  std::unordered_map<std::string, Path>& pathCache) {
  for (SvgNode const& child : children) {
    drawNode(canvas, child, pathCache);
  }
}

void drawNode(Canvas& canvas, SvgNode const& node, std::unordered_map<std::string, Path>& pathCache) {
  auto const* impl = detail::SvgAccess::impl(node);
  if (!impl) {
    return;
  }
  std::visit([&](auto const& value) {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, SvgPathElement>) {
      Path const& path = cachedPathFor(value.d, pathCache);
      withOpacity(canvas, value.opacity, [&] {
        canvas.drawPath(path, value.fill, value.stroke);
      });
    } else if constexpr (std::is_same_v<T, SvgRectElement>) {
      Rect const rect{value.x, value.y, value.width, value.height};
      withOpacity(canvas, value.opacity, [&] {
        canvas.drawRect(rect, value.cornerRadius, value.fill, value.stroke);
      });
    } else if constexpr (std::is_same_v<T, SvgCircleElement>) {
      withOpacity(canvas, value.opacity, [&] {
        canvas.drawCircle(Point{value.cx, value.cy}, value.r, value.fill, value.stroke);
      });
    } else if constexpr (std::is_same_v<T, SvgEllipseElement>) {
      Path path;
      path.ellipse(Point{value.cx, value.cy}, value.rx, value.ry);
      withOpacity(canvas, value.opacity, [&] {
        canvas.drawPath(path, value.fill, value.stroke);
      });
    } else if constexpr (std::is_same_v<T, SvgLineElement>) {
      withOpacity(canvas, value.opacity, [&] {
        canvas.drawLine(Point{value.x1, value.y1}, Point{value.x2, value.y2}, value.stroke);
      });
    } else if constexpr (std::is_same_v<T, SvgPolygonElement>) {
      Path path = pathFromPoints(value.points, true);
      withOpacity(canvas, value.opacity, [&] {
        canvas.drawPath(path, value.fill, value.stroke);
      });
    } else if constexpr (std::is_same_v<T, SvgPolylineElement>) {
      Path path = pathFromPoints(value.points, false);
      withOpacity(canvas, value.opacity, [&] {
        canvas.drawPath(path, value.fill, value.stroke);
      });
    } else if constexpr (std::is_same_v<T, SvgGroupElement>) {
      withOpacity(canvas, value.opacity, [&] {
        canvas.save();
        canvas.transform(value.transform);
        drawChildren(canvas, value.children, pathCache);
        canvas.restore();
      });
    }
  }, impl->storage);
}

Mat3 viewBoxTransform(Rect viewBox, Rect frame, SvgPreserveAspectRatio preserveAspectRatio) {
  if (viewBox.width <= 0.f || viewBox.height <= 0.f || frame.width <= 0.f || frame.height <= 0.f) {
    return Mat3::identity();
  }

  if (preserveAspectRatio == SvgPreserveAspectRatio::Stretch) {
    float const sx = frame.width / viewBox.width;
    float const sy = frame.height / viewBox.height;
    return Mat3::translate(frame.x - viewBox.x * sx, frame.y - viewBox.y * sy) * Mat3::scale(sx, sy);
  }

  float const sx = frame.width / viewBox.width;
  float const sy = frame.height / viewBox.height;
  float const scale = preserveAspectRatio == SvgPreserveAspectRatio::Slice ? std::max(sx, sy) : std::min(sx, sy);
  float const tx = frame.x + (frame.width - viewBox.width * scale) * 0.5f - viewBox.x * scale;
  float const ty = frame.y + (frame.height - viewBox.height * scale) * 0.5f - viewBox.y * scale;
  return Mat3::translate(tx, ty) * Mat3::scale(scale);
}

} // namespace

Size Svg::measure(MeasureContext& ctx, LayoutConstraints const& constraints,
                  LayoutHints const&, TextSystem&) const {
  ctx.advanceChildSlot();
  return measureSvg(*this, constraints);
}

std::unique_ptr<scenegraph::SceneNode> Svg::mount(MountContext& ctx) const {
  Size const measured = measureSvg(*this, ctx.constraints());
  Size const size = assignedSize(ctx.constraints(), measured);
  Svg document = *this;
  auto pathCache = std::make_shared<std::unordered_map<std::string, Path>>();
  auto draw = [document = std::move(document), pathCache](Canvas& canvas, Rect frame) mutable {
    if (document.viewBox.width <= 0.f || document.viewBox.height <= 0.f ||
        frame.width <= 0.f || frame.height <= 0.f) {
      return;
    }
    canvas.save();
    canvas.clipRect(frame, CornerRadius{}, true);
    canvas.transform(viewBoxTransform(document.viewBox, frame, document.preserveAspectRatio));
    drawChildren(canvas, document.children, *pathCache);
    canvas.restore();
  };

  auto renderNode = std::make_unique<scenegraph::RenderNode>(
      Rect{0.f, 0.f, size.width, size.height}, std::move(draw));
  auto* rawRenderNode = renderNode.get();
  rawRenderNode->setPurity(scenegraph::RenderNode::Purity::Pure);
  rawRenderNode->setLayoutConstraints(ctx.constraints());
  rawRenderNode->setRelayout([rawRenderNode, document = *this](LayoutConstraints const& constraints) mutable {
    rawRenderNode->setSize(assignedSize(constraints, measureSvg(document, constraints)));
  });

  auto rasterNode = std::make_unique<scenegraph::RasterCacheNode>(
      Rect{0.f, 0.f, size.width, size.height});
  auto* rawRasterNode = rasterNode.get();
  rasterNode->setSubtree(std::move(renderNode));
  rawRasterNode->setLayoutConstraints(ctx.constraints());
  rawRasterNode->setRelayout([rawRasterNode](LayoutConstraints const& constraints) {
    if (scenegraph::SceneNode* child = rawRasterNode->subtree()) {
      child->relayout(constraints);
      child->setPosition(Point{});
      rawRasterNode->setSize(child->size());
    }
  });
  return rasterNode;
}

} // namespace lambda
