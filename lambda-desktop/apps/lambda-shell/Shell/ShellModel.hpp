#pragma once

#include "Shell/UI/LambdaShellTypes.hpp"

#include <Lambda/Reactive/Signal.hpp>

#include <ctime>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace lambda_shell {

struct AppRegistryEntry;
struct ShellConfig;

class ShellModel {
public:
  struct SnapshotChanges {
    bool dockItems = false;
    bool activeTitle = false;

    [[nodiscard]] bool any() const { return dockItems || activeTitle; }
  };

  std::vector<DockItem> const& dockItems() const { return dockItems_.peek(); }
  bool launcherOpen() const { return launcherOpen_.peek(); }
  bool launcherUiVisible() const { return launcherUiVisible_.peek(); }
  float launcherWidth() const { return launcherWidth_.peek(); }
  float launcherHeight() const { return launcherHeight_.peek(); }
  std::string const& query() const { return query_.peek(); }
  int queryCursor() const { return queryCursor_.peek(); }
  int highlighted() const { return highlighted_.peek(); }
  std::string const& activeTitle() const { return activeTitle_.peek(); }
  std::string const& timeText() const { return timeText_.peek(); }
  int dockClockWidth() const { return dockClockWidth_.peek(); }
  int dockItemSize() const { return dockItemSize_.peek(); }
  lambda_shell::SystemStatus const& systemStatus() const { return systemStatus_.peek(); }
  std::vector<DockItem> const& launcherResults() const { return launcherResults_.peek(); }

  lambdaui::Signal<std::vector<DockItem>>& dockItemsSignal() { return dockItems_; }
  lambdaui::Signal<bool>& launcherOpenSignal() { return launcherOpen_; }
  lambdaui::Signal<bool>& launcherUiVisibleSignal() { return launcherUiVisible_; }
  lambdaui::Signal<float>& launcherWidthSignal() { return launcherWidth_; }
  lambdaui::Signal<float>& launcherHeightSignal() { return launcherHeight_; }
  lambdaui::Signal<std::string>& querySignal() { return query_; }
  lambdaui::Signal<int>& queryCursorSignal() { return queryCursor_; }
  lambdaui::Signal<int>& highlightedSignal() { return highlighted_; }
  lambdaui::Signal<std::string>& activeTitleSignal() { return activeTitle_; }
  lambdaui::Signal<std::string>& timeTextSignal() { return timeText_; }
  lambdaui::Signal<int>& dockClockWidthSignal() { return dockClockWidth_; }
  lambdaui::Signal<int>& dockItemSizeSignal() { return dockItemSize_; }
  lambdaui::Signal<SystemStatus>& systemStatusSignal() { return systemStatus_; }
  lambdaui::Signal<std::vector<DockItem>>& launcherResultsSignal() { return launcherResults_; }

  static std::string formatTimeText();
  static std::string formatTimeText(std::string_view format);

  void resetDockItems();
  void setDockItems(std::vector<AppRegistryEntry> const& apps, ShellConfig const& config);
  [[nodiscard]] bool setSystemStatus(SystemStatus status);
  [[nodiscard]] bool setDockDpiScale(float scale);
  void setPreviewFocus(std::string_view appId);
  [[nodiscard]] SnapshotChanges applySnapshot(std::string_view json);
  [[nodiscard]] bool refreshTimeText();
  [[nodiscard]] bool setDockClockWidth(int width);
  void openLauncher();
  void closeLauncher();
  void setLauncherUiVisible(bool visible);
  void setLauncherSize(float width, float height);
  void setQuery(std::string query);
  void setHighlighted(int index);
  void moveHighlight(int delta);
  void appendQueryText(std::string_view text);
  void backspaceQuery();
  void deleteQueryForward();
  void moveQueryCursor(int delta);
  void moveQueryCursorToStart();
  void moveQueryCursorToEnd();

  void activateItem(DockItem const& item,
                    std::function<void(std::string const& line)> sendIpc,
                    std::uint64_t requestId = 0);

private:
  void refreshLauncherResults();
  static bool dockItemsVisualStateEqual(std::vector<DockItem> const& a,
                                        std::vector<DockItem> const& b);

  lambdaui::Signal<std::vector<DockItem>> dockItems_;
  lambdaui::Signal<bool> launcherOpen_{false};
  lambdaui::Signal<bool> launcherUiVisible_{false};
  lambdaui::Signal<float> launcherWidth_{1.f};
  lambdaui::Signal<float> launcherHeight_{1.f};
  lambdaui::Signal<std::string> query_;
  lambdaui::Signal<int> queryCursor_{0};
  lambdaui::Signal<int> highlighted_{0};
  lambdaui::Signal<std::string> activeTitle_;
  lambdaui::Signal<std::string> timeText_{formatTimeText()};
  lambdaui::Signal<int> dockClockWidth_{kDockClockMinWidth};
  lambdaui::Signal<int> dockItemSize_{kDockIconSize};
  lambdaui::Signal<SystemStatus> systemStatus_;
  lambdaui::Signal<std::vector<DockItem>> launcherResults_;
  bool showRunningUnpinned_ = true;
  std::string iconTheme_;
  float dockDpiScale_ = 1.f;
  std::string clockFormat_ = "%a %d %b, %H:%M";
};

} // namespace lambda_shell
