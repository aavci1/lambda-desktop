#pragma once

#include <Flux/Core/Geometry.hpp>

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace lambda_shell {

inline constexpr int kTopBarHeight = 36;
inline constexpr int kDockBottom = 12;
inline constexpr int kDockCell = 48;
inline constexpr int kDockPaddingX = 10;
inline constexpr int kDockPaddingY = 8;
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

struct TopBarProps {
  std::string title;
  std::string timeText;
  std::function<void()> onOpenLauncher;
};

struct DockProps {
  std::vector<DockItem> items;
  int hoverIndex = -1;
  int width = 1;
  std::function<void()> onOpenLauncher;
  std::function<void(DockItem const&)> onActivateItem;
};

struct CommandLauncherProps {
  std::vector<DockItem> items;
  std::string query;
  int highlighted = 0;
  int width = 1;
  int height = 1;
  bool open = false;
  std::function<void(DockItem const&)> onActivateResult;
  std::function<void()> onDismiss;
};

} // namespace lambda_shell
