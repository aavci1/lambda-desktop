#include "FilesStore.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <system_error>

#if defined(__APPLE__)
#include <TargetConditionals.h>
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

int runShellCommand(std::string const& command) {
  if (command.empty()) {
    return -1;
  }
  return std::system(command.c_str());
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

} // namespace

std::filesystem::path homeDirectory() {
  if (auto const home = pathIfExists(pathFromEnv("HOME"))) {
    return *home;
  }
  return std::filesystem::current_path();
}

std::vector<SidebarPlace> const& sidebarPlaces() {
  static std::vector<SidebarPlace> const places = [] {
    std::filesystem::path const home = homeDirectory();
    std::vector<SidebarPlace> built;
    built.push_back({.id = "home", .label = "Home", .icon = flux::IconName::Home, .path = home});

    auto add = [&built](char const* id, char const* label, flux::IconName icon, std::filesystem::path path) {
      for (auto const& existing : built) {
        if (existing.id == id) {
          return;
        }
      }
      if (auto const resolved = pathIfExists(std::move(path))) {
        built.push_back({.id = id, .label = label, .icon = icon, .path = *resolved});
      }
    };

    add("desktop", "Desktop", flux::IconName::DesktopWindows, home / "Desktop");
    if (std::filesystem::path const docs = pathFromEnv("XDG_DOCUMENTS_DIR"); !docs.empty()) {
      add("documents", "Documents", flux::IconName::Description, docs);
    } else {
      add("documents", "Documents", flux::IconName::Description, home / "Documents");
    }
    if (std::filesystem::path const dl = pathFromEnv("XDG_DOWNLOAD_DIR"); !dl.empty()) {
      add("downloads", "Downloads", flux::IconName::Download, dl);
    } else {
      add("downloads", "Downloads", flux::IconName::Download, home / "Downloads");
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
    result.entries.push_back(FileEntry{
        .name = name,
        .path = entry.path(),
        .isDirectory = isDir,
        .size = size,
        .visualKind = visualKindForPath(entry.path(), isDir),
    });
  }

  std::sort(result.entries.begin(), result.entries.end(), [](FileEntry const& a, FileEntry const& b) {
    if (a.isDirectory != b.isDirectory) {
      return a.isDirectory > b.isDirectory;
    }
    return ciLess(a.name, b.name);
  });
  return result;
}

std::vector<BreadcrumbCrumb> breadcrumbCrumbs(std::filesystem::path const& path) {
  std::vector<BreadcrumbCrumb> crumbs;
  std::error_code ec;
  std::filesystem::path current = std::filesystem::weakly_canonical(path, ec);
  if (ec || current.empty()) {
    current = path;
  }

  std::filesystem::path const home = homeDirectory();
  crumbs.push_back({"Home", home});

  if (current == home) {
    return crumbs;
  }

  std::filesystem::path relative = current;
  if (std::filesystem::path relFromHome = std::filesystem::relative(current, home, ec); !ec) {
    relative = relFromHome;
  } else if (current.has_root_path() && current.root_path() != current) {
    relative = current.relative_path();
  }

  std::filesystem::path accumulated = home;
  for (std::filesystem::path const& part : relative) {
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
  if (entry.isDirectory) {
    error = "Use navigation for folders.";
    return false;
  }
#if defined(__APPLE__)
  int const code = runShellCommand("open " + shellQuote(entry.path.string()));
#else
  int const code = runShellCommand("xdg-open " + shellQuote(entry.path.string()));
#endif
  if (code != 0) {
    error = "Could not open this item with the system handler.";
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

} // namespace lambda_files
