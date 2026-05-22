#pragma once

#include "Shell/UI/LambdaShellTypes.hpp"

#include <ctime>
#include <functional>
#include <string>
#include <vector>

namespace lambda_shell {

class ShellModel {
public:
  using ChangeCallback = std::function<void()>;

  void setOnChanged(ChangeCallback callback);

  std::vector<DockItem> const& dockItems() const { return dockItems_; }
  bool launcherOpen() const { return launcherOpen_; }
  std::string const& query() const { return query_; }
  int highlighted() const { return highlighted_; }
  std::string const& activeTitle() const { return activeTitle_; }
  lambda_shell::SystemStatus const& systemStatus() const { return systemStatus_; }
  std::string timeText() const;

  void resetDockItems();
  void setPreviewFocus(std::string_view appId);
  void applySnapshot(std::string_view json);
  void openLauncher();
  void closeLauncher();
  void setQuery(std::string query);
  void setHighlighted(int index);
  void moveHighlight(int delta);
  void appendQueryText(std::string_view text);
  void backspaceQuery();
  std::vector<DockItem> launcherResults() const;

  void activateItem(DockItem const& item, std::function<void(std::string const& line)> sendIpc);

private:
  static bool appIdMatches(std::string_view requested, std::string_view actual);
  void notifyChanged();

  std::vector<DockItem> dockItems_;
  bool launcherOpen_ = false;
  std::string query_;
  int highlighted_ = 0;
  std::string activeTitle_;
  lambda_shell::SystemStatus systemStatus_;
  ChangeCallback onChanged_;
};

} // namespace lambda_shell
