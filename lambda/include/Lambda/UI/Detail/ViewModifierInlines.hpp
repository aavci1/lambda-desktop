#pragma once

#include <Lambda/UI/ViewModifiers.hpp>

namespace lambda {

template<typename Derived>
Element ViewModifiers<Derived>::padding(Reactive::Bindable<float> all) && {
  return Element{std::move(static_cast<Derived&>(*this))}.padding(std::move(all));
}

template<typename Derived>
Element ViewModifiers<Derived>::padding(Reactive::Bindable<EdgeInsets> insets) && {
  return Element{std::move(static_cast<Derived&>(*this))}.padding(std::move(insets));
}

template<typename Derived>
Element ViewModifiers<Derived>::padding(Reactive::Bindable<float> top,
                                        Reactive::Bindable<float> right,
                                        Reactive::Bindable<float> bottom,
                                        Reactive::Bindable<float> left) && {
  return Element{std::move(static_cast<Derived&>(*this))}
      .padding(std::move(top), std::move(right), std::move(bottom), std::move(left));
}

template<typename Derived>
Element ViewModifiers<Derived>::fill(Reactive::Bindable<FillStyle> style) && {
  return Element{std::move(static_cast<Derived&>(*this))}.fill(std::move(style));
}

template<typename Derived>
Element ViewModifiers<Derived>::fill(Reactive::Bindable<Color> color) && {
  return Element{std::move(static_cast<Derived&>(*this))}.fill(std::move(color));
}

template<typename Derived>
Element ViewModifiers<Derived>::shadow(Reactive::Bindable<ShadowStyle> style) && {
  return Element{std::move(static_cast<Derived&>(*this))}.shadow(std::move(style));
}

template<typename Derived>
Element ViewModifiers<Derived>::size(Reactive::Bindable<float> width,
                                     Reactive::Bindable<float> height) && {
  return Element{std::move(static_cast<Derived&>(*this))}.size(std::move(width), std::move(height));
}

template<typename Derived>
Element ViewModifiers<Derived>::width(Reactive::Bindable<float> w) && {
  return Element{std::move(static_cast<Derived&>(*this))}.width(std::move(w));
}

template<typename Derived>
Element ViewModifiers<Derived>::height(Reactive::Bindable<float> h) && {
  return Element{std::move(static_cast<Derived&>(*this))}.height(std::move(h));
}

template<typename Derived>
Element ViewModifiers<Derived>::stroke(Reactive::Bindable<StrokeStyle> style) && {
  return Element{std::move(static_cast<Derived&>(*this))}.stroke(std::move(style));
}

template<typename Derived>
Element ViewModifiers<Derived>::stroke(Reactive::Bindable<Color> color,
                                       Reactive::Bindable<float> width) && {
  return Element{std::move(static_cast<Derived&>(*this))}
      .stroke(std::move(color), std::move(width));
}

template<typename Derived>
Element ViewModifiers<Derived>::cornerRadius(Reactive::Bindable<CornerRadius> radius) && {
  return Element{std::move(static_cast<Derived&>(*this))}.cornerRadius(std::move(radius));
}

template<typename Derived>
Element ViewModifiers<Derived>::cornerRadius(Reactive::Bindable<float> radius) && {
  return Element{std::move(static_cast<Derived&>(*this))}.cornerRadius(std::move(radius));
}

template<typename Derived>
Element ViewModifiers<Derived>::opacity(Reactive::Bindable<float> o) && {
  return Element{std::move(static_cast<Derived&>(*this))}.opacity(std::move(o));
}

template<typename Derived>
Element ViewModifiers<Derived>::position(Reactive::Bindable<Vec2> p) && {
  return Element{std::move(static_cast<Derived&>(*this))}.position(std::move(p));
}

template<typename Derived>
Element ViewModifiers<Derived>::position(Reactive::Bindable<float> x,
                                         Reactive::Bindable<float> y) && {
  return Element{std::move(static_cast<Derived&>(*this))}.position(std::move(x), std::move(y));
}

template<typename Derived>
Element ViewModifiers<Derived>::translate(Reactive::Bindable<Vec2> delta) && {
  return Element{std::move(static_cast<Derived&>(*this))}.translate(std::move(delta));
}

template<typename Derived>
Element ViewModifiers<Derived>::translate(Reactive::Bindable<float> dx,
                                          Reactive::Bindable<float> dy) && {
  return Element{std::move(static_cast<Derived&>(*this))}.translate(std::move(dx), std::move(dy));
}

template<typename Derived>
Element ViewModifiers<Derived>::rotate(Reactive::Bindable<float> radians) && {
  return Element{std::move(static_cast<Derived&>(*this))}.rotate(std::move(radians));
}

template<typename Derived>
Element ViewModifiers<Derived>::scale(Reactive::Bindable<float> factor) && {
  return Element{std::move(static_cast<Derived&>(*this))}.scale(std::move(factor));
}

template<typename Derived>
Element ViewModifiers<Derived>::scale(Reactive::Bindable<Vec2> factors) && {
  return Element{std::move(static_cast<Derived&>(*this))}.scale(std::move(factors));
}

template<typename Derived>
Element ViewModifiers<Derived>::scale(Reactive::Bindable<float> sx,
                                      Reactive::Bindable<float> sy) && {
  return Element{std::move(static_cast<Derived&>(*this))}.scale(std::move(sx), std::move(sy));
}

template<typename Derived>
Element ViewModifiers<Derived>::clipContent(bool clip) && {
  return Element{std::move(static_cast<Derived&>(*this))}.clipContent(clip);
}

template<typename Derived>
Element ViewModifiers<Derived>::rasterize() && {
  return Element{std::move(static_cast<Derived&>(*this))}.rasterize();
}

template<typename Derived>
template<typename T>
Element ViewModifiers<Derived>::rasterizeInvalidateOn(Reactive::Bindable<T> binding) && {
  return Element{std::move(static_cast<Derived&>(*this))}
      .rasterizeInvalidateOn(std::move(binding));
}

template<typename Derived>
Element ViewModifiers<Derived>::overlay(Element over) && {
  return Element{std::move(static_cast<Derived&>(*this))}.overlay(std::move(over));
}

template<typename Derived>
Element ViewModifiers<Derived>::key(std::string key) && {
  return Element{std::move(static_cast<Derived&>(*this))}.key(std::move(key));
}

template<typename Derived>
Element ViewModifiers<Derived>::onTap(std::function<void()> handler, MouseButton button) && {
  return Element{std::move(static_cast<Derived&>(*this))}.onTap(std::move(handler), button);
}

template<typename Derived>
Element ViewModifiers<Derived>::onTap(std::function<void(MouseButton)> handler) && {
  return Element{std::move(static_cast<Derived&>(*this))}.onTap(std::move(handler));
}

template<typename Derived>
Element ViewModifiers<Derived>::onTap(std::function<void(MouseButton, Modifiers)> handler) && {
  return Element{std::move(static_cast<Derived&>(*this))}.onTap(std::move(handler));
}

template<typename Derived>
Element ViewModifiers<Derived>::onPointerEnter(std::function<void()> handler) && {
  return Element{std::move(static_cast<Derived&>(*this))}.onPointerEnter(std::move(handler));
}

template<typename Derived>
Element ViewModifiers<Derived>::onPointerExit(std::function<void()> handler) && {
  return Element{std::move(static_cast<Derived&>(*this))}.onPointerExit(std::move(handler));
}

template<typename Derived>
Element ViewModifiers<Derived>::onFocus(std::function<void()> handler) && {
  return Element{std::move(static_cast<Derived&>(*this))}.onFocus(std::move(handler));
}

template<typename Derived>
Element ViewModifiers<Derived>::onBlur(std::function<void()> handler) && {
  return Element{std::move(static_cast<Derived&>(*this))}.onBlur(std::move(handler));
}

template<typename Derived>
Element ViewModifiers<Derived>::onPointerDown(std::function<void(Point)> handler, MouseButton button) && {
  return Element{std::move(static_cast<Derived&>(*this))}.onPointerDown(std::move(handler), button);
}

template<typename Derived>
Element ViewModifiers<Derived>::onPointerDown(std::function<void(Point, MouseButton)> handler) && {
  return Element{std::move(static_cast<Derived&>(*this))}.onPointerDown(std::move(handler));
}

template<typename Derived>
Element ViewModifiers<Derived>::onPointerUp(std::function<void(Point)> handler, MouseButton button) && {
  return Element{std::move(static_cast<Derived&>(*this))}.onPointerUp(std::move(handler), button);
}

template<typename Derived>
Element ViewModifiers<Derived>::onPointerUp(std::function<void(Point, MouseButton)> handler) && {
  return Element{std::move(static_cast<Derived&>(*this))}.onPointerUp(std::move(handler));
}

template<typename Derived>
Element ViewModifiers<Derived>::onPointerMove(std::function<void(Point)> handler) && {
  return Element{std::move(static_cast<Derived&>(*this))}.onPointerMove(std::move(handler));
}

template<typename Derived>
Element ViewModifiers<Derived>::onScroll(std::function<void(Vec2)> handler) && {
  return Element{std::move(static_cast<Derived&>(*this))}.onScroll(std::move(handler));
}

template<typename Derived>
Element ViewModifiers<Derived>::onKeyDown(std::function<void(KeyCode, Modifiers)> handler) && {
  return Element{std::move(static_cast<Derived&>(*this))}.onKeyDown(std::move(handler));
}

template<typename Derived>
Element ViewModifiers<Derived>::onKeyUp(std::function<void(KeyCode, Modifiers)> handler) && {
  return Element{std::move(static_cast<Derived&>(*this))}.onKeyUp(std::move(handler));
}

template<typename Derived>
Element ViewModifiers<Derived>::onTextInput(std::function<void(std::string const&)> handler) && {
  return Element{std::move(static_cast<Derived&>(*this))}.onTextInput(std::move(handler));
}

template<typename Derived>
Element ViewModifiers<Derived>::focusable(bool enabled) && {
  return Element{std::move(static_cast<Derived&>(*this))}.focusable(enabled);
}

template<typename Derived>
Element ViewModifiers<Derived>::focusable(Reactive::Bindable<bool> enabled) && {
  return Element{std::move(static_cast<Derived&>(*this))}.focusable(std::move(enabled));
}

template<typename Derived>
Element ViewModifiers<Derived>::cursor(Cursor c) && {
  return Element{std::move(static_cast<Derived&>(*this))}.cursor(c);
}

template<typename Derived>
Element ViewModifiers<Derived>::cursor(Reactive::Bindable<Cursor> c) && {
  return Element{std::move(static_cast<Derived&>(*this))}.cursor(std::move(c));
}

template<typename Derived>
Element ViewModifiers<Derived>::windowDragRegion(bool enabled) && {
  return Element{std::move(static_cast<Derived&>(*this))}.windowDragRegion(enabled);
}

template<typename Derived>
Element ViewModifiers<Derived>::windowResizeRegion(WindowResizeEdge edge) && {
  return Element{std::move(static_cast<Derived&>(*this))}.windowResizeRegion(edge);
}

template<typename Derived>
Element ViewModifiers<Derived>::flex(float grow) && {
  return Element{std::move(static_cast<Derived&>(*this))}.flex(grow);
}

template<typename Derived>
Element ViewModifiers<Derived>::flex(float grow, float shrink) && {
  return Element{std::move(static_cast<Derived&>(*this))}.flex(grow, shrink);
}

template<typename Derived>
Element ViewModifiers<Derived>::flex(float grow, float shrink, float basis) && {
  return Element{std::move(static_cast<Derived&>(*this))}.flex(grow, shrink, basis);
}

template<typename Derived>
Element ViewModifiers<Derived>::minMainSize(float size) && {
  return Element{std::move(static_cast<Derived&>(*this))}.minMainSize(size);
}

template<typename Derived>
Element ViewModifiers<Derived>::colSpan(std::size_t span) && {
  return Element{std::move(static_cast<Derived&>(*this))}.colSpan(span);
}

template<typename Derived>
Element ViewModifiers<Derived>::rowSpan(std::size_t span) && {
  return Element{std::move(static_cast<Derived&>(*this))}.rowSpan(span);
}

template<typename Derived>
template<typename Key>
Element ViewModifiers<Derived>::environment(typename EnvironmentKey<Key>::Value value) && {
  return Element{std::move(static_cast<Derived&>(*this))}
      .template environment<Key>(std::move(value));
}

template<typename Derived>
template<typename Key>
Element ViewModifiers<Derived>::environment(
    Reactive::Signal<typename EnvironmentKey<Key>::Value> signal) && {
  return Element{std::move(static_cast<Derived&>(*this))}
      .template environment<Key>(std::move(signal));
}

} // namespace lambda
