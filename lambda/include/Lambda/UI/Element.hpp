#pragma once

/// \file Lambda/UI/Element.hpp
///
/// Type-erased UI component wrapper: holds any UI component, dispatches `measure`,
/// optional flex overrides, and per-subtree environment values.

#include <Lambda/Graphics/Styles.hpp>
#include <Lambda/Reactive/Bindable.hpp>
#include <Lambda/UI/Component.hpp>
#include <Lambda/UI/Detail/ElementModifiers.hpp>
#include <Lambda/UI/Environment.hpp>
#include <Lambda/UI/EnvironmentBinding.hpp>
#include <Lambda/Layout/LayoutEngine.hpp>
#include <Lambda/UI/Leaves.hpp>
#include <Lambda/UI/MeasureContext.hpp>

#include <cstddef>
#include <functional>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <typeindex>
#include <utility>
#include <vector>

namespace lambdaui {

class Element;
class MountContext;
class TextSystem;
struct Popover;
struct Rectangle;
struct Text;
struct PathShape;
struct Render;
struct VStack;
struct HStack;
struct ZStack;
struct Grid;
struct OffsetView;
struct ScrollView;
struct ScaleAroundCenter;
struct Spacer;
struct PopoverCalloutShape;
namespace views {
struct Image;
} // namespace views
namespace scenegraph {
class SceneNode;
}
namespace detail {
struct EnvironmentOverride {
  virtual ~EnvironmentOverride() = default;
  virtual EnvironmentBinding apply(EnvironmentBinding const& parent) const = 0;
};

template<typename Key>
struct ValueEnvironmentOverride final : EnvironmentOverride {
  using Value = typename EnvironmentKey<Key>::Value;

  explicit ValueEnvironmentOverride(Value valueIn)
      : value(std::move(valueIn)) {}

  EnvironmentBinding apply(EnvironmentBinding const& parent) const override {
    return parent.withValue<Key>(value);
  }

  Value value;
};

template<typename Key>
struct SignalEnvironmentOverride final : EnvironmentOverride {
  using Value = typename EnvironmentKey<Key>::Value;

  explicit SignalEnvironmentOverride(Reactive::Signal<Value> signalIn)
      : signal(std::move(signalIn)) {}

  EnvironmentBinding apply(EnvironmentBinding const& parent) const override {
    return parent.withSignal<Key>(signal);
  }

  Reactive::Signal<Value> signal;
};

struct LayoutOverrides {
  std::optional<float> flexGrow;
  std::optional<float> flexShrink;
  std::optional<float> flexBasis;
  std::optional<float> minMainSize;
  std::optional<std::size_t> colSpan;
  std::optional<std::size_t> rowSpan;
};
} // namespace detail

template<typename>
inline constexpr bool alwaysFalse = false;

class Element {
public:
  template<typename C>
  Element(C component);
  Element(Spacer spacer);

  Element(Element const& other);
  Element& operator=(Element const& other);
  Element(Element&&) noexcept = default;
  Element& operator=(Element&&) noexcept = default;

  Size measure(MeasureContext& ctx, LayoutConstraints const& constraints, LayoutHints const& hints, TextSystem& textSystem) const;
  std::unique_ptr<scenegraph::SceneNode> mount(MountContext& ctx) const;
  [[nodiscard]] bool mountsWhenCollapsed() const;
  [[nodiscard]] detail::ElementModifiers const* modifiers() const noexcept {
    return modifiers_.get();
  }
  template<typename T>
  [[nodiscard]] bool is() const noexcept;

  template<typename T>
  [[nodiscard]] T const& as() const;

  float flexGrow() const;
  float flexShrink() const;
  std::optional<float> flexBasis() const;
  float minMainSize() const;
  std::size_t colSpan() const;
  std::size_t rowSpan() const;

  Element flex(float grow) &&;
  Element flex(float grow, float shrink) &&;
  Element flex(float grow, float shrink, float basis) &&;
  Element minMainSize(float size) &&;
  Element colSpan(std::size_t span) &&;
  Element rowSpan(std::size_t span) &&;
  Element key(std::string key) &&;
  [[nodiscard]] std::optional<std::string> const& explicitKey() const noexcept { return key_; }

  template<typename Key>
  Element environment(typename EnvironmentKey<Key>::Value value) && {
    envOverrides_.push_back(
        std::make_shared<detail::ValueEnvironmentOverride<Key>>(std::move(value)));
    return std::move(*this);
  }

  template<typename Key>
  Element environment(Reactive::Signal<typename EnvironmentKey<Key>::Value> signal) && {
    envOverrides_.push_back(
        std::make_shared<detail::SignalEnvironmentOverride<Key>>(std::move(signal)));
    return std::move(*this);
  }

  Element padding(Reactive::Bindable<float> all) &&;
  Element padding(Reactive::Bindable<EdgeInsets> insets) &&;
  Element padding(Reactive::Bindable<float> top, Reactive::Bindable<float> right,
                  Reactive::Bindable<float> bottom, Reactive::Bindable<float> left) &&;
  Element fill(Reactive::Bindable<FillStyle> style) &&;
  Element fill(Reactive::Bindable<Color> color) &&;
  Element shadow(Reactive::Bindable<ShadowStyle> style) &&;
  Element size(Reactive::Bindable<float> width, Reactive::Bindable<float> height) &&;
  Element width(Reactive::Bindable<float> w) &&;
  Element height(Reactive::Bindable<float> h) &&;
  Element stroke(Reactive::Bindable<StrokeStyle> style) &&;
  Element stroke(Reactive::Bindable<Color> c, Reactive::Bindable<float> width) &&;
  Element cornerRadius(Reactive::Bindable<CornerRadius> radius) &&;
  Element cornerRadius(Reactive::Bindable<float> radius) &&;
  Element opacity(Reactive::Bindable<float> opacity) &&;
  Element position(Reactive::Bindable<Vec2> p) &&;
  Element position(Reactive::Bindable<float> x, Reactive::Bindable<float> y) &&;
  Element translate(Reactive::Bindable<Vec2> delta) &&;
  Element translate(Reactive::Bindable<float> dx, Reactive::Bindable<float> dy) &&;
  Element rotate(Reactive::Bindable<float> radians) &&;
  Element scale(Reactive::Bindable<float> factor) &&;
  Element scale(Reactive::Bindable<Vec2> factors) &&;
  Element scale(Reactive::Bindable<float> sx, Reactive::Bindable<float> sy) &&;
  Element clipContent(bool clip) &&;
  /// Renders this subtree into an offscreen texture and reuses that texture until the subtree,
  /// bounds, or DPI scale changes.
  Element rasterize() &&;
  template <typename T>
  Element rasterizeInvalidateOn(Reactive::Bindable<T> binding) && {
    detail::ElementModifiers& modifiers = writableModifiers();
    modifiers.rasterize = true;
    if (binding.isReactive()) {
      modifiers.rasterizeInvalidators.emplace_back([binding = std::move(binding)] mutable {
        (void)binding.evaluate();
      });
    }
    return std::move(*this);
  }
  Element overlay(Element over) &&;

  Element onTap(std::function<void()> handler, MouseButton button = MouseButton::Left) &&;
  Element onTap(std::function<void(MouseButton)> handler) &&;
  Element onTap(std::function<void(MouseButton, Modifiers)> handler) &&;
  Element onPointerEnter(std::function<void()> handler) &&;
  Element onPointerExit(std::function<void()> handler) &&;
  Element onFocus(std::function<void()> handler) &&;
  Element onBlur(std::function<void()> handler) &&;
  Element onPointerDown(std::function<void(Point)> handler, MouseButton button = MouseButton::Left) &&;
  Element onPointerDown(std::function<void(Point, MouseButton)> handler) &&;
  Element onPointerUp(std::function<void(Point)> handler, MouseButton button = MouseButton::Left) &&;
  Element onPointerUp(std::function<void(Point, MouseButton)> handler) &&;
  Element onPointerMove(std::function<void(Point)> handler) &&;
  Element onScroll(std::function<void(Vec2)> handler) &&;
  Element onKeyDown(std::function<void(KeyCode, Modifiers)> handler) &&;
  Element onKeyUp(std::function<void(KeyCode, Modifiers)> handler) &&;
  Element onTextInput(std::function<void(std::string const&)> handler) &&;
  Element focusable(bool enabled) &&;
  Element focusable(Reactive::Bindable<bool> enabled) &&;
  Element cursor(Cursor c) &&;
  Element cursor(Reactive::Bindable<Cursor> c) &&;
  Element windowDragRegion(bool enabled = true) &&;
  Element windowResizeRegion(WindowResizeEdge edge) &&;

private:
  friend Popover* detail::popoverOverlayStateIf(Element& el);

  struct Concept {
    virtual ~Concept() = default;
    virtual std::type_index modelType() const noexcept = 0;
    virtual void const* rawValuePtr() const noexcept = 0;
    virtual Size measure(MeasureContext& ctx, LayoutConstraints const& constraints,
                         LayoutHints const& hints, TextSystem& textSystem) const = 0;
    virtual std::unique_ptr<scenegraph::SceneNode> mount(MountContext& ctx) const = 0;
  };

  template<typename C>
  struct Model;

  std::shared_ptr<Concept> impl_;
  bool mountsWhenCollapsed_ = false;
  std::vector<std::shared_ptr<detail::EnvironmentOverride const>> envOverrides_;
  std::shared_ptr<detail::ElementModifiers> modifiers_;
  std::optional<std::string> key_{};
  std::unique_ptr<detail::LayoutOverrides> overrides_;

  detail::ElementModifiers& writableModifiers();
  detail::LayoutOverrides& writableOverrides();
  Size measureWithModifiersImpl(MeasureContext& ctx, LayoutConstraints const& constraints,
                                LayoutHints const& hints, TextSystem& textSystem) const;
};

template<typename... Args>
std::vector<Element> children(Args&&... args);

} // namespace lambdaui

#include <Lambda/UI/Detail/ElementTemplates.hpp>
#include <Lambda/UI/Detail/ViewModifierInlines.hpp>
