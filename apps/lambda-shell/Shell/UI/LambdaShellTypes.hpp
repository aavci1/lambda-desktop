#pragma once

#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Reactive/Bindable.hpp>
#include <Lambda/Reactive/Signal.hpp>
#include <Lambda/UI/IconName.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace lambda_shell {

inline constexpr int kDockBottom = 8;
inline constexpr int kDockCornerRadius = 18;
inline constexpr int kDockCell = 56;
inline constexpr int kDockMinItemSize = 32;
inline constexpr int kDockMaxItemSize = 96;
inline constexpr int kDockSingleRowThreshold = 40;
inline constexpr int kDockClockMinWidth = 1;
inline constexpr float kDockClockDateFontSize = 11.f;
inline constexpr float kDockClockTimeFontSize = 17.f;
inline constexpr float kDockClockSingleRowFontSize = 14.f;
inline constexpr float kDockClockDateFontWeight = 580.f;
inline constexpr float kDockClockTimeFontWeight = 680.f;
inline constexpr float kDockClockSingleRowFontWeight = 640.f;
inline constexpr float kDockClockLeadingPaddingX = 4.f;
inline constexpr float kDockClockTrailingPaddingX = 8.f;
inline constexpr int kDockStatusColumns = 4;
inline constexpr int kDockStatusRows = 2;
inline constexpr int kDockStatusItemCount = 8;
inline constexpr int kDockStatusCell = 22;
inline constexpr int kDockStatusIconSize = 18;
inline constexpr int kDockStatusGridGap = 4;
inline constexpr int kDockIconSize = 48;
inline constexpr int kDockDotSize = 4;
inline constexpr int kDockIconDotGap = 2;
inline constexpr int kDockDotBelowPad = 5;
/// Space above the icon; equals gap + dot + padding below the dot.
inline constexpr int kDockSlotMargin = kDockIconDotGap + kDockDotSize + kDockDotBelowPad;
inline constexpr int kDockSlotHeight =
    kDockSlotMargin + kDockIconSize + kDockIconDotGap + kDockDotSize + kDockDotBelowPad;
inline constexpr float kDockPaddingX = 10.f;
inline constexpr float kDockPaddingTop = 0.f;
inline constexpr float kDockPaddingBottom = 0.f;
inline constexpr int kDockGap = 6;
inline constexpr int kDockSeparatorWidth = 9;
inline constexpr int kDockSeparatorLineWidth = 1;
inline constexpr int kDockMenuContentWidth = 176;
inline constexpr int kDockMenuContentHeight = 110;
inline constexpr int kDockMenuPadding = 6;
inline constexpr int kDockMenuArrowHeight = 8;
inline constexpr int kDockMenuSurfaceInset = 2;
inline constexpr int kDockMenuCalloutWidth = kDockMenuContentWidth + kDockMenuPadding * 2;
inline constexpr int kDockMenuCalloutHeight =
    kDockMenuContentHeight + kDockMenuPadding * 2 + kDockMenuArrowHeight;
inline constexpr int kDockMenuSurfaceWidth = kDockMenuCalloutWidth + kDockMenuSurfaceInset * 2;
inline constexpr int kDockMenuSurfaceHeight = kDockMenuCalloutHeight + kDockMenuSurfaceInset * 2;
inline constexpr int kSessionMenuContentWidth = 190;
inline constexpr int kSessionMenuContentHeight = 206;
inline constexpr int kSessionMenuCalloutWidth = kSessionMenuContentWidth + kDockMenuPadding * 2;
inline constexpr int kSessionMenuCalloutHeight =
    kSessionMenuContentHeight + kDockMenuPadding * 2 + kDockMenuArrowHeight;
inline constexpr int kSessionMenuSurfaceWidth = kSessionMenuCalloutWidth + kDockMenuSurfaceInset * 2;
inline constexpr int kSessionMenuSurfaceHeight = kSessionMenuCalloutHeight + kDockMenuSurfaceInset * 2;

struct DockItem {
  std::string id;
  std::string kind;
  std::string label;
  std::string appId;
  bool running = false;
  bool focused = false;
  bool disabled = false;
  bool pinned = false;
  std::string icon;
  std::string iconPath;
  int iconPixelSize = kDockIconSize;

  bool operator==(DockItem const& other) const = default;
};

struct LauncherLayout {
  float fieldX = 0.f;
  float fieldY = 80.f;
  float fieldW = 0.f;
  int tileW = 126;
  int tileH = 96;
  int gap = 12;
  int columns = 1;
  int gridW = 0;
  int startX = 0;
};

int clampedDockItemSize(int itemSize);
bool dockUsesSingleRowDocklets(int itemSize);
int dockCell(int itemSize = kDockIconSize);
int dockSlotHeight(int itemSize = kDockIconSize);
int dockStatusCellSize(int itemSize = kDockIconSize);
int dockStatusIconSize(int itemSize = kDockIconSize);
int dockStatusGridGap(int itemSize = kDockIconSize);
int dockStatusGridColumns(int itemSize = kDockIconSize);
int dockStatusGridRows(int itemSize = kDockIconSize);
int dockStatusGridWidth(int itemSize = kDockIconSize);
int dockStatusGridHeight(int itemSize = kDockIconSize);
float dockClockDateRowHeight(int itemSize = kDockIconSize);
float dockClockTimeRowHeight(int itemSize = kDockIconSize);
float dockClockRowGap(int itemSize = kDockIconSize);
float dockClockDateFontSize(int itemSize = kDockIconSize);
float dockClockTimeFontSize(int itemSize = kDockIconSize);
float dockClockSingleRowFontSize(int itemSize = kDockIconSize);
int dockItemWidth(DockItem const& item, int itemSize = kDockIconSize);
int dockItemsWidth(std::vector<DockItem> const& items, int itemSize = kDockIconSize);
int dockStatusWidth(int clockWidth = kDockClockMinWidth, int itemSize = kDockIconSize);
int dockWidth(std::vector<DockItem> const& items,
              int clockWidth = kDockClockMinWidth,
              int itemSize = kDockIconSize);
int dockHeight(int itemSize = kDockIconSize);
std::optional<std::size_t> dockItemIndexAt(std::vector<DockItem> const& items,
                                           double x,
                                           double y,
                                           int itemSize = kDockIconSize);

std::vector<DockItem> launcherResults(std::vector<DockItem> const& items, std::string query);
LauncherLayout launcherLayout(int width);
std::optional<std::size_t> launcherResultAt(int width,
                                            bool launcherOpen,
                                            std::vector<DockItem> const& results,
                                            double x,
                                            double y);
bool launcherPointerInsideContent(int width,
                                  bool launcherOpen,
                                  std::vector<DockItem> const& results,
                                  double x,
                                  double y);

struct TrayStatusItem {
  std::string id;
  std::string label;
  lambda::IconName icon = lambda::IconName::Widgets;

  bool operator==(TrayStatusItem const& other) const = default;
};

struct SystemStatus {
  std::string network = "unknown";
  std::string wifi = "unknown";
  std::string bluetooth = "unknown";
  std::string volume = "unknown";
  std::string battery = "unknown";
  std::string media = "unknown";
  std::vector<TrayStatusItem> trayItems;

  bool operator==(SystemStatus const& other) const = default;
};

enum class StatusAvailability : std::uint8_t {
  Unavailable,
  Available,
};

enum class DockStatusAction : std::uint8_t {
  Primary,
  Secondary,
  ScrollUp,
  ScrollDown,
};

struct DockletStatusItem {
  std::string id;
  lambda::IconName icon = lambda::IconName::Circle;
  std::string label;
  StatusAvailability availability = StatusAvailability::Unavailable;
  bool active = false;

  bool operator==(DockletStatusItem const&) const = default;
};

std::vector<DockletStatusItem> dockletStatusItems(SystemStatus const& status);
std::string dockClockDateText(std::string_view timeText);
std::string dockClockTimeText(std::string_view timeText);

struct DockProps {
  lambda::Signal<std::vector<DockItem>> items;
  lambda::Signal<std::string> timeText;
  lambda::Signal<int> clockWidth;
  lambda::Signal<int> itemSize{kDockIconSize};
  lambda::Reactive::Bindable<SystemStatus> system{SystemStatus{}};
  int hoverIndex = -1;
  bool fullWidth = false;
  lambda::Reactive::Bindable<int> width{1};
  std::function<void()> onOpenLauncher;
  std::function<void(DockItem const&)> onActivateItem;
  std::function<void(DockItem const&)> onShowMenu;
  std::function<void(std::string const&, DockStatusAction)> onStatusAction;
};

struct DockMenuProps {
  DockItem item;
  float surfaceWidth = static_cast<float>(kDockMenuSurfaceWidth);
  float surfaceHeight = static_cast<float>(kDockMenuSurfaceHeight);
  float menuX = 0.f;
  float menuY = 0.f;
  std::function<void(DockItem const&)> onNewWindow;
  std::function<void(DockItem const&)> onTogglePinned;
  std::function<void(DockItem const&)> onQuitItem;
  std::function<void()> onDismiss;
};

struct SessionMenuProps {
  float surfaceWidth = static_cast<float>(kSessionMenuSurfaceWidth);
  float surfaceHeight = static_cast<float>(kSessionMenuSurfaceHeight);
  float menuX = 0.f;
  float menuY = 0.f;
  std::function<void(std::string const&)> onAction;
  std::function<void()> onDismiss;
};

struct CommandLauncherProps {
  lambda::Signal<std::vector<DockItem>> results;
  lambda::Signal<std::string> query;
  lambda::Reactive::Bindable<int> highlighted{0};
  lambda::Reactive::Bindable<int> width{1};
  lambda::Reactive::Bindable<int> height{1};
  std::function<void(DockItem const&)> onActivateResult;
  std::function<void()> onDismiss;
};

} // namespace lambda_shell
