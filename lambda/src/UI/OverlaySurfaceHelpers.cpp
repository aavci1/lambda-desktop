#include <Lambda/UI/OverlaySurfaceHelpers.hpp>

#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Core/Color.hpp>
#include <Lambda/UI/Theme.hpp>

namespace lambdaui {

ResolvedAlertCardColors resolveAlertCardColors(Color cardColor, Color cardStrokeColor, float cornerRadius,
                                               Theme const& theme) {
  return ResolvedAlertCardColors{.cardFill = resolveColor(cardColor, theme.controlBackgroundColor, theme),
                                 .cardStroke = resolveColor(cardStrokeColor, theme.separatorColor, theme),
                                 .cornerRadius = CornerRadius{resolveFloat(cornerRadius, theme.radiusXLarge)}};
}

Color resolveAlertBackdropColor(Color backdropColor, Theme const& theme) {
  return resolveColor(backdropColor, theme.modalScrimColor, theme);
}

ResolvedPopoverCardBody resolvePopoverCardBody(Color backgroundColor, Color borderColor, float borderWidth,
                                               float cornerRadius, float contentPadding,
                                               Theme const& theme) {
  return ResolvedPopoverCardBody{
      .background = resolveColor(backgroundColor, theme.elevatedBackgroundColor, theme),
      .border = resolveColor(borderColor, theme.separatorColor, theme),
      .borderWidth = borderWidth,
      .cornerRadius = CornerRadius{resolveFloat(cornerRadius, theme.radiusLarge)},
      .contentPadding = resolveFloat(contentPadding, theme.space3),
  };
}

Color resolvePopoverBackdropColor(Color backdropColor, Theme const& theme) {
  return resolveColor(backdropColor, theme.popoverScrimColor, theme);
}

} // namespace lambdaui
