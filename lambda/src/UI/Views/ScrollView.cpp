#include <Lambda/UI/Views/ScrollView.hpp>

#include <Lambda/Reactive/Animation.hpp>
#include <Lambda/Reactive/Effect.hpp>
#include <Lambda/SceneGraph/SceneNode.hpp>
#include <Lambda/UI/InteractionData.hpp>
#include <Lambda/SceneGraph/RectNode.hpp>
#include <Lambda/UI/Detail/MountPosition.hpp>
#include <Lambda/UI/MeasureContext.hpp>
#include <Lambda/UI/Theme.hpp>

#include "UI/ViewLayout/ScrollLayout.hpp"
#include "UI/ViewLayout/ContainerScope.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace lambdaui {

namespace {

constexpr float kScrollIndicatorIdleBeforeFadeOutSeconds = 0.85f;

LayoutConstraints fixedConstraints(Size size) {
  return LayoutConstraints{
      .maxWidth = std::max(0.f, size.width),
      .maxHeight = std::max(0.f, size.height),
      .minWidth = std::max(0.f, size.width),
      .minHeight = std::max(0.f, size.height),
  };
}

Size viewportFromConstraints(ScrollAxis axis, Size contentSize, LayoutConstraints const& constraints) {
  return layout::resolveMeasuredScrollViewSize(axis, contentSize, constraints);
}

bool fixedFiniteWidth(LayoutConstraints const& constraints) {
  return std::isfinite(constraints.maxWidth) && constraints.maxWidth > 0.f &&
         constraints.minWidth >= constraints.maxWidth - 0.01f;
}

bool fixedFiniteHeight(LayoutConstraints const& constraints) {
  return std::isfinite(constraints.maxHeight) && constraints.maxHeight > 0.f &&
         constraints.minHeight >= constraints.maxHeight - 0.01f;
}

Size scrollViewportHintForConstraints(ScrollAxis axis, LayoutConstraints const& constraints, Size viewportHint) {
  switch (axis) {
  case ScrollAxis::Vertical:
    if (std::isfinite(constraints.maxWidth) && constraints.maxWidth > 0.f) {
      viewportHint.width = constraints.maxWidth;
    }
    if (fixedFiniteHeight(constraints)) {
      viewportHint.height = constraints.maxHeight;
    }
    break;
  case ScrollAxis::Horizontal:
    if (std::isfinite(constraints.maxHeight) && constraints.maxHeight > 0.f) {
      viewportHint.height = constraints.maxHeight;
    }
    if (fixedFiniteWidth(constraints)) {
      viewportHint.width = constraints.maxWidth;
    }
    break;
  case ScrollAxis::Both:
    if (fixedFiniteWidth(constraints)) {
      viewportHint.width = constraints.maxWidth;
    }
    if (fixedFiniteHeight(constraints)) {
      viewportHint.height = constraints.maxHeight;
    }
    break;
  }
  return viewportHint;
}

bool sameLayoutScalar(float a, float b) noexcept {
  if (a == b) {
    return true;
  }
  if (!std::isfinite(a) || !std::isfinite(b)) {
    return false;
  }
  return std::fabs(a - b) <= 0.01f;
}

bool sameLayoutConstraints(LayoutConstraints const& a, LayoutConstraints const& b) noexcept {
  return sameLayoutScalar(a.maxWidth, b.maxWidth) &&
         sameLayoutScalar(a.maxHeight, b.maxHeight) &&
         sameLayoutScalar(a.minWidth, b.minWidth) &&
         sameLayoutScalar(a.minHeight, b.minHeight);
}

Size resolvedScrollContentSize(ScrollAxis axis, Size contentFromState,
                              std::vector<Size> const& measuredChildSizes) {
  Size const measured = layout::scrollContentSize(axis, measuredChildSizes);
  if (contentFromState.width <= 0.f && contentFromState.height <= 0.f) {
    return measured;
  }
  return Size{std::max(contentFromState.width, measured.width),
              std::max(contentFromState.height, measured.height)};
}

Size relayoutChildAndStoreConstraints(std::unique_ptr<scenegraph::SceneNode>& child,
                                      LayoutConstraints const& constraints) {
  if (!child) {
    return {};
  }
  (void)child->relayout(constraints);
  child->setLayoutConstraints(constraints);
  return child->size();
}

Size measureChild(Element const& child, MeasureContext& ctx, LayoutConstraints const& constraints,
                  TextSystem& textSystem) {
  ctx.pushConstraints(constraints, LayoutHints{});
  Size size = child.measure(ctx, constraints, LayoutHints{}, textSystem);
  ctx.popConstraints();
  return size;
}

Color scrollIndicatorColor(EnvironmentBinding const& environment) {
  Theme const theme = environment.value<ThemeKey>();
  return Color{
      theme.secondaryLabelColor.r,
      theme.secondaryLabelColor.g,
      theme.secondaryLabelColor.b,
      0.55f,
  };
}

void setIndicatorBounds(scenegraph::RectNode& node, layout::ScrollIndicatorMetrics const& metrics) {
  node.setBounds(Rect{metrics.x, metrics.y, metrics.width, metrics.height});
  node.setCornerRadius(CornerRadius{std::min(metrics.width, metrics.height) * 0.5f});
}

struct ScrollIndicatorPair {
  layout::ScrollIndicatorMetrics vertical{};
  layout::ScrollIndicatorMetrics horizontal{};
};

ScrollIndicatorPair makeIndicators(ScrollAxis axis, Point offset, Size viewport, Size content) {
  bool const canScrollVertical = axis == ScrollAxis::Vertical || axis == ScrollAxis::Both;
  bool const canScrollHorizontal = axis == ScrollAxis::Horizontal || axis == ScrollAxis::Both;
  ScrollIndicatorPair out{};
  if (canScrollVertical) {
    out.vertical = layout::makeVerticalIndicator(offset, viewport, content, false);
  }
  if (canScrollHorizontal) {
    out.horizontal = layout::makeHorizontalIndicator(offset, viewport, content,
                                                     out.vertical.visible());
  }
  if (canScrollVertical) {
    out.vertical = layout::makeVerticalIndicator(offset, viewport, content,
                                                 out.horizontal.visible());
  }
  return out;
}

struct ScrollViewPlan {
  Size viewport{};
  LayoutConstraints childConstraints{};
  std::vector<Size> childSizes{};
  layout::ScrollContentLayout contentLayout{};
};

template<typename MeasureChildFn>
ScrollViewPlan planScrollViewLayout(ScrollView const& scrollView, LayoutConstraints const& constraints,
                                    Size viewportHint, Point scrollOffset, MeasureChildFn&& measureChild) {
  ScrollViewPlan plan{};
  plan.viewport = scrollViewportHintForConstraints(scrollView.axis, constraints, viewportHint);

  if (plan.viewport.width <= 0.f || plan.viewport.height <= 0.f) {
    LayoutConstraints const premeasureConstraints =
        layout::scrollChildConstraints(scrollView.axis, constraints, plan.viewport);
    plan.childSizes.reserve(scrollView.children.size());
    for (Element const& child : scrollView.children) {
      plan.childSizes.push_back(measureChild(child, premeasureConstraints));
    }
    plan.viewport = viewportFromConstraints(
        scrollView.axis, layout::scrollContentSize(scrollView.axis, plan.childSizes), constraints);
  }

  plan.childConstraints = layout::scrollChildConstraints(scrollView.axis, constraints, plan.viewport);
  if (plan.childSizes.empty()) {
    plan.childSizes.reserve(scrollView.children.size());
    for (Element const& child : scrollView.children) {
      plan.childSizes.push_back(measureChild(child, plan.childConstraints));
    }
  }
  plan.contentLayout =
      layout::layoutScrollContent(scrollView.axis, plan.viewport, scrollOffset, plan.childSizes);
  return plan;
}

} // namespace

Point clampScrollOffset(ScrollAxis axis, Point offset, Size const& viewport, Size const& content) {
  return layout::clampScrollOffset(axis, offset, viewport, content);
}

Size OffsetView::measure(MeasureContext& ctx, LayoutConstraints const& constraints,
                         LayoutHints const&, TextSystem& textSystem) const {
  LayoutConstraints childConstraints = layout::scrollChildConstraints(axis, constraints, *viewportSize);
  std::vector<Size> childSizes;
  childSizes.reserve(children.size());
  for (Element const& child : children) {
    childSizes.push_back(measureChild(child, ctx, childConstraints, textSystem));
  }
  return layout::scrollContentSize(axis, childSizes);
}

std::unique_ptr<scenegraph::SceneNode> OffsetView::mount(MountContext& ctx) const {
  LayoutConstraints childConstraints = layout::scrollChildConstraints(axis, ctx.constraints(), *viewportSize);
  std::vector<Size> childSizes;
  childSizes.reserve(children.size());
  for (Element const& child : children) {
    childSizes.push_back(measureChild(child, ctx.measureContext(), childConstraints, ctx.textSystem()));
  }
  Size const content = layout::scrollContentSize(axis, childSizes);
  contentSize = content;

  auto group = std::make_unique<scenegraph::SceneNode>(
      Rect{0.f, 0.f, std::max(0.f, content.width), std::max(0.f, content.height)});
  layout::ScrollContentLayout const layoutPlan =
      layout::layoutScrollContent(axis, *viewportSize, offset, childSizes);
  for (std::size_t index = 0; index < children.size(); ++index) {
    MountContext childCtx = ctx.childWithSharedScope(fixedConstraints(layoutPlan.slots[index].assignedSize), {});
    auto childNode = children[index].mount(childCtx);
    if (childNode) {
      detail::setLayoutPosition(*childNode, layoutPlan.slots[index].origin);
      group->appendChild(std::move(childNode));
    }
  }
  return group;
}

Size ScrollView::measure(MeasureContext& ctx, LayoutConstraints const& constraints,
                         LayoutHints const&, TextSystem& textSystem) const {
  ContainerMeasureScope scope(ctx);
  ScrollViewPlan const plan =
      planScrollViewLayout(*this, constraints, Size{}, Point{},
                           [&](Element const& child, LayoutConstraints const& childConstraints) {
                             return measureChild(child, ctx, childConstraints, textSystem);
                           });
  return plan.viewport;
}

std::unique_ptr<scenegraph::SceneNode> ScrollView::mount(MountContext& ctx) const {
  ScrollViewPlan const plan =
      planScrollViewLayout(*this, ctx.constraints(), Size{}, *scrollOffset,
                           [&](Element const& child, LayoutConstraints const& childConstraints) {
                             return measureChild(child, ctx.measureContext(), childConstraints, ctx.textSystem());
                           });
  Size const viewport = plan.viewport;
  Size const content = plan.contentLayout.contentSize;
  viewportSize = viewport;
  contentSize = content;
  scrollOffset = plan.contentLayout.clampedOffset;

  auto viewportNode = std::make_unique<scenegraph::RectNode>(Rect{0.f, 0.f, viewport.width, viewport.height});
  viewportNode->setClipsContents(true);

  auto contentGroup = std::make_unique<scenegraph::SceneNode>(
      Rect{0.f, 0.f, std::max(0.f, content.width), std::max(0.f, content.height)});

  layout::ScrollContentLayout initialLayout =
      layout::layoutScrollContent(axis, viewport, Point{}, plan.childSizes);
  for (std::size_t index = 0; index < children.size(); ++index) {
    MountContext childCtx = ctx.childWithSharedScope(fixedConstraints(initialLayout.slots[index].assignedSize), {});
    auto childNode = children[index].mount(childCtx);
    if (childNode) {
      detail::setLayoutPosition(*childNode, initialLayout.slots[index].origin);
      contentGroup->appendChild(std::move(childNode));
    }
  }

  auto* rawContentGroup = contentGroup.get();
  rawContentGroup->setPosition(Point{-plan.contentLayout.clampedOffset.x,
                                     -plan.contentLayout.clampedOffset.y});
  rawContentGroup->setLayoutConstraints(plan.childConstraints);
  viewportNode->appendChild(std::move(contentGroup));
  auto* rawViewportNode = viewportNode.get();

  ScrollIndicatorPair const initialIndicators =
      makeIndicators(axis, plan.contentLayout.clampedOffset, viewport, content);
  Theme const theme = ctx.environmentBinding().value<ThemeKey>();
  float const indicatorFadeInDuration = theme.durationFast;
  float const indicatorFadeOutDuration = theme.durationMedium;
  Color const indicatorColor = scrollIndicatorColor(ctx.environmentBinding());
  auto indicatorOverlay =
      std::make_unique<scenegraph::RectNode>(Rect{0.f, 0.f, viewport.width, viewport.height});
  indicatorOverlay->setOpacity(0.f);
  scenegraph::RectNode* rawIndicatorOverlay = indicatorOverlay.get();
  scenegraph::RectNode* rawVerticalIndicator = nullptr;
  scenegraph::RectNode* rawHorizontalIndicator = nullptr;
  if (axis == ScrollAxis::Vertical || axis == ScrollAxis::Both) {
    auto indicator = std::make_unique<scenegraph::RectNode>(
        Rect{initialIndicators.vertical.x, initialIndicators.vertical.y,
             initialIndicators.vertical.width, initialIndicators.vertical.height},
        FillStyle::solid(indicatorColor), StrokeStyle::none(),
        CornerRadius{initialIndicators.vertical.width * 0.5f});
    rawVerticalIndicator = indicator.get();
    indicatorOverlay->appendChild(std::move(indicator));
  }
  if (axis == ScrollAxis::Horizontal || axis == ScrollAxis::Both) {
    auto indicator = std::make_unique<scenegraph::RectNode>(
        Rect{initialIndicators.horizontal.x, initialIndicators.horizontal.y,
             initialIndicators.horizontal.width, initialIndicators.horizontal.height},
        FillStyle::solid(indicatorColor), StrokeStyle::none(),
        CornerRadius{initialIndicators.horizontal.height * 0.5f});
    rawHorizontalIndicator = indicator.get();
    indicatorOverlay->appendChild(std::move(indicator));
  }
  viewportNode->appendChild(std::move(indicatorOverlay));
  Animated<float> indicatorOpacity{0.f};
  Reactive::Signal<float> indicatorOpacitySignal = indicatorOpacity.signal();
  Animated<float> scrollIdleProgress{1.f};
  Reactive::Signal<float> scrollIdleProgressSignal = scrollIdleProgress.signal();
  Reactive::SmallFn<void()> indicatorRedraw = ctx.redrawCallback();
  auto indicatorFadeTargetVisible = std::make_shared<bool>(false);
  Reactive::withOwner(ctx.owner(), [rawIndicatorOverlay, indicatorOpacitySignal,
                                    scrollIdleProgressSignal, indicatorOpacity, scrollIdleProgress,
                                    indicatorFadeTargetVisible,
                                    fadeOutDuration = indicatorFadeOutDuration,
                                    indicatorRedraw]() mutable {
    Reactive::Effect([rawIndicatorOverlay, indicatorOpacitySignal, indicatorRedraw]() mutable {
      if (rawIndicatorOverlay) {
        rawIndicatorOverlay->setOpacity(std::clamp(indicatorOpacitySignal.get(), 0.f, 1.f));
      }
      if (indicatorRedraw) {
        indicatorRedraw();
      }
    });
    Reactive::Effect([scrollIdleProgressSignal, indicatorOpacity, indicatorFadeTargetVisible,
                      fadeOutDuration, indicatorRedraw]() mutable {
      if (scrollIdleProgressSignal.get() < 0.999f || !*indicatorFadeTargetVisible) {
        return;
      }
      *indicatorFadeTargetVisible = false;
      indicatorOpacity.set(0.f, Transition::ease(fadeOutDuration));
      if (indicatorRedraw) {
        indicatorRedraw();
      }
    });
    Reactive::onCleanup([indicatorOpacity, scrollIdleProgress] {
      indicatorOpacity.stop();
      scrollIdleProgress.stop();
    });
  });

  Signal<Point> offsetState = scrollOffset;
  Signal<Size> viewportState = viewportSize;
  Signal<Size> contentState = contentSize;
  ScrollAxis const scrollAxis = axis;
  auto childSizes = std::make_shared<std::vector<Size>>(plan.childSizes);
  Reactive::SmallFn<void()> requestRedraw = ctx.redrawCallback();
  Reactive::withOwner(ctx.owner(), [rawContentGroup, rawVerticalIndicator, rawHorizontalIndicator,
                                    offsetState, viewportState, contentState, scrollAxis, childSizes,
                                    requestRedraw = std::move(requestRedraw)]() mutable {
    Reactive::Effect([rawContentGroup, rawVerticalIndicator, rawHorizontalIndicator, offsetState,
                      viewportState, contentState, scrollAxis, childSizes,
                      requestRedraw]() mutable {
      (void)contentState();
      Size const content =
          resolvedScrollContentSize(scrollAxis, contentState.get(), *childSizes);
      Point const offset = offsetState.get();
      Point const clamped =
          layout::clampScrollOffset(scrollAxis, offset, viewportState.get(), content);
      rawContentGroup->setPosition(Point{-clamped.x, -clamped.y});
      rawContentGroup->setSize(content);
      ScrollIndicatorPair const indicators =
          makeIndicators(scrollAxis, clamped, viewportState.get(), content);
      if (rawVerticalIndicator) {
        setIndicatorBounds(*rawVerticalIndicator, indicators.vertical);
      }
      if (rawHorizontalIndicator) {
        setIndicatorBounds(*rawHorizontalIndicator, indicators.horizontal);
      }
      if (requestRedraw) {
        requestRedraw();
      }
    });
  });

  auto interaction = std::make_unique<InteractionData>();
  Reactive::SmallFn<void()> const scrollRedraw = ctx.redrawCallback();
  auto revealIndicators = [indicatorOpacity, scrollIdleProgress,
                           indicatorFadeTargetVisible,
                           fadeInDuration = indicatorFadeInDuration,
                           scrollRedraw] {
    if (!*indicatorFadeTargetVisible) {
      *indicatorFadeTargetVisible = true;
      indicatorOpacity.set(1.f, Transition::ease(fadeInDuration));
    }
    scrollIdleProgress.set(0.f);
    scrollIdleProgress.set(1.f, Transition::linear(kScrollIndicatorIdleBeforeFadeOutSeconds));
    if (scrollRedraw) {
      scrollRedraw();
    }
  };

  auto dragging = std::make_shared<bool>(false);
  auto downPoint = std::make_shared<Point>();
  bool const dragScroll = dragScrollEnabled;
  interaction->onTapWithModifiers = onTap;
  interaction->onPointerDown = [dragScroll, dragging, downPoint, offsetState](Point point, MouseButton button) {
    if (!dragScroll || button != MouseButton::Left) {
      return;
    }
    *dragging = true;
    *downPoint = Point{point.x + offsetState.peek().x, point.y + offsetState.peek().y};
  };
  interaction->onPointerUp = [dragging](Point, MouseButton button) {
    if (button == MouseButton::Left) {
      *dragging = false;
    }
  };
  interaction->onPointerMove = [dragScroll, dragging, downPoint, offsetState, viewportState,
                                contentState, childSizes, scrollAxis, revealIndicators](Point point) {
    if (!dragScroll || !*dragging) {
      return;
    }
    Point const next{downPoint->x - point.x, downPoint->y - point.y};
    Size const content =
        resolvedScrollContentSize(scrollAxis, contentState.peek(), *childSizes);
    offsetState = layout::clampScrollOffset(scrollAxis, next, viewportState.get(), content);
    revealIndicators();
  };
  interaction->onScroll = [offsetState, viewportState, contentState, childSizes, scrollAxis,
                         revealIndicators](Vec2 delta) {
    Point next = offsetState.get();
    if (scrollAxis == ScrollAxis::Vertical || scrollAxis == ScrollAxis::Both) {
      next.y -= delta.y;
    }
    if (scrollAxis == ScrollAxis::Horizontal || scrollAxis == ScrollAxis::Both) {
      next.x -= delta.x;
    }
    Size const content =
        resolvedScrollContentSize(scrollAxis, contentState.peek(), *childSizes);
    offsetState = layout::clampScrollOffset(scrollAxis, next, viewportState.get(), content);
    revealIndicators();
  };
  viewportNode->setInteraction(std::move(interaction));
  ScrollView scrollView = *this;
  rawViewportNode->setLayoutConstraints(ctx.constraints());
  rawContentGroup->setRelayout([rawContentGroup, rawVerticalIndicator, rawHorizontalIndicator,
                                offsetState, viewportState, contentState, childSizes,
                                scrollAxis, requestRedraw = ctx.redrawCallback()](
                                   LayoutConstraints const& constraints) mutable {
    auto mountedChildren = rawContentGroup->children();
    childSizes->clear();
    childSizes->reserve(mountedChildren.size());
    for (std::unique_ptr<scenegraph::SceneNode>& child : mountedChildren) {
      childSizes->push_back(relayoutChildAndStoreConstraints(child, constraints));
    }
    layout::ScrollContentLayout const contentLayout =
        layout::layoutScrollContent(scrollAxis, viewportState.peek(), offsetState.peek(), *childSizes);
    layout::ScrollContentLayout const childLayout =
        layout::layoutScrollContent(scrollAxis, viewportState.peek(), Point{}, *childSizes);
    for (std::size_t i = 0; i < mountedChildren.size() && i < childLayout.slots.size(); ++i) {
      if (mountedChildren[i]) {
        mountedChildren[i]->setPosition(childLayout.slots[i].origin);
      }
    }
    rawContentGroup->setSize(contentLayout.contentSize);
    rawContentGroup->setPosition(Point{-contentLayout.clampedOffset.x,
                                       -contentLayout.clampedOffset.y});
    contentState = contentLayout.contentSize;
    offsetState = contentLayout.clampedOffset;
    ScrollIndicatorPair const indicators =
        makeIndicators(scrollAxis, contentLayout.clampedOffset, viewportState.peek(),
                       contentLayout.contentSize);
    if (rawVerticalIndicator) {
      setIndicatorBounds(*rawVerticalIndicator, indicators.vertical);
    }
    if (rawHorizontalIndicator) {
      setIndicatorBounds(*rawHorizontalIndicator, indicators.horizontal);
    }
    if (requestRedraw) {
      requestRedraw();
    }
  });
  rawViewportNode->setRelayout([rawViewportNode, rawContentGroup, rawIndicatorOverlay,
                                rawVerticalIndicator, rawHorizontalIndicator,
                                scrollView = std::move(scrollView), offsetState,
                                viewportState, contentState, childSizes](
                                   LayoutConstraints const& constraints) mutable {
    auto mountedChildren = rawContentGroup->children();
    ScrollViewPlan plan{};
    plan.viewport =
        scrollViewportHintForConstraints(scrollView.axis, constraints, viewportState.peek());
    LayoutConstraints premeasureConstraints =
        layout::scrollChildConstraints(scrollView.axis, constraints, plan.viewport);
    childSizes->clear();
    childSizes->reserve(mountedChildren.size());
    for (std::unique_ptr<scenegraph::SceneNode>& child : mountedChildren) {
      childSizes->push_back(relayoutChildAndStoreConstraints(child, premeasureConstraints));
    }
    if (plan.viewport.width <= 0.f || plan.viewport.height <= 0.f) {
      plan.viewport = viewportFromConstraints(
          scrollView.axis, layout::scrollContentSize(scrollView.axis, *childSizes), constraints);
    }
    plan.childConstraints = layout::scrollChildConstraints(scrollView.axis, constraints, plan.viewport);
    rawContentGroup->setLayoutConstraints(plan.childConstraints);
    if (!sameLayoutConstraints(premeasureConstraints, plan.childConstraints)) {
      for (std::size_t i = 0; i < mountedChildren.size(); ++i) {
        if (mountedChildren[i]) {
          (*childSizes)[i] =
              relayoutChildAndStoreConstraints(mountedChildren[i], plan.childConstraints);
        }
      }
    }
    plan.contentLayout =
        layout::layoutScrollContent(scrollView.axis, plan.viewport, offsetState.peek(), *childSizes);
    layout::ScrollContentLayout const childLayout =
        layout::layoutScrollContent(scrollView.axis, plan.viewport, Point{}, *childSizes);
    for (std::size_t i = 0; i < mountedChildren.size() && i < childLayout.slots.size(); ++i) {
      if (mountedChildren[i]) {
        mountedChildren[i]->setPosition(childLayout.slots[i].origin);
      }
    }
    rawViewportNode->setSize(plan.viewport);
    rawContentGroup->setSize(plan.contentLayout.contentSize);
    rawContentGroup->setPosition(Point{-plan.contentLayout.clampedOffset.x,
                                       -plan.contentLayout.clampedOffset.y});
    if (rawIndicatorOverlay) {
      rawIndicatorOverlay->setSize(plan.viewport);
    }
    ScrollIndicatorPair const indicators =
        makeIndicators(scrollView.axis, plan.contentLayout.clampedOffset, plan.viewport,
                       plan.contentLayout.contentSize);
    if (rawVerticalIndicator) {
      setIndicatorBounds(*rawVerticalIndicator, indicators.vertical);
    }
    if (rawHorizontalIndicator) {
      setIndicatorBounds(*rawHorizontalIndicator, indicators.horizontal);
    }
    viewportState = plan.viewport;
    contentState = plan.contentLayout.contentSize;
    offsetState = plan.contentLayout.clampedOffset;
  });

  return viewportNode;
}

} // namespace lambdaui
