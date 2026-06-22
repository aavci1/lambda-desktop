#pragma once

/// \file Lambda/UI/OverlaySurfaceHelpers.hpp
///
/// Shared theme resolution for floating card surfaces (\ref Alert card, \ref Popover body / backdrop).
/// Not a substitute for \ref Element modifiers — keeps overlay colour math in one place.

#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Core/Color.hpp>
#include <Lambda/Graphics/Styles.hpp>

namespace lambda {

struct Theme;

struct ResolvedAlertCardColors {
  Color cardFill;
  Color cardStroke;
  CornerRadius cornerRadius;
};

ResolvedAlertCardColors resolveAlertCardColors(Color cardColor, Color cardStrokeColor, float cornerRadius,
                                               Theme const& theme);

Color resolveAlertBackdropColor(Color backdropColor, Theme const& theme);

struct ResolvedPopoverCardBody {
  Color background;
  Color border;
  float borderWidth;
  CornerRadius cornerRadius;
  float contentPadding;
};

ResolvedPopoverCardBody resolvePopoverCardBody(Color backgroundColor, Color borderColor, float borderWidth,
                                               float cornerRadius, float contentPadding,
                                               Theme const& theme);

Color resolvePopoverBackdropColor(Color backdropColor, Theme const& theme);

} // namespace lambda
