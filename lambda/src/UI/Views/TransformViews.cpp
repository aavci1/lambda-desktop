#include <Lambda/UI/Views/ScaleAroundCenter.hpp>

#include <Lambda/Reactive/Effect.hpp>
#include <Lambda/SceneGraph/SceneNode.hpp>
#include <Lambda/UI/MeasureContext.hpp>

#include "SceneGraph/SceneBounds.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace lambdaui {

namespace {

LayoutConstraints fixedConstraints(Size size) {
  return LayoutConstraints{
      .maxWidth = std::max(0.f, size.width),
      .maxHeight = std::max(0.f, size.height),
      .minWidth = std::max(0.f, size.width),
      .minHeight = std::max(0.f, size.height),
  };
}

void includeVisualBounds(scenegraph::SceneNode& group) {
  Rect const visual = scenegraph::detail::subtreeLocalVisualBounds(group);
  Size size = group.size();
  size.width = std::max(size.width, visual.width);
  size.height = std::max(size.height, visual.height);
  group.setSize(size);
}

float sanitizedScale(float value) noexcept {
  if (!std::isfinite(value)) {
    return 1.f;
  }
  return std::max(0.f, value);
}

std::pair<float, float> resolveScale(Reactive::Bindable<float> const& scale,
                                     Reactive::Bindable<float> const& scaleX,
                                     Reactive::Bindable<float> const& scaleY) {
  float const base = sanitizedScale(scale.evaluate());
  return {base * sanitizedScale(scaleX.evaluate()), base * sanitizedScale(scaleY.evaluate())};
}

} // namespace

Size ScaleAroundCenter::measure(MeasureContext& ctx, LayoutConstraints const& constraints,
                                LayoutHints const& hints, TextSystem& textSystem) const {
  ctx.pushConstraints(constraints, hints);
  Size size = child.measure(ctx, constraints, hints, textSystem);
  ctx.popConstraints();
  return size;
}

std::unique_ptr<scenegraph::SceneNode> ScaleAroundCenter::mount(MountContext& ctx) const {
  Size const measured = measure(ctx.measureContext(), ctx.constraints(), ctx.hints(), ctx.textSystem());
  auto group = std::make_unique<scenegraph::SceneNode>(
      Rect{0.f, 0.f, std::max(0.f, measured.width), std::max(0.f, measured.height)});

  MountContext childCtx = ctx.childWithSharedScope(fixedConstraints(measured), ctx.hints());
  auto childNode = child.mount(childCtx);
  if (!childNode) {
    return group;
  }

  auto* rawChild = childNode.get();
  auto frameSize = std::make_shared<Size>(measured);
  Reactive::Bindable<float> scaleBinding = scale;
  Reactive::Bindable<float> scaleXBinding = scaleX;
  Reactive::Bindable<float> scaleYBinding = scaleY;
  Reactive::SmallFn<void()> requestRedraw = ctx.redrawCallback();
  auto applyScale = [rawChild, frameSize](float sx, float sy) {
    Point const pivot{frameSize->width * 0.5f, frameSize->height * 0.5f};
    rawChild->setTransform(Mat3::translate(pivot) * Mat3::scale(sx, sy) *
                           Mat3::translate(Point{-pivot.x, -pivot.y}));
  };
  auto const initialScale = resolveScale(scaleBinding, scaleXBinding, scaleYBinding);
  applyScale(initialScale.first, initialScale.second);
  if (scaleBinding.isReactive() || scaleXBinding.isReactive() || scaleYBinding.isReactive()) {
    Reactive::Bindable<float> effectScaleBinding = scaleBinding;
    Reactive::Bindable<float> effectScaleXBinding = scaleXBinding;
    Reactive::Bindable<float> effectScaleYBinding = scaleYBinding;
    Reactive::withOwner(ctx.owner(), [scaleBinding = std::move(effectScaleBinding),
                                      scaleXBinding = std::move(effectScaleXBinding),
                                      scaleYBinding = std::move(effectScaleYBinding), applyScale,
                                      requestRedraw = std::move(requestRedraw)]() mutable {
      Reactive::Effect([scaleBinding, scaleXBinding, scaleYBinding, applyScale, requestRedraw]() mutable {
        auto const resolved = resolveScale(scaleBinding, scaleXBinding, scaleYBinding);
        applyScale(resolved.first, resolved.second);
        if (requestRedraw) {
          requestRedraw();
        }
      });
    });
  }

  group->appendChild(std::move(childNode));
  includeVisualBounds(*group);
  auto* rawGroup = group.get();
  rawGroup->setLayoutConstraints(ctx.constraints());
  rawGroup->setRelayout([rawGroup, rawChild, frameSize, applyScale, scaleBinding,
                         scaleXBinding, scaleYBinding](
                            LayoutConstraints const& constraints) mutable {
    if (rawChild) {
      rawChild->relayout(constraints);
      *frameSize = rawChild->size();
      rawChild->relayout(fixedConstraints(*frameSize), false);
      rawChild->setPosition(Point{});
    }
    rawGroup->setSize(*frameSize);
    auto const resolved = resolveScale(scaleBinding, scaleXBinding, scaleYBinding);
    applyScale(resolved.first, resolved.second);
    includeVisualBounds(*rawGroup);
  });
  return group;
}

} // namespace lambdaui
