#pragma once

#include "Shell/ShellModel.hpp"
#include "Shell/UI/LambdaCommandLauncher.hpp"
#include "Shell/UI/LambdaDock.hpp"

#include <Lambda/UI/Element.hpp>
#include <Lambda/UI/KeyCodes.hpp>
#include <Lambda/UI/Views/Views.hpp>

#include <functional>

namespace lambda_shell {

struct ShellDockView {
  ShellModel& model;
  std::function<void()> onOpenLauncher;
  std::function<void(DockItem const&)> onActivateItem;
  std::function<void(DockItem const&)> onShowMenu;
  std::function<void(std::string const&, DockStatusAction)> onStatusAction;
  bool fullWidth = false;

  lambda::Element body() const {
    auto const items = model.dockItemsSignal();
    auto const timeText = model.timeTextSignal();
    auto const clockWidth = model.dockClockWidthSignal();
    auto const itemSize = model.dockItemSizeSignal();
    auto const systemStatus = model.systemStatusSignal();
    lambda::Reactive::Bindable<int> widthBinding{[items, clockWidth, itemSize] {
      return dockWidth(items(), clockWidth(), itemSize());
    }};
    return lambda::Element{LambdaDock{DockProps{
        .items = items,
        .timeText = timeText,
        .clockWidth = clockWidth,
        .itemSize = itemSize,
        .system = lambda::Reactive::Bindable<SystemStatus>{[systemStatus] { return systemStatus(); }},
        .hoverIndex = -1,
        .fullWidth = fullWidth,
        .width = widthBinding,
        .onOpenLauncher = onOpenLauncher,
        .onActivateItem = onActivateItem,
        .onShowMenu = onShowMenu,
        .onStatusAction = onStatusAction,
    }}};
  }
};

struct ShellDockMenuView {
  DockItem item;
  float surfaceWidth = static_cast<float>(kDockMenuSurfaceWidth);
  float surfaceHeight = static_cast<float>(kDockMenuSurfaceHeight);
  float menuX = 0.f;
  float menuY = 0.f;
  std::function<void(DockItem const&)> onNewWindow;
  std::function<void(DockItem const&)> onTogglePinned;
  std::function<void(DockItem const&)> onQuitItem;
  std::function<void()> onDismiss;

  lambda::Element body() const {
    return lambda::Element{LambdaDockMenu{DockMenuProps{
        .item = item,
        .surfaceWidth = surfaceWidth,
        .surfaceHeight = surfaceHeight,
        .menuX = menuX,
        .menuY = menuY,
        .onNewWindow = onNewWindow,
        .onTogglePinned = onTogglePinned,
        .onQuitItem = onQuitItem,
        .onDismiss = onDismiss,
    }}};
  }
};

struct ShellLauncherView {
  ShellModel& model;
  std::function<void(DockItem const&)> onActivateResult;
  std::function<void()> onDismiss;
  std::function<void(lambda::KeyCode, lambda::Modifiers)> onKeyDown;

  lambda::Element body() const {
    auto const results = model.launcherResultsSignal();
    auto const query = model.querySignal();
    auto const highlighted = model.highlightedSignal();

    auto const uiVisible = model.launcherUiVisibleSignal();
    auto const launcherWidth = model.launcherWidthSignal();
    auto const launcherHeight = model.launcherHeightSignal();

    auto buildLauncher = [this, results, query, highlighted, launcherWidth, launcherHeight] {
      lambda::Reactive::Bindable<float> widthBinding{
          [launcherWidth] { return std::max(1.f, launcherWidth()); }};
      lambda::Reactive::Bindable<float> heightBinding{
          [launcherHeight] { return std::max(1.f, launcherHeight()); }};
      lambda::Reactive::Bindable<int> widthIntBinding{
          [widthBinding] { return static_cast<int>(std::max(1.f, widthBinding.evaluate())); }};
      lambda::Reactive::Bindable<int> heightIntBinding{
          [heightBinding] { return static_cast<int>(std::max(1.f, heightBinding.evaluate())); }};
      lambda::Reactive::Bindable<int> clampedHighlight{[results, highlighted] {
        auto const items = results();
        if (items.empty()) return 0;
        return std::clamp(highlighted.evaluate(), 0, static_cast<int>(items.size()) - 1);
      }};

      auto root = lambda::Element{LambdaCommandLauncher{CommandLauncherProps{
          .results = results,
          .query = query,
          .highlighted = clampedHighlight,
          .width = widthIntBinding,
          .height = heightIntBinding,
          .onActivateResult = onActivateResult,
          .onDismiss = onDismiss,
      }}}
                        .width(widthBinding)
                        .height(heightBinding);
      if (onKeyDown) {
        root = std::move(root).onKeyDown(std::move(onKeyDown));
      }
      return root;
    };

    return lambda::Show(
        uiVisible,
        buildLauncher,
        [] {
          return lambda::Rectangle{}.size(1.f, 1.f).fill(lambda::Colors::transparent);
        });
  }
};

} // namespace lambda_shell
