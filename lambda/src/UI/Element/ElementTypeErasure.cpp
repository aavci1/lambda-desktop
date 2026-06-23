#include <Lambda/UI/Element.hpp>

#include <Lambda/Reactive/Effect.hpp>
#include <Lambda/UI/InteractionData.hpp>
#include <Lambda/SceneGraph/RasterCacheNode.hpp>
#include <Lambda/SceneGraph/RectNode.hpp>
#include <Lambda/SceneGraph/SceneNode.hpp>
#include <Lambda/UI/Hooks.hpp>
#include <Lambda/UI/MountContext.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/Detail/LayoutDebugDump.hpp>
#include <Lambda/UI/Views/Spacer.hpp>

#include "UI/Element/ModifierLayoutHelpers.hpp"

#include <algorithm>
#include <cassert>
#include <concepts>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

namespace lambdaui {

namespace {

float positive(float value) {
  return std::isfinite(value) ? std::max(0.f, value) : 0.f;
}

LayoutConstraints insetConstraints(LayoutConstraints constraints, EdgeInsets padding) {
  float const dx = std::max(0.f, padding.left) + std::max(0.f, padding.right);
  float const dy = std::max(0.f, padding.top) + std::max(0.f, padding.bottom);
  if (std::isfinite(constraints.maxWidth)) {
    constraints.maxWidth = std::max(0.f, constraints.maxWidth - dx);
  }
  if (std::isfinite(constraints.maxHeight)) {
    constraints.maxHeight = std::max(0.f, constraints.maxHeight - dy);
  }
  constraints.minWidth = std::max(0.f, constraints.minWidth - dx);
  constraints.minHeight = std::max(0.f, constraints.minHeight - dy);
  if (std::isfinite(constraints.maxWidth)) {
    constraints.minWidth = std::min(constraints.minWidth, constraints.maxWidth);
  }
  if (std::isfinite(constraints.maxHeight)) {
    constraints.minHeight = std::min(constraints.minHeight, constraints.maxHeight);
  }
  return constraints;
}

LayoutConstraints modifierInnerConstraints(LayoutConstraints constraints, EdgeInsets padding,
                                           LayoutHints const& hints, float width, float height,
                                           bool hasWidth, bool hasHeight) {
  float const dx = std::max(0.f, padding.left) + std::max(0.f, padding.right);
  float const dy = std::max(0.f, padding.top) + std::max(0.f, padding.bottom);
  float const resolvedWidth = detail::resolvedModifierWidth(constraints, hints, width);
  float const resolvedHeight = detail::resolvedModifierHeight(constraints, hints, height);
  bool const hasResolvedWidth = detail::hasResolvedModifierWidth(constraints, hints, hasWidth);
  bool const hasResolvedHeight = detail::hasResolvedModifierHeight(constraints, hints, hasHeight);
  constraints = insetConstraints(constraints, padding);
  if (hasResolvedWidth) {
    float const innerWidth = std::max(0.f, resolvedWidth - dx);
    constraints.maxWidth = innerWidth;
    constraints.minWidth = innerWidth;
  }
  if (hasResolvedHeight) {
    float const innerHeight = std::max(0.f, resolvedHeight - dy);
    constraints.maxHeight = innerHeight;
    constraints.minHeight = innerHeight;
  }
  if (std::isfinite(constraints.maxWidth)) {
    constraints.minWidth = std::min(constraints.minWidth, constraints.maxWidth);
  }
  if (std::isfinite(constraints.maxHeight)) {
    constraints.minHeight = std::min(constraints.minHeight, constraints.maxHeight);
  }
  return constraints;
}

Size resolveModifierOuterSize(Size size, LayoutConstraints const& constraints,
                              LayoutHints const& hints, float width, float height,
                              bool hasWidth, bool hasHeight) {
  float const resolvedWidth = detail::resolvedModifierWidth(constraints, hints, width);
  float const resolvedHeight = detail::resolvedModifierHeight(constraints, hints, height);
  bool const hasResolvedWidth = detail::hasResolvedModifierWidth(constraints, hints, hasWidth);
  bool const hasResolvedHeight = detail::hasResolvedModifierHeight(constraints, hints, hasHeight);
  if (hasResolvedWidth) {
    size.width = resolvedWidth;
  }
  if (hasResolvedHeight) {
    size.height = resolvedHeight;
  }
  return Size{positive(size.width), positive(size.height)};
}

LayoutConstraints fixedConstraints(Size size) {
  return LayoutConstraints{
      .maxWidth = std::max(0.f, size.width),
      .maxHeight = std::max(0.f, size.height),
      .minWidth = std::max(0.f, size.width),
      .minHeight = std::max(0.f, size.height),
  };
}

Theme activeTheme(EnvironmentBinding const& environment) {
  return environment.value<ThemeKey>();
}

std::unique_ptr<detail::LayoutOverrides> cloneLayoutOverrides(detail::LayoutOverrides const* overrides) {
  if (!overrides) {
    return nullptr;
  }
  return std::make_unique<detail::LayoutOverrides>(*overrides);
}

template <typename Gradient>
Gradient resolveGradientStops(Gradient gradient, Theme const& theme) {
  for (std::uint8_t i = 0; i < gradient.stopCount; ++i) {
    gradient.stops[i].color = resolveColor(gradient.stops[i].color, theme);
  }
  return gradient;
}

FillStyle resolveFillStyle(FillStyle style, Theme const& theme) {
  Color color{};
  if (style.solidColor(&color)) {
    style.data = resolveColor(color, theme);
    return style;
  }
  LinearGradient gradient{};
  if (style.linearGradient(&gradient)) {
    style.data = resolveGradientStops(gradient, theme);
    return style;
  }
  RadialGradient radial{};
  if (style.radialGradient(&radial)) {
    style.data = resolveGradientStops(radial, theme);
    return style;
  }
  ConicalGradient conical{};
  if (style.conicalGradient(&conical)) {
    style.data = resolveGradientStops(conical, theme);
  }
  return style;
}

StrokeStyle resolveStrokeStyle(StrokeStyle style, Theme const& theme) {
  if (style.type == StrokeStyle::Type::Solid) {
    style.color = resolveColor(style.color, theme);
  }
  return style;
}

ShadowStyle resolveShadowStyle(ShadowStyle style, Theme const& theme) {
  style.color = resolveColor(style.color, theme);
  return style;
}

void relayoutStoredAncestors(scenegraph::SceneNode& node) {
  constexpr float epsilon = 0.01f;
  // Trees deeper than 64 stored scene-graph ancestors are not supported for reactive
  // relayout propagation. This bounds the synchronous relayout walk; current demos are
  // far shallower than this retained depth guard.
  scenegraph::SceneNode* current = &node;
  for (int depth = 0; depth < 64; ++depth) {
    scenegraph::SceneNode* parent = current->parent();
    if (!parent) {
      layoutDebugDumpAttached("runtime-relayout");
      return;
    }
    Size const oldSize = parent->size();
    if (!parent->relayoutStoredConstraints()) {
      layoutDebugDumpAttached("runtime-relayout");
      return;
    }
    Size const newSize = parent->size();
    if (std::abs(newSize.width - oldSize.width) <= epsilon &&
        std::abs(newSize.height - oldSize.height) <= epsilon) {
      layoutDebugDumpAttached("runtime-relayout");
      return;
    }
    current = parent;
  }
  assert(false && "reactive relayout ancestor walk exceeded the 64-level depth cap");
}

struct BindingClosureSizeProbeSetter {
  scenegraph::RectNode* node = nullptr;
  EnvironmentBinding environment;

  void operator()(Color) const {}
};

struct BindingClosureSizeProbe {
  Reactive::Bindable<Color> binding;
  BindingClosureSizeProbeSetter setter;
  Reactive::SmallFn<void()> requestRedraw;
  std::optional<Color> lastValue;
  bool compareEvaluatedValue = true;

  void operator()() {}
};

static_assert(sizeof(BindingClosureSizeProbe) <= Reactive::BindingFn::inlineCapacity,
              "Binding closure exceeded BindingFn inline budget; bump the SBO size");

template<typename T, typename Setter>
void installBinding(MountContext& ctx, Reactive::Bindable<T> binding, Setter setter,
                    bool compareEvaluatedValue = true) {
  if (!binding.isReactive()) {
    setter(binding.evaluate());
    return;
  }

  Reactive::SmallFn<void()> requestRedraw = ctx.redrawCallback();
  Reactive::withOwner(ctx.owner(), [&] {
    Reactive::Effect([binding = std::move(binding), setter = std::move(setter),
                      requestRedraw = std::move(requestRedraw),
                      lastValue = std::optional<T>{},
                      compareEvaluatedValue]() mutable {
      T value = binding.evaluate();
      if constexpr (std::equality_comparable<T>) {
        if (compareEvaluatedValue && lastValue && *lastValue == value) {
          return;
        }
        lastValue = value;
      }
      setter(std::move(value));
      if (requestRedraw) {
        requestRedraw();
      }
    });
  });
}

Size measuredOuterSize(Element const& element, MountContext& ctx) {
  ctx.measureContext().pushConstraints(ctx.constraints(), ctx.hints());
  Size size = element.measure(ctx.measureContext(), ctx.constraints(), ctx.hints(), ctx.textSystem());
  ctx.measureContext().popConstraints();
  return Size{positive(size.width), positive(size.height)};
}

} // namespace

namespace detail {

void ElementDeleter::operator()(Element* element) const noexcept {
  delete element;
}

Popover* popoverOverlayStateIf(Element& el) {
  (void)el;
  return nullptr;
}

} // namespace detail

Element::Element(Element const& other)
    : impl_(other.impl_)
    , mountsWhenCollapsed_(other.mountsWhenCollapsed_)
    , envOverrides_(other.envOverrides_)
    , modifiers_(other.modifiers_)
    , key_(other.key_)
    , overrides_(cloneLayoutOverrides(other.overrides_.get()))
{}

Element& Element::operator=(Element const& other) {
  if (this == &other) {
    return *this;
  }
  impl_ = other.impl_;
  mountsWhenCollapsed_ = other.mountsWhenCollapsed_;
  envOverrides_ = other.envOverrides_;
  modifiers_ = other.modifiers_;
  key_ = other.key_;
  overrides_ = cloneLayoutOverrides(other.overrides_.get());
  return *this;
}

Element::Element(Spacer spacer)
    : Element(spacer.body()) {}

detail::ElementModifiers& Element::writableModifiers() {
  if (!modifiers_) {
    modifiers_ = std::make_shared<detail::ElementModifiers>();
  } else if (modifiers_.use_count() != 1) {
    modifiers_ = std::make_shared<detail::ElementModifiers>(*modifiers_);
  }
  return *modifiers_;
}

detail::LayoutOverrides& Element::writableOverrides() {
  if (!overrides_) {
    overrides_ = std::make_unique<detail::LayoutOverrides>();
  }
  return *overrides_;
}

float Element::flexGrow() const {
  if (overrides_ && overrides_->flexGrow) {
    return *overrides_->flexGrow;
  }
  return 0.f;
}

float Element::flexShrink() const {
  if (overrides_ && overrides_->flexShrink) {
    return *overrides_->flexShrink;
  }
  return 0.f;
}

std::optional<float> Element::flexBasis() const {
  return overrides_ ? overrides_->flexBasis : std::nullopt;
}

bool Element::mountsWhenCollapsed() const {
  return mountsWhenCollapsed_;
}

float Element::minMainSize() const {
  if (overrides_ && overrides_->minMainSize) {
    return *overrides_->minMainSize;
  }
  return 0.f;
}

std::size_t Element::colSpan() const {
  if (overrides_ && overrides_->colSpan) {
    return *overrides_->colSpan;
  }
  return 1u;
}

std::size_t Element::rowSpan() const {
  if (overrides_ && overrides_->rowSpan) {
    return *overrides_->rowSpan;
  }
  return 1u;
}

Element Element::flex(float grow) && {
  detail::LayoutOverrides& overrides = writableOverrides();
  overrides.flexGrow = grow;
  overrides.flexShrink = 1.f;
  overrides.flexBasis.reset();
  overrides.minMainSize.reset();
  return std::move(*this);
}

Element Element::flex(float grow, float shrink) && {
  detail::LayoutOverrides& overrides = writableOverrides();
  overrides.flexGrow = grow;
  overrides.flexShrink = shrink;
  overrides.flexBasis.reset();
  overrides.minMainSize.reset();
  return std::move(*this);
}

Element Element::flex(float grow, float shrink, float basis) && {
  detail::LayoutOverrides& overrides = writableOverrides();
  overrides.flexGrow = grow;
  overrides.flexShrink = shrink;
  overrides.flexBasis = std::max(0.f, basis);
  overrides.minMainSize.reset();
  return std::move(*this);
}

Element Element::minMainSize(float size) && {
  writableOverrides().minMainSize = std::max(0.f, size);
  return std::move(*this);
}

Element Element::colSpan(std::size_t span) && {
  writableOverrides().colSpan = std::max<std::size_t>(1, span);
  return std::move(*this);
}

Element Element::rowSpan(std::size_t span) && {
  writableOverrides().rowSpan = std::max<std::size_t>(1, span);
  return std::move(*this);
}

Element Element::key(std::string key) && {
  key_ = std::move(key);
  return std::move(*this);
}

std::unique_ptr<scenegraph::SceneNode> Element::mount(MountContext& ctx) const {
  std::unique_ptr<MountContext> scopedEnvironmentContext;
  if (!envOverrides_.empty()) {
    EnvironmentBinding activeEnvironment = ctx.environmentBinding();
    for (auto const& override : envOverrides_) {
      activeEnvironment = override->apply(activeEnvironment);
    }
    scopedEnvironmentContext = std::make_unique<MountContext>(
        ctx.owner(), ctx.textSystem(), ctx.measureContext(),
        ctx.constraints(), ctx.hints(), ctx.redrawCallback(),
        std::move(activeEnvironment));
  }
  MountContext& activeCtx = scopedEnvironmentContext ? *scopedEnvironmentContext : ctx;
  detail::CurrentMountContextScope const currentMountContext{activeCtx};

  if (!modifiers_ || !modifiers_->needsModifierPass()) {
    return impl_->mount(activeCtx);
  }

  detail::ElementModifiers const& modifiers = *modifiers_;
  EdgeInsets const padding = modifiers.padding.evaluate();
  float const width = modifiers.sizeWidth.evaluate();
  float const height = modifiers.sizeHeight.evaluate();
  LayoutConstraints innerConstraints =
      modifierInnerConstraints(activeCtx.constraints(), padding, activeCtx.hints(), width, height,
                               modifiers.hasSizeWidth, modifiers.hasSizeHeight);
  MountContext innerCtx = activeCtx.childWithSharedScope(innerConstraints, activeCtx.hints());
  std::unique_ptr<scenegraph::SceneNode> content = impl_->mount(innerCtx);
  if (!content) {
    return nullptr;
  }

  Size outerSize = resolveModifierOuterSize(
      measuredOuterSize(*this, activeCtx), activeCtx.constraints(), activeCtx.hints(), width, height,
      modifiers.hasSizeWidth, modifiers.hasSizeHeight);
  auto wrapper = std::make_unique<scenegraph::RectNode>(
      Rect{0.f, 0.f, positive(outerSize.width), positive(outerSize.height)});

  auto* rawWrapper = wrapper.get();
  if (modifiers.hasInteraction()) {
    auto interaction = std::make_unique<InteractionData>();
    if (ComponentKey const* scopeKey = detail::currentInteractionScopeKey()) {
      ComponentKey targetKey = *scopeKey;
      for (LocalId const id : ctx.measureContext().currentElementKey().materialize()) {
        targetKey.push_back(id);
      }
      interaction->stableTargetKey_ = std::move(targetKey);
    } else {
      interaction->stableTargetKey_ = ctx.measureContext().currentElementKey();
    }
    interaction->onTap = modifiers.onTap;
    interaction->onPointerEnter = modifiers.onPointerEnter;
    interaction->onPointerExit = modifiers.onPointerExit;
    interaction->onFocus = modifiers.onFocus;
    interaction->onBlur = modifiers.onBlur;
    interaction->onPointerDown = modifiers.onPointerDown;
    interaction->onPointerUp = modifiers.onPointerUp;
    interaction->onPointerMove = modifiers.onPointerMove;
    interaction->onScroll = modifiers.onScroll;
    interaction->onKeyDown = modifiers.onKeyDown;
    interaction->onKeyUp = modifiers.onKeyUp;
    interaction->onTextInput = modifiers.onTextInput;
    interaction->onTapWithModifiers = modifiers.onTapWithModifiers;
    interaction->focusable_ = modifiers.focusable;
    interaction->cursor = modifiers.cursor;
    interaction->windowDragRegion = modifiers.windowDragRegion;
    interaction->windowResizeEdge = modifiers.windowResizeEdge;
    auto signalChain = detail::currentInteractionSignalChain();
    if (!signalChain.empty()) {
      for (detail::InteractionSignalBundle const* signals : signalChain) {
        interaction->hoverSignals.push_back(signals->hover);
        interaction->pressSignals.push_back(signals->press);
        interaction->focusSignals.push_back(signals->focus);
        interaction->keyboardFocusSignals.push_back(signals->keyboardFocus);
      }
      detail::InteractionSignalBundle const* signals = signalChain.back();
      interaction->hoverSignal = signals->hover;
      interaction->pressSignal = signals->press;
      interaction->focusSignal = signals->focus;
      interaction->keyboardFocusSignal = signals->keyboardFocus;
    }
    rawWrapper->setInteraction(std::move(interaction));
  }
  EnvironmentBinding bindingEnvironment = activeCtx.environmentBinding();
  installBinding<FillStyle>(activeCtx, modifiers.fill, [rawWrapper, bindingEnvironment](FillStyle fill) {
    rawWrapper->setFill(resolveFillStyle(std::move(fill), activeTheme(bindingEnvironment)));
  }, false);
  installBinding<StrokeStyle>(activeCtx, modifiers.stroke, [rawWrapper, bindingEnvironment](StrokeStyle stroke) {
    rawWrapper->setStroke(resolveStrokeStyle(std::move(stroke), activeTheme(bindingEnvironment)));
  }, false);
  installBinding<ShadowStyle>(activeCtx, modifiers.shadow, [rawWrapper, bindingEnvironment](ShadowStyle shadow) {
    rawWrapper->setShadow(resolveShadowStyle(shadow, activeTheme(bindingEnvironment)));
  }, false);
  installBinding<CornerRadius>(activeCtx, modifiers.cornerRadius, [rawWrapper](CornerRadius radius) {
    rawWrapper->setCornerRadius(radius);
  });
  installBinding<float>(activeCtx, modifiers.opacity, [rawWrapper](float opacity) {
    rawWrapper->setOpacity(opacity);
  });
  rawWrapper->setClipsContents(modifiers.clip);

  scenegraph::RasterCacheNode* rawRasterNode = nullptr;
  if (modifiers.rasterize) {
    auto raster = std::make_unique<scenegraph::RasterCacheNode>(
        Rect{0.f, 0.f, content->size().width, content->size().height});
    rawRasterNode = raster.get();
    raster->setSubtree(std::move(content));
    content = std::move(raster);
  }
  scenegraph::SceneNode* rawContent = content.get();
  content->setPosition(Point{std::max(0.f, padding.left), std::max(0.f, padding.top)});
  wrapper->appendChild(std::move(content));

  LayoutConstraints const bindingConstraints = activeCtx.constraints();
  LayoutHints const bindingHints = activeCtx.hints();
  auto applyTransform = [rawWrapper, transform = modifiers.transform]() mutable {
    rawWrapper->setTransform(transform.evaluate());
  };
  applyTransform();
  if (modifiers.transform.isReactive()) {
    Reactive::SmallFn<void()> requestRedraw = activeCtx.redrawCallback();
    Reactive::withOwner(activeCtx.owner(), [applyTransform, requestRedraw = std::move(requestRedraw)]() mutable {
      Reactive::Effect([applyTransform, requestRedraw]() mutable {
        applyTransform();
        if (requestRedraw) {
          requestRedraw();
        }
      });
    });
  }
  if (modifiers.hasSizeWidth) {
    installBinding<float>(activeCtx, modifiers.sizeWidth,
                          [rawWrapper, bindingConstraints, bindingHints, applyTransform](float width) mutable {
                            float const resolvedWidth =
                                detail::resolvedModifierWidth(bindingConstraints, bindingHints, width);
                            Size size = rawWrapper->size();
                            Size const oldSize = size;
                            size.width = resolvedWidth;
                            rawWrapper->setSize(size);
                            applyTransform();
                            if (rawWrapper->size() != oldSize) {
                              relayoutStoredAncestors(*rawWrapper);
                            }
                          });
  }
  if (modifiers.hasSizeHeight) {
    installBinding<float>(activeCtx, modifiers.sizeHeight,
                          [rawWrapper, bindingConstraints, bindingHints, applyTransform](float height) mutable {
                            float const resolvedHeight =
                                detail::resolvedModifierHeight(bindingConstraints, bindingHints, height);
                            Size size = rawWrapper->size();
                            Size const oldSize = size;
                            size.height = resolvedHeight;
                            rawWrapper->setSize(size);
                            applyTransform();
                            if (rawWrapper->size() != oldSize) {
                              relayoutStoredAncestors(*rawWrapper);
                            }
                          });
  }
  installBinding<float>(activeCtx, modifiers.positionX, [rawWrapper, py = modifiers.positionY](float x) mutable {
    rawWrapper->setPosition(Point{x, py.evaluate()});
  });
  installBinding<float>(activeCtx, modifiers.positionY, [rawWrapper, px = modifiers.positionX](float y) mutable {
    rawWrapper->setPosition(Point{px.evaluate(), y});
  });
  if (rawRasterNode) {
    for (Reactive::BindingFn invalidator : modifiers.rasterizeInvalidators) {
      Reactive::SmallFn<void()> requestRedraw = activeCtx.redrawCallback();
      Reactive::withOwner(activeCtx.owner(), [rawRasterNode, invalidator = std::move(invalidator),
                                              requestRedraw = std::move(requestRedraw)]() mutable {
        Reactive::Effect([rawRasterNode, invalidator = std::move(invalidator),
                          requestRedraw = std::move(requestRedraw)]() mutable {
          invalidator();
          rawRasterNode->invalidateCache();
          if (requestRedraw) {
            requestRedraw();
          }
        });
      });
    }
  }

  if (modifiers.overlay) {
    MountContext overlayCtx = activeCtx.childWithSharedScope(LayoutConstraints{
        .maxWidth = outerSize.width,
        .maxHeight = outerSize.height,
        .minWidth = 0.f,
        .minHeight = 0.f,
    }, activeCtx.hints());
    if (auto overlayNode = modifiers.overlay->mount(overlayCtx)) {
      wrapper->appendChild(std::move(overlayNode));
    }
  }

  LayoutHints const relayoutHints = activeCtx.hints();
  rawWrapper->setLayoutConstraints(activeCtx.constraints());
  rawWrapper->setRelayout([rawWrapper, rawContent, relayoutHints,
                           modifiers](LayoutConstraints const& constraints) mutable {
    EdgeInsets const padding = modifiers.padding.evaluate();
    float const padL = std::max(0.f, padding.left);
    float const padR = std::max(0.f, padding.right);
    float const padT = std::max(0.f, padding.top);
    float const padB = std::max(0.f, padding.bottom);
    float const width = modifiers.sizeWidth.evaluate();
    float const height = modifiers.sizeHeight.evaluate();
    float const resolvedWidth = detail::resolvedModifierWidth(constraints, relayoutHints, width);
    float const resolvedHeight = detail::resolvedModifierHeight(constraints, relayoutHints, height);
    bool const hasResolvedWidth =
        detail::hasResolvedModifierWidth(constraints, relayoutHints, modifiers.hasSizeWidth);
    bool const hasResolvedHeight =
        detail::hasResolvedModifierHeight(constraints, relayoutHints, modifiers.hasSizeHeight);
    LayoutConstraints innerConstraints =
        modifierInnerConstraints(constraints, padding, relayoutHints, width, height,
                                 modifiers.hasSizeWidth, modifiers.hasSizeHeight);
    if (rawContent) {
      rawContent->relayout(innerConstraints);
    }
    Size contentSize = rawContent ? rawContent->size() : Size{};
    Size nextSize{contentSize.width + padL + padR, contentSize.height + padT + padB};
    if (hasResolvedWidth) {
      nextSize.width = resolvedWidth;
    }
    if (hasResolvedHeight) {
      nextSize.height = resolvedHeight;
    }
    if (!hasResolvedWidth) {
      nextSize.width = std::max(nextSize.width, constraints.minWidth);
    }
    if (!hasResolvedHeight) {
      nextSize.height = std::max(nextSize.height, constraints.minHeight);
    }
    if (!hasResolvedWidth && std::isfinite(constraints.maxWidth)) {
      nextSize.width = std::min(nextSize.width, constraints.maxWidth);
    }
    if (!hasResolvedHeight && std::isfinite(constraints.maxHeight)) {
      nextSize.height = std::min(nextSize.height, constraints.maxHeight);
    }
    rawWrapper->setSize(Size{positive(nextSize.width), positive(nextSize.height)});
    rawWrapper->setTransform(modifiers.transform.evaluate());
    if (rawContent) {
      rawContent->setPosition(Point{padL, padT});
    }
    auto children = rawWrapper->children();
    for (std::size_t i = 1; i < children.size(); ++i) {
      if (children[i]) {
        children[i]->relayout(fixedConstraints(rawWrapper->size()), false);
      }
    }
  });

  return wrapper;
}

} // namespace lambdaui
