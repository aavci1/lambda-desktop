#include <Lambda/UI/Element.hpp>

#include <cmath>

namespace lambda {

namespace {

float finiteOr(float value, float fallback) {
  return std::isfinite(value) ? value : fallback;
}

Mat3 translateMatrix(Vec2 delta) {
  return Mat3::translate(finiteOr(delta.x, 0.f), finiteOr(delta.y, 0.f));
}

Mat3 rotateMatrix(float radians) {
  return Mat3::rotate(finiteOr(radians, 0.f));
}

Mat3 scaleMatrix(Vec2 factors) {
  return Mat3::scale(finiteOr(factors.x, 1.f), finiteOr(factors.y, 1.f));
}

Reactive::Bindable<Mat3> composeTransform(Reactive::Bindable<Mat3> current,
                                          Reactive::Bindable<Mat3> next) {
  if (!current.isReactive() && !next.isReactive()) {
    return current.value() * next.value();
  }
  return Reactive::Bindable<Mat3>{
      [current = std::move(current), next = std::move(next)] {
        return current.evaluate() * next.evaluate();
      }};
}

Reactive::Bindable<Mat3> translateTransform(Reactive::Bindable<Vec2> delta) {
  if (!delta.isReactive()) {
    return translateMatrix(delta.value());
  }
  return Reactive::Bindable<Mat3>{[delta = std::move(delta)] {
    return translateMatrix(delta.evaluate());
  }};
}

Reactive::Bindable<Mat3> rotateTransform(Reactive::Bindable<float> radians) {
  if (!radians.isReactive()) {
    return rotateMatrix(radians.value());
  }
  return Reactive::Bindable<Mat3>{[radians = std::move(radians)] {
    return rotateMatrix(radians.evaluate());
  }};
}

Reactive::Bindable<Mat3> scaleTransform(Reactive::Bindable<Vec2> factors) {
  if (!factors.isReactive()) {
    return scaleMatrix(factors.value());
  }
  return Reactive::Bindable<Mat3>{[factors = std::move(factors)] {
    return scaleMatrix(factors.evaluate());
  }};
}

} // namespace

detail::ElementModifiers::ElementModifiers(detail::ElementModifiers const& o)
    : padding(o.padding)
    , fill(o.fill)
    , stroke(o.stroke)
    , shadow(o.shadow)
    , cornerRadius(o.cornerRadius)
    , opacity(o.opacity)
    , transform(o.transform)
    , clip(o.clip)
    , positionX(o.positionX)
    , positionY(o.positionY)
    , sizeWidth(o.sizeWidth)
    , sizeHeight(o.sizeHeight)
    , hasSizeWidth(o.hasSizeWidth)
    , hasSizeHeight(o.hasSizeHeight)
    , rasterize(o.rasterize)
    , rasterizeInvalidators(o.rasterizeInvalidators)
    , overlay(o.overlay ? std::make_unique<Element>(*o.overlay) : nullptr)
    , onTap(o.onTap)
    , onPointerEnter(o.onPointerEnter)
    , onPointerExit(o.onPointerExit)
    , onFocus(o.onFocus)
    , onBlur(o.onBlur)
    , onPointerDown(o.onPointerDown)
    , onPointerUp(o.onPointerUp)
    , onPointerMove(o.onPointerMove)
    , onScroll(o.onScroll)
    , onKeyDown(o.onKeyDown)
    , onKeyUp(o.onKeyUp)
    , onTextInput(o.onTextInput)
    , onTapWithModifiers(o.onTapWithModifiers)
    , focusable(o.focusable)
    , cursor(o.cursor)
    , windowDragRegion(o.windowDragRegion)
    , windowResizeEdge(o.windowResizeEdge) {}

detail::ElementModifiers& detail::ElementModifiers::operator=(detail::ElementModifiers const& o) {
  if (this != &o) {
    padding = o.padding;
    fill = o.fill;
    stroke = o.stroke;
    shadow = o.shadow;
    cornerRadius = o.cornerRadius;
    opacity = o.opacity;
    transform = o.transform;
    clip = o.clip;
    positionX = o.positionX;
    positionY = o.positionY;
    sizeWidth = o.sizeWidth;
    sizeHeight = o.sizeHeight;
    hasSizeWidth = o.hasSizeWidth;
    hasSizeHeight = o.hasSizeHeight;
    rasterize = o.rasterize;
    rasterizeInvalidators = o.rasterizeInvalidators;
    overlay = o.overlay ? std::make_unique<Element>(*o.overlay) : nullptr;
    onTap = o.onTap;
    onPointerEnter = o.onPointerEnter;
    onPointerExit = o.onPointerExit;
    onFocus = o.onFocus;
    onBlur = o.onBlur;
    onPointerDown = o.onPointerDown;
    onPointerUp = o.onPointerUp;
    onPointerMove = o.onPointerMove;
    onScroll = o.onScroll;
    onKeyDown = o.onKeyDown;
    onKeyUp = o.onKeyUp;
    onTextInput = o.onTextInput;
    onTapWithModifiers = o.onTapWithModifiers;
    focusable = o.focusable;
    cursor = o.cursor;
    windowDragRegion = o.windowDragRegion;
    windowResizeEdge = o.windowResizeEdge;
  }
  return *this;
}

detail::ElementModifiers::~ElementModifiers() = default;

Element Element::padding(Reactive::Bindable<EdgeInsets> insets) && {
  writableModifiers().padding = std::move(insets);
  return std::move(*this);
}

Element Element::padding(Reactive::Bindable<float> all) && {
  writableModifiers().padding = Reactive::Bindable<EdgeInsets>([all = std::move(all)] {
    return EdgeInsets::uniform(all.evaluate());
  });
  return std::move(*this);
}

Element Element::padding(Reactive::Bindable<float> top, Reactive::Bindable<float> right,
                         Reactive::Bindable<float> bottom, Reactive::Bindable<float> left) && {
  writableModifiers().padding = Reactive::Bindable<EdgeInsets>(
      [top = std::move(top), right = std::move(right), bottom = std::move(bottom),
       left = std::move(left)] {
        return EdgeInsets{
            .top = top.evaluate(),
            .right = right.evaluate(),
            .bottom = bottom.evaluate(),
            .left = left.evaluate(),
        };
      });
  return std::move(*this);
}

Element Element::fill(Reactive::Bindable<FillStyle> style) && {
  writableModifiers().fill = std::move(style);
  return std::move(*this);
}

Element Element::fill(Reactive::Bindable<Color> color) && {
  writableModifiers().fill = Reactive::Bindable<FillStyle>([color = std::move(color)] {
    return FillStyle::solid(color.evaluate());
  });
  return std::move(*this);
}

Element Element::shadow(Reactive::Bindable<ShadowStyle> style) && {
  writableModifiers().shadow = std::move(style);
  return std::move(*this);
}

Element Element::size(Reactive::Bindable<float> width, Reactive::Bindable<float> height) && {
  detail::ElementModifiers& modifiers = writableModifiers();
  modifiers.sizeWidth = std::move(width);
  modifiers.sizeHeight = std::move(height);
  modifiers.hasSizeWidth = true;
  modifiers.hasSizeHeight = true;
  return std::move(*this);
}

Element Element::width(Reactive::Bindable<float> w) && {
  detail::ElementModifiers& modifiers = writableModifiers();
  modifiers.sizeWidth = std::move(w);
  modifiers.hasSizeWidth = true;
  return std::move(*this);
}

Element Element::height(Reactive::Bindable<float> h) && {
  detail::ElementModifiers& modifiers = writableModifiers();
  modifiers.sizeHeight = std::move(h);
  modifiers.hasSizeHeight = true;
  return std::move(*this);
}

Element Element::stroke(Reactive::Bindable<StrokeStyle> style) && {
  writableModifiers().stroke = std::move(style);
  return std::move(*this);
}

Element Element::stroke(Reactive::Bindable<Color> color, Reactive::Bindable<float> width) && {
  writableModifiers().stroke = Reactive::Bindable<StrokeStyle>(
      [color = std::move(color), width = std::move(width)] {
        return StrokeStyle::solid(color.evaluate(), width.evaluate());
      });
  return std::move(*this);
}

Element Element::cornerRadius(Reactive::Bindable<CornerRadius> radius) && {
  writableModifiers().cornerRadius = std::move(radius);
  return std::move(*this);
}

Element Element::cornerRadius(Reactive::Bindable<float> radius) && {
  writableModifiers().cornerRadius = Reactive::Bindable<CornerRadius>([radius = std::move(radius)] {
    return CornerRadius(radius.evaluate());
  });
  return std::move(*this);
}

Element Element::opacity(Reactive::Bindable<float> opacity) && {
  writableModifiers().opacity = std::move(opacity);
  return std::move(*this);
}

Element Element::position(Reactive::Bindable<Vec2> p) && {
  writableModifiers().positionX = Reactive::Bindable<float>([p] { return p.evaluate().x; });
  writableModifiers().positionY = Reactive::Bindable<float>([p] { return p.evaluate().y; });
  return std::move(*this);
}

Element Element::position(Reactive::Bindable<float> x, Reactive::Bindable<float> y) && {
  detail::ElementModifiers& modifiers = writableModifiers();
  modifiers.positionX = std::move(x);
  modifiers.positionY = std::move(y);
  return std::move(*this);
}

Element Element::translate(Reactive::Bindable<Vec2> delta) && {
  detail::ElementModifiers& modifiers = writableModifiers();
  modifiers.transform = composeTransform(std::move(modifiers.transform),
                                         translateTransform(std::move(delta)));
  return std::move(*this);
}

Element Element::translate(Reactive::Bindable<float> dx, Reactive::Bindable<float> dy) && {
  Reactive::Bindable<Vec2> delta =
      (!dx.isReactive() && !dy.isReactive())
          ? Reactive::Bindable<Vec2>{Vec2{dx.value(), dy.value()}}
          : Reactive::Bindable<Vec2>{[dx = std::move(dx), dy = std::move(dy)] {
              return Vec2{dx.evaluate(), dy.evaluate()};
            }};
  detail::ElementModifiers& modifiers = writableModifiers();
  modifiers.transform = composeTransform(std::move(modifiers.transform),
                                         translateTransform(std::move(delta)));
  return std::move(*this);
}

Element Element::rotate(Reactive::Bindable<float> radians) && {
  detail::ElementModifiers& modifiers = writableModifiers();
  modifiers.transform = composeTransform(std::move(modifiers.transform),
                                         rotateTransform(std::move(radians)));
  return std::move(*this);
}

Element Element::scale(Reactive::Bindable<float> factor) && {
  Reactive::Bindable<Vec2> factors =
      !factor.isReactive()
          ? Reactive::Bindable<Vec2>{Vec2{factor.value(), factor.value()}}
          : Reactive::Bindable<Vec2>{[factor = std::move(factor)] {
              float const value = factor.evaluate();
              return Vec2{value, value};
            }};
  detail::ElementModifiers& modifiers = writableModifiers();
  modifiers.transform = composeTransform(std::move(modifiers.transform),
                                         scaleTransform(std::move(factors)));
  return std::move(*this);
}

Element Element::scale(Reactive::Bindable<Vec2> factors) && {
  detail::ElementModifiers& modifiers = writableModifiers();
  modifiers.transform = composeTransform(std::move(modifiers.transform),
                                         scaleTransform(std::move(factors)));
  return std::move(*this);
}

Element Element::scale(Reactive::Bindable<float> sx, Reactive::Bindable<float> sy) && {
  Reactive::Bindable<Vec2> factors =
      (!sx.isReactive() && !sy.isReactive())
          ? Reactive::Bindable<Vec2>{Vec2{sx.value(), sy.value()}}
          : Reactive::Bindable<Vec2>{[sx = std::move(sx), sy = std::move(sy)] {
              return Vec2{sx.evaluate(), sy.evaluate()};
            }};
  detail::ElementModifiers& modifiers = writableModifiers();
  modifiers.transform = composeTransform(std::move(modifiers.transform),
                                         scaleTransform(std::move(factors)));
  return std::move(*this);
}

Element Element::clipContent(bool clip) && {
  writableModifiers().clip = clip;
  return std::move(*this);
}

Element Element::rasterize() && {
  writableModifiers().rasterize = true;
  return std::move(*this);
}

Element Element::overlay(Element over) && {
  writableModifiers().overlay = std::make_unique<Element>(std::move(over));
  return std::move(*this);
}

Element Element::onTap(std::function<void()> handler, MouseButton button) && {
  if (!handler) {
    writableModifiers().onTap = {};
    return std::move(*this);
  }
  writableModifiers().onTap = [handler = std::move(handler), button](MouseButton eventButton) {
    if (eventButton == button) {
      handler();
    }
  };
  return std::move(*this);
}

Element Element::onTap(std::function<void(MouseButton)> handler) && {
  writableModifiers().onTap = std::move(handler);
  return std::move(*this);
}

Element Element::onTap(std::function<void(MouseButton, Modifiers)> handler) && {
  writableModifiers().onTapWithModifiers = std::move(handler);
  return std::move(*this);
}

Element Element::onPointerEnter(std::function<void()> handler) && {
  writableModifiers().onPointerEnter = std::move(handler);
  return std::move(*this);
}

Element Element::onPointerExit(std::function<void()> handler) && {
  writableModifiers().onPointerExit = std::move(handler);
  return std::move(*this);
}

Element Element::onFocus(std::function<void()> handler) && {
  writableModifiers().onFocus = std::move(handler);
  return std::move(*this);
}

Element Element::onBlur(std::function<void()> handler) && {
  writableModifiers().onBlur = std::move(handler);
  return std::move(*this);
}

Element Element::onPointerDown(std::function<void(Point)> handler, MouseButton button) && {
  if (!handler) {
    writableModifiers().onPointerDown = {};
    return std::move(*this);
  }
  writableModifiers().onPointerDown = [handler = std::move(handler), button](Point point, MouseButton eventButton) {
    if (eventButton == button) {
      handler(point);
    }
  };
  return std::move(*this);
}

Element Element::onPointerDown(std::function<void(Point, MouseButton)> handler) && {
  writableModifiers().onPointerDown = std::move(handler);
  return std::move(*this);
}

Element Element::onPointerUp(std::function<void(Point)> handler, MouseButton button) && {
  if (!handler) {
    writableModifiers().onPointerUp = {};
    return std::move(*this);
  }
  writableModifiers().onPointerUp = [handler = std::move(handler), button](Point point, MouseButton eventButton) {
    if (eventButton == button) {
      handler(point);
    }
  };
  return std::move(*this);
}

Element Element::onPointerUp(std::function<void(Point, MouseButton)> handler) && {
  writableModifiers().onPointerUp = std::move(handler);
  return std::move(*this);
}

Element Element::onPointerMove(std::function<void(Point)> handler) && {
  writableModifiers().onPointerMove = std::move(handler);
  return std::move(*this);
}

Element Element::onScroll(std::function<void(Vec2)> handler) && {
  writableModifiers().onScroll = std::move(handler);
  return std::move(*this);
}

Element Element::onKeyDown(std::function<void(KeyCode, Modifiers)> handler) && {
  writableModifiers().onKeyDown = std::move(handler);
  return std::move(*this);
}

Element Element::onKeyUp(std::function<void(KeyCode, Modifiers)> handler) && {
  writableModifiers().onKeyUp = std::move(handler);
  return std::move(*this);
}

Element Element::onTextInput(std::function<void(std::string const&)> handler) && {
  writableModifiers().onTextInput = std::move(handler);
  return std::move(*this);
}

Element Element::focusable(bool enabled) && {
  writableModifiers().focusable = enabled;
  return std::move(*this);
}

Element Element::focusable(Reactive::Bindable<bool> enabled) && {
  writableModifiers().focusable = std::move(enabled);
  return std::move(*this);
}

Element Element::cursor(Cursor c) && {
  writableModifiers().cursor = c;
  return std::move(*this);
}

Element Element::cursor(Reactive::Bindable<Cursor> c) && {
  writableModifiers().cursor = std::move(c);
  return std::move(*this);
}

Element Element::windowDragRegion(bool enabled) && {
  writableModifiers().windowDragRegion = enabled;
  return std::move(*this);
}

Element Element::windowResizeRegion(WindowResizeEdge edge) && {
  writableModifiers().windowResizeEdge = edge;
  return std::move(*this);
}

} // namespace lambda
