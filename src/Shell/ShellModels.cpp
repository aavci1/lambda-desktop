#include "Shell/ShellModels.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <optional>
#include <set>
#include <sstream>

namespace lambda_shell {
namespace {

std::string lowerAscii(std::string_view value) {
  std::string output(value);
  std::transform(output.begin(), output.end(), output.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return output;
}

bool containsCaseInsensitive(std::string_view haystack, std::string_view needle) {
  return lowerAscii(haystack).find(lowerAscii(needle)) != std::string::npos;
}

std::string acronym(std::string_view text) {
  std::string out;
  bool atWord = true;
  for (char ch : text) {
    if (std::isalnum(static_cast<unsigned char>(ch))) {
      if (atWord) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
      atWord = false;
    } else {
      atWord = true;
    }
  }
  return out;
}

bool fuzzyMatch(std::string_view haystack, std::string_view needle) {
  std::string h = lowerAscii(haystack);
  std::string n = lowerAscii(needle);
  std::size_t cursor = 0;
  for (char ch : n) {
    cursor = h.find(ch, cursor);
    if (cursor == std::string::npos) return false;
    ++cursor;
  }
  return true;
}

bool appRunning(AppRegistryEntry const& app, std::vector<ShellWindowSnapshot> const& windows) {
  return std::any_of(windows.begin(), windows.end(), [&](auto const& window) {
    return shellAppIdMatches(app.appId, window.appId);
  });
}

int recentBoost(AppRegistryEntry const& app, std::vector<std::string> const& recentAppIds) {
  for (std::size_t i = 0; i < recentAppIds.size(); ++i) {
    if (shellAppIdMatches(app.appId, recentAppIds[i])) return static_cast<int>(50u - std::min<std::size_t>(i, 50u));
  }
  return 0;
}

int queryScore(AppRegistryEntry const& app, std::string_view query) {
  if (query.empty()) return 10;
  std::string q = lowerAscii(query);
  std::string name = lowerAscii(app.name);
  std::string id = lowerAscii(app.appId);
  if (name.starts_with(q) || id.starts_with(q)) return 1000;
  if (acronym(app.name).starts_with(q)) return 850;
  for (auto const& keyword : app.keywords) {
    if (lowerAscii(keyword).starts_with(q)) return 760;
  }
  if (containsCaseInsensitive(app.name, q) || containsCaseInsensitive(app.appId, q)) return 700;
  if (fuzzyMatch(app.name, q) || fuzzyMatch(app.appId, q)) return 600;
  return 0;
}

std::string trim(std::string_view value) {
  std::size_t begin = 0;
  while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
  std::size_t end = value.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1u]))) --end;
  return std::string(value.substr(begin, end - begin));
}

bool parseBoolValue(std::string value, bool fallback) {
  value = lowerAscii(trim(value));
  if (value == "true" || value == "1" || value == "yes") return true;
  if (value == "false" || value == "0" || value == "no") return false;
  return fallback;
}

std::optional<long long> parseIntegerValue(std::string value) {
  value = trim(value);
  char* end = nullptr;
  long long parsed = std::strtoll(value.c_str(), &end, 10);
  if (end == value.c_str() || *end != '\0') return std::nullopt;
  return parsed;
}

std::vector<std::string> parseStringArray(std::string_view value) {
  std::vector<std::string> strings;
  auto left = value.find('[');
  auto right = value.rfind(']');
  if (left == std::string_view::npos || right == std::string_view::npos || right <= left) return strings;
  std::string current;
  bool quoted = false;
  bool escaped = false;
  for (char ch : value.substr(left + 1u, right - left - 1u)) {
    if (escaped) {
      current.push_back(ch);
      escaped = false;
      continue;
    }
    if (ch == '\\') {
      escaped = true;
      continue;
    }
    if (ch == '"') {
      if (quoted) {
        strings.push_back(current);
        current.clear();
      }
      quoted = !quoted;
      continue;
    }
    if (quoted) current.push_back(ch);
  }
  return strings;
}

} // namespace

NotificationCenterModel::NotificationCenterModel(std::size_t historyLimit)
    : historyLimit_(std::max<std::size_t>(1u, historyLimit)) {}

std::uint64_t NotificationCenterModel::add(std::string appId, std::string title, std::string body) {
  std::uint64_t id = nextId_++;
  notifications_.insert(notifications_.begin(),
                        Notification{.id = id,
                                     .appId = std::move(appId),
                                     .title = std::move(title),
                                     .body = std::move(body)});
  if (notifications_.size() > historyLimit_) notifications_.resize(historyLimit_);
  return id;
}

bool NotificationCenterModel::dismiss(std::uint64_t id) {
  for (auto& notification : notifications_) {
    if (notification.id == id) {
      notification.dismissed = true;
      return true;
    }
  }
  return false;
}

void NotificationCenterModel::clearAll() {
  for (auto& notification : notifications_) {
    notification.dismissed = true;
  }
}

std::vector<Notification> NotificationCenterModel::visible() const {
  if (doNotDisturb_) return {};
  std::vector<Notification> output;
  for (auto const& notification : notifications_) {
    if (!notification.dismissed) output.push_back(notification);
  }
  return output;
}

int NotificationCenterModel::groupCount(std::string_view appId) const {
  return static_cast<int>(std::count_if(notifications_.begin(), notifications_.end(), [&](auto const& notification) {
    return !notification.dismissed && notification.appId == appId;
  }));
}

ClipboardHistoryModel::ClipboardHistoryModel(std::size_t limit)
    : limit_(std::max<std::size_t>(1u, limit)) {}

void ClipboardHistoryModel::addText(std::string text) {
  if (!enabled_ || text.empty()) return;
  entries_.erase(std::remove(entries_.begin(), entries_.end(), text), entries_.end());
  entries_.insert(entries_.begin(), std::move(text));
  if (entries_.size() > limit_) entries_.resize(limit_);
}

void ClipboardHistoryModel::clear() {
  entries_.clear();
}

std::vector<DockModelEntry> buildDockModel(std::vector<AppRegistryEntry> const& pinnedApps,
                                           std::vector<ShellWindowSnapshot> const& windows) {
  std::vector<DockModelEntry> entries;
  for (auto const& app : pinnedApps) {
    DockModelEntry entry;
    entry.appId = app.appId;
    entry.name = app.name;
    entry.icon = app.icon;
    entry.pinned = true;
    for (auto const& window : windows) {
      if (!shellAppIdMatches(app.appId, window.appId)) continue;
      entry.running = true;
      entry.focused = entry.focused || window.focused;
      entry.minimized = entry.minimized || window.minimized;
      entry.windowIds.push_back(window.id);
    }
    entries.push_back(std::move(entry));
  }

  for (auto const& window : windows) {
    bool represented = std::any_of(entries.begin(), entries.end(), [&](auto const& entry) {
      return shellAppIdMatches(entry.appId, window.appId);
    });
    if (represented) continue;
    entries.push_back(DockModelEntry{
        .appId = window.appId,
        .name = window.title.empty() ? window.appId : window.title,
        .icon = window.appId,
        .pinned = false,
        .running = true,
        .focused = window.focused,
        .minimized = window.minimized,
        .windowIds = {window.id},
    });
  }
  return entries;
}

DockClickAction dockClickAction(DockModelEntry const& entry) {
  if (entry.appId.empty()) return {};
  if (!entry.running) return {.kind = DockClickKind::LaunchApp, .appId = entry.appId};
  if (entry.minimized && !entry.windowIds.empty()) return {.kind = DockClickKind::RestoreApp, .appId = entry.appId};
  return {.kind = DockClickKind::FocusApp, .appId = entry.appId};
}

std::vector<LauncherRankedResult> rankLauncherApps(std::vector<AppRegistryEntry> const& apps,
                                                   std::vector<ShellWindowSnapshot> const& windows,
                                                   std::vector<std::string> const& recentAppIds,
                                                   std::string_view query,
                                                   std::size_t limit) {
  std::vector<LauncherRankedResult> ranked;
  for (auto const& app : apps) {
    if (app.hidden || app.noDisplay) continue;
    int score = queryScore(app, query);
    if (score == 0) continue;
    bool running = appRunning(app, windows);
    if (running) score += 100;
    score += recentBoost(app, recentAppIds);
    ranked.push_back({.app = app, .score = score, .running = running});
  }
  std::stable_sort(ranked.begin(), ranked.end(), [](auto const& a, auto const& b) {
    if (a.score != b.score) return a.score > b.score;
    return lowerAscii(a.app.name) < lowerAscii(b.app.name);
  });
  if (ranked.size() > limit) ranked.resize(limit);
  return ranked;
}

std::vector<QuickSettingState> quickSettingsSummary(std::vector<QuickSettingState> providers) {
  std::stable_sort(providers.begin(), providers.end(), [](auto const& a, auto const& b) {
    if (a.availability != b.availability) return a.availability > b.availability;
    return a.id < b.id;
  });
  return providers;
}

ShellConfig defaultShellConfig() {
  return ShellConfig{};
}

ShellConfig parseShellConfig(std::string_view tomlText) {
  ShellConfig config = defaultShellConfig();
  std::istringstream input{std::string(tomlText)};
  std::string section;
  std::string line;
  while (std::getline(input, line)) {
    std::string_view view(line);
    if (auto comment = view.find('#'); comment != std::string_view::npos) view = view.substr(0, comment);
    std::string stripped = trim(view);
    if (stripped.empty()) continue;
    if (stripped.front() == '[' && stripped.back() == ']') {
      section = stripped.substr(1u, stripped.size() - 2u);
      continue;
    }
    auto equals = stripped.find('=');
    if (equals == std::string::npos) continue;
    std::string key = trim(std::string_view(stripped).substr(0, equals));
    std::string value = trim(std::string_view(stripped).substr(equals + 1u));
    std::string fullKey = section.empty() ? key : section + "." + key;
    if (fullKey == "appearance.icon_theme") {
      if (value.size() >= 2u && value.front() == '"' && value.back() == '"') {
        config.iconTheme = value.substr(1u, value.size() - 2u);
      }
    } else if (fullKey == "appearance.symbolic_icon_theme") {
      if (value.size() >= 2u && value.front() == '"' && value.back() == '"') {
        config.symbolicIconTheme = value.substr(1u, value.size() - 2u);
      }
    } else if (fullKey == "appearance.icon_size") {
      if (auto parsed = parseIntegerValue(value); parsed && *parsed > 0 && *parsed <= 512) {
        config.iconSize = static_cast<int>(*parsed);
      }
    } else if (fullKey == "appearance.reduced_motion") {
      config.reducedMotion = parseBoolValue(value, config.reducedMotion);
    } else if (fullKey == "dock.position") {
      if (value == "\"left\"" || value == "\"right\"" || value == "\"bottom\"") {
        config.dockPosition = value.substr(1u, value.size() - 2u);
      }
    } else if (fullKey == "dock.auto_hide") {
      config.dockAutoHide = parseBoolValue(value, config.dockAutoHide);
    } else if (fullKey == "dock.show_running_unpinned") {
      config.showRunningUnpinned = parseBoolValue(value, config.showRunningUnpinned);
    } else if (fullKey == "dock.show_tooltips") {
      config.dockShowTooltips = parseBoolValue(value, config.dockShowTooltips);
    } else if (fullKey == "dock.pinned") {
      auto pins = parseStringArray(value);
      if (!pins.empty()) config.dockPinned = std::move(pins);
    } else if (fullKey == "top_bar.clock_format") {
      if (value.size() >= 2u && value.front() == '"' && value.back() == '"') {
        config.topBarClockFormat = value.substr(1u, value.size() - 2u);
      }
    } else if (fullKey == "top_bar.show_active_title") {
      config.topBarShowActiveTitle = parseBoolValue(value, config.topBarShowActiveTitle);
    } else if (fullKey == "top_bar.modules") {
      auto modules = parseStringArray(value);
      if (!modules.empty()) config.topBarModules = std::move(modules);
    } else if (fullKey == "quick_settings.modules") {
      auto modules = parseStringArray(value);
      if (!modules.empty()) config.quickSettingsModules = std::move(modules);
    } else if (fullKey == "notifications.enabled") {
      config.notificationsEnabled = parseBoolValue(value, config.notificationsEnabled);
    } else if (fullKey == "notifications.do_not_disturb") {
      config.notificationsDoNotDisturb = parseBoolValue(value, config.notificationsDoNotDisturb);
    } else if (fullKey == "notifications.banner_timeout_seconds") {
      if (auto parsed = parseIntegerValue(value); parsed && *parsed >= 0 && *parsed <= 3600) {
        config.notificationBannerTimeoutSeconds = static_cast<int>(*parsed);
      }
    } else if (fullKey == "notifications.history_limit") {
      if (auto parsed = parseIntegerValue(value); parsed && *parsed > 0 && *parsed <= 1000) {
        config.notificationHistoryLimit = static_cast<std::size_t>(*parsed);
      }
    } else if (fullKey == "notifications.show_previews") {
      config.notificationShowPreviews = parseBoolValue(value, config.notificationShowPreviews);
    } else if (fullKey == "clipboard_history.enabled") {
      config.clipboardHistoryEnabled = parseBoolValue(value, config.clipboardHistoryEnabled);
    } else if (fullKey == "clipboard_history.persist") {
      config.clipboardHistoryPersist = parseBoolValue(value, config.clipboardHistoryPersist);
    } else if (fullKey == "clipboard_history.max_entries") {
      if (auto parsed = parseIntegerValue(value); parsed && *parsed > 0 && *parsed <= 1000) {
        config.clipboardHistoryMaxEntries = static_cast<std::size_t>(*parsed);
      }
    } else if (fullKey == "clipboard_history.max_text_bytes") {
      if (auto parsed = parseIntegerValue(value); parsed && *parsed > 0 && *parsed <= 128 * 1024 * 1024) {
        config.clipboardHistoryMaxTextBytes = static_cast<std::size_t>(*parsed);
      }
    } else if (fullKey == "clipboard_history.record_primary_selection") {
      config.clipboardHistoryRecordPrimarySelection =
          parseBoolValue(value, config.clipboardHistoryRecordPrimarySelection);
    } else if (fullKey == "launcher.empty_query") {
      if (value == "\"recommended\"" || value == "\"apps\"" || value == "\"recent\"") {
        config.launcherEmptyQuery = value.substr(1u, value.size() - 2u);
      }
    } else if (fullKey == "launcher.max_results") {
      if (auto parsed = parseIntegerValue(value); parsed && *parsed > 0 && *parsed <= 100) {
        config.launcherMaxResults = static_cast<std::size_t>(*parsed);
      }
    } else if (fullKey == "launcher.show_categories") {
      config.launcherShowCategories = parseBoolValue(value, config.launcherShowCategories);
    }
  }
  return config;
}

} // namespace lambda_shell
