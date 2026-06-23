#pragma once

#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Graphics/Path.hpp>
#include <Lambda/UI/Overlay.hpp>
#include <Lambda/UI/Views/PopoverCalloutPath.hpp>
#include <Lambda/UI/Views/PopoverCalloutShape.hpp>

namespace lambdaui::layout {

struct PopoverCalloutLayout {
  Size totalSize{};
  Size contentSize{};
  Rect cardRect{};
  Point contentOrigin{};
  LayoutConstraints contentConstraints{};
  Path chromePath{};
};

LayoutConstraints innerConstraintsForPopoverContent(PopoverCalloutShape const& value,
                                                    LayoutConstraints constraints);

PopoverCalloutLayout layoutPopoverCallout(PopoverCalloutShape const& value, Size contentSize,
                                          LayoutConstraints const& constraints);

Rect resolveOverlayFrame(Size windowSize, OverlayConfig const& config, Rect contentBounds);

} // namespace lambdaui::layout
