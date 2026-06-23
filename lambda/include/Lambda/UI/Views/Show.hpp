#pragma once

/// \file Lambda/UI/Views/Show.hpp
///
/// Reactive conditional primitive for v5 build-once view trees.

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

template<typename Condition, typename ThenFactory, typename ElseFactory>
class ShowView {
public:
  static constexpr bool mountsWhenCollapsed = true;

  ShowView(Condition condition, ThenFactory thenFactory, ElseFactory elseFactory)
      : condition_(std::move(condition))
      , thenFactory_(std::move(thenFactory))
      , elseFactory_(std::move(elseFactory)) {}

  Size measure(MeasureContext& ctx, LayoutConstraints const& constraints,
               LayoutHints const& hints, TextSystem& textSystem) const {
    ctx.advanceChildSlot();
    EnvironmentBinding const environment = ctx.environmentBinding();
    if (detail::readConditionCopy(condition_)) {
      auto factory = thenFactory_;
      Element element = detail::invokeElementFactory(factory);
      return detail::controlMeasureElement(
          element, environment, textSystem, constraints, hints);
    }
    auto factory = elseFactory_;
    Element element = detail::invokeElementFactory(factory);
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
        condition_, thenFactory_, elseFactory_, frameSize, ctx.environmentBinding(),
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
    Condition condition;
    ThenFactory thenFactory;
    ElseFactory elseFactory;
    Size frameSize{};
    EnvironmentBinding environment;
    TextSystem& textSystem;
    LayoutConstraints constraints;
    LayoutHints hints;
    Reactive::SmallFn<void()> requestRedraw;
    std::optional<ComponentKey> parentInteractionScopeKey;
    std::optional<Size> assignedSlot;
    std::optional<bool> activeBranch;
    std::shared_ptr<Reactive::Scope> branchScope;

    State(Condition conditionIn, ThenFactory thenFactoryIn, ElseFactory elseFactoryIn,
          Size frameSizeIn, EnvironmentBinding environmentIn, TextSystem& textSystemIn,
          LayoutConstraints constraintsIn, LayoutHints hintsIn,
          Reactive::SmallFn<void()> requestRedrawIn,
          std::optional<ComponentKey> parentInteractionScopeKeyIn)
        : condition(std::move(conditionIn))
        , thenFactory(std::move(thenFactoryIn))
        , elseFactory(std::move(elseFactoryIn))
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
      bool const nextBranch = detail::readCondition(condition);
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
      constraints = nextConstraints;
      if (scenegraph::detail::isTransientRelayout()) {
        assignedSlot = detail::controlAssignedSlot(nextConstraints);
      } else {
        assignedSlot.reset();
      }
      frameSize = detail::controlAssignedSize(nextConstraints);
      auto children = group.children();
      if (!children.empty() && children.front()) {
        (void)children.front()->relayout(nextConstraints);
        children.front()->setLayoutConstraints(nextConstraints);
      }
      detail::controlLayoutSingle(group, frameSize);
    }

    void disposeBranch() {
      if (branchScope) {
        branchScope->dispose();
        branchScope.reset();
      }
      activeBranch.reset();
    }

    std::unique_ptr<scenegraph::SceneNode> mountBranch(bool thenBranch) {
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
            element.emplace(thenBranch
                ? detail::invokeElementFactory(thenFactory)
                : detail::invokeElementFactory(elseFactory));
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

  Condition condition_;
  ThenFactory thenFactory_;
  ElseFactory elseFactory_;
};

template<typename Condition, typename ThenFactory, typename ElseFactory>
ShowView<std::decay_t<Condition>, std::decay_t<ThenFactory>, std::decay_t<ElseFactory>>
Show(Condition&& condition, ThenFactory&& thenFactory, ElseFactory&& elseFactory) {
  return ShowView<std::decay_t<Condition>, std::decay_t<ThenFactory>, std::decay_t<ElseFactory>>{
      std::forward<Condition>(condition), std::forward<ThenFactory>(thenFactory),
      std::forward<ElseFactory>(elseFactory)};
}

template<typename Condition, typename ThenFactory>
auto Show(Condition&& condition, ThenFactory&& thenFactory) {
  auto empty = [] {
    return Element{Rectangle{}}.size(0.f, 0.f);
  };
  return Show(std::forward<Condition>(condition), std::forward<ThenFactory>(thenFactory),
              std::move(empty));
}

} // namespace lambdaui
