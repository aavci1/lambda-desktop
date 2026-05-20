#pragma once

#include "Compositor/Chrome/ChromeConfig.hpp"

#include <Flux/Core/Geometry.hpp>

#include <algorithm>

namespace flux::compositor {

struct ChromeControlsMetrics {
  float titleBarHeight = 0.f;
  float controlsWidth = 0.f;
  float buttonSize = 0.f;
  float buttonRadius = 0.f;
  float buttonGap = 0.f;
  float insetRight = 0.f;
  float insetTop = 0.f;
};

struct ChromeControlRects {
  Rect controls{};
  Rect closeButton{};
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
  float const buttonGap = std::max(0.f, static_cast<float>(chrome.buttonGap) * scale);
  float const insetRight = std::max(0.f, static_cast<float>(chrome.controlsInsetRight) * scale);
  float const minControlsWidth = insetRight * 2.f + buttonSize * 2.f + buttonGap;
  float const controlsWidth =
      std::max(static_cast<float>(chrome.controlsWidth) * scale, minControlsWidth);
  return ChromeControlsMetrics{
      .titleBarHeight = height,
      .controlsWidth = controlsWidth,
      .buttonSize = buttonSize,
      .buttonRadius = std::min(std::max(0.f, chrome.buttonRadius * scale), buttonSize * 0.5f),
      .buttonGap = buttonGap,
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
  float const buttonTop = frameTop + metrics.insetTop;
  float const closeRight = frameLeft + frameWidth - metrics.insetRight;
  float const closeLeft = closeRight - metrics.buttonSize;
  float const minimizeRight = closeLeft - metrics.buttonGap;
  float const minimizeLeft = minimizeRight - metrics.buttonSize;
  return ChromeControlRects{
      .controls = Rect::sharp(controlsLeft, frameTop, metrics.controlsWidth, metrics.titleBarHeight),
      .closeButton = Rect::sharp(closeLeft, buttonTop, metrics.buttonSize, metrics.buttonSize),
      .minimizeButton = Rect::sharp(minimizeLeft, buttonTop, metrics.buttonSize, metrics.buttonSize),
  };
}

} // namespace flux::compositor
