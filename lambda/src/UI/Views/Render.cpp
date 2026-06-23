#include <Lambda/UI/Views/Render.hpp>

#include <Lambda/Reactive/Effect.hpp>
#include <Lambda/SceneGraph/RenderNode.hpp>
#include <Lambda/UI/MeasureContext.hpp>
#include <Lambda/UI/MountContext.hpp>

#include <algorithm>
#include <cmath>

namespace lambdaui {

namespace {

class RenderDrawObserver final : public Reactive::detail::Computation {
public:
  RenderDrawObserver(scenegraph::RenderNode& node, Reactive::SmallFn<void()> requestRedraw)
      : node_(&node)
      , requestRedraw_(std::move(requestRedraw)) {}

  void draw(scenegraph::RenderNode::DrawFunction const& drawFn, Canvas& canvas, Rect frame) {
    beginTrackingRun();
    Reactive::detail::clearFlag(flags, Reactive::detail::Dirty);
    Reactive::detail::clearFlag(flags, Reactive::detail::Pending);
    {
      Reactive::detail::ObserverContext context(this);
      drawFn(canvas, frame);
    }
    sweepStaleSources();
  }

  bool hasSources() const noexcept {
    return sources != nullptr;
  }

  void run() override {}

  bool updateIfNeeded() override {
    return false;
  }

  void onDirty() override {
    invalidateNode();
  }

  void onPending() override {
    invalidateNode();
  }

private:
  void invalidateNode() {
    if (node_) {
      node_->invalidate();
    }
    if (requestRedraw_) {
      requestRedraw_();
    }
  }

  scenegraph::RenderNode* node_ = nullptr;
  Reactive::SmallFn<void()> requestRedraw_;
};

struct RenderDrawState {
  std::shared_ptr<RenderDrawObserver> observer;
};

Size assignedSize(LayoutConstraints const& constraints, Size measured) {
  Size size = measured;
  if (std::isfinite(constraints.maxWidth) && constraints.maxWidth > 0.f) {
    size.width = constraints.maxWidth;
  }
  if (std::isfinite(constraints.maxHeight) && constraints.maxHeight > 0.f) {
    size.height = constraints.maxHeight;
  }
  size.width = std::max(size.width, constraints.minWidth);
  size.height = std::max(size.height, constraints.minHeight);
  return Size{std::max(0.f, size.width), std::max(0.f, size.height)};
}

} // namespace

Size Render::measure(MeasureContext& ctx, LayoutConstraints const& constraints,
                     LayoutHints const& hints, TextSystem&) const {
  ctx.advanceChildSlot();
  return measure(constraints, hints);
}

std::unique_ptr<scenegraph::SceneNode> Render::mount(MountContext& ctx) const {
  Size const measured = measure(ctx.constraints(), ctx.hints());
  Size const size = assignedSize(ctx.constraints(), measured);
  auto node = std::make_unique<scenegraph::RenderNode>(
      Rect{0.f, 0.f, size.width, size.height});
  auto* rawNode = node.get();
  auto state = std::make_shared<RenderDrawState>();
  auto requestRedraw = ctx.redrawCallback();
  auto userDraw = draw;
  Reactive::withOwner(ctx.owner(), [&] {
    state->observer = std::make_shared<RenderDrawObserver>(*rawNode, requestRedraw);
    Reactive::detail::ownNode(state->observer);
  });
  rawNode->setDraw([rawNode, state = std::move(state), userDraw = std::move(userDraw)](
                       Canvas& canvas, Rect frame) {
    if (!userDraw) {
      return;
    }
    if (rawNode->purity() == scenegraph::RenderNode::Purity::Pure) {
      userDraw(canvas, frame);
      return;
    }
    if (rawNode->purity() == scenegraph::RenderNode::Purity::Live) {
      state->observer->draw(userDraw, canvas, frame);
      return;
    }
    state->observer->draw(userDraw, canvas, frame);
    rawNode->setPurity(state->observer->hasSources()
                           ? scenegraph::RenderNode::Purity::Live
                           : scenegraph::RenderNode::Purity::Pure);
  });
  auto measure = measureFn;
  LayoutHints hints = ctx.hints();
  rawNode->setLayoutConstraints(ctx.constraints());
  rawNode->setRelayout([rawNode, measure = std::move(measure),
                        hints](LayoutConstraints const& constraints) mutable {
    Size measured{};
    if (measure) {
      measured = measure(constraints, hints);
    }
    Size const nextSize = assignedSize(constraints, measured);
    rawNode->setSize(nextSize);
  });
  return node;
}

} // namespace lambdaui
