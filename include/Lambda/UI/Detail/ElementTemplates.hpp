#pragma once

#include <Lambda/SceneGraph/SceneNode.hpp>
#include <Lambda/UI/Hooks.hpp>
#include <Lambda/UI/MountContext.hpp>

#include <cassert>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace lambda {

namespace detail {

struct EmptyBodyElementCache {};

struct BodyElementCache {
  std::shared_ptr<Reactive::Scope> scope;
  std::optional<Element> element;
};

} // namespace detail

template<typename C>
struct Element::Model : Concept {
  C value;
  using BodyCache = std::conditional_t<BodyComponent<C>, detail::BodyElementCache,
                                       detail::EmptyBodyElementCache>;
  [[no_unique_address]] mutable BodyCache bodyCache_;

  explicit Model(C c) : value(std::move(c)) {}

  std::type_index modelType() const noexcept override { return std::type_index(typeid(C)); }
  void const* rawValuePtr() const noexcept override { return &value; }
  Element const& bodyElement(LayoutConstraints const& constraints) const;

  Size measure(MeasureContext& ctx, LayoutConstraints const& constraints,
               LayoutHints const& hints, TextSystem& textSystem) const override;
  std::unique_ptr<scenegraph::SceneNode> mount(MountContext& ctx) const override;
};

template<typename C>
Element const& Element::Model<C>::bodyElement(LayoutConstraints const& constraints) const {
  static_assert(BodyComponent<C>, "bodyElement() is only valid for body components");
  if (!bodyCache_.element) {
    bodyCache_.scope = std::make_shared<Reactive::Scope>();
    Reactive::withOwner(*bodyCache_.scope, [&] {
      detail::HookLayoutScope const hookScope{constraints};
      detail::HookInteractionSignalScope const interactionScope{*bodyCache_.scope};
      bodyCache_.element.emplace(value.body());
    });
  }
  return *bodyCache_.element;
}

template<typename C>
Size Element::Model<C>::measure(MeasureContext& ctx, LayoutConstraints const& constraints,
                                LayoutHints const& hints, TextSystem& textSystem) const {
  if constexpr (requires(C const& component, MeasureContext& measureContext,
                         LayoutConstraints const& layoutConstraints,
                         LayoutHints const& layoutHints, TextSystem& text) {
                  { component.measure(measureContext, layoutConstraints, layoutHints, text) } -> std::convertible_to<Size>;
                }) {
    return value.measure(ctx, constraints, hints, textSystem);
  } else if constexpr (BodyComponent<C>) {
    Element const& child = bodyElement(constraints);
    return child.measure(ctx, constraints, hints, textSystem);
  } else {
    static_assert(alwaysFalse<C>,
                  "Component must provide either measure(MeasureContext, LayoutConstraints, LayoutHints, "
                  "TextSystem) or body().");
    return {};
  }
}

template<typename C>
std::unique_ptr<scenegraph::SceneNode> Element::Model<C>::mount(MountContext& ctx) const {
  if constexpr (std::is_same_v<C, Rectangle>) {
    return detail::mountRectangle(value, ctx);
  } else if constexpr (std::is_same_v<C, Text>) {
    return detail::mountText(value, ctx);
  } else if constexpr (std::is_same_v<C, VStack>) {
    return detail::mountVStack(value, ctx);
  } else if constexpr (std::is_same_v<C, HStack>) {
    return detail::mountHStack(value, ctx);
  } else if constexpr (std::is_same_v<C, ZStack>) {
    return detail::mountZStack(value, ctx);
  } else if constexpr (requires(C const& component, MountContext& mountContext) {
                         { component.mount(mountContext) } -> std::same_as<std::unique_ptr<scenegraph::SceneNode>>;
                       }) {
    return value.mount(ctx);
  } else if constexpr (BodyComponent<C>) {
    MountContext childCtx = ctx.childWithOwnScope(ctx.constraints(), ctx.hints());
    detail::CurrentMountContextScope const currentMountContext{childCtx};
    Element const& child = bodyElement(ctx.constraints());
    if (bodyCache_.scope) {
      std::shared_ptr<Reactive::Scope> bodyScope = bodyCache_.scope;
      childCtx.owner().onCleanup([bodyScope] {
        bodyScope->dispose();
      });
    }
    return Reactive::withOwner(childCtx.owner(), [&] {
      detail::HookLayoutScope const hookScope{ctx.constraints()};
      detail::HookInteractionSignalScope const interactionScope{*bodyCache_.scope};
      return child.mount(childCtx);
    });
  } else {
    static_assert(alwaysFalse<C>, "Component is not mountable in v5 Stage 4.");
    return nullptr;
  }
}

template<typename C>
Element::Element(C component)
    : impl_(std::make_shared<Model<C>>(std::move(component)))
    , mountsWhenCollapsed_(detail::mountsWhenCollapsedOf<C>())
{}

template<typename T>
bool Element::is() const noexcept {
  return impl_ && impl_->modelType() == std::type_index(typeid(T));
}

template<typename T>
T const& Element::as() const {
  assert(is<T>());
  return *static_cast<T const*>(impl_->rawValuePtr());
}

template<typename... Args>
std::vector<Element> children(Args&&... args) {
  std::vector<Element> v;
  v.reserve(sizeof...(args));
  (v.emplace_back(std::forward<Args>(args)), ...);
  return v;
}

} // namespace lambda
