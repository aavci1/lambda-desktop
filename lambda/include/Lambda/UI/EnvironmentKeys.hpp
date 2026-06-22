#pragma once

#include <Lambda/UI/Environment.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/WindowChrome.hpp>

namespace lambda {

LAMBDA_DEFINE_ENVIRONMENT_KEY(ThemeKey, Theme, Theme::light());
LAMBDA_DEFINE_ENVIRONMENT_KEY(WindowChromeMetricsKey, WindowChromeMetrics, WindowChromeMetrics{});

} // namespace lambda
