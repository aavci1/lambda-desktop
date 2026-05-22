#pragma once

#include "Compositor/Chrome/ChromeConfig.hpp"

#include <Flux/Core/Geometry.hpp>

#include <algorithm>

namespace flux::compositor {

struct ChromeControlsMetrics {
  float titleBarHeight = 0.f;
  float controlsWidth = 0.f;
  float controlWidth = 0.f;
  float buttonSize = 0.f;
  float buttonRadius = 0.f;
  float insetRight = 0.f;
  float insetTop = 0.f;
};

struct ChromeControlRects {
  Rect controls{};
  Rect closeButton{};
  Rect maximizeButton{};
  Rect minimizeButton{};
};

inline ChromeControlsMetrics chromeControlsMetrics(ChromeConfig const& chrome, float titleBarHeight) {
  float const height = std::max(0.f, titleBarHeight);
  float const scale = height > 0.f ? height / 28.f : 1.f;
  float const scaledButton = std::max(0.f, static_cast<float>(chrome.buttonSize) * scale);
  float const scaledTopInset = std::max(0.f, static_cast<float>(chrome.controlsInsetTop) * scale);
  float const buttonFromInset = std::max(0.f, height - scaledTopInset * 2.f);
  float const maxButton = std::max(0.f, height - 2.f);
  float const buttonSize = std::min({scaledButton, buttonFromInset, maxButton});
  float const insetRight = std::max(0.f, static_cast<float>(chrome.controlsInsetRight) * scale);
  float const minControlsWidth = height * 3.f;
  float const controlsWidth =
      std::max(static_cast<float>(chrome.controlsWidth) * scale, minControlsWidth);
  float const controlWidth = controlsWidth / 3.f;
  return ChromeControlsMetrics{
      .titleBarHeight = height,
      .controlsWidth = controlsWidth,
      .controlWidth = controlWidth,
      .buttonSize = buttonSize,
      .buttonRadius = std::min(std::max(0.f, chrome.buttonRadius * scale), buttonSize * 0.5f),
      .insetRight = insetRight,
      .insetTop = std::max(0.f, (height - buttonSize) * 0.5f),
  };
}

inline ChromeControlsMetrics chromeControlsMetrics(ChromeConfig const& chrome) {
  return chromeControlsMetrics(chrome, static_cast<float>(chrome.titleBarHeight));
}

inline ChromeControlRects chromeControlRects(ChromeConfig const& chrome,
                                             float frameLeft,
                                             float frameTop,
                                             float frameWidth,
                                             float titleBarHeight) {
  ChromeControlsMetrics const metrics = chromeControlsMetrics(chrome, titleBarHeight);
  float const controlsLeft = frameLeft + std::max(0.f, frameWidth - metrics.controlsWidth);
  float const closeLeft = controlsLeft + metrics.controlWidth * 2.f;
  float const maximizeLeft = controlsLeft + metrics.controlWidth;
  return ChromeControlRects{
      .controls = Rect::sharp(controlsLeft, frameTop, metrics.controlsWidth, metrics.titleBarHeight),
      .closeButton = Rect::sharp(closeLeft, frameTop, metrics.controlWidth, metrics.titleBarHeight),
      .maximizeButton = Rect::sharp(maximizeLeft, frameTop, metrics.controlWidth, metrics.titleBarHeight),
      .minimizeButton = Rect::sharp(controlsLeft, frameTop, metrics.controlWidth, metrics.titleBarHeight),
  };
}

} // namespace flux::compositor
