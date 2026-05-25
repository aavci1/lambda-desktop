#include "Shell/ShellAppRegistry.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace lambda_shell {
namespace {

std::string trim(std::string_view value) {
  std::size_t begin = 0;
  while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
  std::size_t end = value.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1u]))) --end;
  return std::string(value.substr(begin, end - begin));
}

std::string lowerAscii(std::string_view value) {
  std::string out(value);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return out;
}

std::string unescapeDesktopString(std::string_view value) {
  std::string out;
  out.reserve(value.size());
  bool escaped = false;
  for (char ch : value) {
    if (!escaped) {
      if (ch == '\\') {
        escaped = true;
      } else {
        out.push_back(ch);
      }
      continue;
    }
    switch (ch) {
    case 's': out.push_back(' '); break;
    case 'n': out.push_back('\n'); break;
    case 't': out.push_back('\t'); break;
    case 'r': out.push_back('\r'); break;
    default: out.push_back(ch); break;
    }
    escaped = false;
  }
  if (escaped) out.push_back('\\');
  return out;
}

std::vector<std::string> splitDesktopList(std::string_view value) {
  std::vector<std::string> list;
  std::string item;
  bool escaped = false;
  for (char ch : value) {
    if (!escaped && ch == '\\') {
      escaped = true;
      item.push_back(ch);
      continue;
    }
    if (!escaped && ch == ';') {
      if (!item.empty()) list.push_back(unescapeDesktopString(item));
      item.clear();
      continue;
    }
    item.push_back(ch);
    escaped = false;
  }
  if (!item.empty()) list.push_back(unescapeDesktopString(item));
  return list;
}

bool parseBool(std::string_view value) {
  std::string text = lowerAscii(trim(value));
  return text == "true" || text == "1" || text == "yes";
}

std::string appIdFromDesktopId(std::string id) {
  if (id.ends_with(".desktop")) id.resize(id.size() - 8u);
  return id;
}

void addToken(std::vector<std::string>& out, std::string& token) {
  if (token.empty()) return;
  out.push_back(token);
  token.clear();
}

std::filesystem::path iconCandidate(std::filesystem::path const& dir, std::string const& iconName) {
  static constexpr char const* extensions[] = {".png", ".svg", ".xpm"};
  std::filesystem::path direct = dir / iconName;
  if (std::filesystem::exists(direct)) return direct;
  for (char const* extension : extensions) {
    std::filesystem::path candidate = dir / (iconName + extension);
    if (std::filesystem::exists(candidate)) return candidate;
  }
  return {};
}

std::string desktopIdForPath(std::filesystem::path const& root, std::filesystem::path const& path) {
  std::error_code ec;
  std::filesystem::path relative = std::filesystem::relative(path, root, ec);
  if (ec || relative.empty()) relative = path.filename();
  std::string id;
  for (auto const& part : relative) {
    if (!id.empty()) id.push_back('-');
    id += part.string();
  }
  return id;
}

} // namespace

std::optional<DesktopEntry> parseDesktopEntry(std::string_view text, std::string id) {
  DesktopEntry entry;
  entry.id = std::move(id);
  bool inDesktopEntry = false;
  bool sawDesktopEntry = false;
  std::istringstream input{std::string(text)};
  std::string line;
  while (std::getline(input, line)) {
    std::string_view view(line);
    if (auto comment = view.find('#'); comment != std::string_view::npos) view = view.substr(0, comment);
    std::string stripped = trim(view);
    if (stripped.empty()) continue;
    if (stripped.front() == '[' && stripped.back() == ']') {
      inDesktopEntry = stripped == "[Desktop Entry]";
      sawDesktopEntry = sawDesktopEntry || inDesktopEntry;
      continue;
    }
    if (!inDesktopEntry) continue;
    auto equals = stripped.find('=');
    if (equals == std::string::npos) continue;
    std::string key = stripped.substr(0, equals);
    std::string value = stripped.substr(equals + 1u);
    if (key == "Name") entry.name = unescapeDesktopString(value);
    else if (key == "GenericName") entry.genericName = unescapeDesktopString(value);
    else if (key == "Comment") entry.comment = unescapeDesktopString(value);
    else if (key == "Icon") entry.icon = unescapeDesktopString(value);
    else if (key == "Exec") entry.exec = unescapeDesktopString(value);
    else if (key == "TryExec") entry.tryExec = unescapeDesktopString(value);
    else if (key == "NoDisplay") entry.noDisplay = parseBool(value);
    else if (key == "Hidden") entry.hidden = parseBool(value);
    else if (key == "Categories") entry.categories = splitDesktopList(value);
    else if (key == "Keywords") entry.keywords = splitDesktopList(value);
    else if (key == "MimeType") entry.mimeTypes = splitDesktopList(value);
    else if (key == "StartupWMClass") entry.startupWmClass = unescapeDesktopString(value);
  }
  if (!sawDesktopEntry || entry.name.empty()) return std::nullopt;
  return entry;
}

bool desktopEntryVisible(DesktopEntry const& entry, TryExecResolver const& tryExecResolver) {
  if (entry.hidden || entry.noDisplay || entry.name.empty() || entry.exec.empty()) return false;
  if (!entry.tryExec.empty() && tryExecResolver && !tryExecResolver(entry.tryExec)) return false;
  return true;
}

AppRegistryEntry appEntryFromDesktopEntry(DesktopEntry const& entry) {
  return AppRegistryEntry{
      .appId = appIdFromDesktopId(entry.id.empty() ? entry.name : entry.id),
      .name = entry.name,
      .icon = entry.icon,
      .command = entry.exec,
      .local = false,
      .noDisplay = entry.noDisplay,
      .hidden = entry.hidden,
      .keywords = entry.keywords,
      .mimeTypes = entry.mimeTypes,
      .startupWmClass = entry.startupWmClass,
  };
}

std::vector<std::string> parseDesktopExec(std::string_view exec, std::optional<std::filesystem::path> file) {
  std::vector<std::string> tokens;
  std::string token;
  bool quoted = false;
  bool escaped = false;
  for (std::size_t i = 0; i < exec.size(); ++i) {
    char ch = exec[i];
    if (escaped) {
      token.push_back(ch);
      escaped = false;
      continue;
    }
    if (ch == '\\') {
      escaped = true;
      continue;
    }
    if (ch == '"') {
      quoted = !quoted;
      continue;
    }
    if (!quoted && std::isspace(static_cast<unsigned char>(ch))) {
      addToken(tokens, token);
      continue;
    }
    if (ch == '%' && i + 1u < exec.size()) {
      char code = exec[++i];
      switch (code) {
      case '%': token.push_back('%'); break;
      case 'f':
      case 'u':
        if (file) token += file->string();
        break;
      case 'F':
      case 'U':
        addToken(tokens, token);
        if (file) tokens.push_back(file->string());
        break;
      case 'i':
      case 'c':
      case 'k':
      case 'd':
      case 'D':
      case 'n':
      case 'N':
      case 'v':
      case 'm':
        break;
      default:
        break;
      }
      continue;
    }
    token.push_back(ch);
  }
  addToken(tokens, token);
  return tokens;
}

bool shellAppIdMatches(std::string_view requested, std::string_view actual) {
  if (requested == actual) return true;
  std::string req = lowerAscii(requested);
  std::string app = lowerAscii(actual);
  if (req == app) return true;
  if (req == "terminal" && (app == "lambda-terminal" || app == "foot")) return true;
  if (req == "browser" && (app == "firefox" || app == "org.mozilla.firefox")) return true;
  if (req == "files" && (app == "lambda-files" || app == "files" || app == "org.gnome.nautilus" ||
                         app == "nautilus" || app == "thunar")) {
    return true;
  }
  if (req == "settings" && app == "lambda-settings") return true;
  return false;
}

std::vector<AppRegistryEntry> discoverInstalledDesktopApps(std::vector<std::filesystem::path> const& applicationDirs,
                                                           TryExecResolver const& tryExecResolver) {
  std::vector<AppRegistryEntry> apps;
  std::set<std::string> seen;
  for (auto const& dir : applicationDirs) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec) || ec) continue;
    for (auto const& entry : std::filesystem::recursive_directory_iterator(dir, ec)) {
      if (ec) break;
      if (!entry.is_regular_file(ec) || ec || entry.path().extension() != ".desktop") continue;
      std::ifstream file(entry.path());
      if (!file) continue;
      std::ostringstream contents;
      contents << file.rdbuf();
      auto desktopEntry = parseDesktopEntry(contents.str(), desktopIdForPath(dir, entry.path()));
      if (!desktopEntry || !desktopEntryVisible(*desktopEntry, tryExecResolver)) continue;
      AppRegistryEntry app = appEntryFromDesktopEntry(*desktopEntry);
      if (!seen.insert(app.appId).second) continue;
      apps.push_back(std::move(app));
    }
  }
  std::stable_sort(apps.begin(), apps.end(), [](auto const& a, auto const& b) {
    return lowerAscii(a.name) < lowerAscii(b.name);
  });
  return apps;
}

std::vector<AppRegistryEntry> discoverLocalExampleApps(std::filesystem::path const& examplesDir,
                                                       std::vector<std::string> const& appNames) {
  std::vector<AppRegistryEntry> apps;
  for (auto const& name : appNames) {
    std::filesystem::path executable = examplesDir / name;
    if (!std::filesystem::exists(executable)) continue;
    AppRegistryEntry app;
    app.appId = name;
    app.name = name.starts_with("lambda-") ? name.substr(7u) : name;
    app.icon = name;
    app.command = executable.string();
    app.local = true;
    apps.push_back(std::move(app));
  }
  return apps;
}

std::vector<AppRegistryEntry> mergeAppRegistryEntries(std::vector<AppRegistryEntry> installed,
                                                      std::vector<AppRegistryEntry> local) {
  std::vector<AppRegistryEntry> merged = std::move(local);
  for (auto& app : installed) {
    bool replaced = false;
    for (auto const& localApp : merged) {
      if (shellAppIdMatches(localApp.appId, app.appId) || shellAppIdMatches(app.appId, localApp.appId)) {
        replaced = true;
        break;
      }
    }
    if (!replaced) merged.push_back(std::move(app));
  }
  return merged;
}

std::filesystem::path lookupIconThemePath(std::filesystem::path const& themeRoot,
                                          std::string const& iconName,
                                          int preferredSize) {
  if (iconName.empty()) return {};
  if (std::filesystem::path iconPath(iconName); iconPath.is_absolute() && std::filesystem::exists(iconPath)) {
    return iconPath;
  }

  std::vector<std::filesystem::path> candidates{
      themeRoot / std::to_string(preferredSize) / "apps",
      themeRoot / (std::to_string(preferredSize) + "x" + std::to_string(preferredSize)) / "apps",
      themeRoot / std::to_string(preferredSize) / "mimetypes",
      themeRoot / (std::to_string(preferredSize) + "x" + std::to_string(preferredSize)) / "mimetypes",
      themeRoot / std::to_string(preferredSize) / "places",
      themeRoot / (std::to_string(preferredSize) + "x" + std::to_string(preferredSize)) / "places",
      themeRoot / "scalable" / "apps",
      themeRoot / "scalable" / "mimetypes",
      themeRoot / "scalable" / "places",
      themeRoot / "apps",
      themeRoot / "mimetypes",
      themeRoot / "places",
      themeRoot,
  };
  for (auto const& dir : candidates) {
    if (auto candidate = iconCandidate(dir, iconName); !candidate.empty()) return candidate;
  }
  return {};
}

} // namespace lambda_shell
