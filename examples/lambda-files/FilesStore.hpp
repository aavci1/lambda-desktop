#pragma once

#include <Flux/UI/IconName.hpp>

#include "Shell/ShellAppRegistry.hpp"

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

enum class FileClipboardIntent {
  Copy,
  Cut,
};

enum class FileOperationKind {
  Refresh,
  CreateFolder,
  CreateFile,
  Rename,
  Copy,
  Move,
  Duplicate,
  Trash,
  Restore,
};

enum class FileOperationStatus {
  Idle,
  Running,
  Succeeded,
  Failed,
  Cancelled,
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

  bool operator==(NavigationHistory const&) const = default;
};

struct NavigationResult {
  NavigationHistory history;
  bool ok = false;
  std::string error;

  bool operator==(NavigationResult const&) const = default;
};

struct FileOperationResult {
  bool ok = false;
  std::filesystem::path path;
  std::string error;
};

struct FileOperationError {
  std::filesystem::path path;
  std::string message;

  bool operator==(FileOperationError const&) const = default;
};

struct FileOperationProgress {
  std::uint64_t id = 0;
  FileOperationKind kind = FileOperationKind::Refresh;
  FileOperationStatus status = FileOperationStatus::Idle;
  std::filesystem::path currentPath;
  std::size_t completedItems = 0;
  std::size_t totalItems = 0;
  bool cancellable = false;
  bool cancelRequested = false;
  std::vector<FileOperationError> errors;

  [[nodiscard]] bool active() const noexcept { return status == FileOperationStatus::Running; }
  [[nodiscard]] double fractionComplete() const noexcept;

  bool operator==(FileOperationProgress const&) const = default;
};

struct FileClipboardState {
  FileClipboardIntent intent = FileClipboardIntent::Copy;
  std::vector<std::filesystem::path> paths;

  [[nodiscard]] bool empty() const noexcept { return paths.empty(); }
};

struct DirectoryChangeSet {
  std::vector<std::filesystem::path> added;
  std::vector<std::filesystem::path> removed;
  std::vector<std::filesystem::path> modified;

  bool operator==(DirectoryChangeSet const&) const = default;
};

struct FilesPreferences {
  bool showHidden = false;
  std::string viewMode = "grid";
  FileSortKey sortKey = FileSortKey::Name;
  bool sortAscending = true;
  int iconSize = 96;
  bool showTrash = true;

  bool operator==(FilesPreferences const&) const = default;
};

struct FilesPreferencesLoadResult {
  FilesPreferences preferences;
  std::filesystem::path path;
  std::string error;
  bool created = false;

  bool operator==(FilesPreferencesLoadResult const&) const = default;
};

enum class FileConflictDecision {
  KeepBoth,
  Replace,
  Skip,
  Cancel,
};

enum class FileUndoKind {
  Create,
  Rename,
  Move,
  Trash,
  Copy,
};

struct FileUndoAction {
  FileUndoKind kind = FileUndoKind::Create;
  std::filesystem::path beforePath;
  std::filesystem::path afterPath;
  bool removeCopiedItem = false;
};

enum class FileContextCommandKind {
  Open,
  Reveal,
  Copy,
  Cut,
  Paste,
  Duplicate,
  Trash,
  NewFolder,
  NewFile,
  SelectAll,
};

struct FileContextCommand {
  FileContextCommandKind kind = FileContextCommandKind::Open;
  std::string label;
  bool enabled = false;
  bool destructive = false;

  bool operator==(FileContextCommand const&) const = default;
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

struct MimeAppsList {
  std::map<std::string, std::vector<std::string>> defaults;
  std::map<std::string, std::vector<std::string>> associations;
};

struct OpenWithChoice {
  lambda_shell::AppRegistryEntry app;
  bool isDefault = false;
};

struct OpenEntryPlan {
  bool ok = false;
  std::vector<std::string> command;
  std::string error;

  bool operator==(OpenEntryPlan const&) const = default;
};

struct FileIconLookup {
  std::string iconName;
  std::filesystem::path themePath;
  bool fallback = false;
};

struct FilesModel {
  NavigationHistory history;
  std::vector<FileEntry> entries;
  std::vector<FileEntry> visibleEntries;
  FileSelectionState selection;
  FilesPreferences preferences;
  std::string query;
  std::string error;
  DirectoryChangeSet lastChanges;
  FileOperationProgress operation;

  bool operator==(FilesModel const&) const = default;
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
std::vector<FileEntry> filterEntries(std::vector<FileEntry> const& entries, std::string_view query);
DirectoryChangeSet diffDirectoryEntries(std::vector<FileEntry> before, std::vector<FileEntry> after);
FilesPreferences defaultFilesPreferences();
FilesModel makeFilesModel(std::filesystem::path directory = {},
                          FilesPreferences preferences = defaultFilesPreferences());
FilesModel applyDirectoryListing(FilesModel model,
                                 std::filesystem::path directory,
                                 ListDirectoryResult listing);
FilesModel setFilesModelQuery(FilesModel model, std::string query);
FilesModel setFilesModelPreferences(FilesModel model, FilesPreferences preferences);
std::vector<BreadcrumbCrumb> breadcrumbCrumbs(std::filesystem::path const& path);
std::optional<std::filesystem::path> parentDirectory(std::filesystem::path const& path);
FileVisualKind visualKindForEntry(std::filesystem::path const& path, bool isDirectory);
std::string formatEntrySubtitle(FileEntry const& entry);
std::string gridDisplayName(std::string name);
std::string formatSidebarFooter(std::filesystem::path const& path);

bool openEntry(FileEntry const& entry, std::string& error);
bool openEntryWithApps(FileEntry const& entry,
                       std::vector<lambda_shell::AppRegistryEntry> const& apps,
                       MimeAppsList const& mimeApps,
                       std::string& error);
bool revealEntryInSystem(FileEntry const& entry, std::string& error);

std::string normalizeDirectoryPath(std::filesystem::path path);
NavigationHistory navigateTo(NavigationHistory history, std::filesystem::path path);
NavigationResult navigateToDirectory(NavigationHistory history, std::filesystem::path path);
NavigationHistory goBack(NavigationHistory history);
NavigationHistory goForward(NavigationHistory history);
NavigationHistory goUp(NavigationHistory history);

FileSelectionState selectOnly(std::vector<FileEntry> const& entries, int index);
FileSelectionState toggleSelection(FileSelectionState state, std::vector<FileEntry> const& entries, int index);
FileSelectionState rangeSelection(FileSelectionState state, std::vector<FileEntry> const& entries, int index);
FileSelectionState clearSelection(FileSelectionState state);
FileSelectionState selectAllEntries(std::vector<FileEntry> const& entries);
int focusedSelectionIndex(FileSelectionState const& state, std::vector<FileEntry> const& entries);
FileSelectionState moveSelectionToIndex(FileSelectionState state,
                                        std::vector<FileEntry> const& entries,
                                        int index,
                                        bool extend);
FileSelectionState moveSelectionByOffset(FileSelectionState state,
                                         std::vector<FileEntry> const& entries,
                                         int offset,
                                         bool extend);
std::vector<FileEntry> selectedEntries(std::vector<FileEntry> const& entries, FileSelectionState const& selection);
std::vector<FileContextCommand> contextMenuCommands(std::vector<FileEntry> const& entries,
                                                    FileSelectionState const& selection,
                                                    FileClipboardState const& clipboard,
                                                    bool backgroundMenu);

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
std::filesystem::path resolveConflictPath(std::filesystem::path const& destination,
                                          FileConflictDecision decision);
FileOperationResult undoFileOperation(FileUndoAction const& action);
FileOperationProgress beginFileOperation(FileOperationKind kind,
                                         std::size_t totalItems,
                                         bool cancellable,
                                         std::uint64_t id = 1);
FileOperationProgress advanceFileOperation(FileOperationProgress progress,
                                           std::filesystem::path currentPath = {},
                                           std::size_t completedDelta = 1);
FileOperationProgress failFileOperation(FileOperationProgress progress,
                                        std::filesystem::path path,
                                        std::string message);
FileOperationProgress requestCancelFileOperation(FileOperationProgress progress);
FileOperationProgress completeFileOperation(FileOperationProgress progress);
FileClipboardState makeFileClipboard(std::vector<std::filesystem::path> paths, FileClipboardIntent intent);
std::vector<FileOperationResult> pasteFileClipboard(FileClipboardState const& clipboard,
                                                    std::filesystem::path const& destinationDirectory);

std::string mimeTypeForPath(std::filesystem::path const& path, bool isDirectory);
MimeAppsList parseMimeAppsList(std::string_view text);
std::vector<std::filesystem::path> defaultMimeAppsListPaths();
MimeAppsList loadMimeAppsList(std::vector<std::filesystem::path> const& paths = defaultMimeAppsListPaths());
std::vector<OpenWithChoice> openWithChoices(std::filesystem::path const& path,
                                            bool isDirectory,
                                            std::vector<lambda_shell::AppRegistryEntry> const& apps,
                                            MimeAppsList const& mimeApps);
std::optional<OpenWithChoice> defaultOpenWithChoice(std::filesystem::path const& path,
                                                    bool isDirectory,
                                                    std::vector<lambda_shell::AppRegistryEntry> const& apps,
                                                    MimeAppsList const& mimeApps);
std::vector<std::string> openCommandForChoice(OpenWithChoice const& choice, std::filesystem::path const& path);
OpenEntryPlan openEntryPlan(FileEntry const& entry,
                            std::vector<lambda_shell::AppRegistryEntry> const& apps,
                            MimeAppsList const& mimeApps);
FileIconLookup lookupFileIcon(std::filesystem::path const& themeRoot,
                              std::filesystem::path const& path,
                              bool isDirectory,
                              int preferredSize = 48);
FileIconLookup resolveFileIcon(std::vector<std::filesystem::path> const& themeRoots,
                               std::filesystem::path const& path,
                               bool isDirectory,
                               int preferredSize = 48);
FilesPreferences parseFilesPreferencesToml(std::string_view tomlText);
std::string writeFilesPreferencesToml(FilesPreferences const& preferences);
std::filesystem::path filesPreferencesPath();
FilesPreferencesLoadResult loadFilesPreferences(std::filesystem::path path = filesPreferencesPath());
FileOperationResult saveFilesPreferences(FilesPreferences const& preferences,
                                         std::filesystem::path path = filesPreferencesPath());

std::string serializeUriList(std::vector<std::filesystem::path> const& paths);
std::vector<std::filesystem::path> parseUriList(std::string_view text);

} // namespace lambda_files
