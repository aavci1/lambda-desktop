#pragma once

#include <Flux/UI/IconName.hpp>

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace lambda_files {

enum class FileVisualKind {
  Folder,
  Generic,
  Pdf,
  Image,
  Presentation,
  Sketch,
};

enum class FileSortKey {
  Name,
  Kind,
  Size,
  ModifiedTime,
};

struct FileEntry {
  std::string name;
  std::filesystem::path path;
  bool isDirectory = false;
  std::uintmax_t size = 0;
  std::filesystem::file_time_type modifiedAt{};
  FileVisualKind visualKind = FileVisualKind::Generic;

  bool operator==(FileEntry const& other) const = default;
};

struct SidebarPlace {
  std::string id;
  std::string label;
  flux::IconName icon = flux::IconName::Folder;
  std::filesystem::path path;
};

struct BreadcrumbCrumb {
  std::string label;
  std::filesystem::path path;

  bool operator==(BreadcrumbCrumb const& other) const = default;
};

struct ListDirectoryResult {
  std::vector<FileEntry> entries;
  std::string error;
};

struct NavigationHistory {
  std::string current;
  std::vector<std::string> back;
  std::vector<std::string> forward;

  bool canGoBack() const { return !back.empty(); }
  bool canGoForward() const { return !forward.empty(); }
};

struct FileOperationResult {
  bool ok = false;
  std::filesystem::path path;
  std::string error;
};

struct FileSelectionState {
  std::vector<std::filesystem::path> selected;
  int anchorIndex = -1;

  [[nodiscard]] bool contains(std::filesystem::path const& path) const;
};

struct TrashInfo {
  std::filesystem::path originalPath;
  std::string deletionDate;

  bool operator==(TrashInfo const&) const = default;
};

std::filesystem::path homeDirectory();
std::filesystem::path trashFilesDirectory();
std::filesystem::path trashInfoDirectory();
std::map<std::string, std::filesystem::path> parseXdgUserDirs(std::string_view configText,
                                                              std::filesystem::path const& home);
std::vector<SidebarPlace> const& sidebarPlaces();
ListDirectoryResult listDirectory(std::filesystem::path const& directory, bool includeHidden = false);
std::vector<FileEntry> sortedEntries(std::vector<FileEntry> entries,
                                     FileSortKey key = FileSortKey::Name,
                                     bool ascending = true,
                                     bool directoriesFirst = true);
std::vector<BreadcrumbCrumb> breadcrumbCrumbs(std::filesystem::path const& path);
std::optional<std::filesystem::path> parentDirectory(std::filesystem::path const& path);
FileVisualKind visualKindForEntry(std::filesystem::path const& path, bool isDirectory);
std::string formatEntrySubtitle(FileEntry const& entry);
std::string gridDisplayName(std::string name);
std::string formatSidebarFooter(std::filesystem::path const& path);

bool openEntry(FileEntry const& entry, std::string& error);
bool revealEntryInSystem(FileEntry const& entry, std::string& error);

std::string normalizeDirectoryPath(std::filesystem::path path);
NavigationHistory navigateTo(NavigationHistory history, std::filesystem::path path);
NavigationHistory goBack(NavigationHistory history);
NavigationHistory goForward(NavigationHistory history);
NavigationHistory goUp(NavigationHistory history);

FileSelectionState selectOnly(std::vector<FileEntry> const& entries, int index);
FileSelectionState toggleSelection(FileSelectionState state, std::vector<FileEntry> const& entries, int index);
FileSelectionState rangeSelection(FileSelectionState state, std::vector<FileEntry> const& entries, int index);
FileSelectionState clearSelection(FileSelectionState state);

std::filesystem::path collisionFreePath(std::filesystem::path const& directory, std::string const& preferredName);
FileOperationResult createFolder(std::filesystem::path const& directory, std::string preferredName = "New Folder");
FileOperationResult createFile(std::filesystem::path const& directory, std::string preferredName = "New File.txt");
std::string validateRename(std::filesystem::path const& source, std::string const& newName);
FileOperationResult renamePath(std::filesystem::path const& source, std::string const& newName);
FileOperationResult copyPath(std::filesystem::path const& source, std::filesystem::path const& destinationDirectory);
FileOperationResult movePath(std::filesystem::path const& source, std::filesystem::path const& destinationDirectory);
FileOperationResult duplicatePath(std::filesystem::path const& source);
FileOperationResult trashPath(std::filesystem::path const& source);
FileOperationResult restoreTrashedPath(std::filesystem::path const& trashedPath);
std::optional<TrashInfo> parseTrashInfo(std::filesystem::path const& infoPath);

std::string serializeUriList(std::vector<std::filesystem::path> const& paths);
std::vector<std::filesystem::path> parseUriList(std::string_view text);

} // namespace lambda_files
