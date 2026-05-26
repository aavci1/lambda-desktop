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
inline constexpr int kDockCell = 48;
inline constexpr int kDockIconSize = 40;
inline constexpr int kDockDotSize = 6;
inline constexpr int kDockIconDotGap = 4;
inline constexpr int kDockDotBelowPad = 2;
/// Space above the icon; equals gap + dot + padding below the dot.
inline constexpr int kDockSlotMargin = kDockIconDotGap + kDockDotSize + kDockDotBelowPad;
inline constexpr int kDockSlotHeight =
    kDockSlotMargin + kDockIconSize + kDockIconDotGap + kDockDotSize + kDockDotBelowPad;
inline constexpr float kDockPaddingX = 12.f;
inline constexpr float kDockPaddingTop = 8.f;
inline constexpr float kDockPaddingBottom = 8.f;
inline constexpr int kDockGap = 6;
inline constexpr int kDockSeparatorWidth = 1;

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
  SystemStatus system{};
  std::function<void()> onOpenLauncher;
};

struct DockProps {
  flux::Signal<std::vector<DockItem>> items;
  int hoverIndex = -1;
  flux::Reactive::Bindable<int> width{1};
  std::function<void()> onOpenLauncher;
  std::function<void(DockItem const&)> onActivateItem;
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
