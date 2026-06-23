#pragma once

/// \file Lambda/UI/Views/Switch.hpp
///
/// Reactive multi-branch primitive for v5 build-once view trees.

#include <Lambda/Reactive/Effect.hpp>
#include <Lambda/UI/Views/ControlFlowDetail.hpp>
#include <Lambda/UI/Views/Rectangle.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace lambdaui {

template<typename T>
struct SwitchCase {
  T value;
  std::function<Element()> factory;
};

template<typename T, typename Factory>
SwitchCase<std::decay_t<T>> Case(T&& value, Factory&& factory) {
  using Value = std::decay_t<T>;
  return SwitchCase<Value>{
      .value = std::forward<T>(value),
      .factory = [factory = std::forward<Factory>(factory)]() mutable -> Element {
        return detail::invokeElementFactory(factory);
      },
  };
}

template<typename T, typename Selector>
class SwitchView {
public:
  using Value = std::decay_t<T>;
  static constexpr bool mountsWhenCollapsed = true;

  SwitchView(Selector selector, std::vector<SwitchCase<Value>> cases,
             std::function<Element()> defaultFactory)
      : selector_(std::move(selector))
      , cases_(std::move(cases))
      , defaultFactory_(std::move(defaultFactory)) {}

  Size measure(MeasureContext& ctx, LayoutConstraints const& constraints,
               LayoutHints const& hints, TextSystem& textSystem) const {
    ctx.advanceChildSlot();
    Value const value = detail::readSelectorCopy(selector_);
    EnvironmentBinding const environment = ctx.environmentBinding();
    for (SwitchCase<Value> const& candidate : cases_) {
      if (candidate.value == value) {
        Element element = candidate.factory();
        return detail::controlMeasureElement(
            element, environment, textSystem, constraints, hints);
      }
    }
    Element element = defaultFactory_();
    return detail::controlMeasureElement(
        element, environment, textSystem, constraints, hints);
  }

  std::unique_ptr<scenegraph::SceneNode> mount(MountContext& ctx) const {
    Size const frameSize{};
    auto group = std::make_unique<scenegraph::SceneNode>(
        Rect{0.f, 0.f, detail::controlFiniteOrZero(frameSize.width),
             detail::controlFiniteOrZero(frameSize.height)});

    auto controlScope = std::make_shared<Reactive::Scope>();
    ctx.owner().onCleanup([controlScope] {
      controlScope->dispose();
    });

    auto state = std::make_shared<State>(
        selector_, cases_, defaultFactory_, frameSize, ctx.environmentBinding(),
        ctx.textSystem(), ctx.constraints(), ctx.hints(),
        ctx.redrawCallback(), detail::currentInteractionScopeKeyCopy());

    scenegraph::SceneNode* rawGroup = group.get();
    rawGroup->setLayoutConstraints(ctx.constraints());
    rawGroup->setRelayout([state, rawGroup](LayoutConstraints const& constraints) {
      state->relayout(*rawGroup, constraints);
    });
    Reactive::withOwner(*controlScope, [state, rawGroup] {
      Reactive::Effect([state, rawGroup] {
        state->reconcile(*rawGroup);
      });
    });

    return group;
  }

private:
  struct State {
    Selector selector;
    std::vector<SwitchCase<Value>> cases;
    std::function<Element()> defaultFactory;
    Size frameSize{};
    EnvironmentBinding environment;
    TextSystem& textSystem;
    LayoutConstraints constraints;
    LayoutHints hints;
    Reactive::SmallFn<void()> requestRedraw;
    std::optional<ComponentKey> parentInteractionScopeKey;
    std::optional<Size> assignedSlot;
    std::optional<std::size_t> activeBranch;
    std::shared_ptr<Reactive::Scope> branchScope;

    State(Selector selectorIn, std::vector<SwitchCase<Value>> casesIn,
          std::function<Element()> defaultFactoryIn, Size frameSizeIn,
          EnvironmentBinding environmentIn, TextSystem& textSystemIn,
          LayoutConstraints constraintsIn, LayoutHints hintsIn,
          Reactive::SmallFn<void()> requestRedrawIn,
          std::optional<ComponentKey> parentInteractionScopeKeyIn)
        : selector(std::move(selectorIn))
        , cases(std::move(casesIn))
        , defaultFactory(std::move(defaultFactoryIn))
        , frameSize(frameSizeIn)
        , environment(std::move(environmentIn))
        , textSystem(textSystemIn)
        , constraints(constraintsIn)
        , hints(hintsIn)
        , requestRedraw(std::move(requestRedrawIn))
        , parentInteractionScopeKey(std::move(parentInteractionScopeKeyIn)) {}

    ~State() {
      disposeBranch();
    }

    void reconcile(scenegraph::SceneNode& group) {
      Value const value = detail::readSelector(selector);
      std::size_t const nextBranch = branchIndex(value);
      if (activeBranch && *activeBranch == nextBranch) {
        return;
      }

      Size const oldSize = group.size();
      disposeBranch();
      (void)group.releaseChildren();
      activeBranch = nextBranch;

      auto node = mountBranch(nextBranch);
      if (node) {
        std::vector<std::unique_ptr<scenegraph::SceneNode>> children;
        children.push_back(std::move(node));
        group.replaceChildren(std::move(children));
      }
      detail::controlRelayoutSingleChildInSlot(group, assignedSlot);
      frameSize = assignedSlot.value_or(detail::controlAssignedSize(constraints));
      detail::controlLayoutSingle(group, frameSize);
      detail::controlPropagateLayoutChange(group, oldSize);
      if (requestRedraw) {
        requestRedraw();
      }
    }

    void relayout(scenegraph::SceneNode& group, LayoutConstraints const& nextConstraints) {
      if (scenegraph::detail::isTransientRelayout()) {
        assignedSlot = detail::controlAssignedSlot(nextConstraints);
      } else {
        constraints = nextConstraints;
        assignedSlot.reset();
      }
      frameSize = detail::controlAssignedSize(nextConstraints);
      auto children = group.children();
      if (!children.empty() && children.front()) {
        (void)children.front()->relayout(nextConstraints);
      }
      detail::controlLayoutSingle(group, frameSize);
    }

    std::size_t branchIndex(Value const& value) const {
      for (std::size_t i = 0; i < cases.size(); ++i) {
        if (cases[i].value == value) {
          return i;
        }
      }
      return cases.size();
    }

    void disposeBranch() {
      if (branchScope) {
        branchScope->dispose();
        branchScope.reset();
      }
      activeBranch.reset();
    }

    std::unique_ptr<scenegraph::SceneNode> mountBranch(std::size_t branch) {
      return Reactive::untrack([&] {
        branchScope = std::make_shared<Reactive::Scope>();
        return Reactive::withOwner(*branchScope, [&] {
          MeasureContext factoryMeasureContext{textSystem, environment};
          MountContext factoryMountContext{*branchScope, textSystem, factoryMeasureContext,
                                           constraints, hints, requestRedraw, environment};
          std::optional<Element> element;
          Size measured{};
          {
            detail::CurrentMountContextScope const currentMountContext{factoryMountContext};
            detail::ScopedInteractionScopeKey const parentScope{parentInteractionScopeKey};
            detail::HookInteractionSignalScope const branchInteractionScope{*branchScope};
            element.emplace(branch < cases.size()
                ? cases[branch].factory()
                : defaultFactory());
            measured = detail::controlMeasureElement(
                *element, environment, textSystem, constraints, hints);
          }
          detail::ScopedInteractionScopeKey const parentScope{parentInteractionScopeKey};
          return detail::controlMountElement(
              *element, *branchScope, environment, textSystem,
              detail::controlFixedConstraints(measured), hints, requestRedraw);
        });
      });
    }
  };

  Selector selector_;
  std::vector<SwitchCase<Value>> cases_;
  std::function<Element()> defaultFactory_;
};

template<typename Selector, typename Value = std::decay_t<std::invoke_result_t<Selector&>>>
SwitchView<Value, std::decay_t<Selector>>
Switch(Selector&& selector, std::vector<SwitchCase<Value>> cases,
       std::function<Element()> defaultFactory = [] {
         return Element{Rectangle{}}.size(0.f, 0.f);
       }) {
  return SwitchView<Value, std::decay_t<Selector>>{
      std::forward<Selector>(selector), std::move(cases), std::move(defaultFactory)};
}

} // namespace lambdaui
