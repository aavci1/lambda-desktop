#include "FilesStore.hpp"
#include "FilesTrace.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <system_error>
#include <unordered_map>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace lambda_files {
namespace {

std::optional<std::filesystem::path> pathIfExists(std::filesystem::path path) {
  std::error_code ec;
  if (!std::filesystem::exists(path, ec) || ec) {
    return std::nullopt;
  }
  return std::filesystem::weakly_canonical(path, ec);
}

std::filesystem::path pathFromEnv(char const* name) {
  if (char const* value = std::getenv(name); value && *value) {
    return std::filesystem::path(value);
  }
  return {};
}

std::filesystem::path dataHomeDirectory() {
  if (auto env = pathFromEnv("XDG_DATA_HOME"); !env.empty()) return env;
  return homeDirectory() / ".local" / "share";
}

std::filesystem::path configHomeDirectory() {
  if (auto env = pathFromEnv("XDG_CONFIG_HOME"); !env.empty()) return env;
  return homeDirectory() / ".config";
}

bool ciLess(std::string const& a, std::string const& b) {
  return std::lexicographical_compare(
      a.begin(), a.end(), b.begin(), b.end(), [](unsigned char lhs, unsigned char rhs) {
        return std::tolower(lhs) < std::tolower(rhs);
      });
}

std::string shellQuote(std::string_view text) {
  std::string quoted = "'";
  for (char ch : text) {
    if (ch == '\'') {
      quoted += "'\\''";
    } else {
      quoted.push_back(ch);
    }
  }
  quoted.push_back('\'');
  return quoted;
}

std::string trim(std::string_view value) {
  std::size_t begin = 0;
  while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
  std::size_t end = value.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1u]))) --end;
  return std::string(value.substr(begin, end - begin));
}

std::string lowerAscii(std::string_view value) {
  std::string output(value);
  std::transform(output.begin(), output.end(), output.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return output;
}

std::string unquoteXdgValue(std::string_view value) {
  std::string stripped = trim(value);
  value = stripped;
  if (value.size() >= 2u && value.front() == '"' && value.back() == '"') {
    value.remove_prefix(1);
    value.remove_suffix(1);
  }
  std::string output;
  output.reserve(value.size());
  bool escaped = false;
  for (char ch : value) {
    if (escaped) {
      output.push_back(ch);
      escaped = false;
      continue;
    }
    if (ch == '\\') {
      escaped = true;
      continue;
    }
    output.push_back(ch);
  }
  if (escaped) output.push_back('\\');
  return output;
}

bool isInsideOrEqual(std::filesystem::path const& path, std::filesystem::path const& root) {
  auto pathIt = path.begin();
  auto rootIt = root.begin();
  for (; rootIt != root.end(); ++rootIt, ++pathIt) {
    if (pathIt == path.end() || *pathIt != *rootIt) return false;
  }
  return true;
}

std::string percentEncodePath(std::string const& text) {
  constexpr char hex[] = "0123456789ABCDEF";
  std::string output;
  output.reserve(text.size());
  for (unsigned char ch : text) {
    if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
        ch == '/' || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
      output.push_back(static_cast<char>(ch));
    } else {
      output.push_back('%');
      output.push_back(hex[ch >> 4u]);
      output.push_back(hex[ch & 0x0fu]);
    }
  }
  return output;
}

std::optional<unsigned char> hexByte(char high, char low) {
  auto nibble = [](char ch) -> std::optional<unsigned char> {
    if (ch >= '0' && ch <= '9') return static_cast<unsigned char>(ch - '0');
    if (ch >= 'a' && ch <= 'f') return static_cast<unsigned char>(10 + ch - 'a');
    if (ch >= 'A' && ch <= 'F') return static_cast<unsigned char>(10 + ch - 'A');
    return std::nullopt;
  };
  auto h = nibble(high);
  auto l = nibble(low);
  if (!h || !l) return std::nullopt;
  return static_cast<unsigned char>((*h << 4u) | *l);
}

std::string percentDecode(std::string_view text) {
  std::string output;
  output.reserve(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '%' && i + 2u < text.size()) {
      if (auto byte = hexByte(text[i + 1u], text[i + 2u])) {
        output.push_back(static_cast<char>(*byte));
        i += 2u;
        continue;
      }
    }
    output.push_back(text[i]);
  }
  return output;
}

std::string trashDeletionDate() {
  auto const now = std::chrono::system_clock::now();
  std::time_t const time = std::chrono::system_clock::to_time_t(now);
  std::tm local{};
  localtime_r(&time, &local);
  char buffer[32]{};
  if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &local) == 0) {
    return "1970-01-01T00:00:00";
  }
  return buffer;
}

std::filesystem::path trashInfoPathFor(std::filesystem::path const& trashedPath) {
  return trashInfoDirectory() / (trashedPath.filename().string() + ".trashinfo");
}

std::filesystem::path copyTargetPath(std::filesystem::path const& source,
                                     std::filesystem::path const& destinationDirectory) {
  std::filesystem::path preferred = destinationDirectory / source.filename();
  if (preferred == source) {
    return collisionFreePath(destinationDirectory, source.stem().string() + " copy" + source.extension().string());
  }
  if (std::filesystem::exists(preferred)) {
    return collisionFreePath(destinationDirectory, preferred.filename().string());
  }
  return preferred;
}

bool copyRecursive(std::filesystem::path const& source, std::filesystem::path const& destination, std::error_code& ec) {
  ec.clear();
  std::filesystem::file_status const linkStatus = std::filesystem::symlink_status(source, ec);
  if (ec) return false;
  if (std::filesystem::is_symlink(linkStatus)) {
    std::filesystem::copy_symlink(source, destination, ec);
    return !ec;
  }
  if (std::filesystem::is_directory(source, ec)) {
    if (ec) return false;
    std::filesystem::create_directories(destination, ec);
    if (ec) return false;
    for (auto const& entry : std::filesystem::directory_iterator(source, ec)) {
      if (ec) return false;
      if (!copyRecursive(entry.path(), destination / entry.path().filename(), ec)) return false;
    }
    return true;
  }
  std::filesystem::copy_file(source, destination, std::filesystem::copy_options::none, ec);
  return !ec;
}

std::filesystem::path temporaryCopyPathFor(std::filesystem::path const& target) {
  std::string const name = "." + target.filename().string() + ".copying";
  return collisionFreePath(target.parent_path(), name);
}

bool copyRecursiveToFinalPath(std::filesystem::path const& source,
                              std::filesystem::path const& target,
                              std::error_code& ec) {
  ec.clear();
  std::filesystem::path const temporary = temporaryCopyPathFor(target);
  if (!copyRecursive(source, temporary, ec)) {
    std::error_code cleanupEc;
    std::filesystem::remove_all(temporary, cleanupEc);
    return false;
  }
  std::filesystem::rename(temporary, target, ec);
  if (ec) {
    std::error_code cleanupEc;
    std::filesystem::remove_all(temporary, cleanupEc);
    return false;
  }
  return true;
}

FileOperationResult moveReplacing(std::filesystem::path const& source, std::filesystem::path const& target) {
  std::error_code ec;
  if (!std::filesystem::exists(source, ec) || ec) {
    return {.ok = false, .path = source, .error = "Source does not exist."};
  }
  if (std::filesystem::exists(target, ec) && !ec) {
    return {.ok = false, .path = target, .error = "Destination already exists."};
  }
  std::filesystem::create_directories(target.parent_path(), ec);
  if (ec) return {.ok = false, .path = target.parent_path(), .error = ec.message()};
  std::filesystem::rename(source, target, ec);
  if (!ec) return {.ok = true, .path = target};
  std::error_code copyEc;
  if (!copyRecursiveToFinalPath(source, target, copyEc)) {
    return {.ok = false, .path = target, .error = copyEc.message()};
  }
  std::filesystem::remove_all(source, copyEc);
  if (copyEc) return {.ok = false, .path = target, .error = copyEc.message()};
  return {.ok = true, .path = target};
}

std::string shellCommandFromArgs(std::vector<std::string> const& args);

int runShellCommand(std::string const& command) {
  if (command.empty()) {
    return -1;
  }
  return std::system(command.c_str());
}

#if defined(__unix__) || defined(__APPLE__)
bool setCloseOnExec(int fd) {
  int const flags = fcntl(fd, F_GETFD);
  if (flags == -1) return false;
  return fcntl(fd, F_SETFD, flags | FD_CLOEXEC) != -1;
}

void writeLaunchError(int fd, int errorNumber) {
  unsigned char const* data = reinterpret_cast<unsigned char const*>(&errorNumber);
  std::size_t remaining = sizeof(errorNumber);
  while (remaining > 0) {
    ssize_t const written = write(fd, data, remaining);
    if (written < 0) {
      if (errno == EINTR) continue;
      return;
    }
    data += written;
    remaining -= static_cast<std::size_t>(written);
  }
}

bool readLaunchError(int fd, int& errorNumber) {
  unsigned char* data = reinterpret_cast<unsigned char*>(&errorNumber);
  std::size_t received = 0;
  while (received < sizeof(errorNumber)) {
    ssize_t const count = read(fd, data + received, sizeof(errorNumber) - received);
    if (count < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    if (count == 0) break;
    received += static_cast<std::size_t>(count);
  }
  return received == sizeof(errorNumber);
}

long launchCloseMax() {
  long const value = sysconf(_SC_OPEN_MAX);
  return value > 0 ? value : 1024;
}

void closeInheritedFdsExcept(int preservedFd, long maxFd) {
  for (int fd = 3; fd < maxFd; ++fd) {
    if (fd == preservedFd) continue;
    close(fd);
  }
}

bool launchDetachedProcess(std::vector<std::string> const& args, std::string& error) {
  if (args.empty() || args.front().empty()) {
    error = "Registered application has no launch command.";
    return false;
  }

  std::vector<char*> argv;
  argv.reserve(args.size() + 1u);
  for (auto const& arg : args) {
    argv.push_back(const_cast<char*>(arg.c_str()));
  }
  argv.push_back(nullptr);

  int errorPipe[2]{-1, -1};
  if (pipe(errorPipe) != 0) {
    error = "Could not create launch pipe: " + std::string(std::strerror(errno));
    return false;
  }
  if (!setCloseOnExec(errorPipe[1])) {
    error = "Could not prepare launch pipe: " + std::string(std::strerror(errno));
    close(errorPipe[0]);
    close(errorPipe[1]);
    return false;
  }
  long const maxFd = launchCloseMax();

  pid_t const firstChild = fork();
  if (firstChild < 0) {
    error = "Could not fork launcher: " + std::string(std::strerror(errno));
    close(errorPipe[0]);
    close(errorPipe[1]);
    return false;
  }

  if (firstChild == 0) {
    close(errorPipe[0]);
    if (setsid() < 0) {
      writeLaunchError(errorPipe[1], errno);
      _exit(127);
    }

    pid_t const secondChild = fork();
    if (secondChild < 0) {
      writeLaunchError(errorPipe[1], errno);
      _exit(127);
    }
    if (secondChild > 0) {
      _exit(0);
    }

    closeInheritedFdsExcept(errorPipe[1], maxFd);
    execvp(argv[0], argv.data());
    writeLaunchError(errorPipe[1], errno);
    _exit(127);
  }

  close(errorPipe[1]);
  int launchError = 0;
  bool const childReportedError = readLaunchError(errorPipe[0], launchError);
  close(errorPipe[0]);

  int status = 0;
  while (waitpid(firstChild, &status, 0) < 0 && errno == EINTR) {
  }

  if (childReportedError) {
    error = "Could not launch registered application: " + std::string(std::strerror(launchError));
    return false;
  }
  return true;
}
#else
bool launchDetachedProcess(std::vector<std::string> const& args, std::string& error) {
  std::string command = shellCommandFromArgs(args);
  if (command.empty()) {
    error = "Registered application has no launch command.";
    return false;
  }
  int const code = runShellCommand(command);
  if (code != 0) {
    error = "Could not open this item with the registered application.";
    return false;
  }
  return true;
}
#endif

std::string shellCommandFromArgs(std::vector<std::string> const& args) {
  std::string command;
  for (auto const& arg : args) {
    if (arg.empty()) continue;
    if (!command.empty()) command.push_back(' ');
    command += shellQuote(arg);
  }
  return command;
}

std::size_t utf8ScalarByteLength(unsigned char lead) {
  if (lead < 0x80) {
    return 1;
  }
  if ((lead & 0xE0) == 0xC0) {
    return 2;
  }
  if ((lead & 0xF0) == 0xE0) {
    return 3;
  }
  if ((lead & 0xF8) == 0xF0) {
    return 4;
  }
  return 0;
}

bool validUtf8Scalar(std::string_view text, std::size_t index, std::size_t length) {
  if (length == 0 || index + length > text.size()) {
    return false;
  }
  unsigned char const lead = static_cast<unsigned char>(text[index]);
  std::uint32_t codepoint = 0;
  if (length == 1) {
    return lead < 0x80;
  }
  for (std::size_t offset = 1; offset < length; ++offset) {
    unsigned char const ch = static_cast<unsigned char>(text[index + offset]);
    if ((ch & 0xC0) != 0x80) {
      return false;
    }
  }
  if (length == 2) {
    codepoint = (static_cast<std::uint32_t>(lead & 0x1F) << 6) |
                (static_cast<unsigned char>(text[index + 1]) & 0x3F);
    return codepoint >= 0x80;
  }
  if (length == 3) {
    codepoint = (static_cast<std::uint32_t>(lead & 0x0F) << 12) |
                (static_cast<std::uint32_t>(static_cast<unsigned char>(text[index + 1]) & 0x3F) << 6) |
                (static_cast<unsigned char>(text[index + 2]) & 0x3F);
    return codepoint >= 0x800 && (codepoint < 0xD800 || codepoint > 0xDFFF);
  }
  codepoint = (static_cast<std::uint32_t>(lead & 0x07) << 18) |
              (static_cast<std::uint32_t>(static_cast<unsigned char>(text[index + 1]) & 0x3F) << 12) |
              (static_cast<std::uint32_t>(static_cast<unsigned char>(text[index + 2]) & 0x3F) << 6) |
              (static_cast<unsigned char>(text[index + 3]) & 0x3F);
  return codepoint >= 0x10000 && codepoint <= 0x10FFFF;
}

std::string sanitizedUtf8Prefix(std::string_view text, std::size_t maxScalars, bool& truncated) {
  std::string output;
  output.reserve(std::min(text.size(), maxScalars * 4));
  std::size_t index = 0;
  std::size_t scalars = 0;
  while (index < text.size()) {
    if (scalars >= maxScalars) {
      truncated = true;
      break;
    }
    unsigned char const lead = static_cast<unsigned char>(text[index]);
    std::size_t const length = utf8ScalarByteLength(lead);
    if (validUtf8Scalar(text, index, length)) {
      output.append(text.substr(index, length));
      index += length;
    } else {
      output += "\xEF\xBF\xBD";
      ++index;
    }
    ++scalars;
  }
  return output;
}

FileVisualKind visualKindForPath(std::filesystem::path const& path, bool isDirectory) {
  if (isDirectory) {
    return FileVisualKind::Folder;
  }
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  if (ext == ".pdf") {
    return FileVisualKind::Pdf;
  }
  if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".gif" || ext == ".webp" ||
      ext == ".svg" || ext == ".heic") {
    return FileVisualKind::Image;
  }
  if (ext == ".key") {
    return FileVisualKind::Presentation;
  }
  if (ext == ".sketch") {
    return FileVisualKind::Sketch;
  }
  return FileVisualKind::Generic;
}

std::vector<std::string> splitDesktopIdList(std::string_view value) {
  std::vector<std::string> list;
  std::size_t start = 0;
  while (start <= value.size()) {
    std::size_t end = value.find(';', start);
    std::string item = trim(value.substr(start, end == std::string_view::npos ? value.size() - start : end - start));
    if (!item.empty()) {
      if (item.ends_with(".desktop")) item.resize(item.size() - 8u);
      list.push_back(item);
    }
    if (end == std::string_view::npos) break;
    start = end + 1u;
  }
  return list;
}

bool appIdMatchesDesktopId(lambda_shell::AppRegistryEntry const& app, std::string_view desktopId) {
  std::string id = std::string(desktopId);
  if (id.ends_with(".desktop")) id.resize(id.size() - 8u);
  return lambda_shell::shellAppIdMatches(app.appId, id) || lambda_shell::shellAppIdMatches(id, app.appId);
}

bool appSupportsMime(lambda_shell::AppRegistryEntry const& app, std::string const& mimeType) {
  if (app.hidden) return false;
  return std::find(app.mimeTypes.begin(), app.mimeTypes.end(), mimeType) != app.mimeTypes.end();
}

std::string iconNameForMime(std::string const& mimeType) {
  if (mimeType == "inode/directory") return "folder";
  if (mimeType == "application/pdf") return "application-pdf";
  if (mimeType == "application/zip" || mimeType == "application/gzip" || mimeType == "application/x-tar") {
    return "package-x-generic";
  }
  if (mimeType.starts_with("image/")) return "image-x-generic";
  if (mimeType.starts_with("audio/")) return "audio-x-generic";
  if (mimeType.starts_with("video/")) return "video-x-generic";
  if (mimeType.starts_with("text/")) return "text-x-generic";
  return "application-octet-stream";
}

} // namespace

std::filesystem::path homeDirectory() {
  if (auto const home = pathIfExists(pathFromEnv("HOME"))) {
    return *home;
  }
  return std::filesystem::current_path();
}

std::filesystem::path trashFilesDirectory() {
  return dataHomeDirectory() / "Trash" / "files";
}

std::filesystem::path trashInfoDirectory() {
  return dataHomeDirectory() / "Trash" / "info";
}

bool FileSelectionState::contains(std::filesystem::path const& path) const {
  return std::find(selected.begin(), selected.end(), path) != selected.end();
}

std::map<std::string, std::filesystem::path> parseXdgUserDirs(std::string_view configText,
                                                              std::filesystem::path const& home) {
  std::map<std::string, std::filesystem::path> dirs;
  std::istringstream input{std::string(configText)};
  std::string line;
  while (std::getline(input, line)) {
    std::string_view view(line);
    if (auto hash = view.find('#'); hash != std::string_view::npos) view = view.substr(0, hash);
    std::string stripped = trim(view);
    view = stripped;
    if (view.empty()) continue;
    auto equals = view.find('=');
    if (equals == std::string_view::npos) continue;
    std::string key = trim(view.substr(0, equals));
    constexpr std::string_view prefix = "XDG_";
    constexpr std::string_view suffix = "_DIR";
    if (!key.starts_with(prefix) || !key.ends_with(suffix)) continue;
    key = key.substr(prefix.size(), key.size() - prefix.size() - suffix.size());
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) {
      return static_cast<char>(std::tolower(ch));
    });
    std::string value = unquoteXdgValue(view.substr(equals + 1u));
    if (value.empty()) continue;
    std::filesystem::path path(value);
    std::string pathText = path.string();
    if (pathText == "$HOME") {
      path = home;
    } else if (pathText.starts_with("$HOME/")) {
      path = home / pathText.substr(6u);
    } else if (pathText.starts_with("~/")) {
      path = home / pathText.substr(2u);
    }
    dirs[key] = path.lexically_normal();
  }
  return dirs;
}

std::vector<SidebarPlace> const& sidebarPlaces() {
  static std::vector<SidebarPlace> const places = [] {
    std::filesystem::path const home = homeDirectory();
    std::vector<SidebarPlace> built;
    built.push_back({.id = "home", .label = "Home", .icon = lambda::IconName::Home, .path = home});

    auto add = [&built](char const* id, char const* label, lambda::IconName icon, std::filesystem::path path) {
      for (auto const& existing : built) {
        if (existing.id == id) {
          return;
        }
      }
      if (auto const resolved = pathIfExists(std::move(path))) {
        built.push_back({.id = id, .label = label, .icon = icon, .path = *resolved});
      }
    };

    add("desktop", "Desktop", lambda::IconName::DesktopWindows, home / "Desktop");
    if (std::filesystem::path const docs = pathFromEnv("XDG_DOCUMENTS_DIR"); !docs.empty()) {
      add("documents", "Documents", lambda::IconName::Description, docs);
    } else {
      add("documents", "Documents", lambda::IconName::Description, home / "Documents");
    }
    if (std::filesystem::path const dl = pathFromEnv("XDG_DOWNLOAD_DIR"); !dl.empty()) {
      add("downloads", "Downloads", lambda::IconName::Download, dl);
    } else {
      add("downloads", "Downloads", lambda::IconName::Download, home / "Downloads");
    }
    return built;
  }();
  return places;
}

std::string gridDisplayName(std::string name) {
  constexpr std::size_t kMaxChars = 20;
  if (kMaxChars <= 3) {
    return "...";
  }
  bool truncated = false;
  std::string display = sanitizedUtf8Prefix(name, kMaxChars, truncated);
  if (!truncated) {
    return display;
  }
  truncated = false;
  display = sanitizedUtf8Prefix(name, kMaxChars - 3, truncated);
  display += "...";
  return display;
}

std::string normalizeDirectoryPath(std::filesystem::path path) {
  std::error_code ec;
  std::filesystem::path const canonical = std::filesystem::weakly_canonical(path, ec);
  if (!ec && !canonical.empty()) {
    path = canonical;
  }
  return path.string();
}

ListDirectoryResult listDirectory(std::filesystem::path const& directory, bool includeHidden) {
  ListDirectoryResult result;
  std::error_code ec;
  if (!std::filesystem::exists(directory, ec) || ec) {
    result.error = "Folder does not exist.";
    return result;
  }
  if (!std::filesystem::is_directory(directory, ec) || ec) {
    result.error = "Not a folder.";
    return result;
  }

  for (auto const& entry : std::filesystem::directory_iterator(directory, ec)) {
    if (ec) {
      result.error = ec.message();
      result.entries.clear();
      return result;
    }
    std::string const name = entry.path().filename().string();
    if (name == "." || name == "..") {
      continue;
    }
    if (!includeHidden && !name.empty() && name.front() == '.') {
      continue;
    }
    std::error_code typeEc;
    bool const isDir = entry.is_directory(typeEc);
    if (typeEc) {
      continue;
    }
    std::uintmax_t size = 0;
    if (!isDir) {
      size = entry.file_size(typeEc);
      if (typeEc) {
        size = 0;
      }
    }
    std::filesystem::file_time_type modifiedAt{};
    modifiedAt = entry.last_write_time(typeEc);
    if (typeEc) {
      modifiedAt = {};
    }
    result.entries.push_back(FileEntry{
        .name = name,
        .path = entry.path(),
        .isDirectory = isDir,
        .size = size,
        .modifiedAt = modifiedAt,
        .visualKind = visualKindForPath(entry.path(), isDir),
    });
  }

  result.entries = sortedEntries(std::move(result.entries));
  return result;
}

std::vector<FileEntry> sortedEntries(std::vector<FileEntry> entries,
                                     FileSortKey key,
                                     bool ascending,
                                     bool directoriesFirst) {
  auto compareField = [&](FileEntry const& a, FileEntry const& b) {
    switch (key) {
    case FileSortKey::Name:
      if (ciLess(a.name, b.name)) return true;
      if (ciLess(b.name, a.name)) return false;
      return a.name < b.name;
    case FileSortKey::Kind:
      if (a.visualKind != b.visualKind) {
        return static_cast<int>(a.visualKind) < static_cast<int>(b.visualKind);
      }
      if (ciLess(a.name, b.name)) return true;
      if (ciLess(b.name, a.name)) return false;
      return a.name < b.name;
    case FileSortKey::Size:
      if (a.size != b.size) return a.size < b.size;
      if (ciLess(a.name, b.name)) return true;
      if (ciLess(b.name, a.name)) return false;
      return a.name < b.name;
    case FileSortKey::ModifiedTime:
      if (a.modifiedAt != b.modifiedAt) return a.modifiedAt < b.modifiedAt;
      if (ciLess(a.name, b.name)) return true;
      if (ciLess(b.name, a.name)) return false;
      return a.name < b.name;
    }
    return false;
  };

  std::stable_sort(entries.begin(), entries.end(), [&](FileEntry const& a, FileEntry const& b) {
    if (directoriesFirst && a.isDirectory != b.isDirectory) return a.isDirectory;
    bool const less = compareField(a, b);
    bool const greater = compareField(b, a);
    if (!less && !greater) return false;
    return ascending ? less : greater;
  });
  return entries;
}

std::vector<FileEntry> filterEntries(std::vector<FileEntry> const& entries, std::string_view query) {
  std::string needle = lowerAscii(trim(query));
  if (needle.empty()) return entries;
  std::vector<FileEntry> filtered;
  for (auto const& entry : entries) {
    if (lowerAscii(entry.name).find(needle) != std::string::npos ||
        lowerAscii(entry.path.string()).find(needle) != std::string::npos) {
      filtered.push_back(entry);
    }
  }
  return filtered;
}

DirectoryChangeSet diffDirectoryEntries(std::vector<FileEntry> before, std::vector<FileEntry> after) {
  auto byPath = [](FileEntry const& a, FileEntry const& b) {
    return a.path < b.path;
  };
  std::sort(before.begin(), before.end(), byPath);
  std::sort(after.begin(), after.end(), byPath);

  DirectoryChangeSet changes;
  std::size_t i = 0;
  std::size_t j = 0;
  while (i < before.size() || j < after.size()) {
    if (i >= before.size()) {
      changes.added.push_back(after[j++].path);
      continue;
    }
    if (j >= after.size()) {
      changes.removed.push_back(before[i++].path);
      continue;
    }
    if (before[i].path < after[j].path) {
      changes.removed.push_back(before[i++].path);
      continue;
    }
    if (after[j].path < before[i].path) {
      changes.added.push_back(after[j++].path);
      continue;
    }
    if (before[i].size != after[j].size || before[i].isDirectory != after[j].isDirectory ||
        before[i].modifiedAt != after[j].modifiedAt) {
      changes.modified.push_back(after[j].path);
    }
    ++i;
    ++j;
  }
  return changes;
}

bool directoryListingChanged(std::vector<FileEntry> const& currentEntries,
                             std::string_view currentError,
                             ListDirectoryResult const& nextListing) {
  if (currentError != nextListing.error) {
    return true;
  }
  if (!nextListing.error.empty()) {
    return false;
  }
  DirectoryChangeSet changes = diffDirectoryEntries(currentEntries, nextListing.entries);
  return !changes.added.empty() || !changes.removed.empty() || !changes.modified.empty();
}

FileSelectionState preserveSelectionAfterRefresh(FileSelectionState const& previous,
                                                 std::optional<std::filesystem::path> const& previousAnchor,
                                                 std::vector<FileEntry> const& entries) {
  FileSelectionState next;
  for (auto const& selected : previous.selected) {
    auto found = std::find_if(entries.begin(), entries.end(), [&](FileEntry const& entry) {
      return entry.path == selected;
    });
    if (found != entries.end()) next.selected.push_back(selected);
  }
  if (next.selected.empty()) return next;

  auto indexOf = [&](std::filesystem::path const& path) -> int {
    for (std::size_t i = 0; i < entries.size(); ++i) {
      if (entries[i].path == path) return static_cast<int>(i);
    }
    return -1;
  };
  if (previousAnchor) {
    int const anchor = indexOf(*previousAnchor);
    if (anchor >= 0 && std::find(next.selected.begin(), next.selected.end(), *previousAnchor) != next.selected.end()) {
      next.anchorIndex = anchor;
      return next;
    }
  }
  next.anchorIndex = indexOf(next.selected.front());
  return next;
}

double FileOperationProgress::fractionComplete() const noexcept {
  if (totalItems == 0) {
    return status == FileOperationStatus::Succeeded ? 1.0 : 0.0;
  }
  double fraction = static_cast<double>(completedItems) / static_cast<double>(totalItems);
  if (fraction < 0.0) return 0.0;
  if (fraction > 1.0) return 1.0;
  return fraction;
}

FilesModel makeFilesModel(std::filesystem::path directory, FilesPreferences preferences) {
  if (directory.empty()) directory = homeDirectory();
  FilesModel model;
  model.history.current = normalizeDirectoryPath(directory);
  model.preferences = std::move(preferences);
  return model;
}

FilesModel setFilesModelQuery(FilesModel model, std::string query) {
  model.query = std::move(query);
  model.visibleEntries = filterEntries(
      sortedEntries(model.entries, model.preferences.sortKey, model.preferences.sortAscending),
      model.query);
  return model;
}

FilesModel setFilesModelPreferences(FilesModel model, FilesPreferences preferences) {
  model.preferences = std::move(preferences);
  model.visibleEntries = filterEntries(
      sortedEntries(model.entries, model.preferences.sortKey, model.preferences.sortAscending),
      model.query);
  return model;
}

FilesModel applyDirectoryListing(FilesModel model,
                                 std::filesystem::path directory,
                                 ListDirectoryResult listing) {
  if (!listing.error.empty()) {
    model.error = std::move(listing.error);
    model.operation = failFileOperation(beginFileOperation(FileOperationKind::Refresh, 1, false),
                                        directory,
                                        model.error);
    return model;
  }

  std::optional<std::filesystem::path> previousAnchor;
  if (model.selection.anchorIndex >= 0 &&
      model.selection.anchorIndex < static_cast<int>(model.entries.size())) {
    previousAnchor = model.entries[static_cast<std::size_t>(model.selection.anchorIndex)].path;
  }
  FileSelectionState const previousSelection = model.selection;

  std::vector<FileEntry> sorted = sortedEntries(std::move(listing.entries),
                                                model.preferences.sortKey,
                                                model.preferences.sortAscending);
  model.lastChanges = diffDirectoryEntries(model.entries, sorted);
  model.entries = std::move(sorted);
  model.visibleEntries = filterEntries(model.entries, model.query);
  model.history = navigateTo(std::move(model.history), directory);
  model.selection = preserveSelectionAfterRefresh(previousSelection, previousAnchor, model.entries);
  model.error.clear();
  model.operation = completeFileOperation(beginFileOperation(FileOperationKind::Refresh, 1, false));
  return model;
}

std::vector<BreadcrumbCrumb> breadcrumbCrumbs(std::filesystem::path const& path) {
  std::vector<BreadcrumbCrumb> crumbs;
  std::error_code ec;
  std::filesystem::path current = std::filesystem::weakly_canonical(path, ec);
  if (ec || current.empty()) {
    current = path;
  }

  std::filesystem::path const home = homeDirectory();
  if (isInsideOrEqual(current, home)) {
    crumbs.push_back({"Home", home});
    if (current == home) {
      return crumbs;
    }
    std::filesystem::path accumulated = home;
    std::filesystem::path relative = std::filesystem::relative(current, home, ec);
    if (ec) relative = current.lexically_relative(home);
    for (std::filesystem::path const& part : relative) {
      if (part.empty() || part == ".") continue;
      accumulated /= part;
      crumbs.push_back({part.string(), accumulated});
    }
    return crumbs;
  }

  std::filesystem::path accumulated = current.root_path();
  crumbs.push_back({accumulated.empty() ? "/" : accumulated.string(), accumulated.empty() ? "/" : accumulated});
  if (current == accumulated) return crumbs;
  for (std::filesystem::path const& part : current.relative_path()) {
    if (part.empty() || part == ".") {
      continue;
    }
    accumulated /= part;
    crumbs.push_back({part.string(), accumulated});
  }
  return crumbs;
}

std::optional<std::filesystem::path> parentDirectory(std::filesystem::path const& path) {
  std::error_code ec;
  std::filesystem::path const canonical = std::filesystem::weakly_canonical(path, ec);
  std::filesystem::path const current = ec ? path : canonical;
  if (!current.has_parent_path()) {
    return std::nullopt;
  }
  std::filesystem::path parent = current.parent_path();
  if (parent == current) {
    return std::nullopt;
  }
  if (!std::filesystem::exists(parent, ec) || ec || !std::filesystem::is_directory(parent, ec) || ec) {
    return std::nullopt;
  }
  return parent;
}

FileVisualKind visualKindForEntry(std::filesystem::path const& path, bool isDirectory) {
  return visualKindForPath(path, isDirectory);
}

std::string formatEntrySubtitle(FileEntry const& entry) {
  if (entry.isDirectory) {
    return "Folder";
  }
  if (entry.size < 1024) {
    return std::to_string(entry.size) + " B";
  }
  if (entry.size < 1024 * 1024) {
    return std::to_string((entry.size + 1023) / 1024) + " KB";
  }
  return std::to_string((entry.size + 1024 * 1024 - 1) / (1024 * 1024)) + " MB";
}

std::string formatSidebarFooter(std::filesystem::path const& path) {
  return normalizeDirectoryPath(path);
}

bool openEntry(FileEntry const& entry, std::string& error) {
  auto apps = lambda_shell::buildDefaultAppRegistry(
      lambda_shell::defaultLocalLambdaAppDirs(),
      lambda_shell::defaultXdgApplicationDirs(),
      lambda_shell::executableInPath);
  return openEntryWithApps(entry, apps, loadMimeAppsList(), error);
}

bool openEntryWithApps(FileEntry const& entry,
                       std::vector<lambda_shell::AppRegistryEntry> const& apps,
                       MimeAppsList const& mimeApps,
                       std::string& error) {
  OpenEntryPlan const plan = openEntryPlan(entry, apps, mimeApps);
  if (!plan.ok) {
    error = plan.error;
    return false;
  }
  if (!launchDetachedProcess(plan.command, error)) {
    return false;
  }
  return true;
}

bool revealEntryInSystem(FileEntry const& entry, std::string& error) {
#if defined(__APPLE__)
  int const code = runShellCommand("open -R " + shellQuote(entry.path.string()));
#else
  int const code = runShellCommand("xdg-open " + shellQuote(entry.path.parent_path().string()));
#endif
  if (code != 0) {
    error = "Could not reveal this item in the system file manager.";
    return false;
  }
  return true;
}

NavigationHistory navigateTo(NavigationHistory history, std::filesystem::path path) {
  std::string const next = normalizeDirectoryPath(std::move(path));
  if (history.current == next) {
    history.forward.clear();
    return history;
  }
  if (!history.current.empty()) {
    history.back.push_back(history.current);
  }
  history.current = next;
  history.forward.clear();
  return history;
}

NavigationResult navigateToDirectory(NavigationHistory history, std::filesystem::path path) {
  std::error_code ec;
  if (!std::filesystem::exists(path, ec) || ec) {
    return {.history = std::move(history), .error = "Folder does not exist."};
  }
  if (!std::filesystem::is_directory(path, ec) || ec) {
    return {.history = std::move(history), .error = "Not a folder."};
  }
  std::filesystem::directory_iterator probe(path, ec);
  if (ec) {
    return {.history = std::move(history), .error = ec.message()};
  }
  (void)probe;
  return {.history = navigateTo(std::move(history), std::move(path)), .ok = true};
}

NavigationHistory goBack(NavigationHistory history) {
  if (history.back.empty()) {
    return history;
  }
  if (!history.current.empty()) {
    history.forward.push_back(history.current);
  }
  history.current = history.back.back();
  history.back.pop_back();
  return history;
}

NavigationHistory goForward(NavigationHistory history) {
  if (history.forward.empty()) {
    return history;
  }
  if (!history.current.empty()) {
    history.back.push_back(history.current);
  }
  history.current = history.forward.back();
  history.forward.pop_back();
  return history;
}

NavigationHistory goUp(NavigationHistory history) {
  if (auto const parent = parentDirectory(std::filesystem::path(history.current))) {
    return navigateTo(std::move(history), *parent);
  }
  return history;
}

FileSelectionState selectOnly(std::vector<FileEntry> const& entries, int index) {
  FileSelectionState state;
  if (index < 0 || index >= static_cast<int>(entries.size())) return state;
  state.selected.push_back(entries[static_cast<std::size_t>(index)].path);
  state.anchorIndex = index;
  return state;
}

FileSelectionState toggleSelection(FileSelectionState state, std::vector<FileEntry> const& entries, int index) {
  if (index < 0 || index >= static_cast<int>(entries.size())) return state;
  auto const& path = entries[static_cast<std::size_t>(index)].path;
  auto found = std::find(state.selected.begin(), state.selected.end(), path);
  if (found == state.selected.end()) {
    state.selected.push_back(path);
    state.anchorIndex = index;
  } else {
    state.selected.erase(found);
    if (state.anchorIndex == index) state.anchorIndex = state.selected.empty() ? -1 : index;
  }
  return state;
}

FileSelectionState rangeSelection(FileSelectionState state, std::vector<FileEntry> const& entries, int index) {
  if (index < 0 || index >= static_cast<int>(entries.size())) return state;
  int const anchor = state.anchorIndex >= 0 ? state.anchorIndex : index;
  int const first = std::min(anchor, index);
  int const last = std::max(anchor, index);
  state.selected.clear();
  for (int i = first; i <= last; ++i) {
    state.selected.push_back(entries[static_cast<std::size_t>(i)].path);
  }
  state.anchorIndex = anchor;
  return state;
}

FileSelectionState clearSelection(FileSelectionState state) {
  state.selected.clear();
  state.anchorIndex = -1;
  return state;
}

FileSelectionState selectAllEntries(std::vector<FileEntry> const& entries) {
  FileSelectionState state;
  state.selected.reserve(entries.size());
  for (auto const& entry : entries) {
    state.selected.push_back(entry.path);
  }
  state.anchorIndex = entries.empty() ? -1 : 0;
  return state;
}

int focusedSelectionIndex(FileSelectionState const& state, std::vector<FileEntry> const& entries) {
  auto indexOf = [&](std::filesystem::path const& path) -> int {
    for (std::size_t i = 0; i < entries.size(); ++i) {
      if (entries[i].path == path) return static_cast<int>(i);
    }
    return -1;
  };

  if (state.anchorIndex >= 0 && state.anchorIndex < static_cast<int>(entries.size())) {
    return state.anchorIndex;
  }
  for (auto it = state.selected.rbegin(); it != state.selected.rend(); ++it) {
    int const index = indexOf(*it);
    if (index >= 0) return index;
  }
  return -1;
}

FileSelectionState moveSelectionToIndex(FileSelectionState state,
                                        std::vector<FileEntry> const& entries,
                                        int index,
                                        bool extend) {
  if (entries.empty()) return clearSelection(std::move(state));
  int const clamped = std::clamp(index, 0, static_cast<int>(entries.size()) - 1);
  if (!extend) return selectOnly(entries, clamped);

  if (state.anchorIndex < 0) {
    int const focused = focusedSelectionIndex(state, entries);
    state.anchorIndex = focused >= 0 ? focused : clamped;
  }
  return rangeSelection(std::move(state), entries, clamped);
}

FileSelectionState moveSelectionByOffset(FileSelectionState state,
                                         std::vector<FileEntry> const& entries,
                                         int offset,
                                         bool extend) {
  if (entries.empty()) return clearSelection(std::move(state));

  int focused = focusedSelectionIndex(state, entries);
  if (focused < 0) {
    focused = offset < 0 ? static_cast<int>(entries.size()) - 1 : 0;
  } else {
    focused += offset;
  }
  return moveSelectionToIndex(std::move(state), entries, focused, extend);
}

FilePointerSelectionResult selectionForPointerTap(FileSelectionState state,
                                                  std::vector<FileEntry> const& entries,
                                                  int index,
                                                  lambda::Modifiers modifiers) {
  if (index < 0 || index >= static_cast<int>(entries.size())) {
    return {.selection = std::move(state), .activate = false};
  }
  if (lambda::any(modifiers & lambda::Modifiers::Shift)) {
    return {
        .selection = rangeSelection(std::move(state), entries, index),
        .activate = false,
    };
  }
  if (lambda::any(modifiers & lambda::Modifiers::Ctrl) || lambda::any(modifiers & lambda::Modifiers::Meta)) {
    return {
        .selection = toggleSelection(std::move(state), entries, index),
        .activate = false,
    };
  }
  return {
      .selection = selectOnly(entries, index),
      .activate = true,
  };
}

std::vector<FileEntry> selectedEntries(std::vector<FileEntry> const& entries, FileSelectionState const& selection) {
  std::vector<FileEntry> selected;
  selected.reserve(selection.selected.size());
  for (auto const& entry : entries) {
    if (selection.contains(entry.path)) selected.push_back(entry);
  }
  return selected;
}

std::vector<FileContextCommand> contextMenuCommands(std::vector<FileEntry> const& entries,
                                                    FileSelectionState const& selection,
                                                    FileClipboardState const& clipboard,
                                                    bool backgroundMenu) {
  std::size_t const selectionCount = selectedEntries(entries, selection).size();
  bool const hasSelection = selectionCount > 0;
  bool const singleSelection = selectionCount == 1;
  bool const hasEntries = !entries.empty();
  bool const canPaste = !clipboard.empty();

  if (backgroundMenu || !hasSelection) {
    return {
        {.kind = FileContextCommandKind::NewFolder, .label = "New Folder", .enabled = true},
        {.kind = FileContextCommandKind::NewFile, .label = "New File", .enabled = true},
        {.kind = FileContextCommandKind::Paste, .label = "Paste", .enabled = canPaste},
        {.kind = FileContextCommandKind::SelectAll, .label = "Select All", .enabled = hasEntries},
    };
  }

  return {
      {.kind = FileContextCommandKind::Open, .label = "Open", .enabled = singleSelection},
      {.kind = FileContextCommandKind::Reveal, .label = "Reveal in Folder", .enabled = singleSelection},
      {.kind = FileContextCommandKind::Copy, .label = "Copy", .enabled = hasSelection},
      {.kind = FileContextCommandKind::Cut, .label = "Cut", .enabled = hasSelection},
      {.kind = FileContextCommandKind::Duplicate, .label = "Duplicate", .enabled = hasSelection},
      {.kind = FileContextCommandKind::Trash, .label = "Move to Trash", .enabled = hasSelection, .destructive = true},
  };
}

std::filesystem::path collisionFreePath(std::filesystem::path const& directory, std::string const& preferredName) {
  std::filesystem::path preferred = directory / preferredName;
  if (!std::filesystem::exists(preferred)) return preferred;

  std::filesystem::path stem = std::filesystem::path(preferredName).stem();
  std::filesystem::path extension = std::filesystem::path(preferredName).extension();
  if (stem.empty()) stem = preferredName;
  for (int index = 2; index < 10'000; ++index) {
    std::filesystem::path candidate = directory / (stem.string() + " " + std::to_string(index) + extension.string());
    if (!std::filesystem::exists(candidate)) return candidate;
  }
  return preferred;
}

FileOperationResult createFolder(std::filesystem::path const& directory, std::string preferredName) {
  std::error_code ec;
  if (!std::filesystem::is_directory(directory, ec) || ec) {
    return {.ok = false, .error = "Destination is not a folder."};
  }
  std::filesystem::path path = collisionFreePath(directory, preferredName.empty() ? "New Folder" : preferredName);
  std::filesystem::create_directory(path, ec);
  if (ec) return {.ok = false, .path = path, .error = ec.message()};
  return {.ok = true, .path = path};
}

FileOperationResult createFile(std::filesystem::path const& directory, std::string preferredName) {
  std::error_code ec;
  if (!std::filesystem::is_directory(directory, ec) || ec) {
    return {.ok = false, .error = "Destination is not a folder."};
  }
  std::filesystem::path path = collisionFreePath(directory, preferredName.empty() ? "New File.txt" : preferredName);
  std::ofstream file(path);
  if (!file) return {.ok = false, .path = path, .error = "Could not create file."};
  return {.ok = true, .path = path};
}

std::string validateRename(std::filesystem::path const& source, std::string const& newName) {
  if (newName.empty()) return "Name cannot be empty.";
  if (newName == "." || newName == "..") return "Name is reserved.";
  if (newName.find('/') != std::string::npos || newName.find('\\') != std::string::npos) {
    return "Name cannot contain path separators.";
  }
  std::error_code ec;
  if (!std::filesystem::exists(source, ec) || ec) return "Source does not exist.";
  std::filesystem::path target = source.parent_path() / newName;
  if (target != source && std::filesystem::exists(target, ec) && !ec) return "An item with that name already exists.";
  return {};
}

FileOperationResult renamePath(std::filesystem::path const& source, std::string const& newName) {
  if (std::string error = validateRename(source, newName); !error.empty()) {
    return {.ok = false, .path = source, .error = error};
  }
  std::filesystem::path target = source.parent_path() / newName;
  std::error_code ec;
  std::filesystem::rename(source, target, ec);
  if (ec) return {.ok = false, .path = target, .error = ec.message()};
  return {.ok = true, .path = target};
}

FileOperationResult copyPath(std::filesystem::path const& source, std::filesystem::path const& destinationDirectory) {
  std::error_code ec;
  if (!std::filesystem::exists(source, ec) || ec) return {.ok = false, .path = source, .error = "Source does not exist."};
  if (!std::filesystem::is_directory(destinationDirectory, ec) || ec) {
    return {.ok = false, .path = destinationDirectory, .error = "Destination is not a folder."};
  }
  std::filesystem::path target = copyTargetPath(source, destinationDirectory);
  if (!copyRecursiveToFinalPath(source, target, ec)) {
    return {.ok = false, .path = target, .error = ec.message()};
  }
  return {.ok = true, .path = target};
}

FileOperationResult movePath(std::filesystem::path const& source, std::filesystem::path const& destinationDirectory) {
  std::error_code ec;
  if (!std::filesystem::exists(source, ec) || ec) return {.ok = false, .path = source, .error = "Source does not exist."};
  if (!std::filesystem::is_directory(destinationDirectory, ec) || ec) {
    return {.ok = false, .path = destinationDirectory, .error = "Destination is not a folder."};
  }
  std::filesystem::path target = copyTargetPath(source, destinationDirectory);
  std::filesystem::rename(source, target, ec);
  if (!ec) return {.ok = true, .path = target};
  std::error_code copyEc;
  if (!copyRecursiveToFinalPath(source, target, copyEc)) {
    return {.ok = false, .path = target, .error = copyEc.message()};
  }
  std::filesystem::remove_all(source, copyEc);
  if (copyEc) return {.ok = false, .path = target, .error = copyEc.message()};
  return {.ok = true, .path = target};
}

FileOperationResult duplicatePath(std::filesystem::path const& source) {
  std::error_code ec;
  if (!std::filesystem::exists(source, ec) || ec) return {.ok = false, .path = source, .error = "Source does not exist."};
  std::filesystem::path target =
      collisionFreePath(source.parent_path(), source.stem().string() + " copy" + source.extension().string());
  if (!copyRecursiveToFinalPath(source, target, ec)) {
    return {.ok = false, .path = target, .error = ec.message()};
  }
  return {.ok = true, .path = target};
}

FileOperationResult trashPath(std::filesystem::path const& source) {
  std::error_code ec;
  if (!std::filesystem::exists(source, ec) || ec) return {.ok = false, .path = source, .error = "Source does not exist."};
  std::filesystem::create_directories(trashFilesDirectory(), ec);
  if (ec) return {.ok = false, .path = trashFilesDirectory(), .error = ec.message()};
  std::filesystem::create_directories(trashInfoDirectory(), ec);
  if (ec) return {.ok = false, .path = trashInfoDirectory(), .error = ec.message()};

  std::filesystem::path target = collisionFreePath(trashFilesDirectory(), source.filename().string());
  std::filesystem::path infoPath = trashInfoPathFor(target);
  std::filesystem::rename(source, target, ec);
  if (ec) {
    if (!copyRecursive(source, target, ec)) return {.ok = false, .path = target, .error = ec.message()};
    std::filesystem::remove_all(source, ec);
    if (ec) return {.ok = false, .path = target, .error = ec.message()};
  }

  std::ofstream info(infoPath);
  info << "[Trash Info]\n";
  info << "Path=" << percentEncodePath(std::filesystem::absolute(source).lexically_normal().string()) << "\n";
  info << "DeletionDate=" << trashDeletionDate() << "\n";
  if (!info) return {.ok = false, .path = infoPath, .error = "Could not write trash metadata."};
  return {.ok = true, .path = target};
}

std::optional<TrashInfo> parseTrashInfo(std::filesystem::path const& infoPath) {
  std::ifstream file(infoPath);
  if (!file) return std::nullopt;
  TrashInfo info;
  std::string line;
  while (std::getline(file, line)) {
    if (auto equals = line.find('='); equals != std::string::npos) {
      std::string key = line.substr(0, equals);
      std::string value = line.substr(equals + 1u);
      if (key == "Path") info.originalPath = percentDecode(value);
      if (key == "DeletionDate") info.deletionDate = value;
    }
  }
  if (info.originalPath.empty()) return std::nullopt;
  return info;
}

FileOperationResult restoreTrashedPath(std::filesystem::path const& trashedPath) {
  std::error_code ec;
  if (!std::filesystem::exists(trashedPath, ec) || ec) {
    return {.ok = false, .path = trashedPath, .error = "Trashed item does not exist."};
  }
  std::filesystem::path infoPath = trashInfoPathFor(trashedPath);
  auto info = parseTrashInfo(infoPath);
  if (!info) return {.ok = false, .path = infoPath, .error = "Trash metadata is missing."};
  std::filesystem::create_directories(info->originalPath.parent_path(), ec);
  if (ec) return {.ok = false, .path = info->originalPath.parent_path(), .error = ec.message()};
  std::filesystem::path target = info->originalPath;
  if (std::filesystem::exists(target, ec) && !ec) {
    target = collisionFreePath(target.parent_path(), target.filename().string());
  }
  std::filesystem::rename(trashedPath, target, ec);
  if (ec) {
    if (!copyRecursive(trashedPath, target, ec)) return {.ok = false, .path = target, .error = ec.message()};
    std::filesystem::remove_all(trashedPath, ec);
    if (ec) return {.ok = false, .path = target, .error = ec.message()};
  }
  std::filesystem::remove(infoPath, ec);
  return {.ok = true, .path = target};
}

std::filesystem::path resolveConflictPath(std::filesystem::path const& destination,
                                          FileConflictDecision decision) {
  std::error_code ec;
  bool const exists = std::filesystem::exists(destination, ec) && !ec;
  if (!exists) return destination;
  switch (decision) {
  case FileConflictDecision::KeepBoth:
    return collisionFreePath(destination.parent_path(), destination.filename().string());
  case FileConflictDecision::Replace:
    return destination;
  case FileConflictDecision::Skip:
    return {};
  case FileConflictDecision::Cancel:
    return {};
  }
  return {};
}

FileOperationProgress beginFileOperation(FileOperationKind kind,
                                         std::size_t totalItems,
                                         bool cancellable,
                                         std::uint64_t id) {
  return {
      .id = id,
      .kind = kind,
      .status = FileOperationStatus::Running,
      .totalItems = totalItems,
      .cancellable = cancellable,
  };
}

FileOperationProgress advanceFileOperation(FileOperationProgress progress,
                                           std::filesystem::path currentPath,
                                           std::size_t completedDelta) {
  if (progress.status != FileOperationStatus::Running) return progress;
  progress.currentPath = std::move(currentPath);
  progress.completedItems = std::min(progress.totalItems, progress.completedItems + completedDelta);
  if (progress.totalItems > 0 && progress.completedItems >= progress.totalItems && progress.errors.empty()) {
    progress.status = FileOperationStatus::Succeeded;
  }
  return progress;
}

FileOperationProgress failFileOperation(FileOperationProgress progress,
                                        std::filesystem::path path,
                                        std::string message) {
  progress.currentPath = path;
  progress.errors.push_back({
      .path = std::move(path),
      .message = std::move(message),
  });
  progress.status = FileOperationStatus::Failed;
  return progress;
}

FileOperationProgress requestCancelFileOperation(FileOperationProgress progress) {
  if (!progress.cancellable || progress.status != FileOperationStatus::Running) return progress;
  progress.cancelRequested = true;
  progress.status = FileOperationStatus::Cancelled;
  return progress;
}

FileOperationProgress completeFileOperation(FileOperationProgress progress) {
  if (progress.status == FileOperationStatus::Cancelled || progress.status == FileOperationStatus::Failed) {
    return progress;
  }
  if (!progress.errors.empty()) {
    progress.status = FileOperationStatus::Failed;
    return progress;
  }
  progress.completedItems = progress.totalItems;
  progress.status = FileOperationStatus::Succeeded;
  return progress;
}

FileOperationResult undoFileOperation(FileUndoAction const& action) {
  std::error_code ec;
  switch (action.kind) {
  case FileUndoKind::Create: {
    if (!std::filesystem::exists(action.afterPath, ec) || ec) {
      return {.ok = false, .path = action.afterPath, .error = "Created item no longer exists."};
    }
    if (std::filesystem::is_directory(action.afterPath, ec) && !ec && !std::filesystem::is_empty(action.afterPath, ec)) {
      return {.ok = false, .path = action.afterPath, .error = "Created folder is not empty."};
    }
    std::filesystem::remove(action.afterPath, ec);
    if (ec) return {.ok = false, .path = action.afterPath, .error = ec.message()};
    return {.ok = true, .path = action.afterPath};
  }
  case FileUndoKind::Rename:
  case FileUndoKind::Move:
    return moveReplacing(action.afterPath, action.beforePath);
  case FileUndoKind::Trash:
    return restoreTrashedPath(action.afterPath);
  case FileUndoKind::Copy:
    if (!action.removeCopiedItem) {
      return {.ok = false, .path = action.afterPath, .error = "Copied item is not marked safe to remove."};
    }
    if (!std::filesystem::exists(action.afterPath, ec) || ec) {
      return {.ok = false, .path = action.afterPath, .error = "Copied item no longer exists."};
    }
    std::filesystem::remove_all(action.afterPath, ec);
    if (ec) return {.ok = false, .path = action.afterPath, .error = ec.message()};
    return {.ok = true, .path = action.afterPath};
  }
  return {.ok = false, .error = "Unsupported undo action."};
}

FileClipboardState makeFileClipboard(std::vector<std::filesystem::path> paths, FileClipboardIntent intent) {
  paths.erase(std::remove_if(paths.begin(), paths.end(), [](auto const& path) {
                return path.empty();
              }),
              paths.end());
  return {.intent = intent, .paths = std::move(paths)};
}

std::string serializeFileClipboardText(FileClipboardState const& clipboard) {
  return serializeUriList(clipboard.paths);
}

FileClipboardState fileClipboardFromUriListText(std::string_view text, FileClipboardIntent intent) {
  return makeFileClipboard(parseUriList(text), intent);
}

std::vector<FileOperationResult> pasteFileClipboard(FileClipboardState const& clipboard,
                                                    std::filesystem::path const& destinationDirectory) {
  std::vector<FileOperationResult> results;
  results.reserve(clipboard.paths.size());
  for (auto const& path : clipboard.paths) {
    if (clipboard.intent == FileClipboardIntent::Cut) {
      results.push_back(movePath(path, destinationDirectory));
    } else {
      results.push_back(copyPath(path, destinationDirectory));
    }
  }
  return results;
}

std::string mimeTypeForPath(std::filesystem::path const& path, bool isDirectory) {
  if (isDirectory) return "inode/directory";
  std::string ext = lowerAscii(path.extension().string());
  if (ext == ".txt" || ext == ".md" || ext == ".log" || ext == ".toml" || ext == ".json" ||
      ext == ".cpp" || ext == ".hpp" || ext == ".c" || ext == ".h" || ext == ".sh") {
    return "text/plain";
  }
  if (ext == ".html" || ext == ".htm") return "text/html";
  if (ext == ".png") return "image/png";
  if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
  if (ext == ".gif") return "image/gif";
  if (ext == ".webp") return "image/webp";
  if (ext == ".svg") return "image/svg+xml";
  if (ext == ".pdf") return "application/pdf";
  if (ext == ".zip") return "application/zip";
  if (ext == ".gz") return "application/gzip";
  if (ext == ".tar") return "application/x-tar";
  if (ext == ".mp3") return "audio/mpeg";
  if (ext == ".wav") return "audio/wav";
  if (ext == ".mp4") return "video/mp4";
  return "application/octet-stream";
}

MimeAppsList parseMimeAppsList(std::string_view text) {
  MimeAppsList parsed;
  std::string section;
  std::istringstream input{std::string(text)};
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
    std::string mime = trim(stripped.substr(0, equals));
    std::vector<std::string> ids = splitDesktopIdList(stripped.substr(equals + 1u));
    if (mime.empty() || ids.empty()) continue;
    if (section == "Default Applications") {
      parsed.defaults[mime] = std::move(ids);
    } else if (section == "Added Associations") {
      parsed.associations[mime] = std::move(ids);
    }
  }
  return parsed;
}

std::vector<std::filesystem::path> defaultMimeAppsListPaths() {
  std::vector<std::filesystem::path> paths;
  paths.push_back(configHomeDirectory() / "mimeapps.list");
  paths.push_back(dataHomeDirectory() / "applications" / "mimeapps.list");

  char const* xdgDataDirs = std::getenv("XDG_DATA_DIRS");
  std::string_view dataDirs = xdgDataDirs && *xdgDataDirs ? std::string_view{xdgDataDirs}
                                                          : std::string_view{"/usr/local/share:/usr/share"};
  std::size_t start = 0;
  while (start <= dataDirs.size()) {
    std::size_t const end = dataDirs.find(':', start);
    std::string dir(dataDirs.substr(start, end == std::string_view::npos ? dataDirs.size() - start : end - start));
    if (!dir.empty()) paths.push_back(std::filesystem::path(dir) / "applications" / "mimeapps.list");
    if (end == std::string_view::npos) break;
    start = end + 1u;
  }
  return paths;
}

MimeAppsList loadMimeAppsList(std::vector<std::filesystem::path> const& paths) {
  MimeAppsList merged;
  auto mergeIds = [](std::vector<std::string>& destination, std::vector<std::string> const& source) {
    for (auto const& id : source) {
      if (std::find(destination.begin(), destination.end(), id) == destination.end()) destination.push_back(id);
    }
  };

  for (auto const& path : paths) {
    std::ifstream file(path);
    if (!file) continue;
    std::ostringstream contents;
    contents << file.rdbuf();
    MimeAppsList parsed = parseMimeAppsList(contents.str());
    for (auto const& [mime, ids] : parsed.defaults) {
      auto [it, inserted] = merged.defaults.emplace(mime, ids);
      if (!inserted) mergeIds(it->second, ids);
    }
    for (auto const& [mime, ids] : parsed.associations) {
      mergeIds(merged.associations[mime], ids);
    }
  }
  return merged;
}

std::vector<OpenWithChoice> openWithChoices(std::filesystem::path const& path,
                                            bool isDirectory,
                                            std::vector<lambda_shell::AppRegistryEntry> const& apps,
                                            MimeAppsList const& mimeApps) {
  std::string const mime = mimeTypeForPath(path, isDirectory);
  std::vector<OpenWithChoice> choices;
  std::set<std::string> added;

  auto addApp = [&](lambda_shell::AppRegistryEntry const& app, bool isDefault) {
    if (!appSupportsMime(app, mime) || added.contains(app.appId)) return;
    added.insert(app.appId);
    choices.push_back({.app = app, .isDefault = isDefault});
  };

  if (auto defaults = mimeApps.defaults.find(mime); defaults != mimeApps.defaults.end()) {
    for (auto const& id : defaults->second) {
      auto found = std::find_if(apps.begin(), apps.end(), [&](auto const& app) {
        return appIdMatchesDesktopId(app, id);
      });
      if (found != apps.end()) addApp(*found, true);
    }
  }
  if (auto associations = mimeApps.associations.find(mime); associations != mimeApps.associations.end()) {
    for (auto const& id : associations->second) {
      auto found = std::find_if(apps.begin(), apps.end(), [&](auto const& app) {
        return appIdMatchesDesktopId(app, id);
      });
      if (found != apps.end()) addApp(*found, false);
    }
  }

  std::vector<lambda_shell::AppRegistryEntry> remaining = apps;
  std::stable_sort(remaining.begin(), remaining.end(), [](auto const& a, auto const& b) {
    return ciLess(a.name, b.name);
  });
  for (auto const& app : remaining) {
    addApp(app, false);
  }
  return choices;
}

std::optional<OpenWithChoice> defaultOpenWithChoice(std::filesystem::path const& path,
                                                    bool isDirectory,
                                                    std::vector<lambda_shell::AppRegistryEntry> const& apps,
                                                    MimeAppsList const& mimeApps) {
  auto choices = openWithChoices(path, isDirectory, apps, mimeApps);
  if (choices.empty()) return std::nullopt;
  return choices.front();
}

std::vector<std::string> openCommandForChoice(OpenWithChoice const& choice, std::filesystem::path const& path) {
  return lambda_shell::parseDesktopExec(choice.app.command, path);
}

OpenEntryPlan openEntryPlan(FileEntry const& entry,
                            std::vector<lambda_shell::AppRegistryEntry> const& apps,
                            MimeAppsList const& mimeApps) {
  if (entry.isDirectory) {
    return {.ok = false, .error = "Use navigation for folders."};
  }
  auto choice = defaultOpenWithChoice(entry.path, false, apps, mimeApps);
  if (!choice) {
    return {.ok = false, .error = "No application is registered for this file type."};
  }
  auto command = openCommandForChoice(*choice, entry.path);
  if (command.empty()) {
    return {.ok = false, .error = "Registered application has no launch command."};
  }
  return {.ok = true, .command = std::move(command)};
}

FileIconLookup lookupFileIcon(std::filesystem::path const& themeRoot,
                              std::filesystem::path const& path,
                              bool isDirectory,
                              int preferredSize) {
  double const startMs = trace::nowMs();
  std::string const mime = mimeTypeForPath(path, isDirectory);
  std::string iconName = iconNameForMime(mime);
  if (auto themePath = lambda_shell::lookupIconThemePath(themeRoot, iconName, preferredSize); !themePath.empty()) {
    LAMBDA_FILES_TRACE_EVENT("icon-lookup path=\"%s\" root=\"%s\" mime=\"%s\" icon=\"%s\" fallback=0 hit=1 elapsed=%.3fms\n",
                 path.string().c_str(),
                 themeRoot.string().c_str(),
                 mime.c_str(),
                 iconName.c_str(),
                 trace::nowMs() - startMs);
    return {.iconName = iconName, .themePath = themePath, .fallback = false};
  }
  std::string fallback = isDirectory ? "folder" : "text-x-generic";
  if (mime == "application/octet-stream") fallback = "application-octet-stream";
  if (fallback != iconName) {
    if (auto themePath = lambda_shell::lookupIconThemePath(themeRoot, fallback, preferredSize); !themePath.empty()) {
      LAMBDA_FILES_TRACE_EVENT("icon-lookup path=\"%s\" root=\"%s\" mime=\"%s\" icon=\"%s\" fallback=1 hit=1 elapsed=%.3fms\n",
                   path.string().c_str(),
                   themeRoot.string().c_str(),
                   mime.c_str(),
                   fallback.c_str(),
                   trace::nowMs() - startMs);
      return {.iconName = fallback, .themePath = themePath, .fallback = true};
    }
  }
  LAMBDA_FILES_TRACE_EVENT("icon-lookup path=\"%s\" root=\"%s\" mime=\"%s\" icon=\"%s\" fallback=1 hit=0 elapsed=%.3fms\n",
               path.string().c_str(),
               themeRoot.string().c_str(),
               mime.c_str(),
               fallback.c_str(),
               trace::nowMs() - startMs);
  return {.iconName = fallback, .fallback = true};
}

FileIconLookup resolveFileIcon(std::vector<std::filesystem::path> const& themeRoots,
                               std::filesystem::path const& path,
                               bool isDirectory,
                               int preferredSize) {
  double const startMs = trace::nowMs();
  auto cacheKey = [&] {
    std::string key = path.string();
    key.push_back('\x1f');
    key += isDirectory ? 'd' : 'f';
    key.push_back('\x1f');
    key += std::to_string(preferredSize);
    for (auto const& root : themeRoots) {
      key.push_back('\x1f');
      key += root.string();
    }
    return key;
  }();
  static std::unordered_map<std::string, FileIconLookup> cache;
  if (auto found = cache.find(cacheKey); found != cache.end()) {
    LAMBDA_FILES_TRACE_EVENT("icon-resolve-cache path=\"%s\" dir=%d roots=%zu hit=%d fallback=%d elapsed=%.3fms\n",
                 path.string().c_str(),
                 isDirectory ? 1 : 0,
                 themeRoots.size(),
                 found->second.themePath.empty() ? 0 : 1,
                 found->second.fallback ? 1 : 0,
                 trace::nowMs() - startMs);
    return found->second;
  }
  FileIconLookup fallback;
  int rootsChecked = 0;
  for (auto const& root : themeRoots) {
    ++rootsChecked;
    FileIconLookup const lookup = lookupFileIcon(root, path, isDirectory, preferredSize);
    if (!lookup.themePath.empty()) {
      LAMBDA_FILES_TRACE_EVENT("icon-resolve path=\"%s\" dir=%d roots=%d hit=1 fallback=%d elapsed=%.3fms\n",
                   path.string().c_str(),
                   isDirectory ? 1 : 0,
                   rootsChecked,
                   lookup.fallback ? 1 : 0,
                   trace::nowMs() - startMs);
      cache.emplace(std::move(cacheKey), lookup);
      return lookup;
    }
    if (fallback.iconName.empty() || !lookup.fallback) fallback = lookup;
  }
  if (!fallback.iconName.empty()) {
    LAMBDA_FILES_TRACE_EVENT("icon-resolve path=\"%s\" dir=%d roots=%d hit=0 fallback=1 elapsed=%.3fms\n",
                 path.string().c_str(),
                 isDirectory ? 1 : 0,
                 rootsChecked,
                 trace::nowMs() - startMs);
    cache.emplace(std::move(cacheKey), fallback);
    return fallback;
  }
  FileIconLookup lookup = lookupFileIcon({}, path, isDirectory, preferredSize);
  LAMBDA_FILES_TRACE_EVENT("icon-resolve path=\"%s\" dir=%d roots=%d hit=%d fallback=%d elapsed=%.3fms\n",
               path.string().c_str(),
               isDirectory ? 1 : 0,
               rootsChecked,
               lookup.themePath.empty() ? 0 : 1,
               lookup.fallback ? 1 : 0,
               trace::nowMs() - startMs);
  cache.emplace(std::move(cacheKey), lookup);
  return lookup;
}

FilesPreferences defaultFilesPreferences() {
  return FilesPreferences{};
}

std::filesystem::path filesPreferencesPath() {
  if (auto env = pathFromEnv("LAMBDA_FILES_CONFIG"); !env.empty()) return env;
  return configHomeDirectory() / "lambda-files" / "config.toml";
}

FilesPreferences parseFilesPreferencesToml(std::string_view tomlText) {
  FilesPreferences preferences = defaultFilesPreferences();
  std::istringstream input{std::string(tomlText)};
  std::string line;
  while (std::getline(input, line)) {
    std::string_view view(line);
    if (auto hash = view.find('#'); hash != std::string_view::npos) view = view.substr(0, hash);
    std::string stripped = trim(view);
    if (stripped.empty()) continue;
    auto equals = stripped.find('=');
    if (equals == std::string::npos) continue;
    std::string key = trim(std::string_view(stripped).substr(0, equals));
    std::string value = trim(std::string_view(stripped).substr(equals + 1u));
    if (value.size() >= 2u && value.front() == '"' && value.back() == '"') {
      value = value.substr(1u, value.size() - 2u);
    }
    std::string lower = lowerAscii(value);
    if (key == "show_hidden") {
      if (lower == "true" || lower == "1") preferences.showHidden = true;
      else if (lower == "false" || lower == "0") preferences.showHidden = false;
    } else if (key == "view_mode") {
      if (lower == "grid" || lower == "list") preferences.viewMode = lower;
    } else if (key == "sort_key") {
      if (lower == "name") preferences.sortKey = FileSortKey::Name;
      else if (lower == "kind") preferences.sortKey = FileSortKey::Kind;
      else if (lower == "size") preferences.sortKey = FileSortKey::Size;
      else if (lower == "modified_time") preferences.sortKey = FileSortKey::ModifiedTime;
    } else if (key == "sort_ascending") {
      if (lower == "true" || lower == "1") preferences.sortAscending = true;
      else if (lower == "false" || lower == "0") preferences.sortAscending = false;
    } else if (key == "icon_size") {
      char* end = nullptr;
      long parsed = std::strtol(value.c_str(), &end, 10);
      if (end != value.c_str() && *end == '\0' && parsed >= 32 && parsed <= 256) {
        preferences.iconSize = static_cast<int>(parsed);
      }
    } else if (key == "show_trash") {
      if (lower == "true" || lower == "1") preferences.showTrash = true;
      else if (lower == "false" || lower == "0") preferences.showTrash = false;
    }
  }
  return preferences;
}

std::string writeFilesPreferencesToml(FilesPreferences const& preferences) {
  auto sortKey = [&] {
    switch (preferences.sortKey) {
    case FileSortKey::Name: return "name";
    case FileSortKey::Kind: return "kind";
    case FileSortKey::Size: return "size";
    case FileSortKey::ModifiedTime: return "modified_time";
    }
    return "name";
  };
  std::ostringstream out;
  out << "show_hidden = " << (preferences.showHidden ? "true" : "false") << "\n";
  out << "view_mode = \"" << preferences.viewMode << "\"\n";
  out << "sort_key = \"" << sortKey() << "\"\n";
  out << "sort_ascending = " << (preferences.sortAscending ? "true" : "false") << "\n";
  out << "icon_size = " << preferences.iconSize << "\n";
  out << "show_trash = " << (preferences.showTrash ? "true" : "false") << "\n";
  return out.str();
}

FileOperationResult saveFilesPreferences(FilesPreferences const& preferences, std::filesystem::path path) {
  std::error_code ec;
  if (path.empty()) path = filesPreferencesPath();
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return {.ok = false, .path = path, .error = "Failed to create config directory: " + ec.message()};
  }

  std::filesystem::path const temp =
      path.parent_path().empty() ? std::filesystem::path{"." + path.filename().string() + ".tmp"}
                                 : path.parent_path() / ("." + path.filename().string() + ".tmp");
  {
    std::ofstream out(temp, std::ios::trunc);
    if (!out) return {.ok = false, .path = path, .error = "Failed to open temporary config file."};
    out << writeFilesPreferencesToml(preferences);
    if (!out) {
      std::filesystem::remove(temp, ec);
      return {.ok = false, .path = path, .error = "Failed to write temporary config file."};
    }
  }

  std::filesystem::rename(temp, path, ec);
  if (ec) {
    std::filesystem::remove(temp, ec);
    return {.ok = false, .path = path, .error = "Failed to replace config file: " + ec.message()};
  }
  return {.ok = true, .path = path};
}

FilesPreferencesLoadResult loadFilesPreferences(std::filesystem::path path) {
  if (path.empty()) path = filesPreferencesPath();
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) {
    auto saved = saveFilesPreferences(defaultFilesPreferences(), path);
    return {
        .preferences = defaultFilesPreferences(),
        .path = path,
        .error = saved.ok ? std::string{} : saved.error,
        .created = saved.ok,
    };
  }

  std::ifstream in(path);
  if (!in) {
    return {
        .preferences = defaultFilesPreferences(),
        .path = path,
        .error = "Failed to read config file.",
    };
  }
  std::ostringstream contents;
  contents << in.rdbuf();
  return {
      .preferences = parseFilesPreferencesToml(contents.str()),
      .path = path,
  };
}

std::string serializeUriList(std::vector<std::filesystem::path> const& paths) {
  std::string output;
  for (auto const& path : paths) {
    std::filesystem::path absolute = path.is_absolute() ? path : std::filesystem::absolute(path);
    output += "file://";
    output += percentEncodePath(absolute.lexically_normal().string());
    output += "\r\n";
  }
  return output;
}

std::vector<std::filesystem::path> parseUriList(std::string_view text) {
  std::vector<std::filesystem::path> paths;
  std::size_t start = 0;
  while (start <= text.size()) {
    std::size_t end = text.find_first_of("\r\n", start);
    std::string line = trim(text.substr(start, end == std::string_view::npos ? text.size() - start : end - start));
    if (!line.empty() && line.front() != '#') {
      constexpr std::string_view prefix = "file://";
      if (line.starts_with(prefix)) {
        paths.emplace_back(percentDecode(std::string_view(line).substr(prefix.size())));
      }
    }
    if (end == std::string_view::npos) break;
    start = end + 1u;
    while (start < text.size() && (text[start] == '\r' || text[start] == '\n')) ++start;
  }
  return paths;
}

} // namespace lambda_files
