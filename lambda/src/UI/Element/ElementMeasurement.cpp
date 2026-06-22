#include <Lambda/UI/Element.hpp>

#include <Lambda/Graphics/TextSystem.hpp>
#include <Lambda/UI/Detail/LayoutDebugDump.hpp>
#include <Lambda/UI/Environment.hpp>

#include "UI/ViewLayout/ContainerScope.hpp"
#include "UI/Element/ModifierLayoutHelpers.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

namespace lambda {

namespace {

struct ChildLocalIdScope {
  MeasureContext& ctx;

  ChildLocalIdScope(MeasureContext& context, std::optional<std::string> const& explicitKey)
      : ctx(context) {
    if (explicitKey.has_value()) {
      ctx.pushExplicitChildLocalId(LocalId::fromString(*explicitKey));
    } else {
      ctx.pushExplicitChildLocalId(std::nullopt);
    }
  }

  ~ChildLocalIdScope() { ctx.popExplicitChildLocalId(); }
};

struct EnvironmentBindingScope {
  MeasureContext& ctx;
  EnvironmentBinding previous;
  bool active = false;

  EnvironmentBindingScope(MeasureContext& context,
                          std::vector<std::shared_ptr<detail::EnvironmentOverride const>> const& overrides)
      : ctx(context)
      , previous(context.environmentBinding())
      , active(!overrides.empty()) {
    if (!active) {
      return;
    }
    EnvironmentBinding next = previous;
    for (auto const& override : overrides) {
      next = override->apply(next);
    }
    ctx.setEnvironmentBinding(std::move(next));
  }

  ~EnvironmentBindingScope() {
    if (active) {
      ctx.setEnvironmentBinding(std::move(previous));
    }
  }
};

} // namespace

Size Element::measureWithModifiersImpl(MeasureContext& ctx, LayoutConstraints const& constraints,
                                       LayoutHints const& hints, TextSystem& textSystem) const {
  detail::ElementModifiers const& m = *modifiers_;
  EdgeInsets const padding = m.padding.evaluate();
  float const padL = std::max(0.f, padding.left);
  float const padR = std::max(0.f, padding.right);
  float const padT = std::max(0.f, padding.top);
  float const padB = std::max(0.f, padding.bottom);
  float const padW = padL + padR;
  float const padH = padT + padB;
  float const width = m.sizeWidth.evaluate();
  float const height = m.sizeHeight.evaluate();
  float const resolvedWidth = detail::resolvedModifierWidth(constraints, hints, width);
  float const resolvedHeight = detail::resolvedModifierHeight(constraints, hints, height);
  bool const hasResolvedWidth =
      detail::hasResolvedModifierWidth(constraints, hints, m.hasSizeWidth);
  bool const hasResolvedHeight =
      detail::hasResolvedModifierHeight(constraints, hints, m.hasSizeHeight);
  LayoutConstraints innerCs = constraints;
  if (padW > 0.f || padH > 0.f) {
    if (std::isfinite(innerCs.maxWidth)) {
      innerCs.maxWidth -= padW;
    }
    if (std::isfinite(innerCs.maxHeight)) {
      innerCs.maxHeight -= padH;
    }
    innerCs.minWidth = std::max(0.f, innerCs.minWidth - padW);
    innerCs.minHeight = std::max(0.f, innerCs.minHeight - padH);
  }
  if (hasResolvedWidth) {
    float const innerWidth = std::max(0.f, resolvedWidth - padW);
    innerCs.maxWidth = innerWidth;
    innerCs.minWidth = innerWidth;
  }
  if (hasResolvedHeight) {
    float const innerHeight = std::max(0.f, resolvedHeight - padH);
    innerCs.maxHeight = innerHeight;
    innerCs.minHeight = innerHeight;
  }
  if (std::isfinite(innerCs.maxWidth)) {
    innerCs.minWidth = std::min(innerCs.minWidth, innerCs.maxWidth);
  }
  if (std::isfinite(innerCs.maxHeight)) {
    innerCs.minHeight = std::min(innerCs.minHeight, innerCs.maxHeight);
  }

  Size sz{};
  if (m.overlay) {
    ContainerMeasureScope scope(ctx);
    Size const szUnder = impl_->measure(ctx, innerCs, hints, textSystem);
    Size const szOver = m.overlay->measure(ctx, innerCs, hints, textSystem);
    sz.width = std::max(szUnder.width, szOver.width) + padW;
    sz.height = std::max(szUnder.height, szOver.height) + padH;
  } else {
    ContainerMeasureScope scope(ctx);
    sz = impl_->measure(ctx, innerCs, hints, textSystem);
    sz.width += padW;
    sz.height += padH;
  }
  if (hasResolvedWidth) {
    sz.width = resolvedWidth;
  }
  if (hasResolvedHeight) {
    sz.height = resolvedHeight;
  }
  if (!hasResolvedWidth) {
    sz.width = std::max(sz.width, constraints.minWidth);
  }
  if (!hasResolvedHeight) {
    sz.height = std::max(sz.height, constraints.minHeight);
  }
  if (!hasResolvedWidth && std::isfinite(constraints.maxWidth)) {
    sz.width = std::min(sz.width, constraints.maxWidth);
  }
  if (!hasResolvedHeight && std::isfinite(constraints.maxHeight)) {
    sz.height = std::min(sz.height, constraints.maxHeight);
  }
  return sz;
}

Size Element::measure(MeasureContext& ctx, LayoutConstraints const& constraints,
                      LayoutHints const& hints, TextSystem& textSystem) const {
  ChildLocalIdScope const childIdScope{ctx, key_};
  detail::CurrentMeasureContextScope const currentMeasureContext{ctx};
  EnvironmentBindingScope const environmentBindingScope{ctx, envOverrides_};
  Element const* const prevEl = ctx.currentElement();
  ctx.setCurrentElement(this);
  Size const sz = modifiers_ && modifiers_->needsModifierPass() ? measureWithModifiersImpl(ctx, constraints, hints, textSystem)
                                                                : impl_->measure(ctx, constraints, hints, textSystem);
  ctx.setCurrentElement(prevEl);
  layoutDebugRecordMeasure(constraints, sz);
#ifndef NDEBUG
  assert(std::isfinite(sz.width) && std::isfinite(sz.height));
  assert(sz.width >= 0.f && sz.height >= 0.f);
#endif
  return sz;
}

} // namespace lambda
