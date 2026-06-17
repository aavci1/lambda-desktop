#pragma once

/// \file Lambda/UI/Views/For.hpp
///
/// Reactive keyed list primitive for v5 build-once view trees.

#include <Lambda/Reactive/Effect.hpp>
#include <Lambda/Reactive/Signal.hpp>
#include <Lambda/Layout/Alignment.hpp>
#include <Lambda/UI/Views/ControlFlowDetail.hpp>

#include <algorithm>
#include <concepts>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace lambda {

enum class ForLayout {
  VerticalStack,
  HorizontalStack,
  Overlay,
};

template<typename T, typename KeyFn, typename Factory>
class ForView {
public:
  using Items = std::vector<T>;
  using Key = std::decay_t<std::invoke_result_t<KeyFn&, T const&>>;
  static constexpr bool mountsWhenCollapsed = true;

  static_assert(std::equality_comparable<Key>,
                "For keys must be equality-comparable so rows can be reconciled.");

private:
  struct State;

public:
  ForView(Reactive::Signal<Items> items, KeyFn keyFn, Factory factory,
          float spacing = 0.f, Alignment alignment = Alignment::Start,
          ForLayout layout = ForLayout::VerticalStack)
      : state_(std::make_shared<State>(
            std::move(items), std::move(keyFn), std::move(factory), spacing, alignment, layout)) {}

  Size measure(MeasureContext& ctx, LayoutConstraints const& constraints,
               LayoutHints const&, TextSystem& textSystem) const {
    return state_->measure(ctx, constraints, textSystem);
  }

  std::unique_ptr<scenegraph::SceneNode> mount(MountContext& ctx) const {
    Size const frameSize{};
    auto group = std::make_unique<scenegraph::SceneNode>(
        Rect{0.f, 0.f, detail::controlFiniteOrZero(frameSize.width),
             detail::controlFiniteOrZero(frameSize.height)});
    scenegraph::LayoutFlow flow = scenegraph::LayoutFlow::VerticalStack;
    if (state_->layout == ForLayout::Overlay) {
      flow = scenegraph::LayoutFlow::None;
    } else if (state_->layout == ForLayout::HorizontalStack) {
      flow = scenegraph::LayoutFlow::HorizontalStack;
    }
    group->setLayoutFlow(flow);
    group->setLayoutSpacing(state_->spacing);

    auto controlScope = std::make_shared<Reactive::Scope>();
    auto state = state_;
    ctx.owner().onCleanup([controlScope, state] {
      controlScope->dispose();
      state->dispose();
    });

    state->configureMount(ctx.environmentBinding(), ctx.textSystem(),
                          ctx.constraints(), ctx.redrawCallback(),
                          detail::currentInteractionScopeKeyCopy());

    scenegraph::SceneNode* rawGroup = group.get();
    rawGroup->setLayoutConstraints(ctx.constraints());
    rawGroup->setRelayout([state, rawGroup](LayoutConstraints const& constraints) {
      state->relayout(*rawGroup, constraints);
    });

    Reactive::withOwner(*controlScope, [state, rawGroup] {
      Reactive::Effect([state, rawGroup] {
        Items const nextItems = state->items.get();
        Reactive::untrack([state, rawGroup, &nextItems] {
          state->reconcile(*rawGroup, nextItems);
        });
      });
    });

    return group;
  }

private:
  struct Row {
    Key key;
    Reactive::Signal<std::size_t> index;
    std::shared_ptr<Reactive::Scope> scope;
    Element element;
    Size cachedSize{};
    LayoutConstraints cachedConstraints{};
    LayoutHints cachedHints{};
    bool hasCachedSize = false;

    Row(Key keyIn, Reactive::Signal<std::size_t> indexIn,
        std::shared_ptr<Reactive::Scope> scopeIn, Element elementIn)
        : key(std::move(keyIn))
        , index(std::move(indexIn))
        , scope(std::move(scopeIn))
        , element(std::move(elementIn)) {}

    Row(Row&&) noexcept = default;
    Row& operator=(Row&&) noexcept = default;
    Row(Row const&) = delete;
    Row& operator=(Row const&) = delete;
  };

  struct State {
    Reactive::Signal<Items> items;
    KeyFn keyFn;
    Factory factory;
    float spacing = 0.f;
    Alignment alignment = Alignment::Start;
    ForLayout layout = ForLayout::VerticalStack;
    EnvironmentBinding environment;
    TextSystem* textSystem = nullptr;
    LayoutConstraints constraints;
    Reactive::SmallFn<void()> requestRedraw;
    std::optional<ComponentKey> parentInteractionScopeKey;
    std::vector<Row> rows;
    bool disposed = false;

    State(Reactive::Signal<Items> itemsIn, KeyFn keyFnIn, Factory factoryIn,
          float spacingIn, Alignment alignmentIn, ForLayout layoutIn)
        : items(std::move(itemsIn))
        , keyFn(std::move(keyFnIn))
        , factory(std::move(factoryIn))
        , spacing(spacingIn)
        , alignment(alignmentIn)
        , layout(layoutIn) {}

    ~State() {
      dispose();
    }

    Size measure(MeasureContext& ctx, LayoutConstraints const& nextConstraints,
                 TextSystem& measureTextSystem) {
      ctx.advanceChildSlot();
      if (disposed) {
        return clampSize({}, nextConstraints);
      }
      environment = ctx.environmentBinding();
      textSystem = &measureTextSystem;
      constraints = nextConstraints;
      if (auto key = detail::currentInteractionScopeKeyCopy()) {
        parentInteractionScopeKey = std::move(key);
      }
      reconcileMeasuredRows(items.peek());
      return measuredStackSize(nextConstraints);
    }

    void configureMount(EnvironmentBinding environmentIn,
                        TextSystem& textSystemIn, LayoutConstraints constraintsIn,
                        Reactive::SmallFn<void()> requestRedrawIn,
                        std::optional<ComponentKey> parentInteractionScopeKeyIn) {
      disposed = false;
      environment = std::move(environmentIn);
      textSystem = &textSystemIn;
      constraints = constraintsIn;
      requestRedraw = std::move(requestRedrawIn);
      parentInteractionScopeKey = std::move(parentInteractionScopeKeyIn);
    }

    void dispose() {
      if (disposed) {
        return;
      }
      disposed = true;
      disposeRows(rows);
    }

    void reconcile(scenegraph::SceneNode& group, Items const& nextItems) {
      if (disposed || !textSystem) {
        return;
      }
      Size const oldSize = group.size();
      std::vector<std::unique_ptr<scenegraph::SceneNode>> oldNodes = group.releaseChildren();
      std::vector<Row> oldRows = std::move(rows);
      std::vector<bool> used(oldRows.size(), false);
      std::vector<Row> nextRows;
      std::vector<std::unique_ptr<scenegraph::SceneNode>> nextNodes;
      nextRows.reserve(nextItems.size());
      nextNodes.reserve(nextItems.size());

      LayoutConstraints const childConstraints = rowConstraints(constraints);
      LayoutHints const childHints = rowHints();

      for (std::size_t index = 0; index < nextItems.size(); ++index) {
        T const& item = nextItems[index];
        Key key = std::invoke(keyFn, item);
        std::optional<std::size_t> match = findUnused(oldRows, used, key);
        if (match) {
          std::size_t const oldIndex = *match;
          used[oldIndex] = true;
          Row row = std::move(oldRows[oldIndex]);
          bool const indexChanged = row.index.peek() != index;
          row.index.set(index);
          if (indexChanged) {
            row.hasCachedSize = false;
          }
          std::unique_ptr<scenegraph::SceneNode> node;
          if (oldIndex < oldNodes.size()) {
            node = std::move(oldNodes[oldIndex]);
          }
          if (node) {
            relayoutMountedRow(row, *node, childConstraints, childHints);
          } else {
            ensureMeasured(row, environment, *textSystem,
                           childConstraints, childHints);
            node = mountRowNode(row, childHints);
            if (node) {
              relayoutMountedRow(row, *node, childConstraints, childHints);
            }
          }
          nextRows.push_back(std::move(row));
          nextNodes.push_back(std::move(node));
        } else {
          Row row = createRow(item, index, std::move(key), environment);
          ensureMeasured(row, environment, *textSystem,
                         childConstraints, childHints);
          auto node = mountRowNode(row, childHints);
          if (node) {
            relayoutMountedRow(row, *node, childConstraints, childHints);
          }
          nextNodes.push_back(std::move(node));
          nextRows.push_back(std::move(row));
        }
      }

      for (std::size_t i = 0; i < oldRows.size(); ++i) {
        if (!used[i] && oldRows[i].scope) {
          oldRows[i].scope->dispose();
        }
      }

      rows = std::move(nextRows);
      group.replaceChildren(std::move(nextNodes));
      applyGroupLayout(group, Size{});
      detail::controlPropagateLayoutChange(group, oldSize);
      if (requestRedraw) {
        requestRedraw();
      }
    }

    void reconcileMeasuredRows(Items const& nextItems) {
      if (!textSystem) {
        return;
      }
      std::vector<bool> used(rows.size(), false);
      std::vector<Row> nextRows;
      nextRows.reserve(nextItems.size());

      LayoutConstraints const childConstraints = rowConstraints(constraints);
      LayoutHints const childHints = rowHints();

      for (std::size_t index = 0; index < nextItems.size(); ++index) {
        T const& item = nextItems[index];
        Key key = std::invoke(keyFn, item);
        std::optional<std::size_t> match = findUnused(rows, used, key);
        if (match) {
          std::size_t const oldIndex = *match;
          used[oldIndex] = true;
          Row row = std::move(rows[oldIndex]);
          bool const indexChanged = row.index.peek() != index;
          row.index.set(index);
          if (indexChanged) {
            row.hasCachedSize = false;
          }
          ensureMeasured(row, environment, *textSystem,
                         childConstraints, childHints);
          nextRows.push_back(std::move(row));
        } else {
          Row row = createRow(item, index, std::move(key), environment);
          ensureMeasured(row, environment, *textSystem,
                         childConstraints, childHints);
          nextRows.push_back(std::move(row));
        }
      }

      for (std::size_t i = 0; i < rows.size(); ++i) {
        if (!used[i] && rows[i].scope) {
          rows[i].scope->dispose();
        }
      }

      rows = std::move(nextRows);
    }

    void relayout(scenegraph::SceneNode& group, LayoutConstraints const& nextConstraints) {
      if (disposed) {
        return;
      }
      constraints = nextConstraints;
      LayoutConstraints const childConstraints = rowConstraints(nextConstraints);
      LayoutHints const childHints = rowHints();
      auto children = group.children();
      for (std::size_t i = 0; i < rows.size(); ++i) {
        Row& row = rows[i];
        scenegraph::SceneNode* child = i < children.size() ? children[i].get() : nullptr;
        if (child) {
          relayoutMountedRow(row, *child, childConstraints, childHints);
        }
      }
      applyGroupLayout(group, Size{});
    }

    void applyGroupLayout(scenegraph::SceneNode& group, Size frameSize) const {
      if (layout == ForLayout::Overlay) {
        detail::controlLayoutOverlay(group, frameSize);
      } else if (layout == ForLayout::HorizontalStack) {
        detail::controlLayoutHorizontal(group, frameSize, spacing);
      } else {
        detail::controlLayoutVertical(group, frameSize, spacing);
      }
    }

    static void disposeRows(std::vector<Row>& rowsToDispose) {
      for (Row& row : rowsToDispose) {
        if (row.scope) {
          row.scope->dispose();
        }
      }
      rowsToDispose.clear();
    }

    std::optional<std::size_t> findUnused(std::vector<Row> const& candidates,
                                          std::vector<bool> const& used,
                                          Key const& key) const {
      for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (!used[i] && candidates[i].key == key) {
          return i;
        }
      }
      return std::nullopt;
    }

    Row createRow(T const& item, std::size_t index, Key key,
                  EnvironmentBinding const& rowEnvironment) {
      return Reactive::untrack([&] {
        auto rowScope = std::make_shared<Reactive::Scope>();
        Reactive::Signal<std::size_t> indexSignal = Reactive::withOwner(*rowScope, [&] {
          return Reactive::Signal<std::size_t>(index);
        });

        Element element = Reactive::withOwner(*rowScope, [&] {
          MeasureContext factoryMeasureContext{*textSystem, rowEnvironment};
          MountContext factoryMountContext{*rowScope, *textSystem, factoryMeasureContext,
                                           constraints, rowHints(), requestRedraw, rowEnvironment};
          detail::CurrentMountContextScope const currentMountContext{factoryMountContext};
          detail::ScopedInteractionScopeKey const parentScope{parentInteractionScopeKey};
          detail::HookInteractionSignalScope const rowInteractionScope{*rowScope};
          return detail::invokeForFactory(factory, item, indexSignal);
        });

        return Row{std::move(key), std::move(indexSignal), std::move(rowScope),
                   std::move(element)};
      });
    }

    std::unique_ptr<scenegraph::SceneNode> mountRowNode(Row& row,
                                                       LayoutHints const& childHints) {
      if (!textSystem) {
        return nullptr;
      }
      if (!row.hasCachedSize) {
        ensureMeasured(row, environment, *textSystem,
                       rowConstraints(constraints), childHints);
      }
      detail::ScopedInteractionScopeKey const parentScope{parentInteractionScopeKey};
      return detail::controlMountElement(
          row.element, *row.scope, environment, *textSystem,
          detail::controlFixedConstraints(row.cachedSize), childHints, requestRedraw);
    }

    static void relayoutMountedRow(Row& row, scenegraph::SceneNode& node,
                                   LayoutConstraints const& childConstraints,
                                   LayoutHints const& childHints) {
      (void)node.relayout(childConstraints);
      row.cachedSize = node.size();
      row.cachedConstraints = childConstraints;
      row.cachedHints = childHints;
      row.hasCachedSize = true;
    }

    void ensureMeasured(Row& row, EnvironmentBinding const& measureEnvironment,
                        TextSystem& measureTextSystem,
                        LayoutConstraints const& childConstraints,
                        LayoutHints const& childHints) {
      if (row.hasCachedSize &&
          constraintsEqual(row.cachedConstraints, childConstraints) &&
          hintsEqual(row.cachedHints, childHints)) {
        return;
      }
      detail::ScopedInteractionScopeKey const parentScope{parentInteractionScopeKey};
      detail::HookInteractionSignalScope const rowInteractionScope{*row.scope};
      row.cachedSize = detail::controlMeasureElement(
          row.element, measureEnvironment, measureTextSystem,
          childConstraints, childHints);
      row.cachedConstraints = childConstraints;
      row.cachedHints = childHints;
      row.hasCachedSize = true;
    }

    Size measuredStackSize(LayoutConstraints const& outerConstraints) const {
      if (layout == ForLayout::Overlay) {
        Size size{};
        for (Row const& row : rows) {
          float x = 0.f;
          float y = 0.f;
          if (detail::ElementModifiers const* mods = row.element.modifiers()) {
            x = mods->positionX.evaluate();
            y = mods->positionY.evaluate();
          }
          size.width = std::max(size.width, x + row.cachedSize.width);
          size.height = std::max(size.height, y + row.cachedSize.height);
        }
        return clampSize(size, outerConstraints);
      }
      if (layout == ForLayout::HorizontalStack) {
        Size size{};
        for (std::size_t i = 0; i < rows.size(); ++i) {
          size.width += rows[i].cachedSize.width;
          size.height = std::max(size.height, rows[i].cachedSize.height);
          if (i + 1 < rows.size()) {
            size.width += spacing;
          }
        }
        return clampSize(size, outerConstraints);
      }
      Size size{};
      for (std::size_t i = 0; i < rows.size(); ++i) {
        size.width = std::max(size.width, rows[i].cachedSize.width);
        size.height += rows[i].cachedSize.height;
        if (i + 1 < rows.size()) {
          size.height += spacing;
        }
      }
      return clampSize(size, outerConstraints);
    }

    LayoutConstraints rowConstraints(LayoutConstraints rowConstraintsIn) const {
      rowConstraintsIn.minWidth = 0.f;
      rowConstraintsIn.minHeight = 0.f;
      rowConstraintsIn.maxHeight = std::numeric_limits<float>::infinity();
      return rowConstraintsIn;
    }

    LayoutHints rowHints() const {
      LayoutHints childHints{};
      childHints.vStackCrossAlign = alignment;
      return childHints;
    }

    static Size clampSize(Size size, LayoutConstraints const& outerConstraints) {
      size.width = std::max(size.width, outerConstraints.minWidth);
      size.height = std::max(size.height, outerConstraints.minHeight);
      if (std::isfinite(outerConstraints.maxWidth)) {
        size.width = std::min(size.width, outerConstraints.maxWidth);
      }
      if (std::isfinite(outerConstraints.maxHeight)) {
        size.height = std::min(size.height, outerConstraints.maxHeight);
      }
      return size;
    }

    static bool constraintsEqual(LayoutConstraints const& lhs,
                                 LayoutConstraints const& rhs) {
      return lhs.maxWidth == rhs.maxWidth &&
             lhs.maxHeight == rhs.maxHeight &&
             lhs.minWidth == rhs.minWidth &&
             lhs.minHeight == rhs.minHeight;
    }

    static bool hintsEqual(LayoutHints const& lhs, LayoutHints const& rhs) {
      return lhs.hStackCrossAlign == rhs.hStackCrossAlign &&
             lhs.vStackCrossAlign == rhs.vStackCrossAlign &&
             lhs.zStackHorizontalAlign == rhs.zStackHorizontalAlign &&
             lhs.zStackVerticalAlign == rhs.zStackVerticalAlign;
    }
  };

  std::shared_ptr<State> state_;
};

template<typename T, typename KeyFn, typename Factory>
ForView<T, std::decay_t<KeyFn>, std::decay_t<Factory>>
For(Reactive::Signal<std::vector<T>> items, KeyFn&& keyFn, Factory&& factory,
    float spacing = 0.f, Alignment alignment = Alignment::Start,
    ForLayout layout = ForLayout::VerticalStack) {
  return ForView<T, std::decay_t<KeyFn>, std::decay_t<Factory>>{
      std::move(items), std::forward<KeyFn>(keyFn), std::forward<Factory>(factory),
      spacing, alignment, layout};
}

} // namespace lambda
