#pragma once

#include "Shell/ShellModel.hpp"
#include "Shell/UI/LambdaCommandLauncher.hpp"
#include "Shell/UI/LambdaDock.hpp"
#include "Shell/UI/LambdaTopBar.hpp"

#include <Flux/UI/Element.hpp>
#include <Flux/UI/Hooks.hpp>
#include <Flux/UI/KeyCodes.hpp>
#include <Flux/UI/Views/Views.hpp>

#include <algorithm>
#include <functional>

namespace lambda_shell {

struct ShellTopBarView {
  ShellModel& model;
  std::function<void()> onOpenLauncher;

  flux::Element body() const {
    auto timeText = flux::useState(ShellModel::formatTimeText());
    std::string lastTimeText = timeText.peek();
    flux::useFrame([timeText, lastTimeText](flux::AnimationTick const&) mutable {
      std::string const next = ShellModel::formatTimeText();
      if (next != lastTimeText) {
        lastTimeText = next;
        timeText.set(next);
      }
    });

    auto const activeTitle = model.activeTitleSignal();
    flux::Reactive::Bindable<float> barWidth{[] {
      flux::LayoutConstraints const* constraints = flux::useLayoutConstraints();
      if (constraints && std::isfinite(constraints->maxWidth) && constraints->maxWidth > 0.f) {
        return constraints->maxWidth;
      }
      return 1.f;
    }};
    return flux::Element{LambdaTopBar{TopBarProps{
        .title = flux::Reactive::Bindable<std::string>{[activeTitle] { return activeTitle(); }},
        .timeText = timeText,
        .width = barWidth,
        .system = model.systemStatus(),
        .onOpenLauncher = onOpenLauncher,
    }}}
        .width(barWidth)
        .height(static_cast<float>(kTopBarHeight))
        .fill(flux::Colors::transparent);
  }
};

struct ShellDockView {
  ShellModel& model;
  std::function<void()> onOpenLauncher;
  std::function<void(DockItem const&)> onActivateItem;

  flux::Element body() const {
    auto const items = model.dockItemsSignal();
    flux::Reactive::Bindable<int> widthBinding{[items] { return dockWidth(items()); }};
    return flux::Element{LambdaDock{DockProps{
        .items = items,
        .hoverIndex = -1,
        .width = widthBinding,
        .onOpenLauncher = onOpenLauncher,
        .onActivateItem = onActivateItem,
    }}};
  }
};

struct ShellLauncherView {
  ShellModel& model;
  std::function<void(DockItem const&)> onActivateResult;
  std::function<void()> onDismiss;
  std::function<void(flux::KeyCode, flux::Modifiers)> onKeyDown;

  flux::Element body() const {
    auto const results = model.launcherResultsSignal();
    auto const query = model.querySignal();
    auto const highlighted = model.highlightedSignal();

    auto const uiVisible = model.launcherUiVisibleSignal();
    auto const launcherWidth = model.launcherWidthSignal();
    auto const launcherHeight = model.launcherHeightSignal();

    auto buildLauncher = [this, results, query, highlighted, launcherWidth, launcherHeight] {
      flux::Reactive::Bindable<float> widthBinding{
          [launcherWidth] { return std::max(1.f, launcherWidth()); }};
      flux::Reactive::Bindable<float> heightBinding{
          [launcherHeight] { return std::max(1.f, launcherHeight()); }};
      flux::Reactive::Bindable<int> widthIntBinding{
          [widthBinding] { return static_cast<int>(std::max(1.f, widthBinding.evaluate())); }};
      flux::Reactive::Bindable<int> heightIntBinding{
          [heightBinding] { return static_cast<int>(std::max(1.f, heightBinding.evaluate())); }};
      flux::Reactive::Bindable<int> clampedHighlight{[results, highlighted] {
        auto const items = results();
        if (items.empty()) return 0;
        return std::clamp(highlighted.evaluate(), 0, static_cast<int>(items.size()) - 1);
      }};

      auto root = flux::Element{LambdaCommandLauncher{CommandLauncherProps{
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

    return flux::Show(
        uiVisible,
        buildLauncher,
        [] {
          return flux::Rectangle{}.size(1.f, 1.f).fill(flux::Colors::transparent);
        });
  }
};

} // namespace lambda_shell
