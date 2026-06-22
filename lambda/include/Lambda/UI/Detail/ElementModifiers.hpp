#pragma once

/// \file Lambda/UI/Detail/ElementModifiers.hpp
///
/// Internal retained-UI state used by `Element` measurement/build plumbing.

#include <Lambda/UI/Cursor.hpp>
#include <Lambda/UI/Input.hpp>
#include <Lambda/UI/WindowChrome.hpp>
#include <Lambda/Core/Identity.hpp>
#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Detail/SmallVector.hpp>
#include <Lambda/Graphics/Styles.hpp>
#include <Lambda/Reactive/Bindable.hpp>
#include <Lambda/UI/Environment.hpp>
#include <Lambda/Layout/LayoutEngine.hpp>

#include <cmath>
#include <functional>
#include <memory>
#include <vector>

namespace lambda {

class Element;
struct Popover;

namespace detail {

struct ElementDeleter {
  void operator()(Element* element) const noexcept;
};

using OwnedElementPtr = std::unique_ptr<Element, ElementDeleter>;

Popover* popoverOverlayStateIf(Element& el);

template<typename C>
bool mountsWhenCollapsedOf() {
  if constexpr (requires { C::mountsWhenCollapsed; }) {
    return C::mountsWhenCollapsed;
  } else {
    return false;
  }
}

struct ElementModifiers {
  Reactive::Bindable<EdgeInsets> padding{EdgeInsets{}};
  Reactive::Bindable<FillStyle> fill{FillStyle::none()};
  Reactive::Bindable<StrokeStyle> stroke{StrokeStyle::none()};
  Reactive::Bindable<ShadowStyle> shadow{ShadowStyle::none()};
  Reactive::Bindable<CornerRadius> cornerRadius{CornerRadius{}};
  Reactive::Bindable<float> opacity{1.f};
  Reactive::Bindable<Mat3> transform{Mat3::identity()};
  bool clip = false;
  Reactive::Bindable<float> positionX{0.f};
  Reactive::Bindable<float> positionY{0.f};
  Reactive::Bindable<float> sizeWidth{0.f};
  Reactive::Bindable<float> sizeHeight{0.f};
  bool hasSizeWidth = false;
  bool hasSizeHeight = false;
  bool rasterize = false;
  std::vector<Reactive::BindingFn> rasterizeInvalidators;
  std::unique_ptr<Element> overlay;

  std::function<void(MouseButton)> onTap;
  std::function<void()> onPointerEnter;
  std::function<void()> onPointerExit;
  std::function<void()> onFocus;
  std::function<void()> onBlur;
  std::function<void(Point, MouseButton)> onPointerDown;
  std::function<void(Point, MouseButton)> onPointerUp;
  std::function<void(Point)> onPointerMove;
  std::function<void(Vec2)> onScroll;
  std::function<void(KeyCode, Modifiers)> onKeyDown;
  std::function<void(KeyCode, Modifiers)> onKeyUp;
  std::function<void(std::string const&)> onTextInput;
  std::function<void(MouseButton, Modifiers)> onTapWithModifiers;
  Reactive::Bindable<bool> focusable{false};
  Reactive::Bindable<Cursor> cursor{Cursor::Inherit};
  bool windowDragRegion = false;
  WindowResizeEdge windowResizeEdge = WindowResizeEdge::None;

  bool hasInteraction() const noexcept {
    return static_cast<bool>(onTap) || static_cast<bool>(onPointerEnter) ||
           static_cast<bool>(onPointerExit) || static_cast<bool>(onFocus) ||
           static_cast<bool>(onBlur) || static_cast<bool>(onPointerDown) ||
           static_cast<bool>(onPointerUp) || static_cast<bool>(onPointerMove) ||
           static_cast<bool>(onScroll) || static_cast<bool>(onKeyDown) ||
           static_cast<bool>(onKeyUp) || static_cast<bool>(onTextInput) ||
           static_cast<bool>(onTapWithModifiers) ||
           focusable.isReactive() || focusable.evaluate() ||
           cursor.isReactive() || cursor.evaluate() != Cursor::Inherit ||
           windowDragRegion || windowResizeEdge != WindowResizeEdge::None;
  }

  bool needsModifierPass() const {
    auto const p = padding.evaluate();
    auto const f = fill.evaluate();
    auto const s = stroke.evaluate();
    auto const sh = shadow.evaluate();
    auto const cr = cornerRadius.evaluate();
    auto const op = opacity.evaluate();
    auto const tx = transform.evaluate();
    auto const px = positionX.evaluate();
    auto const py = positionY.evaluate();
    return !p.isZero() || !f.isNone() || !s.isNone() || !sh.isNone() ||
           !cr.isZero() || op < 1.f - 1e-6f || transform.isReactive() ||
           tx != Mat3::identity() ||
           clip || std::fabs(px) > 1e-6f || std::fabs(py) > 1e-6f ||
           hasInteraction() || hasSizeWidth || hasSizeHeight || rasterize ||
           overlay != nullptr;
  }

  ElementModifiers() = default;
  ElementModifiers(ElementModifiers const& o);
  ElementModifiers& operator=(ElementModifiers const& o);
  ElementModifiers(ElementModifiers&&) noexcept = default;
  ElementModifiers& operator=(ElementModifiers&&) noexcept = default;
  ~ElementModifiers();
};

} // namespace detail
} // namespace lambda
