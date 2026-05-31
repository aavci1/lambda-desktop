#pragma once

#include "Shell/UI/LambdaShellTypes.hpp"

#include <Lambda/Core/Color.hpp>
#include <Lambda/UI/Element.hpp>
#include <Lambda/UI/Views/Views.hpp>
#include <Lambda/UI/Window.hpp>

namespace lambda_shell {

/// Visual chrome drawn by the compositor for layer-shell surfaces in production.
/// The preview app draws these locally so shell UI can be tested without Wayland.
namespace shell_preview {

using lambda::LayerShellChromeOptions;
using lambda::LayerShellChromeStyle;

inline LayerShellChromeOptions defaultDockChrome() {
  LayerShellChromeOptions chrome{};
  chrome.style = LayerShellChromeStyle::BlurPanelBorder;
  return chrome;
}

inline lambda::Color chromeTintFill(LayerShellChromeOptions const& chrome) {
  lambda::Color tint = chrome.glass.tintColor;
  tint.a *= chrome.glass.opacity;
  return tint;
}

inline lambda::Color chromeBaseFill(LayerShellChromeOptions const& chrome) {
  lambda::Color base = chrome.glass.baseColor;
  base.a *= chrome.glass.opacity;
  return base;
}

inline lambda::Element backdropLayer(float width,
                                   float height,
                                   LayerShellChromeOptions const& chrome,
                                   lambda::CornerRadius corners = {}) {
  return lambda::ZStack{
      .horizontalAlignment = lambda::Alignment::Stretch,
      .verticalAlignment = lambda::Alignment::Stretch,
      .children = lambda::children(
          lambda::BackdropBlur{
              .radius = chrome.glass.blurRadius,
              .tint = chromeBaseFill(chrome),
              .corners = corners,
          }.size(width, height),
          lambda::Rectangle{}
              .size(width, height)
              .fill(chromeTintFill(chrome))
              .cornerRadius(corners)),
  }.size(width, height);
}

inline lambda::Element wrapDock(lambda::Element content, float width, float height) {
  LayerShellChromeOptions const chrome = defaultDockChrome();
  return lambda::ZStack{
      .horizontalAlignment = lambda::Alignment::Stretch,
      .verticalAlignment = lambda::Alignment::Stretch,
      .children = lambda::children(
          backdropLayer(width, height, chrome, chrome.cornerRadius),
          lambda::Rectangle{}
              .size(width, height)
              .cornerRadius(chrome.cornerRadius)
              .stroke(lambda::StrokeStyle::solid(chrome.glass.borderColor, 1.f)),
          std::move(content)),
  }.size(width, height);
}

} // namespace shell_preview
} // namespace lambda_shell
