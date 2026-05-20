#pragma once

#include <Flux/UI/Environment.hpp>
#include <Flux/UI/Theme.hpp>
#include <Flux/UI/WindowChrome.hpp>

namespace flux {

FLUX_DEFINE_ENVIRONMENT_KEY(ThemeKey, Theme, Theme::light());
FLUX_DEFINE_ENVIRONMENT_KEY(WindowChromeMetricsKey, WindowChromeMetrics, WindowChromeMetrics{});

} // namespace flux
