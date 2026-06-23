#include <Lambda/UI/Views/PopoverCalloutShape.hpp>

#include <Lambda/SceneGraph/SceneNode.hpp>
#include <Lambda/SceneGraph/PathNode.hpp>
#include <Lambda/UI/MeasureContext.hpp>
#include <Lambda/UI/MountContext.hpp>

#include "UI/ViewLayout/OverlayLayout.hpp"
#include "UI/ViewLayout/ContainerScope.hpp"

#include <algorithm>
#include <memory>

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

Size measureContent(Element const& content, MeasureContext& ctx,
                    LayoutConstraints const& constraints, LayoutHints const& hints,
                    TextSystem& textSystem) {
  ctx.pushConstraints(constraints, hints);
  Size size = content.measure(ctx, constraints, hints, textSystem);
  ctx.popConstraints();
  return size;
}

layout::PopoverCalloutLayout measureLayout(PopoverCalloutShape const& shape,
                                           MeasureContext& ctx,
                                           LayoutConstraints const& constraints,
                                           LayoutHints const& hints,
                                           TextSystem& textSystem) {
  LayoutConstraints const inner = layout::innerConstraintsForPopoverContent(shape, constraints);
  Size const contentSize = measureContent(shape.content, ctx, inner, hints, textSystem);
  return layout::layoutPopoverCallout(shape, contentSize, constraints);
}

} // namespace

Size PopoverCalloutShape::measure(MeasureContext& ctx, LayoutConstraints const& constraints,
                                  LayoutHints const& hints, TextSystem& textSystem) const {
  ContainerMeasureScope scope(ctx);
  layout::PopoverCalloutLayout const layout =
      measureLayout(*this, ctx, constraints, hints, textSystem);
  return layout.totalSize;
}

std::unique_ptr<scenegraph::SceneNode> PopoverCalloutShape::mount(MountContext& ctx) const {
  layout::PopoverCalloutLayout const layout =
      measureLayout(*this, ctx.measureContext(), ctx.constraints(), ctx.hints(), ctx.textSystem());

  auto group = std::make_unique<scenegraph::SceneNode>(
      Rect{0.f, 0.f, layout.totalSize.width, layout.totalSize.height});
  auto chrome = std::make_unique<scenegraph::PathNode>(
      Rect{0.f, 0.f, layout.totalSize.width, layout.totalSize.height},
      layout.chromePath,
      FillStyle::solid(backgroundColor),
      borderWidth <= 0.f || borderColor.a <= 0.001f
          ? StrokeStyle::none()
          : StrokeStyle::solid(borderColor, borderWidth));
  scenegraph::PathNode* rawChrome = chrome.get();
  group->appendChild(std::move(chrome));

  MountContext contentCtx = ctx.childWithSharedScope(fixedConstraints(layout.contentSize), {});
  std::unique_ptr<scenegraph::SceneNode> contentNode = content.mount(contentCtx);
  scenegraph::SceneNode* rawContent = contentNode.get();
  if (contentNode) {
    contentNode->relayout(fixedConstraints(layout.contentSize), false);
    contentNode->setPosition(layout.contentOrigin);
    group->appendChild(std::move(contentNode));
  }

  scenegraph::SceneNode* rawGroup = group.get();
  PopoverCalloutShape shape = *this;
  rawGroup->setLayoutConstraints(ctx.constraints());
  rawGroup->setRelayout([rawGroup, rawChrome, rawContent, shape](
                            LayoutConstraints const& constraints) mutable {
    LayoutConstraints const inner =
        layout::innerConstraintsForPopoverContent(shape, constraints);
    if (rawContent) {
      rawContent->relayout(inner);
    }
    Size const contentSize = rawContent ? rawContent->size() : Size{};
    layout::PopoverCalloutLayout const nextLayout =
        layout::layoutPopoverCallout(shape, contentSize, constraints);
    rawGroup->setSize(nextLayout.totalSize);
    if (rawChrome) {
      rawChrome->setBounds(Rect{0.f, 0.f, nextLayout.totalSize.width,
                                nextLayout.totalSize.height});
      rawChrome->setPath(nextLayout.chromePath);
    }
    if (rawContent) {
      rawContent->relayout(fixedConstraints(nextLayout.contentSize), false);
      rawContent->setPosition(nextLayout.contentOrigin);
    }
  });

  return group;
}

} // namespace lambdaui
