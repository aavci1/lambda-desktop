#pragma once

#include "Shell/ShellModel.hpp"
#include "Shell/UI/LambdaCommandLauncher.hpp"
#include "Shell/UI/LambdaDock.hpp"
#include "Shell/UI/LambdaTopBar.hpp"

#include <Flux/UI/Element.hpp>
#include <Flux/UI/Hooks.hpp>
#include <Flux/UI/KeyCodes.hpp>
#include <Flux/UI/Views/Views.hpp>

#include <functional>

namespace lambda_shell {

struct ShellTopBarView {
  ShellModel const& model;
  std::function<void()> onOpenLauncher;

  flux::Element body() const {
    return flux::Element{LambdaTopBar{TopBarProps{
        .title = model.activeTitle(),
        .timeText = model.timeText(),
        .system = model.systemStatus(),
        .onOpenLauncher = onOpenLauncher,
    }}};
  }
};

struct ShellDockView {
  ShellModel const& model;
  std::function<void()> onOpenLauncher;
  std::function<void(DockItem const&)> onActivateItem;

  flux::Element body() const {
    int const width = dockWidth(model.dockItems());
    return flux::Element{LambdaDock{DockProps{
        .items = model.dockItems(),
        .hoverIndex = -1,
        .width = width,
        .onOpenLauncher = onOpenLauncher,
        .onActivateItem = onActivateItem,
    }}};
  }
};

struct ShellLauncherView {
  ShellModel const& model;
  std::function<void(DockItem const&)> onActivateResult;
  std::function<void()> onDismiss;
  std::function<void(flux::KeyCode, flux::Modifiers)> onKeyDown;

  flux::Element body() const {
    if (!model.launcherOpen()) {
      return flux::Rectangle{}.size(1.f, 1.f).fill(flux::Colors::transparent);
    }
    flux::Rect const bounds = flux::useBounds();
    int const width = static_cast<int>(std::max(1.f, bounds.width));
    int const height = static_cast<int>(std::max(1.f, bounds.height));
    auto results = model.launcherResults();
    int const highlighted =
        results.empty() ? 0 : std::clamp(model.highlighted(), 0, static_cast<int>(results.size()) - 1);
    auto root = flux::Element{LambdaCommandLauncher{CommandLauncherProps{
        .items = model.dockItems(),
        .query = model.query(),
        .highlighted = highlighted,
        .width = width,
        .height = height,
        .open = true,
        .onActivateResult = onActivateResult,
        .onDismiss = onDismiss,
    }}}.size(static_cast<float>(width), static_cast<float>(height));
    if (onKeyDown) {
      root = std::move(root).onKeyDown(std::move(onKeyDown));
    }
    return root;
  }
};

} // namespace lambda_shell
