#pragma once

/// \file Lambda/UI/Views/PopoverCalloutPath.hpp
///
/// Part of the Lambda public API.


#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Graphics/Path.hpp>
#include <Lambda/UI/Views/PopoverPlacement.hpp>

namespace lambdaui {

/// Single closed path: merged rounded card + callout triangle. Coordinates are in the popover
/// element's local space (0,0) top-left of the measured bounds.
///
/// \param cardRect Position/size of the card (rounded rect portion) in local space.
/// \param aw Base width of the triangle (horizontal for Below/Above; vertical span for End/Start).
/// \param ah Tip depth (vertical for Below/Above; horizontal depth for End/Start).
/// \param total Measured width/height of the callout (must match layout).
Path buildPopoverCalloutPath(PopoverPlacement placement, CornerRadius cornerRadius, bool arrow,
                             float aw, float ah, Rect cardRect, Size total);

} // namespace lambdaui
