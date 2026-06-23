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

  lambdaui::Element body() const {
    auto const items = model.dockItemsSignal();
    auto const timeText = model.timeTextSignal();
    auto const clockWidth = model.dockClockWidthSignal();
    auto const itemSize = model.dockItemSizeSignal();
    auto const systemStatus = model.systemStatusSignal();
    lambdaui::Reactive::Bindable<int> widthBinding{[items, clockWidth, itemSize] {
      return dockWidth(items(), clockWidth(), itemSize());
    }};
    return lambdaui::Element{LambdaDock{DockProps{
        .items = items,
        .timeText = timeText,
        .clockWidth = clockWidth,
        .itemSize = itemSize,
        .system = lambdaui::Reactive::Bindable<SystemStatus>{[systemStatus] { return systemStatus(); }},
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

  lambdaui::Element body() const {
    return lambdaui::Element{LambdaDockMenu{DockMenuProps{
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

  lambdaui::Element body() const {
    return lambdaui::Element{LambdaSessionMenu{SessionMenuProps{
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
  bool showPreview = true;
  std::function<void(std::uint64_t)> onDismiss;
  std::function<void(std::uint64_t, std::string const&)> onAction;

  lambdaui::Element body() const {
    float const surfaceWidth = std::max(1.f, width);
    float const surfaceHeight = std::max(1.f, height);
    float const contentX = 16.f;
    float const textWidth = std::max(1.f, surfaceWidth - 64.f);
    std::string const appLabel = notification.appId.empty() ? std::string("Notification") : notification.appId;

    std::vector<lambdaui::Element> layers;
    layers.push_back(lambdaui::Rectangle{}
                         .size(surfaceWidth, surfaceHeight)
                         .fill(lambdaui::FillStyle::solid(lambdaui::VisualTokens::elevatedSurface))
                         .stroke(lambdaui::StrokeStyle::solid(lambdaui::VisualTokens::border, 1.f))
                         .cornerRadius(12.f));
    layers.push_back(lambdaui::Text{
        .text = appLabel,
        .font = lambdaui::Font{.family = "", .size = 11.f, .weight = 640.f},
        .color = lambdaui::VisualTokens::secondaryText,
        .horizontalAlignment = lambdaui::HorizontalAlignment::Leading,
        .verticalAlignment = lambdaui::VerticalAlignment::Center,
        .wrapping = lambdaui::TextWrapping::NoWrap,
        .maxLines = 1,
    }.size(textWidth, 16.f).position(contentX, 10.f));
    layers.push_back(lambdaui::Text{
        .text = notification.title.empty() ? appLabel : notification.title,
        .font = lambdaui::Font{.family = "", .size = 15.f, .weight = 680.f},
        .color = lambdaui::VisualTokens::primaryText,
        .horizontalAlignment = lambdaui::HorizontalAlignment::Leading,
        .verticalAlignment = lambdaui::VerticalAlignment::Center,
        .wrapping = lambdaui::TextWrapping::NoWrap,
        .maxLines = 1,
    }.size(textWidth, 22.f).position(contentX, 28.f));
    float const actionRowHeight = (!notification.actions.empty() && onAction) ? 36.f : 0.f;
    if (showPreview && !notification.body.empty()) {
      layers.push_back(lambdaui::Text{
          .text = notification.body,
          .font = lambdaui::Font{.family = "", .size = 13.f, .weight = 460.f},
          .color = lambdaui::VisualTokens::secondaryText,
          .horizontalAlignment = lambdaui::HorizontalAlignment::Leading,
          .verticalAlignment = lambdaui::VerticalAlignment::Top,
          .wrapping = lambdaui::TextWrapping::Wrap,
          .maxLines = 2,
      }.size(std::max(1.f, surfaceWidth - 32.f), std::max(18.f, 36.f - actionRowHeight * 0.45f))
          .position(contentX, 54.f));
    }

    if (!notification.actions.empty() && onAction) {
      std::size_t const actionCount = std::min<std::size_t>(2u, notification.actions.size());
      float const gap = 8.f;
      float const rowWidth = std::max(1.f, surfaceWidth - 32.f);
      float const buttonWidth = std::max(1.f, (rowWidth - gap * static_cast<float>(actionCount - 1u)) /
                                               static_cast<float>(actionCount));
      float const buttonY = surfaceHeight - 38.f;
      for (std::size_t index = 0; index < actionCount; ++index) {
        auto const action = notification.actions[index];
        layers.push_back(lambdaui::Element{lambdaui::Button{
                             .label = action.label.empty() ? action.key : action.label,
                             .variant = lambdaui::ButtonVariant::Secondary,
                             .style = lambdaui::Button::Style{
                                 .font = lambdaui::Font{.family = "", .size = 12.f, .weight = 620.f},
                                 .paddingH = 8.f,
                                 .paddingV = 5.f,
                                 .cornerRadius = 8.f,
                                 .accentColor = lambdaui::VisualTokens::accent,
                             },
                             .onTap = [callback = onAction, id = notification.id, key = action.key] {
                               callback(id, key);
                             },
                         }}
                             .size(buttonWidth, 28.f)
                             .position(contentX + static_cast<float>(index) * (buttonWidth + gap), buttonY));
      }
    }

    if (onDismiss) {
      layers.push_back(lambdaui::Element{lambdaui::IconButton{
                           .icon = lambdaui::IconName::Close,
                           .style = lambdaui::IconButton::Style{
                               .size = 28.f,
                               .weight = 560.f,
                               .color = lambdaui::VisualTokens::secondaryText,
                           },
                           .onTap = [callback = onDismiss, id = notification.id] { callback(id); },
                       }}
                           .position(surfaceWidth - 40.f, 10.f));
    }

    return lambdaui::ZStack{
        .horizontalAlignment = lambdaui::Alignment::Start,
        .verticalAlignment = lambdaui::Alignment::Start,
        .children = std::move(layers),
    }.size(surfaceWidth, surfaceHeight);
  }
};

struct ShellLauncherView {
  ShellModel& model;
  std::function<void(DockItem const&)> onActivateResult;
  std::function<void()> onDismiss;
  std::function<void(lambdaui::KeyCode, lambdaui::Modifiers)> onKeyDown;

  lambdaui::Element body() const {
    auto const results = model.launcherResultsSignal();
    auto const query = model.querySignal();
    auto const highlighted = model.highlightedSignal();

    auto const uiVisible = model.launcherUiVisibleSignal();
    auto const launcherWidth = model.launcherWidthSignal();
    auto const launcherHeight = model.launcherHeightSignal();

    auto buildLauncher = [this, results, query, highlighted, launcherWidth, launcherHeight] {
      lambdaui::Reactive::Bindable<float> widthBinding{
          [launcherWidth] { return std::max(1.f, launcherWidth()); }};
      lambdaui::Reactive::Bindable<float> heightBinding{
          [launcherHeight] { return std::max(1.f, launcherHeight()); }};
      lambdaui::Reactive::Bindable<int> widthIntBinding{
          [widthBinding] { return static_cast<int>(std::max(1.f, widthBinding.evaluate())); }};
      lambdaui::Reactive::Bindable<int> heightIntBinding{
          [heightBinding] { return static_cast<int>(std::max(1.f, heightBinding.evaluate())); }};
      lambdaui::Reactive::Bindable<int> clampedHighlight{[results, highlighted] {
        auto const items = results();
        if (items.empty()) return 0;
        return std::clamp(highlighted.evaluate(), 0, static_cast<int>(items.size()) - 1);
      }};

      auto root = lambdaui::Element{LambdaCommandLauncher{CommandLauncherProps{
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

    return lambdaui::Show(
        uiVisible,
        buildLauncher,
        [] {
          return lambdaui::Rectangle{}.size(1.f, 1.f).fill(lambdaui::Colors::transparent);
        });
  }
};

} // namespace lambda_shell
