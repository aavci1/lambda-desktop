#include "UI/ViewLayout/OverlayLayout.hpp"

#include "Layout/LayoutHelpers.hpp"

#include <algorithm>
#include <cmath>

namespace lambdaui::layout {

namespace {

float resolveCrossAlignedX(Size win, Rect const& anchor, Rect contentBounds,
                           OverlayConfig::CrossAlignment alignment) {
  float const centeredX =
      (anchor.x + anchor.width * 0.5f) - (contentBounds.x + contentBounds.width * 0.5f);
  float const startAlignedX = anchor.x - contentBounds.x;
  float const endAlignedX =
      (anchor.x + anchor.width) - (contentBounds.x + contentBounds.width);

  auto fitsHorizontally = [win, width = contentBounds.width](float x) {
    return x >= 0.f && (x + width) <= win.width;
  };

  switch (alignment) {
  case OverlayConfig::CrossAlignment::Center:
    return centeredX;
  case OverlayConfig::CrossAlignment::Start:
    return startAlignedX;
  case OverlayConfig::CrossAlignment::End:
    return endAlignedX;
  case OverlayConfig::CrossAlignment::PreferStart:
    if (fitsHorizontally(startAlignedX)) {
      return startAlignedX;
    }
    if (fitsHorizontally(endAlignedX)) {
      return endAlignedX;
    }
    return startAlignedX;
  case OverlayConfig::CrossAlignment::PreferEnd:
    if (fitsHorizontally(endAlignedX)) {
      return endAlignedX;
    }
    if (fitsHorizontally(startAlignedX)) {
      return startAlignedX;
    }
    return endAlignedX;
  }
  return centeredX;
}

Rect adjustedAnchor(OverlayConfig const& cfg) {
  Rect anchor = *cfg.anchor;
  if (cfg.anchorMaxHeight && anchor.height > *cfg.anchorMaxHeight) {
    anchor.height = *cfg.anchorMaxHeight;
  }
  EdgeInsets const outsets = cfg.anchorOutsets;
  anchor.x -= outsets.left;
  anchor.y -= outsets.top;
  anchor.width += outsets.left + outsets.right;
  anchor.height += outsets.top + outsets.bottom;
  anchor.width = std::max(0.f, anchor.width);
  anchor.height = std::max(0.f, anchor.height);
  return anchor;
}

} // namespace

LayoutConstraints innerConstraintsForPopoverContent(PopoverCalloutShape const& value,
                                                    LayoutConstraints constraints) {
  if (value.maxSize) {
    if (std::isfinite(value.maxSize->width) && value.maxSize->width > 0.f) {
      constraints.maxWidth = std::min(constraints.maxWidth, value.maxSize->width);
    }
    if (std::isfinite(value.maxSize->height) && value.maxSize->height > 0.f) {
      constraints.maxHeight = std::min(constraints.maxHeight, value.maxSize->height);
    }
  }

  float const pad = value.padding;
  float const arrowDepth = PopoverCalloutShape::kArrowH;
  float availableWidth = constraints.maxWidth;
  float availableHeight = constraints.maxHeight;
  if (std::isfinite(availableWidth)) {
    availableWidth -= 2.f * pad;
  }
  if (std::isfinite(availableHeight)) {
    availableHeight -= 2.f * pad;
  }

  PopoverPlacement const placement = value.placement.evaluate();
  if (value.arrow) {
    switch (placement) {
    case PopoverPlacement::Below:
    case PopoverPlacement::Above:
      if (std::isfinite(availableHeight)) {
        availableHeight -= arrowDepth;
      }
      break;
    case PopoverPlacement::End:
    case PopoverPlacement::Start:
      if (std::isfinite(availableWidth)) {
        availableWidth -= arrowDepth;
      }
      break;
    }
  }

  constraints.maxWidth = std::max(0.f, availableWidth);
  constraints.maxHeight = std::max(0.f, availableHeight);
  clampLayoutMinToMax(constraints);
  return constraints;
}

PopoverCalloutLayout layoutPopoverCallout(PopoverCalloutShape const& value, Size contentSize,
                                          LayoutConstraints const& constraints) {
  PopoverCalloutLayout layout{};
  layout.contentSize = contentSize;
  layout.contentConstraints = innerConstraintsForPopoverContent(value, constraints);

  float const pad = std::max(0.f, value.padding);
  float const arrowWidth = value.arrow ? PopoverCalloutShape::kArrowW : 0.f;
  float const arrowDepth = value.arrow ? PopoverCalloutShape::kArrowH : 0.f;
  float const cardWidth = contentSize.width + 2.f * pad;
  float const cardHeight = contentSize.height + 2.f * pad;

  PopoverPlacement const placement = value.placement.evaluate();
  switch (placement) {
  case PopoverPlacement::Below:
    layout.totalSize = Size{cardWidth, cardHeight + arrowDepth};
    layout.cardRect = Rect{0.f, arrowDepth, cardWidth, cardHeight};
    layout.contentOrigin = Point{pad, arrowDepth + pad};
    break;
  case PopoverPlacement::Above:
    layout.totalSize = Size{cardWidth, cardHeight + arrowDepth};
    layout.cardRect = Rect{0.f, 0.f, cardWidth, cardHeight};
    layout.contentOrigin = Point{pad, pad};
    break;
  case PopoverPlacement::End: {
    float const totalHeight = std::max(cardHeight, arrowWidth);
    float const cardY = std::max(0.f, (totalHeight - cardHeight) * 0.5f);
    layout.totalSize = Size{cardWidth + arrowDepth, totalHeight};
    layout.cardRect = Rect{arrowDepth, cardY, cardWidth, cardHeight};
    layout.contentOrigin = Point{arrowDepth + pad, cardY + pad};
    break;
  }
  case PopoverPlacement::Start: {
    float const totalHeight = std::max(cardHeight, arrowWidth);
    float const cardY = std::max(0.f, (totalHeight - cardHeight) * 0.5f);
    layout.totalSize = Size{cardWidth + arrowDepth, totalHeight};
    layout.cardRect = Rect{0.f, cardY, cardWidth, cardHeight};
    layout.contentOrigin = Point{pad, cardY + pad};
    break;
  }
  }

  layout.chromePath = buildPopoverCalloutPath(placement, value.cornerRadius, value.arrow,
                                              arrowWidth, arrowDepth, layout.cardRect,
                                              layout.totalSize);
  return layout;
}

Rect resolveOverlayFrame(Size win, OverlayConfig const& cfg, Rect contentBounds) {
  if (!cfg.anchor.has_value()) {
    float const x = std::clamp((win.width - contentBounds.width) * 0.5f, 0.f,
                               std::max(0.f, win.width - contentBounds.width));
    float const y = std::clamp((win.height - contentBounds.height) * 0.5f, 0.f,
                               std::max(0.f, win.height - contentBounds.height));
    return Rect{x, y, contentBounds.width, contentBounds.height};
  }

  Rect const anchor = adjustedAnchor(cfg);
  float const centerY = anchor.y + anchor.height * 0.5f;
  float const centerLocalY = contentBounds.y + contentBounds.height * 0.5f;
  float const tipTopLocalY = contentBounds.y;
  float const tipBottomLocalY = contentBounds.y + contentBounds.height;

  float x = 0.f;
  float y = 0.f;
  switch (cfg.placement) {
  case OverlayConfig::Placement::Below:
    x = resolveCrossAlignedX(win, anchor, contentBounds, cfg.crossAlignment);
    y = anchor.y + anchor.height - tipTopLocalY;
    break;
  case OverlayConfig::Placement::Above:
    x = resolveCrossAlignedX(win, anchor, contentBounds, cfg.crossAlignment);
    y = anchor.y - tipBottomLocalY;
    break;
  case OverlayConfig::Placement::End:
    x = anchor.x + anchor.width;
    y = centerY - centerLocalY;
    break;
  case OverlayConfig::Placement::Start:
    x = anchor.x - contentBounds.width;
    y = centerY - centerLocalY;
    break;
  }

  x += cfg.offset.x;
  y += cfg.offset.y;
  x = std::clamp(x, 0.f, std::max(0.f, win.width - contentBounds.width));
  y = std::clamp(y, 0.f, std::max(0.f, win.height - contentBounds.height));
  return Rect{x, y, contentBounds.width, contentBounds.height};
}

} // namespace lambdaui::layout
