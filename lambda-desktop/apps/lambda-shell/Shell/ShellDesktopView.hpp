#pragma once

#include "Shell/ShellModel.hpp"
#include "Shell/ShellPreviewChrome.hpp"
#include "Shell/ShellViews.hpp"

#include <Lambda/UI/Views/Views.hpp>

#include <functional>

namespace lambda_shell {

/// Single-window layout used by the cross-platform shell preview and design tooling.
struct ShellDesktopView {
  ShellModel& model;
  std::function<void()> onOpenLauncher;
  std::function<void(DockItem const&)> onActivateItem;
  std::function<void(DockItem const&)> onShowDockItemMenu;
  std::function<void(std::string const&, DockStatusAction)> onStatusAction;
  std::function<void(DockItem const&)> onActivateLauncherResult;
  std::function<void()> onDismissLauncher;
  std::function<void(lambdaui::KeyCode, lambdaui::Modifiers)> onLauncherKeyDown;
  float width = 960.f;
  float height = 620.f;

  lambdaui::Element body() const {
    auto const open = model.launcherOpenSignal();
    int const itemSize = model.dockItemSize();
    int const dockW = dockWidth(model.dockItems(), model.dockClockWidth(), itemSize);
    float const dockH = static_cast<float>(dockHeight(itemSize));
    float const dockX = (width - static_cast<float>(dockW)) * 0.5f;
    float const dockY = height - dockH - static_cast<float>(kDockBottom);

    std::vector<lambdaui::Element> layers;
    layers.push_back(lambdaui::Rectangle{}
                         .size(width, height)
                         .fill(lambdaui::FillStyle::linearGradient(
                             lambdaui::Color{0.05f, 0.07f, 0.12f, 1.f},
                             lambdaui::Color{0.14f, 0.20f, 0.33f, 1.f},
                             {0.f, 0.f},
                             {1.f, 1.f})));

    layers.push_back(shell_preview::wrapDock(
            lambdaui::Element{ShellDockView{
                model,
                onOpenLauncher,
                onActivateItem,
                onShowDockItemMenu,
                onStatusAction,
                false,
            }},
            static_cast<float>(dockW),
            dockH)
            .position(dockX, dockY));

    layers.push_back(lambdaui::Show(
        open,
        [this] {
          return lambdaui::Element{ShellLauncherView{
              model,
              onActivateLauncherResult,
              onDismissLauncher,
              onLauncherKeyDown,
          }}.position(0.f, 0.f).size(width, height);
        },
        [] {
          return lambdaui::Rectangle{}.size(0.f, 0.f);
        }));

    return lambdaui::ZStack{.children = std::move(layers)}.size(width, height);
  }
};

} // namespace lambda_shell
