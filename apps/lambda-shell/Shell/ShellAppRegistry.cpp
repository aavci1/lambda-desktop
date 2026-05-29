#include "Shell/ShellAppRegistry.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <optional>
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

std::vector<std::string> splitColonList(std::string_view value) {
  std::vector<std::string> parts;
  std::string part;
  for (char ch : value) {
    if (ch == ':') {
      parts.push_back(part);
      part.clear();
      continue;
    }
    part.push_back(ch);
  }
  parts.push_back(part);
  return parts;
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

std::vector<std::string> splitCommaList(std::string_view value) {
  std::vector<std::string> list;
  std::string item;
  for (char ch : value) {
    if (ch == ',') {
      std::string stripped = trim(item);
      if (!stripped.empty()) list.push_back(std::move(stripped));
      item.clear();
      continue;
    }
    item.push_back(ch);
  }
  std::string stripped = trim(item);
  if (!stripped.empty()) list.push_back(std::move(stripped));
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

std::vector<std::string> iconThemeInherits(std::filesystem::path const& themeRoot) {
  std::ifstream input(themeRoot / "index.theme");
  if (!input) return {};
  std::vector<std::string> inherits;
  std::string line;
  while (std::getline(input, line)) {
    std::string_view view(line);
    if (auto comment = view.find('#'); comment != std::string_view::npos) view = view.substr(0, comment);
    std::string stripped = trim(view);
    auto equals = stripped.find('=');
    if (equals == std::string::npos) continue;
    std::string key = trim(std::string_view(stripped).substr(0, equals));
    if (key != "Inherits") continue;
    inherits = splitCommaList(stripped.substr(equals + 1u));
    break;
  }
  return inherits;
}

std::filesystem::path configuredShellPath() {
  if (char const* explicitPath = std::getenv("LAMBDA_SHELL_CONFIG"); explicitPath && *explicitPath) {
    return explicitPath;
  }
  if (char const* configHome = std::getenv("XDG_CONFIG_HOME"); configHome && *configHome) {
    return std::filesystem::path{configHome} / "lambda-shell" / "config.toml";
  }
  if (char const* home = std::getenv("HOME"); home && *home) {
    return std::filesystem::path{home} / ".config" / "lambda-shell" / "config.toml";
  }
  return {};
}

std::string iconThemeFromShellConfig(std::filesystem::path const& path) {
  std::ifstream input(path);
  if (!input) return {};
  std::ostringstream contents;
  contents << input.rdbuf();
  try {
    toml::table const root = toml::parse(contents.str());
    if (auto* appearance = root["appearance"].as_table()) {
      if (auto theme = (*appearance)["icon_theme"].value<std::string>()) return *theme;
    }
  } catch (...) {
    return {};
  }
  return {};
}

bool executableFile(std::filesystem::path const& path) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec) || ec) return false;
  std::filesystem::perms const permissions = std::filesystem::status(path, ec).permissions();
  if (ec) return false;
  constexpr auto executableBits = std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec |
                                  std::filesystem::perms::others_exec;
  return (permissions & executableBits) != std::filesystem::perms::none;
}

void addUniquePath(std::vector<std::filesystem::path>& paths, std::filesystem::path path) {
  if (path.empty()) return;
  path = path.lexically_normal();
  auto const key = path.string();
  auto const duplicate = std::any_of(paths.begin(), paths.end(), [&](auto const& existing) {
    return existing.lexically_normal().string() == key;
  });
  if (!duplicate) paths.push_back(std::move(path));
}

std::filesystem::path currentExecutablePath() {
#if defined(__linux__)
  std::error_code ec;
  auto path = std::filesystem::read_symlink("/proc/self/exe", ec);
  if (!ec) return path;
#endif
  return {};
}

std::string localLambdaAppTitle(std::string_view appId) {
  static std::map<std::string, std::string> const titles{
      {"lambda-editor", "Editor"},
      {"lambda-files", "Files"},
      {"lambda-browser", "Browser"},
      {"lambda-preview", "Preview"},
      {"lambda-settings", "Settings"},
      {"lambda-terminal", "Terminal"},
  };
  if (auto it = titles.find(std::string(appId)); it != titles.end()) return it->second;
  if (appId.starts_with("lambda-")) return std::string(appId.substr(7u));
  return std::string(appId);
}

std::string localLambdaAppBundleName(std::string_view appId) {
  static std::map<std::string, std::string> const bundleNames{
      {"lambda-editor", "Lambda Editor"},
      {"lambda-files", "Lambda Files"},
      {"lambda-browser", "Lambda Browser"},
      {"lambda-preview", "Lambda Preview"},
      {"lambda-settings", "Lambda Settings"},
      {"lambda-terminal", "Lambda Terminal"},
  };
  if (auto it = bundleNames.find(std::string(appId)); it != bundleNames.end()) return it->second;
  return localLambdaAppTitle(appId);
}

std::optional<std::filesystem::path> localLambdaAppExecutable(std::filesystem::path const& appDir,
                                                             std::string const& appName) {
  std::string const bundleName = localLambdaAppBundleName(appName);
  std::vector<std::filesystem::path> candidates{
      appDir / appName / appName,
      appDir / appName / (bundleName + ".app") / "Contents" / "MacOS" / bundleName,
      appDir / (bundleName + ".app") / "Contents" / "MacOS" / bundleName,
  };
  for (auto const& candidate : candidates) {
    if (executableFile(candidate)) return candidate;
  }
  return std::nullopt;
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

std::string shellQuote(std::string_view value) {
  std::string quoted{"'"};
  for (char c : value) {
    if (c == '\'') {
      quoted += "'\\''";
    } else {
      quoted.push_back(c);
    }
  }
  quoted.push_back('\'');
  return quoted;
}

std::string shellCommandFromArgs(std::vector<std::string> const& args) {
  std::string command;
  for (auto const& arg : args) {
    if (arg.empty()) continue;
    if (!command.empty()) command.push_back(' ');
    command += shellQuote(arg);
  }
  return command;
}

bool registryContainsApp(std::vector<AppRegistryEntry> const& apps, std::string_view appId) {
  return std::any_of(apps.begin(), apps.end(), [appId](AppRegistryEntry const& app) {
    return shellAppIdMatches(appId, app.appId) || shellAppIdMatches(app.appId, appId);
  });
}

struct IconThemeDir {
  std::filesystem::path relativePath;
  int size = 48;
  int minSize = 48;
  int maxSize = 48;
  int threshold = 2;
  int scale = 1;
  std::string type = "Threshold";
};

int iconDirNominalSize(IconThemeDir const& dir) {
  return std::max(1, dir.size * std::max(1, dir.scale));
}

std::optional<int> parseInteger(std::string_view value) {
  std::string text = trim(value);
  char* end = nullptr;
  long parsed = std::strtol(text.c_str(), &end, 10);
  if (end == text.c_str() || *end != '\0') return std::nullopt;
  return static_cast<int>(parsed);
}

std::vector<IconThemeDir> iconThemeDirs(std::filesystem::path const& themeRoot) {
  std::ifstream input(themeRoot / "index.theme");
  if (!input) return {};

  std::vector<std::string> directoryNames;
  std::map<std::string, IconThemeDir> dirs;
  std::string section;
  std::string line;
  while (std::getline(input, line)) {
    std::string_view view(line);
    if (auto comment = view.find('#'); comment != std::string_view::npos) view = view.substr(0, comment);
    std::string stripped = trim(view);
    if (stripped.empty()) continue;
    if (stripped.front() == '[' && stripped.back() == ']') {
      section = stripped.substr(1u, stripped.size() - 2u);
      if (section != "Icon Theme" && !section.empty()) {
        dirs.try_emplace(section, IconThemeDir{.relativePath = section});
      }
      continue;
    }
    auto equals = stripped.find('=');
    if (equals == std::string::npos) continue;
    std::string key = trim(std::string_view(stripped).substr(0, equals));
    std::string value = trim(std::string_view(stripped).substr(equals + 1u));
    if (section == "Icon Theme") {
      if (key == "Directories" || key == "ScaledDirectories") {
        auto names = splitCommaList(value);
        directoryNames.insert(directoryNames.end(), names.begin(), names.end());
      }
      continue;
    }
    auto found = dirs.find(section);
    if (found == dirs.end()) continue;
    IconThemeDir& dir = found->second;
    if (key == "Size") {
      if (auto parsed = parseInteger(value)) dir.size = *parsed;
    } else if (key == "MinSize") {
      if (auto parsed = parseInteger(value)) dir.minSize = *parsed;
    } else if (key == "MaxSize") {
      if (auto parsed = parseInteger(value)) dir.maxSize = *parsed;
    } else if (key == "Threshold") {
      if (auto parsed = parseInteger(value)) dir.threshold = *parsed;
    } else if (key == "Scale") {
      if (auto parsed = parseInteger(value)) dir.scale = std::max(1, *parsed);
    } else if (key == "Type") {
      dir.type = value;
    }
  }

  std::vector<IconThemeDir> ordered;
  std::set<std::string> seen;
  for (auto const& name : directoryNames) {
    auto found = dirs.find(name);
    if (found == dirs.end() || !seen.insert(name).second) continue;
    IconThemeDir dir = found->second;
    if (dir.minSize == 48 && dir.maxSize == 48) {
      dir.minSize = dir.size;
      dir.maxSize = dir.size;
    }
    ordered.push_back(std::move(dir));
  }
  for (auto& [name, dir] : dirs) {
    if (!seen.insert(name).second) continue;
    if (dir.minSize == 48 && dir.maxSize == 48) {
      dir.minSize = dir.size;
      dir.maxSize = dir.size;
    }
    ordered.push_back(std::move(dir));
  }
  return ordered;
}

int iconDirDistance(IconThemeDir const& dir, int preferredSize) {
  int const scale = std::max(1, dir.scale);
  int const size = std::max(1, dir.size * scale);
  int const minSize = std::max(1, dir.minSize * scale);
  int const maxSize = std::max(minSize, dir.maxSize * scale);
  if (dir.type == "Scalable") {
    if (preferredSize >= minSize && preferredSize <= maxSize) return 0;
    return preferredSize < minSize ? minSize - preferredSize : preferredSize - maxSize;
  }
  int const distance = std::abs(size - preferredSize);
  if (dir.type == "Threshold" && distance <= std::max(0, dir.threshold * scale)) return 0;
  return distance;
}

bool betterIconMatch(IconThemeDir const& candidateDir,
                     int candidateDistance,
                     IconThemeDir const& currentDir,
                     int currentDistance,
                     int preferredSize) {
  if (candidateDistance == 0 || currentDistance == 0) {
    return candidateDistance < currentDistance;
  }
  bool const candidateUndersized = iconDirNominalSize(candidateDir) < preferredSize;
  bool const currentUndersized = iconDirNominalSize(currentDir) < preferredSize;
  if (candidateUndersized != currentUndersized) {
    return !candidateUndersized;
  }
  return candidateDistance < currentDistance;
}

IconThemeDir fallbackIconDirForSize(std::string const& size) {
  IconThemeDir dir;
  dir.relativePath = size;
  if (size == "scalable") {
    dir.size = 64;
    dir.minSize = 16;
    dir.maxSize = 512;
    dir.type = "Scalable";
    return dir;
  }
  if (size == "symbolic") {
    dir.size = 16;
    dir.minSize = 16;
    dir.maxSize = 16;
    dir.type = "Fixed";
    return dir;
  }
  std::string numeric = size;
  if (auto x = numeric.find('x'); x != std::string::npos) {
    numeric = numeric.substr(0, x);
  }
  if (auto parsed = parseInteger(numeric)) {
    dir.size = *parsed;
    dir.minSize = *parsed;
    dir.maxSize = *parsed;
  }
  dir.type = "Fixed";
  return dir;
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
  if (req == "browser" && (app == "lambda-browser" || app == "firefox" || app == "org.mozilla.firefox")) {
    return true;
  }
  if (req == "editor" && app == "lambda-editor") return true;
  if (req == "files" && (app == "lambda-files" || app == "files" || app == "org.gnome.nautilus" ||
                         app == "nautilus" || app == "thunar")) {
    return true;
  }
  if (req == "preview" && app == "lambda-preview") return true;
  if (req == "settings" && app == "lambda-settings") return true;
  return false;
}

bool executableInPath(std::string const& executable) {
  if (executable.empty()) return false;
  std::filesystem::path path{executable};
  if (path.has_parent_path()) return executableFile(path);
  char const* pathEnv = std::getenv("PATH");
  if (!pathEnv || !*pathEnv) return false;
  for (auto const& entry : splitColonList(pathEnv)) {
    std::filesystem::path dir = entry.empty() ? std::filesystem::path{"."} : std::filesystem::path{entry};
    if (executableFile(dir / executable)) return true;
  }
  return false;
}

std::vector<std::filesystem::path> defaultXdgApplicationDirs() {
  std::vector<std::filesystem::path> dirs;
  if (char const* xdgDataHome = std::getenv("XDG_DATA_HOME"); xdgDataHome && *xdgDataHome) {
    dirs.emplace_back(std::filesystem::path{xdgDataHome} / "applications");
  } else if (char const* home = std::getenv("HOME"); home && *home) {
    dirs.emplace_back(std::filesystem::path{home} / ".local" / "share" / "applications");
  }

  char const* xdgDataDirs = std::getenv("XDG_DATA_DIRS");
  std::string_view dataDirs = xdgDataDirs && *xdgDataDirs ? std::string_view{xdgDataDirs}
                                                          : std::string_view{"/usr/local/share:/usr/share"};
  for (auto const& entry : splitColonList(dataDirs)) {
    if (!entry.empty()) dirs.emplace_back(std::filesystem::path{entry} / "applications");
  }
  return dirs;
}

std::vector<std::filesystem::path> defaultIconThemeRoots(std::string const& themeName) {
  std::vector<std::filesystem::path> bases;
  if (char const* home = std::getenv("HOME"); home && *home) {
    bases.emplace_back(std::filesystem::path{home} / ".icons");
  }
  if (char const* xdgDataHome = std::getenv("XDG_DATA_HOME"); xdgDataHome && *xdgDataHome) {
    bases.emplace_back(std::filesystem::path{xdgDataHome} / "icons");
  } else if (char const* home = std::getenv("HOME"); home && *home) {
    bases.emplace_back(std::filesystem::path{home} / ".local" / "share" / "icons");
  }

  char const* xdgDataDirs = std::getenv("XDG_DATA_DIRS");
  std::string_view dataDirs = xdgDataDirs && *xdgDataDirs ? std::string_view{xdgDataDirs}
                                                          : std::string_view{"/usr/local/share:/usr/share"};
  for (auto const& entry : splitColonList(dataDirs)) {
    if (!entry.empty()) bases.emplace_back(std::filesystem::path{entry} / "icons");
  }

  std::vector<std::filesystem::path> roots;
  std::set<std::string> seenThemes;
  std::set<std::string> seenRoots;
  auto addTheme = [&](auto const& self, std::string const& theme, int depth) -> void {
    if (theme.empty() || depth > 8 || !seenThemes.insert(theme).second) return;
    for (auto const& base : bases) {
      std::filesystem::path root = base / theme;
      std::error_code ec;
      if (!std::filesystem::is_directory(root, ec) || ec) continue;
      std::string key = root.string();
      if (seenRoots.insert(key).second) roots.push_back(root);
      for (auto const& inherited : iconThemeInherits(root)) {
        self(self, inherited, depth + 1);
      }
    }
  };

  std::string const resolvedTheme = themeName.empty() ? configuredIconThemeName() : themeName;
  addTheme(addTheme, resolvedTheme, 0);
  addTheme(addTheme, "hicolor", 0);

  if (char const* xdgDataHome = std::getenv("XDG_DATA_HOME"); xdgDataHome && *xdgDataHome) {
    roots.emplace_back(std::filesystem::path{xdgDataHome} / "pixmaps");
  }
  for (auto const& entry : splitColonList(dataDirs)) {
    if (!entry.empty()) roots.emplace_back(std::filesystem::path{entry} / "pixmaps");
  }
  return roots;
}

std::vector<std::filesystem::path> defaultLocalLambdaAppDirs() {
  std::vector<std::filesystem::path> dirs;
  if (char const* appDirs = std::getenv("LAMBDA_APP_DIRS"); appDirs && *appDirs) {
    for (auto const& entry : splitColonList(appDirs)) {
      if (!entry.empty()) addUniquePath(dirs, entry);
    }
  }

  auto const executablePath = currentExecutablePath();
  if (!executablePath.empty()) {
    auto const executableDir = executablePath.parent_path();
    addUniquePath(dirs, executableDir.parent_path());
  }

  std::error_code ec;
  auto const current = std::filesystem::current_path(ec);
  if (!ec) {
    addUniquePath(dirs, current / "apps");
  }
  return dirs;
}

std::vector<std::string> defaultLocalLambdaAppNames() {
  return {"lambda-browser", "lambda-editor", "lambda-files", "lambda-preview", "lambda-settings", "lambda-terminal"};
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

std::vector<AppRegistryEntry> discoverLocalLambdaApps(std::vector<std::filesystem::path> const& appDirs,
                                                      std::vector<std::string> const& appNames) {
  std::vector<AppRegistryEntry> apps;
  static std::map<std::string, std::string> const themedIcons{
      {"lambda-editor", "accessories-text-editor"},
      {"lambda-files", "system-file-manager"},
      {"lambda-browser", "web-browser"},
      {"lambda-preview", "image-viewer"},
      {"lambda-settings", "preferences-system"},
      {"lambda-terminal", "utilities-terminal"},
  };
  std::set<std::string> seen;
  for (auto const& name : appNames) {
    if (!seen.insert(name).second) continue;
    for (auto const& appDir : appDirs) {
      auto executable = localLambdaAppExecutable(appDir, name);
      if (!executable) continue;
      AppRegistryEntry app;
      app.appId = name;
      app.name = localLambdaAppTitle(name);
      auto icon = themedIcons.find(name);
      app.icon = icon == themedIcons.end() ? name : icon->second;
      app.command = executable->string();
      app.local = true;
      apps.push_back(std::move(app));
      break;
    }
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

std::vector<AppRegistryEntry> builtinFallbackAppEntries() {
  AppRegistryEntry terminal;
  terminal.appId = "terminal";
  terminal.name = "Terminal";
  terminal.icon = "utilities-terminal";
  terminal.command = "lambda-terminal";

  AppRegistryEntry browser;
  browser.appId = "browser";
  browser.name = "Browser";
  browser.icon = "web-browser";
  browser.command = "lambda-browser";

  return {std::move(terminal), std::move(browser)};
}

std::vector<AppRegistryEntry> buildDefaultAppRegistry(std::vector<std::filesystem::path> const& appDirs,
                                                      std::vector<std::filesystem::path> const& applicationDirs,
                                                      TryExecResolver const& tryExecResolver) {
  auto installed = discoverInstalledDesktopApps(applicationDirs, tryExecResolver);
  auto local = discoverLocalLambdaApps(appDirs, defaultLocalLambdaAppNames());
  auto registry = mergeAppRegistryEntries(std::move(installed), std::move(local));
  for (auto fallback : builtinFallbackAppEntries()) {
    if (!registryContainsApp(registry, fallback.appId)) registry.push_back(std::move(fallback));
  }
  return registry;
}

std::optional<std::string> resolveAppLaunchCommand(std::string_view requestedAppId,
                                                   std::vector<AppRegistryEntry> const& apps) {
  for (auto const& app : apps) {
    if (app.hidden || app.noDisplay || app.command.empty()) continue;
    if (!shellAppIdMatches(requestedAppId, app.appId) && !shellAppIdMatches(app.appId, requestedAppId)) continue;
    std::vector<std::string> args = app.local ? std::vector<std::string>{app.command} : parseDesktopExec(app.command);
    std::string command = shellCommandFromArgs(args);
    if (!command.empty()) return command;
  }
  return std::nullopt;
}

std::filesystem::path lookupIconThemePath(std::filesystem::path const& themeRoot,
                                          std::string const& iconName,
                                          int preferredSize) {
  if (iconName.empty()) return {};
  if (std::filesystem::path iconPath(iconName); iconPath.is_absolute() && std::filesystem::exists(iconPath)) {
    return iconPath;
  }

  struct Match {
    std::filesystem::path path;
    IconThemeDir dir;
    int distance = 0;
  };
  std::optional<Match> best;
  std::set<std::string> seenCandidatePaths;
  auto consider = [&](std::filesystem::path const& path, IconThemeDir const& dir) {
    if (path.empty()) return;
    if (!seenCandidatePaths.insert(path.string()).second) return;
    int const distance = iconDirDistance(dir, preferredSize);
    if (!best || betterIconMatch(dir, distance, best->dir, best->distance, preferredSize)) {
      best = Match{.path = path, .dir = dir, .distance = distance};
    }
  };
  for (auto const& dir : iconThemeDirs(themeRoot)) {
    consider(iconCandidate(themeRoot / dir.relativePath, iconName), dir);
  }

  std::string const size = std::to_string(preferredSize);
  std::string const sizePair = size + "x" + size;
  std::vector<std::string> sizes{size, sizePair, "scalable", "256", "256x256", "128", "128x128", "96", "96x96",
                                 "64", "64x64", "48", "48x48", "32", "32x32", "24", "24x24",
                                 "22", "22x22", "16", "16x16", "symbolic"};
  static constexpr char const* categories[] = {
      "apps", "actions", "categories", "mimetypes", "places", "status", "legacy",
  };
  for (char const* category : categories) {
    for (auto const& candidateSize : sizes) {
      IconThemeDir dir = fallbackIconDirForSize(candidateSize);
      consider(iconCandidate(themeRoot / candidateSize / category, iconName), dir);
      dir.relativePath = std::filesystem::path{category} / candidateSize;
      consider(iconCandidate(themeRoot / dir.relativePath, iconName), dir);
    }
  }
  for (char const* category : categories) {
    IconThemeDir dir;
    dir.relativePath = category;
    consider(iconCandidate(themeRoot / dir.relativePath, iconName), dir);
  }
  IconThemeDir rootDir;
  consider(iconCandidate(themeRoot, iconName), rootDir);
  return best ? best->path : std::filesystem::path{};
}

std::string configuredIconThemeName() {
  if (char const* theme = std::getenv("LAMBDA_ICON_THEME"); theme && *theme) {
    return theme;
  }
  return iconThemeFromShellConfig(configuredShellPath());
}

std::filesystem::path resolveIconThemePath(std::string const& iconName,
                                           std::string const& themeName,
                                           int preferredSize) {
  if (iconName.empty()) return {};
  if (std::filesystem::path iconPath(iconName); iconPath.is_absolute() && std::filesystem::exists(iconPath)) {
    return iconPath;
  }
  std::string const resolvedTheme = themeName.empty() ? configuredIconThemeName() : themeName;
  for (auto const& root : defaultIconThemeRoots(resolvedTheme)) {
    if (auto path = lookupIconThemePath(root, iconName, preferredSize); !path.empty()) return path;
  }
  return {};
}

} // namespace lambda_shell
