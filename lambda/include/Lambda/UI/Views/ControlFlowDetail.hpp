#pragma once

#include <Lambda/Reactive/Scope.hpp>
#include <Lambda/Reactive/SmallFn.hpp>
#include <Lambda/SceneGraph/SceneNode.hpp>
#include <Lambda/SceneGraph/RectNode.hpp>
#include <Lambda/UI/Element.hpp>
#include <Lambda/UI/Hooks.hpp>
#include <Lambda/UI/MeasureContext.hpp>
#include <Lambda/UI/MountContext.hpp>
#include <Lambda/UI/Detail/MountPosition.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace lambdaui::detail {

inline float controlFiniteOrZero(float value) {
  return std::isfinite(value) ? std::max(0.f, value) : 0.f;
}

inline Size controlAssignedSize(LayoutConstraints const& constraints) {
  Size size{};
  size.width = std::isfinite(constraints.maxWidth)
      ? std::max(constraints.minWidth, constraints.maxWidth)
      : std::max(0.f, constraints.minWidth);
  size.height = std::isfinite(constraints.maxHeight)
      ? std::max(constraints.minHeight, constraints.maxHeight)
      : std::max(0.f, constraints.minHeight);
  return size;
}

inline LayoutConstraints controlFixedConstraints(Size size) {
  return LayoutConstraints{
      .maxWidth = std::max(0.f, size.width),
      .maxHeight = std::max(0.f, size.height),
      .minWidth = std::max(0.f, size.width),
      .minHeight = std::max(0.f, size.height),
  };
}

inline bool controlIsPositiveSize(Size size) {
  constexpr float epsilon = 0.01f;
  return size.width > epsilon && size.height > epsilon;
}

inline bool controlIsFixedAxis(float minValue, float maxValue) {
  constexpr float epsilon = 0.01f;
  return std::isfinite(maxValue) && std::abs(maxValue - minValue) <= epsilon;
}

inline std::optional<Size> controlAssignedSlot(LayoutConstraints const& constraints) {
  if (!controlIsFixedAxis(constraints.minWidth, constraints.maxWidth) ||
      !controlIsFixedAxis(constraints.minHeight, constraints.maxHeight)) {
    return std::nullopt;
  }
  Size size = controlAssignedSize(constraints);
  if (!controlIsPositiveSize(size)) {
    return std::nullopt;
  }
  return size;
}

inline void controlRelayoutSingleChildInSlot(scenegraph::SceneNode& group,
                                             std::optional<Size> const& assignedSlot) {
  if (!assignedSlot) {
    return;
  }
  auto children = group.children();
  if (!children.empty() && children.front()) {
    (void)children.front()->relayout(controlFixedConstraints(*assignedSlot), false);
  }
}

inline Size controlMeasureElement(Element const& element, EnvironmentBinding const& environment,
                                  TextSystem& textSystem, LayoutConstraints const& constraints,
                                  LayoutHints const& hints) {
  MeasureContext measureContext{textSystem, environment};
  measureContext.pushConstraints(constraints, hints);
  Size measured = element.measure(measureContext, constraints, hints, textSystem);
  measureContext.popConstraints();
  return measured;
}

inline std::unique_ptr<scenegraph::SceneNode>
controlMountElement(Element const& element, Reactive::Scope& owner,
                    EnvironmentBinding const& environment,
                    TextSystem& textSystem, LayoutConstraints const& constraints,
                    LayoutHints const& hints,
                    Reactive::SmallFn<void()> const& requestRedraw) {
  MeasureContext measureContext{textSystem, environment};
  MountContext mountContext{owner, textSystem, measureContext, constraints,
                            hints, requestRedraw, environment};
  return Reactive::withOwner(owner, [&] {
    CurrentMountContextScope const currentMountContext{mountContext};
    HookInteractionSignalScope const interactionScope{owner};
    return element.mount(mountContext);
  });
}

inline void controlLayoutVertical(scenegraph::SceneNode& group, Size frameSize, float spacing) {
  float y = 0.f;
  float width = frameSize.width;
  auto children = group.children();
  for (std::size_t i = 0; i < children.size(); ++i) {
    auto& child = children[i];
    child->setPosition(Point{0.f, y});
    Size const childSize = child->size();
    width = std::max(width, childSize.width);
    y += childSize.height;
    if (i + 1 < children.size()) {
      y += spacing;
    }
  }
  group.setSize(Size{controlFiniteOrZero(width), controlFiniteOrZero(std::max(frameSize.height, y))});
}

inline void controlLayoutHorizontal(scenegraph::SceneNode& group, Size frameSize, float spacing) {
  float x = 0.f;
  float height = frameSize.height;
  auto children = group.children();
  for (std::size_t i = 0; i < children.size(); ++i) {
    auto& child = children[i];
    child->setPosition(Point{x, 0.f});
    Size const childSize = child->size();
    height = std::max(height, childSize.height);
    x += childSize.width;
    if (i + 1 < children.size()) {
      x += spacing;
    }
  }
  group.setSize(Size{controlFiniteOrZero(std::max(frameSize.width, x)), controlFiniteOrZero(height)});
}

inline void controlLayoutSingle(scenegraph::SceneNode& group, Size frameSize) {
  Size size = frameSize;
  auto children = group.children();
  if (!children.empty()) {
    Size const childSize = children.front()->size();
    size.width = std::max(size.width, childSize.width);
    size.height = std::max(size.height, childSize.height);
  }
  group.setSize(Size{controlFiniteOrZero(size.width), controlFiniteOrZero(size.height)});
}

inline bool controlClipsContents(scenegraph::SceneNode const& node) {
  return node.kind() == scenegraph::SceneNodeKind::Rect &&
         static_cast<scenegraph::RectNode const&>(node).clipsContents();
}

inline Size controlChildExtents(scenegraph::SceneNode const& parent) {
  Size extents{};
  for (auto const& child : parent.children()) {
    if (!child) {
      continue;
    }
    Point const position = child->position();
    Size const size = child->size();
    extents.width = std::max(extents.width, position.x + size.width);
    extents.height = std::max(extents.height, position.y + size.height);
  }
  return extents;
}

inline void controlLayoutOverlay(scenegraph::SceneNode& group, Size frameSize) {
  Size const extents = controlChildExtents(group);
  group.setSize(Size{controlFiniteOrZero(std::max(frameSize.width, extents.width)),
                       controlFiniteOrZero(std::max(frameSize.height, extents.height))});
}

inline Size controlStackExtents(scenegraph::SceneNode const& parent,
                                scenegraph::LayoutFlow flow) {
  constexpr float epsilon = 0.01f;
  Size extents{};
  for (auto const& child : parent.children()) {
    if (!child) {
      continue;
    }
    Point const position = child->position();
    Size const size = child->size();
    if (flow == scenegraph::LayoutFlow::VerticalStack) {
      extents.width = std::max(extents.width, position.x + size.width);
      if (size.height > epsilon) {
        extents.height = std::max(extents.height, position.y + size.height);
      }
    } else if (flow == scenegraph::LayoutFlow::HorizontalStack) {
      if (size.width > epsilon) {
        extents.width = std::max(extents.width, position.x + size.width);
      }
      extents.height = std::max(extents.height, position.y + size.height);
    }
  }
  return extents;
}

inline void controlPropagateLayoutChange(scenegraph::SceneNode& node, Size oldSize) {
  constexpr float epsilon = 0.01f;
  scenegraph::SceneNode* child = &node;
  Size childOldSize = oldSize;
  Size childNewSize = child->size();

  while (scenegraph::SceneNode* parent = child->parent()) {
    if (controlClipsContents(*parent)) {
      parent->invalidateSubtreeLayout();
      (void)parent->relayoutStoredConstraints();
      break;
    }

    Size const parentOldSize = parent->size();
    Point const childPosition = child->position();
    Size parentNewSize = parentOldSize;

    switch (parent->layoutFlow()) {
    case scenegraph::LayoutFlow::VerticalStack: {
      float delta = childNewSize.height - childOldSize.height;
      if (childOldSize.height <= epsilon && childNewSize.height > epsilon) {
        delta += parent->layoutSpacing();
      } else if (childOldSize.height > epsilon && childNewSize.height <= epsilon) {
        delta -= parent->layoutSpacing();
      }
      if (std::abs(delta) > epsilon) {
        bool afterChild = false;
        for (auto& sibling : parent->children()) {
          if (sibling.get() == child) {
            afterChild = true;
            continue;
          }
          if (afterChild && sibling->position().y >= childPosition.y - epsilon) {
            Point position = sibling->position();
            position.y += delta;
            sibling->setPosition(position);
          }
        }
      }
      Size const extents = controlStackExtents(*parent, parent->layoutFlow());
      parentNewSize.width = std::max(parentNewSize.width, extents.width);
      parentNewSize.height = extents.height;
      break;
    }
    case scenegraph::LayoutFlow::HorizontalStack: {
      float delta = childNewSize.width - childOldSize.width;
      if (childOldSize.width <= epsilon && childNewSize.width > epsilon) {
        delta += parent->layoutSpacing();
      } else if (childOldSize.width > epsilon && childNewSize.width <= epsilon) {
        delta -= parent->layoutSpacing();
      }
      if (std::abs(delta) > epsilon) {
        bool afterChild = false;
        for (auto& sibling : parent->children()) {
          if (sibling.get() == child) {
            afterChild = true;
            continue;
          }
          if (afterChild && sibling->position().x >= childPosition.x - epsilon) {
            Point position = sibling->position();
            position.x += delta;
            sibling->setPosition(position);
          }
        }
      }
      Size const extents = controlStackExtents(*parent, parent->layoutFlow());
      parentNewSize.width = extents.width;
      parentNewSize.height = std::max(parentNewSize.height, extents.height);
      break;
    }
    case scenegraph::LayoutFlow::None: {
      float const trailingRight = std::max(0.f, parentOldSize.width -
          (childPosition.x + childOldSize.width));
      float const trailingBottom = std::max(0.f, parentOldSize.height -
          (childPosition.y + childOldSize.height));
      Size const extents = controlChildExtents(*parent);
      parentNewSize.width = std::max(extents.width,
          childPosition.x + childNewSize.width + trailingRight);
      parentNewSize.height = std::max(extents.height,
          childPosition.y + childNewSize.height + trailingBottom);
      break;
    }
    }

    if (!parent->parent()) {
      break;
    }
    parent->setSize(Size{controlFiniteOrZero(parentNewSize.width),
                         controlFiniteOrZero(parentNewSize.height)});
    child = parent;
    childOldSize = parentOldSize;
    childNewSize = parent->size();
  }
}

template<typename Factory>
Element invokeElementFactory(Factory& factory) {
  auto result = std::invoke(factory);
  return Element{std::move(result)};
}

template<typename Factory, typename T>
Element invokeForFactory(Factory& factory, T const& item,
                         Reactive::Signal<std::size_t> const& index) {
  if constexpr (std::is_invocable_v<Factory&, T const&, Reactive::Signal<std::size_t>>) {
    auto result = std::invoke(factory, item, index);
    return Element{std::move(result)};
  } else {
    static_assert(std::is_invocable_v<Factory&, T const&>,
                  "For row factory must accept (T const&) or (T const&, Signal<size_t>).");
    auto result = std::invoke(factory, item);
    return Element{std::move(result)};
  }
}

template<typename Condition>
bool readCondition(Condition& condition) {
  if constexpr (std::is_invocable_v<Condition&>) {
    return static_cast<bool>(std::invoke(condition));
  } else {
    return static_cast<bool>(condition);
  }
}

template<typename Condition>
bool readConditionCopy(Condition condition) {
  return readCondition(condition);
}

template<typename Selector>
auto readSelector(Selector& selector) {
  if constexpr (std::is_invocable_v<Selector&>) {
    return std::invoke(selector);
  } else {
    return selector;
  }
}

template<typename Selector>
auto readSelectorCopy(Selector selector) {
  return readSelector(selector);
}

} // namespace lambdaui::detail
