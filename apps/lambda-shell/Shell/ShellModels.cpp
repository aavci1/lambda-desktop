#include "Shell/ShellModels.hpp"

#include "Shell/ShellIpc.hpp"
#include "Shell/UI/LambdaShellTypes.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <set>
#include <sstream>
#include <utility>

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

bool validColorScheme(std::string_view value) {
  return value == "no-preference" || value == "prefer-dark" || value == "prefer-light";
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

bool usableWindowAppId(std::string_view appId) {
  return !appId.empty() && appId != "unknown";
}

int recentBoost(AppRegistryEntry const& app, std::vector<std::string> const& recentAppIds) {
  for (std::size_t i = 0; i < recentAppIds.size(); ++i) {
    if (shellAppIdMatches(app.appId, recentAppIds[i])) return static_cast<int>(50u - std::min<std::size_t>(i, 50u));
  }
  return 0;
}

int queryScoreText(std::string_view title,
                   std::string_view id,
                   std::vector<std::string_view> keywords,
                   std::string_view query) {
  if (query.empty()) return 10;
  std::string q = lowerAscii(query);
  std::string name = lowerAscii(title);
  std::string lowerId = lowerAscii(id);
  if (name.starts_with(q) || lowerId.starts_with(q)) return 1000;
  if (acronym(title).starts_with(q)) return 850;
  for (auto const& keyword : keywords) {
    if (lowerAscii(keyword).starts_with(q)) return 760;
  }
  if (containsCaseInsensitive(title, q) || containsCaseInsensitive(id, q)) return 700;
  if (fuzzyMatch(title, q) || fuzzyMatch(id, q)) return 600;
  return 0;
}

int queryScore(AppRegistryEntry const& app, std::string_view query) {
  std::vector<std::string_view> keywords;
  keywords.reserve(app.keywords.size());
  for (auto const& keyword : app.keywords) keywords.push_back(keyword);
  return queryScoreText(app.name, app.appId, keywords, query);
}

std::vector<std::string_view> stringViews(std::vector<std::string> const& strings) {
  std::vector<std::string_view> views;
  views.reserve(strings.size());
  for (auto const& string : strings) views.push_back(string);
  return views;
}

std::string tomlQuote(std::string_view value) {
  std::string output = "\"";
  for (char ch : value) {
    if (ch == '"' || ch == '\\') output.push_back('\\');
    output.push_back(ch);
  }
  output.push_back('"');
  return output;
}

std::string tomlStringArray(std::vector<std::string> const& values) {
  std::string output = "[";
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i > 0) output += ", ";
    output += tomlQuote(values[i]);
  }
  output += "]";
  return output;
}

std::optional<std::int64_t> readTomlInt(toml::table const& table, char const* key) {
  return table[key].value<std::int64_t>();
}

std::optional<bool> readTomlBool(toml::table const& table, char const* key) {
  return table[key].value<bool>();
}

std::optional<double> readTomlDouble(toml::table const& table, char const* key) {
  if (auto value = table[key].value<double>()) return *value;
  if (auto value = table[key].value<std::int64_t>()) return static_cast<double>(*value);
  return std::nullopt;
}

std::optional<std::string> readTomlString(toml::table const& table, char const* key) {
  return table[key].value<std::string>();
}

std::optional<int> hexNibble(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return 10 + ch - 'a';
  if (ch >= 'A' && ch <= 'F') return 10 + ch - 'A';
  return std::nullopt;
}

std::optional<std::uint8_t> hexByte(std::string_view value, std::size_t offset) {
  auto high = hexNibble(value[offset]);
  auto low = hexNibble(value[offset + 1u]);
  if (!high || !low) return std::nullopt;
  return static_cast<std::uint8_t>((*high << 4) | *low);
}

std::optional<lambda::Color> parseHexColor(std::string_view value) {
  if ((value.size() != 7u && value.size() != 9u) || value.front() != '#') return std::nullopt;
  auto r = hexByte(value, 1u);
  auto g = hexByte(value, 3u);
  auto b = hexByte(value, 5u);
  auto a = value.size() == 9u ? hexByte(value, 7u) : std::optional<std::uint8_t>{static_cast<std::uint8_t>(255u)};
  if (!r || !g || !b || !a) return std::nullopt;
  float constexpr scale = 1.f / 255.f;
  return lambda::Color{*r * scale, *g * scale, *b * scale, *a * scale};
}

std::optional<lambda::Color> readTomlColor(toml::table const& table, char const* key) {
  auto value = readTomlString(table, key);
  if (!value) return std::nullopt;
  return parseHexColor(*value);
}

std::uint8_t colorByte(float value) {
  return static_cast<std::uint8_t>(std::clamp(value, 0.f, 1.f) * 255.f + 0.5f);
}

std::string tomlColor(lambda::Color color) {
  std::ostringstream out;
  out << "\"#" << std::hex << std::nouppercase << std::setfill('0')
      << std::setw(2) << static_cast<int>(colorByte(color.r))
      << std::setw(2) << static_cast<int>(colorByte(color.g))
      << std::setw(2) << static_cast<int>(colorByte(color.b))
      << std::setw(2) << static_cast<int>(colorByte(color.a))
      << "\"";
  return out.str();
}

std::vector<std::string> readTomlStringArray(toml::table const& table, char const* key) {
  std::vector<std::string> strings;
  auto* array = table[key].as_array();
  if (!array) return strings;
  for (auto const& item : *array) {
    if (auto value = item.value<std::string>()) strings.push_back(*value);
  }
  return strings;
}

bool jsonBoolField(std::string_view object, std::string_view name, bool fallback = false) {
  std::string const key = "\"" + std::string(name) + "\"";
  std::size_t pos = object.find(key);
  if (pos == std::string_view::npos) return fallback;
  pos = object.find(':', pos + key.size());
  if (pos == std::string_view::npos) return fallback;
  ++pos;
  while (pos < object.size() && std::isspace(static_cast<unsigned char>(object[pos]))) ++pos;
  if (object.substr(pos, 4) == "true") return true;
  if (object.substr(pos, 5) == "false") return false;
  return fallback;
}

std::vector<std::string_view> jsonArrayObjects(std::string_view json, std::string_view field) {
  std::vector<std::string_view> objects;
  std::string const key = "\"" + std::string(field) + "\"";
  std::size_t pos = json.find(key);
  if (pos == std::string_view::npos) return objects;
  pos = json.find('[', pos + key.size());
  if (pos == std::string_view::npos) return objects;

  bool inString = false;
  bool escaped = false;
  int depth = 0;
  std::size_t objectStart = std::string_view::npos;
  for (std::size_t i = pos + 1u; i < json.size(); ++i) {
    char const ch = json[i];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (inString && ch == '\\') {
      escaped = true;
      continue;
    }
    if (ch == '"') {
      inString = !inString;
      continue;
    }
    if (inString) continue;
    if (ch == '[' && depth == 0) continue;
    if (ch == ']' && depth == 0) break;
    if (ch == '{') {
      if (depth == 0) objectStart = i;
      ++depth;
      continue;
    }
    if (ch == '}') {
      if (depth <= 0) break;
      --depth;
      if (depth == 0 && objectStart != std::string_view::npos) {
        objects.push_back(json.substr(objectStart, i - objectStart + 1u));
        objectStart = std::string_view::npos;
      }
    }
  }
  return objects;
}

} // namespace

NotificationCenterModel::NotificationCenterModel(std::size_t historyLimit)
    : historyLimit_(std::max<std::size_t>(1u, historyLimit)) {}

std::uint64_t NotificationCenterModel::add(std::string appId, std::string title, std::string body) {
  std::uint64_t id = nextId_++;
  upsert(id, std::move(appId), std::move(title), std::move(body));
  return id;
}

std::uint64_t NotificationCenterModel::upsert(std::uint64_t id,
                                              std::string appId,
                                              std::string title,
                                              std::string body) {
  if (id == 0) {
    return add(std::move(appId), std::move(title), std::move(body));
  }

  auto existing = std::find_if(notifications_.begin(), notifications_.end(), [&](auto const& notification) {
    return notification.id == id;
  });
  if (existing != notifications_.end()) {
    notifications_.erase(existing);
  }

  notifications_.insert(notifications_.begin(),
                        Notification{.id = id,
                                     .appId = std::move(appId),
                                     .title = std::move(title),
                                     .body = std::move(body)});
  nextId_ = std::max(nextId_, id + 1u);
  trimHistory();
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

void NotificationCenterModel::setHistoryLimit(std::size_t limit) {
  historyLimit_ = std::max<std::size_t>(1u, limit);
  trimHistory();
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

void NotificationCenterModel::trimHistory() {
  if (notifications_.size() > historyLimit_) notifications_.resize(historyLimit_);
}

ClipboardHistoryModel::ClipboardHistoryModel(std::size_t limit)
    : ClipboardHistoryModel(ClipboardHistoryPolicy{.maxEntries = limit}) {}

ClipboardHistoryModel::ClipboardHistoryModel(ClipboardHistoryPolicy policy) {
  setPolicy(std::move(policy));
}

void ClipboardHistoryModel::setPolicy(ClipboardHistoryPolicy policy) {
  policy.maxEntries = std::max<std::size_t>(1u, policy.maxEntries);
  policy.maxTextBytes = std::max<std::size_t>(1u, policy.maxTextBytes);
  policy_ = std::move(policy);
  if (entries_.size() > policy_.maxEntries) entries_.resize(policy_.maxEntries);
}

void ClipboardHistoryModel::addText(std::string text, ClipboardHistorySource source) {
  if (!policy_.enabled || text.empty()) return;
  if (source == ClipboardHistorySource::PrimarySelection && !policy_.recordPrimarySelection) return;
  if (text.size() > policy_.maxTextBytes) return;
  entries_.erase(std::remove(entries_.begin(), entries_.end(), text), entries_.end());
  entries_.insert(entries_.begin(), std::move(text));
  if (entries_.size() > policy_.maxEntries) entries_.resize(policy_.maxEntries);
}

void ClipboardHistoryModel::clear() {
  entries_.clear();
}

std::vector<std::string> ClipboardHistoryModel::entriesForPersistence() const {
  if (!policy_.enabled || !policy_.persist) return {};
  return entries_;
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
    if (!usableWindowAppId(window.appId)) continue;
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

std::vector<LauncherResult> buildLauncherResults(std::vector<AppRegistryEntry> const& apps,
                                                 std::vector<ShellWindowSnapshot> const& windows,
                                                 std::vector<SettingsPanelEntry> const& settingsPanels,
                                                 std::vector<ShellActionEntry> const& shellActions,
                                                 std::vector<std::string> const& recentAppIds,
                                                 std::string_view query,
                                                 std::size_t limit,
                                                 std::vector<LauncherProviderError> const& errors) {
  std::vector<LauncherResult> results;

  for (auto const& error : errors) {
    if (error.message.empty()) continue;
    results.push_back({
        .kind = LauncherResultKind::ErrorState,
        .id = error.providerId.empty() ? "launcher-error" : error.providerId,
        .providerId = error.providerId.empty() ? "error" : error.providerId,
        .title = "Provider unavailable",
        .subtitle = error.message,
        .icon = "dialog-warning",
        .score = 20'000,
        .disabled = true,
    });
  }

  for (auto const& app : apps) {
    if (app.hidden || app.noDisplay) continue;
    int score = queryScore(app, query);
    if (score == 0) continue;
    bool const running = appRunning(app, windows);
    if (running) score += 100;
    score += recentBoost(app, recentAppIds);
    score += 30;
    results.push_back({
        .kind = LauncherResultKind::App,
        .id = app.appId,
        .providerId = "apps",
        .title = app.name.empty() ? app.appId : app.name,
        .subtitle = app.command,
        .icon = app.icon,
        .score = score,
        .running = running,
    });
  }

  for (auto const& window : windows) {
    std::vector<std::string_view> keywords{window.appId};
    int score = queryScoreText(window.title.empty() ? window.appId : window.title,
                               std::to_string(window.id),
                               keywords,
                               query);
    if (score == 0) continue;
    score += 40;
    if (window.focused) score += 25;
    results.push_back({
        .kind = LauncherResultKind::Window,
        .id = std::to_string(window.id),
        .providerId = "windows",
        .title = window.title.empty() ? window.appId : window.title,
        .subtitle = window.appId,
        .icon = window.appId,
        .score = score,
        .running = true,
        .windowId = window.id,
    });
  }

  for (auto const& panel : settingsPanels) {
    int score = queryScoreText(panel.title, panel.id, stringViews(panel.keywords), query);
    if (score == 0) continue;
    results.push_back({
        .kind = LauncherResultKind::SettingsPanel,
        .id = panel.id,
        .providerId = "settings",
        .title = panel.title,
        .subtitle = panel.subtitle,
        .icon = panel.icon,
        .score = score + 20,
    });
  }

  for (auto const& action : shellActions) {
    int score = queryScoreText(action.title, action.id, stringViews(action.keywords), query);
    if (score == 0) continue;
    results.push_back({
        .kind = LauncherResultKind::ShellAction,
        .id = action.id,
        .providerId = "shell-actions",
        .title = action.title,
        .subtitle = action.subtitle,
        .icon = action.icon,
        .score = score + 10,
    });
  }

  std::stable_sort(results.begin(), results.end(), [](auto const& a, auto const& b) {
    if (a.score != b.score) return a.score > b.score;
    if (a.providerId != b.providerId) return a.providerId < b.providerId;
    return lowerAscii(a.title) < lowerAscii(b.title);
  });

  if (results.empty()) {
    results.push_back({
        .kind = LauncherResultKind::EmptyState,
        .id = query.empty() ? "launcher-empty" : "launcher-no-results",
        .providerId = "empty",
        .title = query.empty() ? "Start typing" : "No results",
        .subtitle = query.empty() ? std::string{"Apps, windows, settings, and shell actions appear here."}
                                  : std::string{},
        .icon = "system-search",
        .disabled = true,
    });
  } else if (results.size() > limit) {
    results.resize(limit);
  }

  return results;
}

LauncherAction launcherActivationForResult(LauncherResult const& result) {
  if (result.disabled) return {};
  switch (result.kind) {
  case LauncherResultKind::App:
    return {
        .kind = result.running ? LauncherActionKind::FocusApp : LauncherActionKind::LaunchApp,
        .target = result.id,
    };
  case LauncherResultKind::Window:
    return {
        .kind = LauncherActionKind::FocusWindow,
        .target = result.id,
        .windowId = result.windowId,
    };
  case LauncherResultKind::SettingsPanel:
    return {
        .kind = LauncherActionKind::OpenSettingsPanel,
        .target = result.id,
    };
  case LauncherResultKind::ShellAction:
    return {
        .kind = LauncherActionKind::RunShellAction,
        .target = result.id,
    };
  case LauncherResultKind::EmptyState:
  case LauncherResultKind::ErrorState:
    return {};
  }
  return {};
}

std::vector<QuickSettingState> quickSettingsSummary(std::vector<QuickSettingState> providers) {
  std::stable_sort(providers.begin(), providers.end(), [](auto const& a, auto const& b) {
    if (a.availability != b.availability) return a.availability > b.availability;
    return a.id < b.id;
  });
  return providers;
}

std::vector<ShellStatusModuleState> shellStatusModules(ShellSystemStatusSnapshot const& snapshot,
                                                       std::vector<std::string> const& moduleIds) {
  auto availabilityFor = [](std::string const& value) {
    std::string const lower = lowerAscii(value);
    if (lower.empty() || lower == "unavailable") return QuickSettingAvailability::Unavailable;
    if (lower == "unknown") return QuickSettingAvailability::Unknown;
    return QuickSettingAvailability::Available;
  };
  auto make = [&](std::string const& id, std::string label, std::string value) {
    QuickSettingAvailability const availability = availabilityFor(value);
    return ShellStatusModuleState{
        .id = id,
        .label = std::move(label),
        .value = std::move(value),
        .availability = availability,
    };
  };

  std::vector<ShellStatusModuleState> modules;
  modules.reserve(moduleIds.size());
  for (auto const& id : moduleIds) {
    if (id == "network") {
      modules.push_back(make(id, "Network", snapshot.network));
    } else if (id == "wifi") {
      modules.push_back(make(id, "Wi-Fi", snapshot.wifi));
    } else if (id == "bluetooth") {
      modules.push_back(make(id, "Bluetooth", snapshot.bluetooth));
    } else if (id == "volume" || id == "audio") {
      modules.push_back(make(id, "Volume", snapshot.volume));
    } else if (id == "battery" || id == "power") {
      modules.push_back(make(id, "Battery", snapshot.battery));
    } else if (id == "media" || id == "now-playing") {
      modules.push_back(make(id, "Media", snapshot.media));
    } else if (id == "notifications") {
      modules.push_back({
          .id = id,
          .label = "Notifications",
          .value = {},
          .availability = QuickSettingAvailability::Available,
      });
    } else if (id == "clipboard") {
      modules.push_back({
          .id = id,
          .label = "Clipboard",
          .value = {},
          .availability = QuickSettingAvailability::Available,
      });
    } else if (id == "clock") {
      modules.push_back({
          .id = id,
          .label = "Clock",
          .value = {},
          .availability = QuickSettingAvailability::Available,
      });
    } else {
      modules.push_back({
          .id = id,
          .label = id,
          .value = {},
          .availability = QuickSettingAvailability::Unavailable,
      });
    }
  }
  return modules;
}

ShellDesktopSnapshot parseShellSnapshot(std::string_view json) {
  ShellDesktopSnapshot snapshot;

  for (std::string_view object : jsonArrayObjects(json, "apps")) {
    AppRegistryEntry app;
    app.appId = lambda::shell::jsonStringField(object, "id");
    if (app.appId.empty()) app.appId = lambda::shell::jsonStringField(object, "appId");
    app.name = lambda::shell::jsonStringField(object, "name");
    app.icon = lambda::shell::jsonStringField(object, "icon");
    app.command = lambda::shell::jsonStringField(object, "command");
    if (!app.appId.empty()) snapshot.apps.push_back(std::move(app));
  }

  for (std::string_view object : jsonArrayObjects(json, "windows")) {
    ShellWindowSnapshot window;
    window.id = lambda::shell::jsonUintField(object, "id");
    window.appId = lambda::shell::jsonStringField(object, "appId");
    window.title = lambda::shell::jsonStringField(object, "title");
    window.focused = jsonBoolField(object, "focused");
    window.minimized = lambda::shell::jsonStringField(object, "state") == "minimized";
    if (window.id != 0 || !window.appId.empty()) snapshot.windows.push_back(std::move(window));
  }

  snapshot.activeWindowId = lambda::shell::jsonUintField(json, "activeWindowId");
  return snapshot;
}

ShellConfig defaultShellConfig() {
  return ShellConfig{};
}

ClipboardHistoryPolicy clipboardHistoryPolicy(ShellConfig const& config) {
  return ClipboardHistoryPolicy{
      .enabled = config.clipboardHistoryEnabled,
      .persist = config.clipboardHistoryPersist,
      .maxEntries = config.clipboardHistoryMaxEntries,
      .maxTextBytes = config.clipboardHistoryMaxTextBytes,
      .recordPrimarySelection = config.clipboardHistoryRecordPrimarySelection,
  };
}

ShellConfig parseShellConfig(std::string_view tomlText) {
  ShellConfig config = defaultShellConfig();
  toml::table root;
  try {
    root = toml::parse(std::string(tomlText));
  } catch (...) {
    return config;
  }

  if (auto* appearance = root["appearance"].as_table()) {
    if (auto value = readTomlString(*appearance, "icon_theme")) config.iconTheme = *value;
    if (auto value = readTomlString(*appearance, "symbolic_icon_theme")) config.symbolicIconTheme = *value;
    if (auto value = readTomlString(*appearance, "color_scheme"); value && validColorScheme(*value)) {
      config.colorScheme = *value;
    }
    if (auto value = readTomlColor(*appearance, "accent_color")) config.accentColor = *value;
    if (auto value = readTomlBool(*appearance, "high_contrast")) config.highContrast = *value;
    if (auto value = readTomlBool(*appearance, "reduced_motion")) config.reducedMotion = *value;
  }

  if (auto* dock = root["dock"].as_table()) {
    if (auto value = readTomlString(*dock, "position");
        value && (*value == "left" || *value == "right" || *value == "bottom")) {
      config.dockPosition = *value;
    }
    if (auto value = readTomlBool(*dock, "auto_hide")) config.dockAutoHide = *value;
    if (auto value = readTomlBool(*dock, "full_width")) config.dockFullWidth = *value;
    if (auto value = readTomlInt(*dock, "item_size");
        value && *value >= kDockMinItemSize && *value <= kDockMaxItemSize) {
      config.dockItemSize = static_cast<int>(*value);
    }
    if (auto value = readTomlInt(*dock, "bottom_gap"); value && *value >= 0 && *value <= 64) {
      config.dockBottomGap = static_cast<int>(*value);
    }
    if (auto value = readTomlInt(*dock, "corner_radius"); value && *value >= 0 && *value <= 48) {
      config.dockCornerRadius = static_cast<int>(*value);
    }
    if (auto value = readTomlDouble(*dock, "blur_radius"); value && *value >= 0.0 && *value <= 160.0) {
      config.dockMaterial.blurRadius = static_cast<float>(*value);
    }
    if (auto value = readTomlDouble(*dock, "opacity"); value && *value >= 0.0 && *value <= 1.0) {
      config.dockMaterial.opacity = static_cast<float>(*value);
    }
    if (auto value = readTomlColor(*dock, "base_color")) config.dockMaterial.baseColor = *value;
    if (auto value = readTomlColor(*dock, "tint_color")) config.dockMaterial.tintColor = *value;
    if (auto value = readTomlColor(*dock, "border_color")) config.dockMaterial.borderColor = *value;
    if (auto value = readTomlString(*dock, "clock_format")) config.dockClockFormat = *value;
    if (auto value = readTomlBool(*dock, "show_running_unpinned")) config.showRunningUnpinned = *value;
    if (auto value = readTomlBool(*dock, "show_tooltips")) config.dockShowTooltips = *value;
    auto pins = readTomlStringArray(*dock, "pinned");
    if (!pins.empty()) config.dockPinned = std::move(pins);
  }

  if (auto* quickSettings = root["quick_settings"].as_table()) {
    auto modules = readTomlStringArray(*quickSettings, "modules");
    if (!modules.empty()) config.quickSettingsModules = std::move(modules);
  }

  if (auto* notifications = root["notifications"].as_table()) {
    if (auto value = readTomlBool(*notifications, "enabled")) config.notificationsEnabled = *value;
    if (auto value = readTomlBool(*notifications, "do_not_disturb")) config.notificationsDoNotDisturb = *value;
    if (auto value = readTomlInt(*notifications, "banner_timeout_seconds"); value && *value >= 0 && *value <= 3600) {
      config.notificationBannerTimeoutSeconds = static_cast<int>(*value);
    }
    if (auto value = readTomlInt(*notifications, "history_limit"); value && *value > 0 && *value <= 1000) {
      config.notificationHistoryLimit = static_cast<std::size_t>(*value);
    }
    if (auto value = readTomlBool(*notifications, "show_previews")) config.notificationShowPreviews = *value;
  }

  if (auto* clipboard = root["clipboard_history"].as_table()) {
    if (auto value = readTomlBool(*clipboard, "enabled")) config.clipboardHistoryEnabled = *value;
    if (auto value = readTomlBool(*clipboard, "persist")) config.clipboardHistoryPersist = *value;
    if (auto value = readTomlInt(*clipboard, "max_entries"); value && *value > 0 && *value <= 1000) {
      config.clipboardHistoryMaxEntries = static_cast<std::size_t>(*value);
    }
    if (auto value = readTomlInt(*clipboard, "max_text_bytes"); value && *value > 0 && *value <= 128 * 1024 * 1024) {
      config.clipboardHistoryMaxTextBytes = static_cast<std::size_t>(*value);
    }
    if (auto value = readTomlBool(*clipboard, "record_primary_selection")) {
      config.clipboardHistoryRecordPrimarySelection = *value;
    }
  }

  if (auto* launcher = root["launcher"].as_table()) {
    if (auto value = readTomlString(*launcher, "empty_query");
        value && (*value == "recommended" || *value == "apps" || *value == "recent")) {
      config.launcherEmptyQuery = *value;
    }
    if (auto value = readTomlInt(*launcher, "max_results"); value && *value > 0 && *value <= 100) {
      config.launcherMaxResults = static_cast<std::size_t>(*value);
    }
    if (auto value = readTomlBool(*launcher, "show_categories")) config.launcherShowCategories = *value;
  }
  return config;
}

std::string writeShellConfigToml(ShellConfig const& config) {
  std::ostringstream out;
  out << "[appearance]\n";
  out << "icon_theme = " << tomlQuote(config.iconTheme) << "\n";
  out << "symbolic_icon_theme = " << tomlQuote(config.symbolicIconTheme) << "\n";
  out << "color_scheme = " << tomlQuote(validColorScheme(config.colorScheme) ? config.colorScheme : "no-preference") << "\n";
  out << "accent_color = " << tomlColor(config.accentColor) << "\n";
  out << "high_contrast = " << (config.highContrast ? "true" : "false") << "\n";
  out << "reduced_motion = " << (config.reducedMotion ? "true" : "false") << "\n\n";

  out << "[dock]\n";
  out << "position = " << tomlQuote(config.dockPosition) << "\n";
  out << "auto_hide = " << (config.dockAutoHide ? "true" : "false") << "\n";
  out << "full_width = " << (config.dockFullWidth ? "true" : "false") << "\n";
  out << "item_size = " << clampedDockItemSize(config.dockItemSize) << "\n";
  out << "bottom_gap = " << config.dockBottomGap << "\n";
  out << "corner_radius = " << config.dockCornerRadius << "\n";
  out << "blur_radius = " << config.dockMaterial.blurRadius << "\n";
  out << "opacity = " << config.dockMaterial.opacity << "\n";
  out << "base_color = " << tomlColor(config.dockMaterial.baseColor) << "\n";
  out << "tint_color = " << tomlColor(config.dockMaterial.tintColor) << "\n";
  out << "border_color = " << tomlColor(config.dockMaterial.borderColor) << "\n";
  out << "clock_format = " << tomlQuote(config.dockClockFormat) << "\n";
  out << "show_running_unpinned = " << (config.showRunningUnpinned ? "true" : "false") << "\n";
  out << "show_tooltips = " << (config.dockShowTooltips ? "true" : "false") << "\n";
  out << "pinned = " << tomlStringArray(config.dockPinned) << "\n\n";

  out << "[quick_settings]\n";
  out << "modules = " << tomlStringArray(config.quickSettingsModules) << "\n\n";

  out << "[notifications]\n";
  out << "enabled = " << (config.notificationsEnabled ? "true" : "false") << "\n";
  out << "do_not_disturb = " << (config.notificationsDoNotDisturb ? "true" : "false") << "\n";
  out << "banner_timeout_seconds = " << config.notificationBannerTimeoutSeconds << "\n";
  out << "history_limit = " << config.notificationHistoryLimit << "\n";
  out << "show_previews = " << (config.notificationShowPreviews ? "true" : "false") << "\n\n";

  out << "[clipboard_history]\n";
  out << "enabled = " << (config.clipboardHistoryEnabled ? "true" : "false") << "\n";
  out << "persist = " << (config.clipboardHistoryPersist ? "true" : "false") << "\n";
  out << "max_entries = " << config.clipboardHistoryMaxEntries << "\n";
  out << "max_text_bytes = " << config.clipboardHistoryMaxTextBytes << "\n";
  out << "record_primary_selection = " << (config.clipboardHistoryRecordPrimarySelection ? "true" : "false")
      << "\n\n";

  out << "[launcher]\n";
  out << "empty_query = " << tomlQuote(config.launcherEmptyQuery) << "\n";
  out << "max_results = " << config.launcherMaxResults << "\n";
  out << "show_categories = " << (config.launcherShowCategories ? "true" : "false") << "\n";
  return out.str();
}

std::filesystem::path shellConfigPath() {
  if (char const* explicitPath = std::getenv("LAMBDA_SHELL_CONFIG"); explicitPath && *explicitPath) {
    return explicitPath;
  }
  if (char const* configHome = std::getenv("XDG_CONFIG_HOME"); configHome && *configHome) {
    return std::filesystem::path(configHome) / "lambda-shell" / "config.toml";
  }
  if (char const* home = std::getenv("HOME"); home && *home) {
    return std::filesystem::path(home) / ".config" / "lambda-shell" / "config.toml";
  }
  return std::filesystem::temp_directory_path() / "lambda-shell" / "config.toml";
}

ShellConfigLoadResult loadShellConfig(std::filesystem::path path) {
  if (path.empty()) path = shellConfigPath();
  ShellConfigLoadResult result;
  result.config = defaultShellConfig();
  result.path = path;

  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) {
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
      result.error = "Could not create shell config directory.";
      return result;
    }
    std::ofstream out(path);
    if (!out) {
      result.error = "Could not create shell config.";
      return result;
    }
    out << writeShellConfigToml(result.config);
    result.created = true;
    return result;
  }

  std::ifstream in(path);
  if (!in) {
    result.error = "Could not read shell config.";
    return result;
  }
  std::ostringstream contents;
  contents << in.rdbuf();
  result.config = parseShellConfig(contents.str());
  return result;
}

} // namespace lambda_shell
