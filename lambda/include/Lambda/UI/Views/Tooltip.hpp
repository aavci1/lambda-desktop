#pragma once

/// \file Lambda/UI/Views/Tooltip.hpp
///
/// Part of the Lambda public API.


#include <Lambda/UI/Views/PopoverPlacement.hpp>

#include <string>

namespace lambdaui {

struct TooltipConfig {
  std::string text;
  PopoverPlacement placement = PopoverPlacement::Above;
  int delayMs = 0;
};

void useTooltip(TooltipConfig const& config);
void useTooltip(std::string text);

} // namespace lambdaui
