#pragma once

#include <Flux/UI/IconName.hpp>

#include <cstdint>
#include <filesystem>
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

struct FileEntry {
  std::string name;
  std::filesystem::path path;
  bool isDirectory = false;
  std::uintmax_t size = 0;
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

std::filesystem::path homeDirectory();
std::vector<SidebarPlace> const& sidebarPlaces();
ListDirectoryResult listDirectory(std::filesystem::path const& directory, bool includeHidden = false);
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

} // namespace lambda_files
