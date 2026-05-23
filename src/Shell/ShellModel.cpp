#include "Shell/ShellModel.hpp"

#include "Shell/ShellJson.hpp"

#include <Flux/Shell/ShellIpc.hpp>

#include <algorithm>
#include <cstdio>

namespace lambda_shell {

std::string ShellModel::formatTimeText() {
  char buffer[64]{};
  std::time_t now = std::time(nullptr);
  std::tm local{};
  localtime_r(&now, &local);
  std::strftime(buffer, sizeof(buffer), "%a %d %b, %H:%M", &local);
  return buffer;
}

void ShellModel::refreshLauncherResults() {
  launcherResults_.set(lambda_shell::launcherResults(dockItems_.peek(), query_.peek()));
}

void ShellModel::setPreviewFocus(std::string_view appId) {
  auto items = dockItems_.peek();
  for (auto& item : items) {
    item.running = !appId.empty() && item.kind == "app" && item.appId == appId;
    item.focused = item.running;
  }
  dockItems_.set(std::move(items));
  if (!appId.empty()) {
    for (auto const& item : dockItems_.peek()) {
      if (item.focused) {
        activeTitle_.set(item.label);
        return;
      }
    }
  } else {
    activeTitle_.set(std::string{});
  }
}

void ShellModel::resetDockItems() {
  dockItems_.set({
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
  });
  refreshLauncherResults();
}

bool ShellModel::appIdMatches(std::string_view requested, std::string_view actual) {
  if (requested == actual) return true;
  if (requested == "terminal" && actual == "foot") return true;
  if (requested == "browser" && actual == "firefox") return true;
  if (requested == "files" && (actual == "org.gnome.Nautilus" || actual == "nautilus" || actual == "thunar")) return true;
  return false;
}

bool ShellModel::dockItemsVisualStateEqual(std::vector<DockItem> const& a,
                                           std::vector<DockItem> const& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i].running != b[i].running || a[i].focused != b[i].focused) return false;
  }
  return true;
}

void ShellModel::applySnapshot(std::string_view json) {
  auto items = dockItems_.peek();
  for (auto& item : items) {
    item.running = false;
    item.focused = false;
  }
  std::string nextTitle;
  for (auto& item : items) {
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
          nextTitle = jsonStringField(json, "title", pos);
          if (nextTitle.empty()) nextTitle = item.label;
        }
        break;
      }
      search = valueEnd + 1u;
    }
  }

  SystemStatus nextStatus{
      .network = jsonStringField(json, "network"),
      .wifi = jsonStringField(json, "wifi"),
      .bluetooth = jsonStringField(json, "bluetooth"),
      .volume = jsonStringField(json, "volume"),
      .battery = jsonStringField(json, "battery"),
  };

  if (!dockItemsVisualStateEqual(items, dockItems_.peek())) {
    dockItems_.set(std::move(items));
    refreshLauncherResults();
  }
  if (nextTitle != activeTitle_.peek()) {
    activeTitle_.set(std::move(nextTitle));
  }
  if (!(nextStatus == systemStatus_.peek())) {
    systemStatus_.set(std::move(nextStatus));
  }
}

void ShellModel::openLauncher() {
  if (launcherOpen_.peek()) return;
  launcherOpen_.set(true);
  launcherUiVisible_.set(false);
  query_.set(std::string{});
  highlighted_.set(0);
  refreshLauncherResults();
}

void ShellModel::closeLauncher() {
  if (!launcherOpen_.peek()) return;
  launcherOpen_.set(false);
  launcherUiVisible_.set(false);
  launcherWidth_.set(1.f);
  launcherHeight_.set(1.f);
  query_.set(std::string{});
  highlighted_.set(0);
  refreshLauncherResults();
}

void ShellModel::setLauncherUiVisible(bool visible) {
  if (launcherUiVisible_.peek() == visible) return;
  launcherUiVisible_.set(visible);
}

void ShellModel::setLauncherSize(float width, float height) {
  float const nextWidth = std::max(1.f, width);
  float const nextHeight = std::max(1.f, height);
  if (launcherWidth_.peek() == nextWidth && launcherHeight_.peek() == nextHeight) {
    return;
  }
  launcherWidth_.set(nextWidth);
  launcherHeight_.set(nextHeight);
}

void ShellModel::setQuery(std::string query) {
  query_.set(std::move(query));
  highlighted_.set(0);
  refreshLauncherResults();
}

void ShellModel::setHighlighted(int index) {
  highlighted_.set(index);
}

void ShellModel::moveHighlight(int delta) {
  auto const results = launcherResults_.peek();
  if (results.empty()) return;
  highlighted_.set(std::clamp(highlighted_.peek() + delta, 0, static_cast<int>(results.size()) - 1));
}

void ShellModel::appendQueryText(std::string_view text) {
  std::string next = query_.peek();
  if (next.size() >= 128u) return;
  next.append(text);
  query_.set(std::move(next));
  highlighted_.set(0);
  refreshLauncherResults();
}

void ShellModel::backspaceQuery() {
  std::string next = query_.peek();
  if (!next.empty()) next.pop_back();
  query_.set(std::move(next));
  refreshLauncherResults();
}

void ShellModel::activateItem(DockItem const& item, std::function<void(std::string const& line)> sendIpc) {
  if (!sendIpc) return;
  if (item.running) {
    sendIpc(flux::shell::serializeFocusApp(item.appId));
  } else if (item.kind == "app") {
    sendIpc(flux::shell::serializeLaunchApp(item.appId));
  }
}

} // namespace lambda_shell
