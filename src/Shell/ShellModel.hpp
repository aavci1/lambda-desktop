#pragma once

#include "Shell/UI/LambdaShellTypes.hpp"

#include <Flux/Reactive/Signal.hpp>

#include <ctime>
#include <functional>
#include <string>
#include <vector>

namespace lambda_shell {

class ShellModel {
public:
  std::vector<DockItem> const& dockItems() const { return dockItems_.peek(); }
  bool launcherOpen() const { return launcherOpen_.peek(); }
  bool launcherUiVisible() const { return launcherUiVisible_.peek(); }
  float launcherWidth() const { return launcherWidth_.peek(); }
  float launcherHeight() const { return launcherHeight_.peek(); }
  std::string const& query() const { return query_.peek(); }
  int highlighted() const { return highlighted_.peek(); }
  std::string const& activeTitle() const { return activeTitle_.peek(); }
  lambda_shell::SystemStatus const& systemStatus() const { return systemStatus_.peek(); }
  std::vector<DockItem> const& launcherResults() const { return launcherResults_.peek(); }

  flux::Signal<std::vector<DockItem>>& dockItemsSignal() { return dockItems_; }
  flux::Signal<bool>& launcherOpenSignal() { return launcherOpen_; }
  flux::Signal<bool>& launcherUiVisibleSignal() { return launcherUiVisible_; }
  flux::Signal<float>& launcherWidthSignal() { return launcherWidth_; }
  flux::Signal<float>& launcherHeightSignal() { return launcherHeight_; }
  flux::Signal<std::string>& querySignal() { return query_; }
  flux::Signal<int>& highlightedSignal() { return highlighted_; }
  flux::Signal<std::string>& activeTitleSignal() { return activeTitle_; }
  flux::Signal<SystemStatus>& systemStatusSignal() { return systemStatus_; }
  flux::Signal<std::vector<DockItem>>& launcherResultsSignal() { return launcherResults_; }

  static std::string formatTimeText();

  void resetDockItems();
  void setPreviewFocus(std::string_view appId);
  void applySnapshot(std::string_view json);
  void openLauncher();
  void closeLauncher();
  void setLauncherUiVisible(bool visible);
  void setLauncherSize(float width, float height);
  void setQuery(std::string query);
  void setHighlighted(int index);
  void moveHighlight(int delta);
  void appendQueryText(std::string_view text);
  void backspaceQuery();

  void activateItem(DockItem const& item, std::function<void(std::string const& line)> sendIpc);

private:
  static bool appIdMatches(std::string_view requested, std::string_view actual);
  void refreshLauncherResults();
  static bool dockItemsVisualStateEqual(std::vector<DockItem> const& a,
                                        std::vector<DockItem> const& b);

  flux::Signal<std::vector<DockItem>> dockItems_;
  flux::Signal<bool> launcherOpen_{false};
  flux::Signal<bool> launcherUiVisible_{false};
  flux::Signal<float> launcherWidth_{1.f};
  flux::Signal<float> launcherHeight_{1.f};
  flux::Signal<std::string> query_;
  flux::Signal<int> highlighted_{0};
  flux::Signal<std::string> activeTitle_;
  flux::Signal<SystemStatus> systemStatus_;
  flux::Signal<std::vector<DockItem>> launcherResults_;
};

} // namespace lambda_shell
