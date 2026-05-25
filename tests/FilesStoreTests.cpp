#include "FilesStore.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace {

struct ScopedEnv {
  explicit ScopedEnv(char const* name)
      : name(name) {
    if (char const* value = std::getenv(name); value) {
      hadOriginal = true;
      original = value;
    }
  }

  ~ScopedEnv() {
    if (!hadOriginal) {
      unsetenv(name);
    } else {
      setenv(name, original.c_str(), 1);
    }
  }

  char const* name;
  bool hadOriginal = false;
  std::string original;
};

std::filesystem::path tempRoot(char const* name) {
  auto path = std::filesystem::temp_directory_path() /
              (std::string(name) + "-" + std::to_string(static_cast<unsigned long long>(getpid())));
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path);
  return path;
}

} // namespace

TEST_CASE("FilesStore parses XDG user directories") {
  std::filesystem::path const home = "/home/tester";
  auto dirs = lambda_files::parseXdgUserDirs(R"(
XDG_DESKTOP_DIR="$HOME/Desktop"
XDG_DOWNLOAD_DIR="$HOME/Downloads"
XDG_DOCUMENTS_DIR="/data/Documents"
XDG_TEMPLATES_DIR=~/Templates
BROKEN=value
)",
                                             home);

  CHECK(dirs.at("desktop") == home / "Desktop");
  CHECK(dirs.at("download") == home / "Downloads");
  CHECK(dirs.at("documents") == "/data/Documents");
  CHECK(dirs.at("templates") == home / "Templates");
  CHECK_FALSE(dirs.contains("broken"));
}

TEST_CASE("FilesStore home directory falls back when HOME is unusable") {
  ScopedEnv home("HOME");
  setenv("HOME", "/path/that/does/not/exist/lambda", 1);
  CHECK(lambda_files::homeDirectory() == std::filesystem::current_path());
}

TEST_CASE("FilesStore breadcrumbs handle home root and outside home") {
  ScopedEnv homeEnv("HOME");
  auto root = tempRoot("lambda-files-breadcrumb-test");
  auto home = root / "home";
  auto nested = home / "Projects" / "Flux";
  auto outside = root / "outside" / "Folder";
  std::filesystem::create_directories(nested);
  std::filesystem::create_directories(outside);
  setenv("HOME", home.c_str(), 1);

  auto homeCrumbs = lambda_files::breadcrumbCrumbs(home);
  REQUIRE(homeCrumbs.size() == 1);
  CHECK(homeCrumbs[0] == lambda_files::BreadcrumbCrumb{"Home", home});

  auto nestedCrumbs = lambda_files::breadcrumbCrumbs(nested);
  REQUIRE(nestedCrumbs.size() == 3);
  CHECK(nestedCrumbs[0] == lambda_files::BreadcrumbCrumb{"Home", home});
  CHECK(nestedCrumbs[1].label == "Projects");
  CHECK(nestedCrumbs[2].path == nested);

  auto rootCrumbs = lambda_files::breadcrumbCrumbs("/");
  REQUIRE(rootCrumbs.size() == 1);
  CHECK(rootCrumbs[0].label == "/");

  auto outsideCrumbs = lambda_files::breadcrumbCrumbs(outside);
  REQUIRE(outsideCrumbs.size() >= 3);
  CHECK(outsideCrumbs[0].label == "/");
  CHECK(outsideCrumbs.back().label == "Folder");
  CHECK(outsideCrumbs.back().path == outside);

  std::filesystem::remove_all(root);
}

TEST_CASE("FilesStore sorts entries by name kind size and modified time") {
  using lambda_files::FileEntry;
  using lambda_files::FileSortKey;
  using lambda_files::FileVisualKind;
  auto now = std::filesystem::file_time_type::clock::now();
  std::vector<FileEntry> entries{
      {.name = "zeta.txt", .path = "/tmp/zeta.txt", .isDirectory = false, .size = 20, .modifiedAt = now,
       .visualKind = FileVisualKind::Generic},
      {.name = "Alpha", .path = "/tmp/Alpha", .isDirectory = true, .size = 0, .modifiedAt = now,
       .visualKind = FileVisualKind::Folder},
      {.name = "image.png", .path = "/tmp/image.png", .isDirectory = false, .size = 5,
       .modifiedAt = now + std::chrono::seconds(1), .visualKind = FileVisualKind::Image},
      {.name = "book.pdf", .path = "/tmp/book.pdf", .isDirectory = false, .size = 100,
       .modifiedAt = now - std::chrono::seconds(1), .visualKind = FileVisualKind::Pdf},
  };

  auto byName = lambda_files::sortedEntries(entries);
  CHECK(byName[0].name == "Alpha");
  CHECK(byName[1].name == "book.pdf");

  auto bySizeDescending = lambda_files::sortedEntries(entries, FileSortKey::Size, false, false);
  CHECK(bySizeDescending[0].name == "book.pdf");
  CHECK(bySizeDescending.back().name == "Alpha");

  auto byKind = lambda_files::sortedEntries(entries, FileSortKey::Kind, true, false);
  CHECK(byKind[0].visualKind == FileVisualKind::Folder);
  CHECK(byKind[1].visualKind == FileVisualKind::Generic);

  auto byModified = lambda_files::sortedEntries(entries, FileSortKey::ModifiedTime, true, false);
  CHECK(byModified[0].name == "book.pdf");
  CHECK(byModified.back().name == "image.png");
}

TEST_CASE("FilesStore directory listing records modified time and keeps folder-first name order") {
  auto root = tempRoot("lambda-files-listing-test");
  std::filesystem::create_directories(root / "Beta");
  std::filesystem::create_directories(root / "alpha");
  {
    std::ofstream(root / "zeta.txt") << "z";
    std::ofstream(root / ".hidden") << "h";
  }

  auto visible = lambda_files::listDirectory(root, false);
  REQUIRE(visible.error.empty());
  REQUIRE(visible.entries.size() == 3);
  CHECK(visible.entries[0].name == "alpha");
  CHECK(visible.entries[1].name == "Beta");
  CHECK(visible.entries[2].name == "zeta.txt");
  CHECK(visible.entries[2].modifiedAt != std::filesystem::file_time_type{});

  auto hidden = lambda_files::listDirectory(root, true);
  CHECK(hidden.entries.size() == 4);

  std::filesystem::remove_all(root);
}

TEST_CASE("FilesStore selection supports single toggle range and clear") {
  std::vector<lambda_files::FileEntry> entries{
      {.name = "a", .path = "/tmp/a"},
      {.name = "b", .path = "/tmp/b"},
      {.name = "c", .path = "/tmp/c"},
      {.name = "d", .path = "/tmp/d"},
  };

  auto state = lambda_files::selectOnly(entries, 1);
  CHECK(state.selected == std::vector<std::filesystem::path>{"/tmp/b"});
  CHECK(state.anchorIndex == 1);
  CHECK(state.contains("/tmp/b"));

  state = lambda_files::toggleSelection(state, entries, 3);
  CHECK(state.selected == std::vector<std::filesystem::path>{"/tmp/b", "/tmp/d"});
  CHECK(state.anchorIndex == 3);

  state = lambda_files::toggleSelection(state, entries, 1);
  CHECK(state.selected == std::vector<std::filesystem::path>{"/tmp/d"});

  state = lambda_files::rangeSelection(state, entries, 0);
  CHECK(state.selected == std::vector<std::filesystem::path>{"/tmp/a", "/tmp/b", "/tmp/c", "/tmp/d"});
  CHECK(state.anchorIndex == 3);

  state = lambda_files::clearSelection(state);
  CHECK(state.selected.empty());
  CHECK(state.anchorIndex == -1);
}

TEST_CASE("FilesStore creates folders and files with collision-free names") {
  auto root = tempRoot("lambda-files-create-test");
  REQUIRE(lambda_files::createFolder(root, "New Folder").ok);

  auto folder = lambda_files::createFolder(root, "New Folder");
  CHECK(folder.ok);
  CHECK(folder.path.filename() == "New Folder 2");
  CHECK(std::filesystem::is_directory(folder.path));

  auto file = lambda_files::createFile(root, "Note.txt");
  CHECK(file.ok);
  CHECK(file.path.filename() == "Note.txt");
  auto file2 = lambda_files::createFile(root, "Note.txt");
  CHECK(file2.ok);
  CHECK(file2.path.filename() == "Note 2.txt");

  std::filesystem::remove_all(root);
}

TEST_CASE("FilesStore validates and renames paths") {
  auto root = tempRoot("lambda-files-rename-test");
  {
    std::ofstream(root / "old.txt") << "old";
    std::ofstream(root / "taken.txt") << "taken";
  }

  CHECK(lambda_files::validateRename(root / "old.txt", "").find("empty") != std::string::npos);
  CHECK(lambda_files::validateRename(root / "old.txt", "../bad").find("separator") != std::string::npos);
  CHECK(lambda_files::validateRename(root / "old.txt", "taken.txt").find("exists") != std::string::npos);

  auto renamed = lambda_files::renamePath(root / "old.txt", "new.txt");
  CHECK(renamed.ok);
  CHECK(renamed.path == root / "new.txt");
  CHECK(std::filesystem::exists(root / "new.txt"));
  CHECK_FALSE(std::filesystem::exists(root / "old.txt"));

  std::filesystem::remove_all(root);
}

TEST_CASE("FilesStore copies moves and duplicates files and folders") {
  auto root = tempRoot("lambda-files-operation-test");
  auto sourceDir = root / "source";
  auto destination = root / "destination";
  std::filesystem::create_directories(sourceDir / "nested");
  std::filesystem::create_directories(destination);
  {
    std::ofstream(sourceDir / "nested" / "file.txt") << "hello";
    std::ofstream(root / "single.txt") << "one";
  }

  auto copiedDir = lambda_files::copyPath(sourceDir, destination);
  CHECK(copiedDir.ok);
  CHECK(std::filesystem::exists(destination / "source" / "nested" / "file.txt"));

  auto copiedAgain = lambda_files::copyPath(sourceDir, destination);
  CHECK(copiedAgain.ok);
  CHECK(copiedAgain.path.filename() == "source 2");

  auto duplicate = lambda_files::duplicatePath(root / "single.txt");
  CHECK(duplicate.ok);
  CHECK(duplicate.path.filename() == "single copy.txt");
  CHECK(std::filesystem::exists(duplicate.path));

  auto moved = lambda_files::movePath(root / "single.txt", destination);
  CHECK(moved.ok);
  CHECK(std::filesystem::exists(destination / "single.txt"));
  CHECK_FALSE(std::filesystem::exists(root / "single.txt"));

  std::filesystem::remove_all(root);
}

TEST_CASE("FilesStore serializes and parses URI lists") {
  std::vector<std::filesystem::path> paths{"/tmp/alpha.txt", "/tmp/space name.txt"};
  std::string const uriList = lambda_files::serializeUriList(paths);
  CHECK(uriList.find("file:///tmp/space%20name.txt") != std::string::npos);

  auto parsed = lambda_files::parseUriList("# comment\r\nfile:///tmp/alpha.txt\r\nfile:///tmp/space%20name.txt\r\n");
  CHECK(parsed == paths);
}
