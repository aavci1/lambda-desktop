#include "Shell/ShellModel.hpp"

#include "Shell/ShellAppRegistry.hpp"
#include "Shell/ShellJson.hpp"
#include "Shell/ShellModels.hpp"

#include "Shell/ShellIpc.hpp"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <set>
#include <utility>

namespace lambda_shell {
namespace {

AppRegistryEntry appEntry(std::string appId, std::string name, std::string icon, std::string command) {
  AppRegistryEntry app;
  app.appId = std::move(appId);
  app.name = std::move(name);
  app.icon = std::move(icon);
  app.command = std::move(command);
  return app;
}

std::vector<AppRegistryEntry> defaultDockRegistry() {
  auto apps = std::vector<AppRegistryEntry>{
      appEntry("lambda-files", "Files", "system-file-manager", "lambda-files"),
      appEntry("lambda-editor", "Editor", "accessories-text-editor", "lambda-editor"),
      appEntry("lambda-preview", "Preview", "image-viewer", "lambda-preview"),
      appEntry("lambda-terminal", "Terminal", "utilities-terminal", "lambda-terminal"),
      appEntry("lambda-settings", "Settings", "preferences-system", "lambda-settings"),
  };
  auto fallbacks = builtinFallbackAppEntries();
  apps.insert(apps.end(), fallbacks.begin(), fallbacks.end());
  return apps;
}

bool appMatchesPin(AppRegistryEntry const& app, std::string_view pin) {
  return shellAppIdMatches(pin, app.appId) || shellAppIdMatches(app.appId, pin);
}

bool appIdsMatch(std::string_view a, std::string_view b) {
  return shellAppIdMatches(a, b) || shellAppIdMatches(b, a);
}

bool usableWindowAppId(std::string_view appId) {
  return !appId.empty() && appId != "unknown";
}

std::string resolvedIconPath(std::string const& icon, std::string const& theme, int size) {
  auto path = resolveIconThemePath(icon, theme, size);
  return path.empty() ? std::string{} : path.string();
}

std::vector<std::string> dockIconCandidates(std::string const& icon,
                                            std::string const& appId) {
  std::vector<std::string> candidates;
  auto add = [&](std::string value) {
    if (value.empty()) return;
    if (std::find(candidates.begin(), candidates.end(), value) == candidates.end()) {
      candidates.push_back(std::move(value));
    }
  };

  if (std::filesystem::path iconPath{icon}; iconPath.is_absolute()) {
    add(icon);
  }

  if (shellAppIdMatches("settings", appId)) {
    add("systemsettings");
    add("breeze-settings");
    add("preferences-system");
  } else if (shellAppIdMatches("files", appId)) {
    add("system-file-manager");
    add("folder");
  } else if (shellAppIdMatches("terminal", appId)) {
    add("utilities-terminal");
    add("terminal");
  }
  add(icon);
  add(appId);
  return candidates;
}

std::string resolvedDockIconPath(std::string const& icon,
                                 std::string const& appId,
                                 std::string const& kind,
                                 std::string const& theme,
                                 int size) {
  (void)kind;
  for (auto const& candidate : dockIconCandidates(icon, appId)) {
    std::string path = resolvedIconPath(candidate, theme, size);
    if (!path.empty()) return path;
  }
  return {};
}

int scaledIconPixelSize(int logicalSize, float dpiScale) {
  float const scale = std::clamp(dpiScale, 0.5f, 4.f);
  return std::max(1, static_cast<int>(std::ceil(static_cast<float>(logicalSize) * scale)));
}

} // namespace

std::string ShellModel::formatTimeText() {
  return formatTimeText("%a %d %b, %H:%M");
}

std::string ShellModel::formatTimeText(std::string_view format) {
  char buffer[64]{};
  std::time_t now = std::time(nullptr);
  std::tm local{};
  localtime_r(&now, &local);
  std::string fmt = std::string(format.empty() ? std::string_view{"%a %d %b, %H:%M"} : format);
  std::strftime(buffer, sizeof(buffer), fmt.c_str(), &local);
  return buffer;
}

void ShellModel::refreshLauncherResults() {
  launcherResults_.set(lambda_shell::launcherResults(dockItems_.peek(), query_.peek()));
}

void ShellModel::setPreviewFocus(std::string_view appId) {
  auto items = dockItems_.peek();
  for (auto& item : items) {
    item.running = !appId.empty() && item.kind == "app" && appIdsMatch(item.appId, appId);
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
  setDockItems(defaultDockRegistry(), defaultShellConfig());
}

void ShellModel::setDockItems(std::vector<AppRegistryEntry> const& apps, ShellConfig const& config) {
  showRunningUnpinned_ = config.showRunningUnpinned;
  iconTheme_ = config.iconTheme;
  iconSize_ = config.iconSize;
  clockFormat_ = config.dockClockFormat;
  timeText_.set(formatTimeText(clockFormat_));
  int const iconPixelSize = scaledIconPixelSize(iconSize_, dockDpiScale_);
  std::vector<DockItem> items;
  DockItem launcher;
  launcher.id = "launcher";
  launcher.kind = "launcher";
  launcher.label = "Launcher";
  launcher.icon = "lambda";
  items.push_back(std::move(launcher));

  DockItem firstSeparator;
  firstSeparator.id = "sep1";
  firstSeparator.kind = "separator";
  items.push_back(std::move(firstSeparator));

  std::set<std::string> added;
  for (auto const& pin : config.dockPinned) {
    auto found = std::find_if(apps.begin(), apps.end(), [&](AppRegistryEntry const& app) {
      return !app.hidden && !app.noDisplay && !app.command.empty() && appMatchesPin(app, pin);
    });
    if (found == apps.end()) continue;
    std::string appId = found->appId.empty() ? pin : found->appId;
    if (!added.insert(appId).second) continue;
    DockItem item;
    item.id = appId;
    item.kind = "app";
    item.label = found->name.empty() ? appId : found->name;
    item.appId = appId;
    item.pinned = true;
    item.icon = found->icon;
    item.iconPixelSize = iconPixelSize;
    item.iconPath = resolvedDockIconPath(found->icon, item.appId, item.kind, iconTheme_, iconPixelSize);
    items.push_back(std::move(item));
  }
  dockItems_.set(std::move(items));
  refreshLauncherResults();
}

bool ShellModel::setDockDpiScale(float scale) {
  scale = std::clamp(scale, 0.5f, 4.f);
  if (std::abs(scale - dockDpiScale_) < 0.001f) return false;
  dockDpiScale_ = scale;

  int const iconPixelSize = scaledIconPixelSize(iconSize_, dockDpiScale_);
  auto items = dockItems_.peek();
  bool changed = false;
  for (auto& item : items) {
    if (item.kind == "separator" || item.kind == "launcher") continue;
    std::string const nextPath = resolvedDockIconPath(item.icon, item.appId, item.kind, iconTheme_, iconPixelSize);
    if (item.iconPixelSize != iconPixelSize || item.iconPath != nextPath) {
      item.iconPixelSize = iconPixelSize;
      item.iconPath = nextPath;
      changed = true;
    }
  }
  if (!changed) return false;
  dockItems_.set(std::move(items));
  refreshLauncherResults();
  return true;
}

bool ShellModel::dockItemsVisualStateEqual(std::vector<DockItem> const& a,
                                           std::vector<DockItem> const& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i] != b[i]) return false;
  }
  return true;
}

ShellModel::SnapshotChanges ShellModel::applySnapshot(std::string_view json) {
  SnapshotChanges changes{};
  ShellDesktopSnapshot const snapshot = parseShellSnapshot(json);
  auto items = dockItems_.peek();
  items.erase(std::remove_if(items.begin(), items.end(), [](DockItem const& item) {
                return item.kind == "app" && !item.pinned;
              }),
              items.end());
  for (auto& item : items) {
    if (item.kind != "app") continue;
    item.running = false;
    item.focused = false;
  }
  std::string nextTitle;
  for (auto const& window : snapshot.windows) {
    if (!usableWindowAppId(window.appId)) continue;
    bool represented = false;
    for (auto& item : items) {
      if (item.kind != "app" || item.appId.empty()) continue;
      if (appIdsMatch(item.appId, window.appId)) {
        represented = true;
        item.running = true;
        item.focused = item.focused || window.focused;
        if (item.focused) {
          nextTitle = window.title;
          if (nextTitle.empty()) nextTitle = item.label;
        }
        break;
      }
    }
    if (represented || !showRunningUnpinned_) continue;
    auto app = std::find_if(snapshot.apps.begin(), snapshot.apps.end(), [&](AppRegistryEntry const& candidate) {
      return appIdsMatch(candidate.appId, window.appId);
    });
    std::string icon = app == snapshot.apps.end() || app->icon.empty() ? window.appId : app->icon;
    DockItem item;
    item.id = window.appId;
    item.kind = "app";
    item.label = window.title.empty() ? window.appId : window.title;
    item.appId = window.appId;
    item.running = true;
    item.focused = window.focused;
    item.icon = icon;
    item.iconPixelSize = scaledIconPixelSize(iconSize_, dockDpiScale_);
    item.iconPath = resolvedDockIconPath(icon, item.appId, item.kind, iconTheme_, item.iconPixelSize);
    if (item.focused) nextTitle = item.label;
    items.push_back(std::move(item));
  }

  SystemStatus nextStatus{
      .network = snapshot.system.network,
      .wifi = snapshot.system.wifi,
      .bluetooth = snapshot.system.bluetooth,
      .volume = snapshot.system.volume,
      .battery = snapshot.system.battery,
  };

  if (!dockItemsVisualStateEqual(items, dockItems_.peek())) {
    dockItems_.set(std::move(items));
    refreshLauncherResults();
    changes.dockItems = true;
  }
  if (nextTitle != activeTitle_.peek()) {
    activeTitle_.set(std::move(nextTitle));
    changes.activeTitle = true;
  }
  if (!(nextStatus == systemStatus_.peek())) {
    systemStatus_.set(std::move(nextStatus));
    changes.systemStatus = true;
  }
  return changes;
}

bool ShellModel::refreshTimeText() {
  std::string next = formatTimeText(clockFormat_);
  if (next == timeText_.peek()) return false;
  timeText_.set(std::move(next));
  return true;
}

bool ShellModel::setDockClockWidth(int width) {
  int const next = std::max(kDockClockMinWidth, width);
  if (dockClockWidth_.peek() == next) return false;
  dockClockWidth_.set(next);
  return true;
}

void ShellModel::openLauncher() {
  if (launcherOpen_.peek()) return;
  launcherOpen_.set(true);
  launcherUiVisible_.set(false);
  query_.set(std::string{});
  queryCursor_.set(0);
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
  queryCursor_.set(0);
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
  int const cursor = static_cast<int>(query.size());
  query_.set(std::move(query));
  queryCursor_.set(cursor);
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
  std::size_t const insertAt = static_cast<std::size_t>(std::clamp(queryCursor_.peek(), 0, static_cast<int>(next.size())));
  std::size_t const available = 128u - next.size();
  std::string_view const inserted = text.substr(0, available);
  next.insert(insertAt, inserted);
  query_.set(std::move(next));
  queryCursor_.set(static_cast<int>(insertAt + inserted.size()));
  highlighted_.set(0);
  refreshLauncherResults();
}

void ShellModel::backspaceQuery() {
  std::string next = query_.peek();
  int const cursor = std::clamp(queryCursor_.peek(), 0, static_cast<int>(next.size()));
  if (cursor > 0) {
    next.erase(static_cast<std::size_t>(cursor - 1), 1u);
    queryCursor_.set(cursor - 1);
  }
  query_.set(std::move(next));
  refreshLauncherResults();
}

void ShellModel::deleteQueryForward() {
  std::string next = query_.peek();
  int const cursor = std::clamp(queryCursor_.peek(), 0, static_cast<int>(next.size()));
  if (cursor < static_cast<int>(next.size())) {
    next.erase(static_cast<std::size_t>(cursor), 1u);
  }
  query_.set(std::move(next));
  queryCursor_.set(cursor);
  refreshLauncherResults();
}

void ShellModel::moveQueryCursor(int delta) {
  int const next = std::clamp(queryCursor_.peek() + delta, 0, static_cast<int>(query_.peek().size()));
  queryCursor_.set(next);
}

void ShellModel::moveQueryCursorToStart() {
  queryCursor_.set(0);
}

void ShellModel::moveQueryCursorToEnd() {
  queryCursor_.set(static_cast<int>(query_.peek().size()));
}

void ShellModel::activateItem(DockItem const& item,
                              std::function<void(std::string const& line)> sendIpc,
                              std::uint64_t requestId) {
  if (!sendIpc) return;
  if (item.running) {
    sendIpc(lambda::shell::serializeFocusApp(item.appId, requestId));
  } else if (item.kind == "app") {
    sendIpc(lambda::shell::serializeLaunchApp(item.appId, requestId));
  }
}

} // namespace lambda_shell
