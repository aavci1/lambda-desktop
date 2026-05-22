#include "Shell/ShellModel.hpp"

#include "Shell/ShellJson.hpp"

#include <algorithm>
#include <cstdio>

namespace lambda_shell {

void ShellModel::setOnChanged(ChangeCallback callback) {
  onChanged_ = std::move(callback);
}

void ShellModel::notifyChanged() {
  if (onChanged_) {
    onChanged_();
  }
}

std::string ShellModel::timeText() const {
  char buffer[64]{};
  std::time_t now = std::time(nullptr);
  std::tm local{};
  localtime_r(&now, &local);
  std::strftime(buffer, sizeof(buffer), "%a %d %b, %H:%M", &local);
  return buffer;
}

void ShellModel::setPreviewFocus(std::string_view appId) {
  for (auto& item : dockItems_) {
    item.running = !appId.empty() && item.kind == "app" && item.appId == appId;
    item.focused = item.running;
    if (item.focused) activeTitle_ = item.label;
  }
  if (appId.empty()) activeTitle_.clear();
  notifyChanged();
}

void ShellModel::resetDockItems() {
  dockItems_ = {
      {"launcher", "launcher", "Launcher", {}, false, false, false},
      {"sep1", "separator", "", {}, false, false, false},
      {"files", "app", "Files", "files", false, false, false},
      {"browser", "app", "Browser", "browser", false, false, false},
      {"terminal", "app", "Terminal", "terminal", false, false, false},
      {"settings", "app", "Settings", "settings", false, false, false},
      {"calendar", "app", "Calendar", "calendar", false, false, false},
      {"mail", "app", "Mail", "mail", false, false, false},
      {"music", "app", "Music", "music", false, false, false},
      {"sep2", "separator", "", {}, false, false, false},
      {"trash", "trash", "Trash", "trash", false, false, true},
  };
}

bool ShellModel::appIdMatches(std::string_view requested, std::string_view actual) {
  if (requested == actual) return true;
  if (requested == "terminal" && actual == "foot") return true;
  if (requested == "browser" && actual == "firefox") return true;
  if (requested == "files" && (actual == "org.gnome.Nautilus" || actual == "nautilus" || actual == "thunar")) return true;
  return false;
}

void ShellModel::applySnapshot(std::string_view json) {
  for (auto& item : dockItems_) {
    item.running = false;
    item.focused = false;
  }
  activeTitle_.clear();
  for (auto& item : dockItems_) {
    if (item.appId.empty()) continue;
    std::size_t search = 0;
    while (search < json.size()) {
      std::size_t const pos = json.find("\"appId\":\"", search);
      if (pos == std::string_view::npos) break;
      std::size_t const valueStart = pos + 9u;
      std::size_t const valueEnd = json.find('"', valueStart);
      if (valueEnd == std::string_view::npos) break;
      std::string_view actualAppId = json.substr(valueStart, valueEnd - valueStart);
      if (appIdMatches(item.appId, actualAppId)) {
        item.running = true;
        std::size_t const objectEnd = json.find('}', valueEnd);
        item.focused = objectEnd != std::string_view::npos &&
                       json.substr(valueEnd, objectEnd - valueEnd).find("\"focused\":true") != std::string_view::npos;
        if (item.focused) {
          activeTitle_ = jsonStringField(json, "title", pos);
          if (activeTitle_.empty()) activeTitle_ = item.label;
        }
        break;
      }
      search = valueEnd + 1u;
    }
  }
  systemStatus_.network = jsonStringField(json, "network");
  systemStatus_.wifi = jsonStringField(json, "wifi");
  systemStatus_.bluetooth = jsonStringField(json, "bluetooth");
  systemStatus_.volume = jsonStringField(json, "volume");
  systemStatus_.battery = jsonStringField(json, "battery");
  notifyChanged();
}

void ShellModel::openLauncher() {
  if (launcherOpen_) return;
  launcherOpen_ = true;
  query_.clear();
  highlighted_ = 0;
  notifyChanged();
}

void ShellModel::closeLauncher() {
  if (!launcherOpen_) return;
  launcherOpen_ = false;
  query_.clear();
  highlighted_ = 0;
  notifyChanged();
}

void ShellModel::setQuery(std::string query) {
  query_ = std::move(query);
  highlighted_ = 0;
  notifyChanged();
}

void ShellModel::setHighlighted(int index) {
  highlighted_ = index;
  notifyChanged();
}

void ShellModel::moveHighlight(int delta) {
  auto results = launcherResults();
  if (results.empty()) return;
  highlighted_ = std::clamp(highlighted_ + delta, 0, static_cast<int>(results.size()) - 1);
  notifyChanged();
}

void ShellModel::appendQueryText(std::string_view text) {
  if (query_.size() >= 128u) return;
  query_.append(text);
  highlighted_ = 0;
  notifyChanged();
}

void ShellModel::backspaceQuery() {
  if (!query_.empty()) query_.pop_back();
  notifyChanged();
}

std::vector<DockItem> ShellModel::launcherResults() const {
  return lambda_shell::launcherResults(dockItems_, query_);
}

void ShellModel::activateItem(DockItem const& item, std::function<void(std::string const& line)> sendIpc) {
  if (!sendIpc) return;
  if (item.running) {
    sendIpc("{\"type\":\"lambda.windowManager.focusApp\",\"appId\":\"" + escapeJson(item.appId) + "\"}");
  } else if (item.kind == "app") {
    sendIpc("{\"type\":\"lambda.windowManager.launchApp\",\"appId\":\"" + escapeJson(item.appId) + "\"}");
  }
  closeLauncher();
}

} // namespace lambda_shell
