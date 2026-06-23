#pragma once

#include "Shell/ShellModels.hpp"
#include "Shell/UI/LambdaShellTypes.hpp"

#include <Lambda/Core/Color.hpp>
#include <Lambda/UI/Element.hpp>
#include <Lambda/UI/Views/Views.hpp>
#include <Lambda/UI/Window.hpp>

namespace lambda_shell {

/// Visual chrome drawn by the compositor for layer-shell surfaces in production.
/// The preview app draws these locally so shell UI can be tested without Wayland.
namespace shell_preview {

using lambdaui::LayerShellChromeOptions;
using lambdaui::LayerShellChromeStyle;

inline LayerShellChromeOptions defaultDockChrome() {
  DockMaterialConfig const material{};
  LayerShellChromeOptions chrome{};
  chrome.style = LayerShellChromeStyle::BlurPanelBorder;
  chrome.cornerRadius = lambdaui::CornerRadius{static_cast<float>(kDockCornerRadius)};
  chrome.glass.blurRadius = material.blurRadius;
  chrome.glass.opacity = material.opacity;
  chrome.glass.baseColor = material.baseColor;
  chrome.glass.tintColor = material.tintColor;
  chrome.glass.borderColor = material.borderColor;
  return chrome;
}

inline lambdaui::Color chromeTintFill(LayerShellChromeOptions const& chrome) {
  lambdaui::Color tint = chrome.glass.tintColor;
  tint.a *= chrome.glass.opacity;
  return tint;
}

inline lambdaui::Color chromeBaseFill(LayerShellChromeOptions const& chrome) {
  lambdaui::Color base = chrome.glass.baseColor;
  base.a *= chrome.glass.opacity;
  return base;
}

inline lambdaui::Element backdropLayer(float width,
                                   float height,
                                   LayerShellChromeOptions const& chrome,
                                   lambdaui::CornerRadius corners = {}) {
  return lambdaui::ZStack{
      .horizontalAlignment = lambdaui::Alignment::Stretch,
      .verticalAlignment = lambdaui::Alignment::Stretch,
      .children = lambdaui::children(
          lambdaui::BackdropBlur{
              .radius = chrome.glass.blurRadius,
              .tint = chromeBaseFill(chrome),
              .corners = corners,
          }.size(width, height),
          lambdaui::Rectangle{}
              .size(width, height)
              .fill(chromeTintFill(chrome))
              .cornerRadius(corners)),
  }.size(width, height);
}

inline lambdaui::Element wrapDock(lambdaui::Element content, float width, float height) {
  LayerShellChromeOptions const chrome = defaultDockChrome();
  return lambdaui::ZStack{
      .horizontalAlignment = lambdaui::Alignment::Stretch,
      .verticalAlignment = lambdaui::Alignment::Stretch,
      .children = lambdaui::children(
          backdropLayer(width, height, chrome, chrome.cornerRadius),
          lambdaui::Rectangle{}
              .size(width, height)
              .cornerRadius(chrome.cornerRadius)
              .stroke(lambdaui::StrokeStyle::solid(chrome.glass.borderColor, 1.f)),
          std::move(content)),
  }.size(width, height);
}

} // namespace shell_preview
} // namespace lambda_shell
