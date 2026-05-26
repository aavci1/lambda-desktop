#pragma once

#include <Flux/Core/Geometry.hpp>
#include <Flux/Reactive/Bindable.hpp>
#include <Flux/Reactive/Signal.hpp>
#include <Flux/UI/IconName.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace lambda_shell {

inline constexpr int kTopBarHeight = 36;
inline constexpr int kDockBottom = 12;
inline constexpr int kDockCell = 56;
inline constexpr int kDockIconSize = 48;
inline constexpr int kDockDotSize = 4;
inline constexpr int kDockIconDotGap = 2;
inline constexpr int kDockDotBelowPad = 1;
/// Space above the icon; equals gap + dot + padding below the dot.
inline constexpr int kDockSlotMargin = kDockIconDotGap + kDockDotSize + kDockDotBelowPad;
inline constexpr int kDockSlotHeight =
    kDockSlotMargin + kDockIconSize + kDockIconDotGap + kDockDotSize + kDockDotBelowPad;
inline constexpr float kDockPaddingX = 12.f;
inline constexpr float kDockPaddingTop = 5.f;
inline constexpr float kDockPaddingBottom = 5.f;
inline constexpr int kDockGap = 6;
inline constexpr int kDockSeparatorWidth = 1;
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

int dockItemWidth(DockItem const& item);
int dockWidth(std::vector<DockItem> const& items);
int dockHeight();
std::optional<std::size_t> dockItemIndexAt(std::vector<DockItem> const& items, double x, double y);

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

struct SystemStatus {
  std::string network = "unknown";
  std::string wifi = "unknown";
  std::string bluetooth = "unknown";
  std::string volume = "unknown";
  std::string battery = "unknown";

  bool operator==(SystemStatus const& other) const = default;
};

enum class StatusAvailability : std::uint8_t {
  Unavailable,
  Available,
};

struct TopBarStatusItem {
  std::string id;
  flux::IconName icon = flux::IconName::Circle;
  std::string label;
  StatusAvailability availability = StatusAvailability::Unavailable;
  bool active = false;

  bool operator==(TopBarStatusItem const&) const = default;
};

std::vector<TopBarStatusItem> topBarStatusItems(SystemStatus const& status);

struct TopBarProps {
  flux::Reactive::Bindable<std::string> title;
  flux::Reactive::Bindable<std::string> timeText;
  flux::Reactive::Bindable<float> width{1.f};
  flux::Reactive::Bindable<SystemStatus> system{SystemStatus{}};
  std::function<void()> onOpenLauncher;
};

struct DockProps {
  flux::Signal<std::vector<DockItem>> items;
  int hoverIndex = -1;
  flux::Reactive::Bindable<int> width{1};
  std::function<void()> onOpenLauncher;
  std::function<void(DockItem const&)> onActivateItem;
  std::function<void(DockItem const&)> onShowMenu;
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

struct CommandLauncherProps {
  flux::Signal<std::vector<DockItem>> results;
  flux::Signal<std::string> query;
  flux::Reactive::Bindable<int> highlighted{0};
  flux::Reactive::Bindable<int> width{1};
  flux::Reactive::Bindable<int> height{1};
  std::function<void(DockItem const&)> onActivateResult;
  std::function<void()> onDismiss;
};

} // namespace lambda_shell
