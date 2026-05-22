#pragma once

#include "Shell/ShellModel.hpp"
#include "Shell/ShellViews.hpp"

#include <Flux/UI/Views/Views.hpp>

#include <functional>

namespace lambda_shell {

/// Single-window layout used by the cross-platform shell preview and design tooling.
struct ShellDesktopView {
  ShellModel& model;
  std::function<void()> onOpenLauncher;
  std::function<void(DockItem const&)> onActivateItem;
  std::function<void(DockItem const&)> onActivateLauncherResult;
  std::function<void()> onDismissLauncher;
  std::function<void(flux::KeyCode, flux::Modifiers)> onLauncherKeyDown;
  float width = 960.f;
  float height = 620.f;

  flux::Element body() const {
    auto const open = model.launcherOpenSignal();
    int const dockW = dockWidth(model.dockItems());
    float const dockX = (width - static_cast<float>(dockW)) * 0.5f;
    float const dockY = height - static_cast<float>(dockHeight()) - static_cast<float>(kDockBottom);

    std::vector<flux::Element> layers;
    layers.push_back(flux::Rectangle{}
                         .size(width, height)
                         .fill(flux::FillStyle::linearGradient(
                             flux::Color{0.05f, 0.07f, 0.12f, 1.f},
                             flux::Color{0.14f, 0.20f, 0.33f, 1.f},
                             {0.f, 0.f},
                             {1.f, 1.f})));
    layers.push_back(flux::Element{ShellTopBarView{model, onOpenLauncher}}
                         .size(width, static_cast<float>(kTopBarHeight))
                         .position(0.f, 0.f));
    layers.push_back(flux::Element{ShellDockView{model, onOpenLauncher, onActivateItem}}.position(dockX, dockY));

    layers.push_back(flux::Show(
        open,
        [this] {
          return flux::Element{ShellLauncherView{
              model,
              onActivateLauncherResult,
              onDismissLauncher,
              onLauncherKeyDown,
          }}.position(0.f, 0.f).size(width, height);
        },
        [] {
          return flux::Rectangle{}.size(0.f, 0.f);
        }));

    return flux::ZStack{.children = std::move(layers)}.size(width, height);
  }
};

} // namespace lambda_shell
