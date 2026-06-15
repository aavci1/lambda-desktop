#pragma once

#include "Shell/ShellModel.hpp"
#include "Shell/ShellModels.hpp"
#include "Shell/UI/LambdaCommandLauncher.hpp"
#include "Shell/UI/LambdaDock.hpp"

#include <Lambda/Graphics/TextLayoutOptions.hpp>
#include <Lambda/UI/Element.hpp>
#include <Lambda/UI/IconName.hpp>
#include <Lambda/UI/KeyCodes.hpp>
#include <Lambda/UI/VisualTokens.hpp>
#include <Lambda/UI/Views/Views.hpp>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

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

struct ShellSessionMenuView {
  float surfaceWidth = static_cast<float>(kSessionMenuSurfaceWidth);
  float surfaceHeight = static_cast<float>(kSessionMenuSurfaceHeight);
  float menuX = 0.f;
  float menuY = 0.f;
  std::function<void(std::string const&)> onAction;
  std::function<void()> onDismiss;

  lambda::Element body() const {
    return lambda::Element{LambdaSessionMenu{SessionMenuProps{
        .surfaceWidth = surfaceWidth,
        .surfaceHeight = surfaceHeight,
        .menuX = menuX,
        .menuY = menuY,
        .onAction = onAction,
        .onDismiss = onDismiss,
    }}};
  }
};

struct ShellNotificationBannerView {
  Notification notification;
  float width = 360.f;
  float height = 96.f;
  std::function<void(std::uint64_t)> onDismiss;

  lambda::Element body() const {
    float const surfaceWidth = std::max(1.f, width);
    float const surfaceHeight = std::max(1.f, height);
    float const contentX = 16.f;
    float const textWidth = std::max(1.f, surfaceWidth - 64.f);
    std::string const appLabel = notification.appId.empty() ? std::string("Notification") : notification.appId;

    std::vector<lambda::Element> layers;
    layers.push_back(lambda::Rectangle{}
                         .size(surfaceWidth, surfaceHeight)
                         .fill(lambda::FillStyle::solid(lambda::VisualTokens::elevatedSurface))
                         .stroke(lambda::StrokeStyle::solid(lambda::VisualTokens::border, 1.f))
                         .cornerRadius(12.f));
    layers.push_back(lambda::Text{
        .text = appLabel,
        .font = lambda::Font{.family = "", .size = 11.f, .weight = 640.f},
        .color = lambda::VisualTokens::secondaryText,
        .horizontalAlignment = lambda::HorizontalAlignment::Leading,
        .verticalAlignment = lambda::VerticalAlignment::Center,
        .wrapping = lambda::TextWrapping::NoWrap,
        .maxLines = 1,
    }.size(textWidth, 16.f).position(contentX, 10.f));
    layers.push_back(lambda::Text{
        .text = notification.title.empty() ? appLabel : notification.title,
        .font = lambda::Font{.family = "", .size = 15.f, .weight = 680.f},
        .color = lambda::VisualTokens::primaryText,
        .horizontalAlignment = lambda::HorizontalAlignment::Leading,
        .verticalAlignment = lambda::VerticalAlignment::Center,
        .wrapping = lambda::TextWrapping::NoWrap,
        .maxLines = 1,
    }.size(textWidth, 22.f).position(contentX, 28.f));
    if (!notification.body.empty()) {
      layers.push_back(lambda::Text{
          .text = notification.body,
          .font = lambda::Font{.family = "", .size = 13.f, .weight = 460.f},
          .color = lambda::VisualTokens::secondaryText,
          .horizontalAlignment = lambda::HorizontalAlignment::Leading,
          .verticalAlignment = lambda::VerticalAlignment::Top,
          .wrapping = lambda::TextWrapping::Wrap,
          .maxLines = 2,
      }.size(std::max(1.f, surfaceWidth - 32.f), 36.f).position(contentX, 54.f));
    }

    if (onDismiss) {
      layers.push_back(lambda::Element{lambda::IconButton{
                           .icon = lambda::IconName::Close,
                           .style = lambda::IconButton::Style{
                               .size = 28.f,
                               .weight = 560.f,
                               .color = lambda::VisualTokens::secondaryText,
                           },
                           .onTap = [callback = onDismiss, id = notification.id] { callback(id); },
                       }}
                           .position(surfaceWidth - 40.f, 10.f));
    }

    return lambda::ZStack{
        .horizontalAlignment = lambda::Alignment::Start,
        .verticalAlignment = lambda::Alignment::Start,
        .children = std::move(layers),
    }.size(surfaceWidth, surfaceHeight);
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
