#include "FilesStore.hpp"

#include <doctest/doctest.h>

#include <algorithm>
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

std::filesystem::path canonicalPath(std::filesystem::path const& path) {
  std::error_code ec;
  std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
  if (!ec && !canonical.empty()) {
    return canonical;
  }
  return path;
}

bool hasStagingCopyPath(std::filesystem::path const& root) {
  std::error_code ec;
  for (auto const& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
    if (ec) return true;
    if (entry.path().filename().string().find(".copying") != std::string::npos) return true;
  }
  return false;
}

lambda::system::UDisks2OperationResult storageSuccess(std::string value = {}) {
  lambda::system::UDisks2OperationResult result;
  result.ok = true;
  result.value = std::move(value);
  return result;
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

TEST_CASE("FilesStore surfaces UDisks2 volumes in sidebar places") {
  std::vector<lambda_files::SidebarPlace> places = {
      {.id = "home", .label = "Home", .icon = lambda::IconName::Home, .path = "/home/tester"},
      {.id = "downloads", .label = "Downloads", .icon = lambda::IconName::Download, .path = "/media/DUP"},
  };

  lambda::system::UDisks2Snapshot snapshot;
  snapshot.volumes.push_back(lambda::system::UDisks2VolumeSnapshot{
      .path = "/org/freedesktop/UDisks2/block_devices/sdb1",
      .drivePath = "/org/freedesktop/UDisks2/drives/Lambda_USB",
      .label = "USB_DISK",
      .hasFilesystem = true,
      .mountPoints = {"/media/USB_DISK"},
      .jobs = {
          lambda::system::UDisks2JobSnapshot{
              .path = "/org/freedesktop/UDisks2/jobs/1",
              .operation = "filesystem-unmount",
              .progress = 0.5,
              .progressValid = true,
              .cancelable = true,
          },
      },
      .drive = lambda::system::UDisks2DriveSnapshot{
          .path = "/org/freedesktop/UDisks2/drives/Lambda_USB",
          .ejectable = true,
      },
  });
  snapshot.volumes.push_back(lambda::system::UDisks2VolumeSnapshot{
      .path = "/org/freedesktop/UDisks2/block_devices/sdc1",
      .label = "DUP",
      .hasFilesystem = true,
      .mountPoints = {"/media/DUP"},
  });
  snapshot.volumes.push_back(lambda::system::UDisks2VolumeSnapshot{
      .path = "/org/freedesktop/UDisks2/block_devices/sdd1",
      .drivePath = "/org/freedesktop/UDisks2/drives/Lambda_USB",
      .label = "UNMOUNTED",
      .hasFilesystem = true,
      .drive = lambda::system::UDisks2DriveSnapshot{
          .path = "/org/freedesktop/UDisks2/drives/Lambda_USB",
          .ejectable = true,
      },
  });
  snapshot.volumes.push_back(lambda::system::UDisks2VolumeSnapshot{
      .path = "/org/freedesktop/UDisks2/block_devices/sde1",
      .drivePath = "/org/freedesktop/UDisks2/drives/Lambda_USB",
      .label = "LOCKED",
      .cleartextDevice = "/",
      .encrypted = true,
      .unlocked = false,
      .drive = lambda::system::UDisks2DriveSnapshot{
          .path = "/org/freedesktop/UDisks2/drives/Lambda_USB",
          .ejectable = true,
      },
  });
  snapshot.volumes.push_back(lambda::system::UDisks2VolumeSnapshot{
      .path = "/org/freedesktop/UDisks2/block_devices/sdf1",
      .label = "CRYPT_PARENT",
      .cleartextDevice = "/org/freedesktop/UDisks2/block_devices/dm_2d0",
      .encrypted = true,
      .unlocked = true,
  });
  snapshot.volumes.push_back(lambda::system::UDisks2VolumeSnapshot{
      .path = "/org/freedesktop/UDisks2/block_devices/dm_2d0",
      .drivePath = "/org/freedesktop/UDisks2/drives/Lambda_USB",
      .label = "UNLOCKED",
      .cryptoBackingDevice = "/org/freedesktop/UDisks2/block_devices/sdf1",
      .hasFilesystem = true,
      .cleartext = true,
      .drive = lambda::system::UDisks2DriveSnapshot{
          .path = "/org/freedesktop/UDisks2/drives/Lambda_USB",
          .ejectable = true,
      },
  });
  snapshot.volumes.push_back(lambda::system::UDisks2VolumeSnapshot{
      .path = "/org/freedesktop/UDisks2/block_devices/nvme0n1p1",
      .label = "ROOT",
      .hintSystem = true,
      .hasFilesystem = true,
      .mountPoints = {"/"},
  });

  auto withVolumes = lambda_files::sidebarPlacesWithVolumes(std::move(places), snapshot);
  auto findPlace = [&withVolumes](std::string const& id) -> lambda_files::SidebarPlace const* {
    auto found = std::find_if(withVolumes.begin(), withVolumes.end(), [&](auto const& place) {
      return place.id == id;
    });
    return found == withVolumes.end() ? nullptr : &*found;
  };

  REQUIRE(withVolumes.size() == 6);
  auto const* mounted = findPlace("volume:/org/freedesktop/UDisks2/block_devices/sdb1");
  REQUIRE(mounted != nullptr);
  CHECK(mounted->label == "USB_DISK");
  CHECK(mounted->kind == lambda_files::SidebarPlaceKind::Volume);
  CHECK(mounted->icon == lambda::IconName::DevicesOther);
  CHECK(mounted->path == "/media/USB_DISK");
  CHECK(mounted->subtitle == "Unmounting 50%");
  CHECK(mounted->volumeMounted);
  CHECK(mounted->volumeEjectable);
  CHECK(mounted->volumeCancelable);
  CHECK(mounted->jobPath == "/org/freedesktop/UDisks2/jobs/1");

  auto mountedCommands = lambda_files::sidebarVolumeContextCommands(*mounted);
  REQUIRE(mountedCommands.size() == 5);
  CHECK(mountedCommands[0].kind == lambda_files::SidebarVolumeCommandKind::Open);
  CHECK(mountedCommands[0].enabled);
  CHECK(mountedCommands[1].kind == lambda_files::SidebarVolumeCommandKind::Unmount);
  CHECK(mountedCommands[1].enabled);
  CHECK(mountedCommands[2].kind == lambda_files::SidebarVolumeCommandKind::ForceUnmount);
  CHECK(mountedCommands[2].destructive);
  CHECK(mountedCommands[3].kind == lambda_files::SidebarVolumeCommandKind::Eject);
  CHECK(mountedCommands[3].enabled);
  CHECK(mountedCommands[4].kind == lambda_files::SidebarVolumeCommandKind::Cancel);
  CHECK(mountedCommands[4].enabled);

  auto const* unmounted = findPlace("volume:/org/freedesktop/UDisks2/block_devices/sdd1");
  REQUIRE(unmounted != nullptr);
  CHECK(unmounted->path.empty());
  CHECK(unmounted->subtitle == "Not mounted");
  CHECK(unmounted->volumeMountable);
  auto unmountedCommands = lambda_files::sidebarVolumeContextCommands(*unmounted);
  REQUIRE(unmountedCommands.size() == 3);
  CHECK(unmountedCommands[0].kind == lambda_files::SidebarVolumeCommandKind::Open);
  CHECK(unmountedCommands[0].enabled);
  CHECK(unmountedCommands[1].kind == lambda_files::SidebarVolumeCommandKind::Mount);
  CHECK(unmountedCommands[1].enabled);

  auto const* locked = findPlace("volume:/org/freedesktop/UDisks2/block_devices/sde1");
  REQUIRE(locked != nullptr);
  CHECK(locked->icon == lambda::IconName::Lock);
  CHECK(locked->subtitle == "Locked");
  CHECK(locked->volumeLocked);
  auto lockedCommands = lambda_files::sidebarVolumeContextCommands(*locked);
  REQUIRE(lockedCommands.size() == 3);
  CHECK_FALSE(lockedCommands[0].enabled);
  CHECK_FALSE(lockedCommands[1].enabled);
  CHECK(lockedCommands[2].enabled);

  CHECK(findPlace("volume:/org/freedesktop/UDisks2/block_devices/sdf1") == nullptr);
  auto const* cleartext = findPlace("volume:/org/freedesktop/UDisks2/block_devices/dm_2d0");
  REQUIRE(cleartext != nullptr);
  CHECK(cleartext->icon == lambda::IconName::LockOpen);
  CHECK(cleartext->encryptedPath == "/org/freedesktop/UDisks2/block_devices/sdf1");
}

TEST_CASE("FilesStore performs sidebar volume actions through a storage backend") {
  lambda_files::SidebarPlace unmounted{
      .id = "volume:/org/freedesktop/UDisks2/block_devices/sdb1",
      .label = "USB_DISK",
      .kind = lambda_files::SidebarPlaceKind::Volume,
      .volumePath = "/org/freedesktop/UDisks2/block_devices/sdb1",
      .volumeMountable = true,
  };
  lambda_files::SidebarPlace mounted = unmounted;
  mounted.path = "/media/USB_DISK";
  mounted.volumeMounted = true;
  mounted.volumeMountable = false;
  mounted.drivePath = "/org/freedesktop/UDisks2/drives/Lambda_USB";
  mounted.jobPath = "/org/freedesktop/UDisks2/jobs/1";
  mounted.volumeEjectable = true;
  mounted.volumeCancelable = true;

  int mountCalls = 0;
  int unmountCalls = 0;
  bool lastForce = false;
  int ejectCalls = 0;
  int cancelCalls = 0;
  lambda_files::SidebarVolumeActionBackend backend{
      .mountFilesystem = [&](std::string const& path) {
        CHECK(path == unmounted.volumePath);
        ++mountCalls;
        return storageSuccess("/media/USB_DISK");
      },
      .unmountFilesystem = [&](std::string const& path, bool force) {
        CHECK(path == mounted.volumePath);
        ++unmountCalls;
        lastForce = force;
        return storageSuccess();
      },
      .ejectDrive = [&](std::string const& path) {
        CHECK(path == mounted.drivePath);
        ++ejectCalls;
        return storageSuccess();
      },
      .cancelJob = [&](std::string const& path) {
        CHECK(path == mounted.jobPath);
        ++cancelCalls;
        return storageSuccess();
      },
  };

  auto openMounted = lambda_files::performSidebarVolumeAction(
      mounted,
      lambda_files::SidebarVolumeCommandKind::Open,
      backend);
  CHECK(openMounted.ok);
  CHECK(openMounted.navigateToPath);
  CHECK(openMounted.path == "/media/USB_DISK");
  CHECK(mountCalls == 0);

  auto mountResult = lambda_files::performSidebarVolumeAction(
      unmounted,
      lambda_files::SidebarVolumeCommandKind::Open,
      backend);
  CHECK(mountResult.ok);
  CHECK(mountResult.refreshPlaces);
  CHECK(mountResult.navigateToPath);
  CHECK(mountResult.path == "/media/USB_DISK");
  CHECK(mountCalls == 1);

  auto plainMountResult = lambda_files::performSidebarVolumeAction(
      unmounted,
      lambda_files::SidebarVolumeCommandKind::Mount,
      backend);
  CHECK(plainMountResult.ok);
  CHECK(plainMountResult.refreshPlaces);
  CHECK_FALSE(plainMountResult.navigateToPath);
  CHECK(mountCalls == 2);

  auto unmountResult = lambda_files::performSidebarVolumeAction(
      mounted,
      lambda_files::SidebarVolumeCommandKind::Unmount,
      backend);
  CHECK(unmountResult.ok);
  CHECK(unmountResult.refreshPlaces);
  CHECK_FALSE(unmountResult.navigateToPath);
  CHECK(unmountCalls == 1);
  CHECK_FALSE(lastForce);

  auto forceResult = lambda_files::performSidebarVolumeAction(
      mounted,
      lambda_files::SidebarVolumeCommandKind::ForceUnmount,
      backend);
  CHECK(forceResult.ok);
  CHECK(unmountCalls == 2);
  CHECK(lastForce);

  auto ejectResult = lambda_files::performSidebarVolumeAction(
      mounted,
      lambda_files::SidebarVolumeCommandKind::Eject,
      backend);
  CHECK(ejectResult.ok);
  CHECK(ejectCalls == 1);

  auto cancelResult = lambda_files::performSidebarVolumeAction(
      mounted,
      lambda_files::SidebarVolumeCommandKind::Cancel,
      backend);
  CHECK(cancelResult.ok);
  CHECK(cancelCalls == 1);

  backend.unmountFilesystem = [](std::string const&, bool) {
    lambda::system::UDisks2OperationResult result;
    result.userMessage = "The volume is busy. Close files using it, then retry or force unmount.";
    result.retryable = true;
    result.canForce = true;
    return result;
  };
  auto busy = lambda_files::performSidebarVolumeAction(
      mounted,
      lambda_files::SidebarVolumeCommandKind::Unmount,
      backend);
  CHECK_FALSE(busy.ok);
  CHECK(busy.retryable);
  CHECK(busy.canForce);
  CHECK(busy.error.find("busy") != std::string::npos);

  lambda_files::SidebarPlace locked = unmounted;
  locked.volumeMountable = false;
  locked.volumeLocked = true;
  auto lockedOpen = lambda_files::performSidebarVolumeAction(
      locked,
      lambda_files::SidebarVolumeCommandKind::Open,
      backend);
  CHECK_FALSE(lockedOpen.ok);
  CHECK(lockedOpen.error.find("locked") != std::string::npos);
}

TEST_CASE("FilesStore breadcrumbs handle home root and outside home") {
  ScopedEnv homeEnv("HOME");
  auto root = tempRoot("lambda-files-breadcrumb-test");
  auto homeForEnv = root / "home";
  auto nestedForCreate = homeForEnv / "Projects" / "Lambda";
  auto outsideForCreate = root / "outside" / "Folder";
  std::filesystem::create_directories(nestedForCreate);
  std::filesystem::create_directories(outsideForCreate);
  setenv("HOME", homeForEnv.c_str(), 1);
  auto home = canonicalPath(homeForEnv);
  auto nested = canonicalPath(nestedForCreate);
  auto outside = canonicalPath(outsideForCreate);

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

TEST_CASE("FilesStore validated navigation keeps previous directory on invalid paths") {
  auto root = tempRoot("lambda-files-navigation-test");
  auto first = root / "first";
  auto second = root / "second";
  std::filesystem::create_directories(first);
  std::filesystem::create_directories(second);
  {
    std::ofstream(root / "not-a-folder.txt") << "file";
  }

  lambda_files::NavigationHistory history{.current = lambda_files::normalizeDirectoryPath(first)};
  auto missing = lambda_files::navigateToDirectory(history, root / "missing");
  CHECK_FALSE(missing.ok);
  CHECK(missing.error == "Folder does not exist.");
  CHECK(missing.history == history);

  auto file = lambda_files::navigateToDirectory(history, root / "not-a-folder.txt");
  CHECK_FALSE(file.ok);
  CHECK(file.error == "Not a folder.");
  CHECK(file.history == history);

  auto valid = lambda_files::navigateToDirectory(history, second);
  CHECK(valid.ok);
  CHECK(valid.error.empty());
  CHECK(valid.history.current == lambda_files::normalizeDirectoryPath(second));
  CHECK(valid.history.back == std::vector<std::string>{lambda_files::normalizeDirectoryPath(first)});
  CHECK(valid.history.forward.empty());

  std::filesystem::remove_all(root);
}

TEST_CASE("FilesStore normalizes paths and keeps deterministic navigation history") {
  auto root = tempRoot("lambda-files-history-test");
  auto alpha = root / "alpha";
  auto beta = alpha / "beta";
  auto gamma = root / "gamma";
  std::filesystem::create_directories(beta);
  std::filesystem::create_directories(gamma);

  CHECK(lambda_files::normalizeDirectoryPath(beta / "..") == lambda_files::normalizeDirectoryPath(alpha));
  std::error_code ec;
  std::filesystem::create_directory_symlink(alpha, root / "alpha-link", ec);
  if (!ec) {
    CHECK(lambda_files::normalizeDirectoryPath(root / "alpha-link" / "beta" / "..") ==
          lambda_files::normalizeDirectoryPath(alpha));
  }

  lambda_files::NavigationHistory history{.current = lambda_files::normalizeDirectoryPath(alpha)};
  history = lambda_files::navigateTo(history, beta);
  history = lambda_files::navigateTo(history, gamma);
  CHECK(history.current == lambda_files::normalizeDirectoryPath(gamma));
  CHECK(history.back == std::vector<std::string>{lambda_files::normalizeDirectoryPath(alpha),
                                                 lambda_files::normalizeDirectoryPath(beta)});
  CHECK(history.forward.empty());

  history = lambda_files::goBack(history);
  CHECK(history.current == lambda_files::normalizeDirectoryPath(beta));
  CHECK(history.back == std::vector<std::string>{lambda_files::normalizeDirectoryPath(alpha)});
  CHECK(history.forward == std::vector<std::string>{lambda_files::normalizeDirectoryPath(gamma)});

  history = lambda_files::goBack(history);
  CHECK(history.current == lambda_files::normalizeDirectoryPath(alpha));
  CHECK(history.back.empty());
  CHECK(history.forward == std::vector<std::string>{lambda_files::normalizeDirectoryPath(gamma),
                                                    lambda_files::normalizeDirectoryPath(beta)});

  history = lambda_files::goForward(history);
  CHECK(history.current == lambda_files::normalizeDirectoryPath(beta));
  CHECK(history.back == std::vector<std::string>{lambda_files::normalizeDirectoryPath(alpha)});
  CHECK(history.forward == std::vector<std::string>{lambda_files::normalizeDirectoryPath(gamma)});

  auto up = lambda_files::goUp(lambda_files::NavigationHistory{.current = lambda_files::normalizeDirectoryPath(beta)});
  CHECK(up.current == lambda_files::normalizeDirectoryPath(alpha));
  CHECK(up.back == std::vector<std::string>{lambda_files::normalizeDirectoryPath(beta)});

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
  bool const hasModifiedTime =
      visible.entries[2].modifiedAt != std::filesystem::file_time_type{};
  CHECK(hasModifiedTime);

  auto hidden = lambda_files::listDirectory(root, true);
  CHECK(hidden.entries.size() == 4);

  std::filesystem::remove_all(root);
}

TEST_CASE("FilesStore filters entries and computes directory refresh diffs") {
  using lambda_files::FileEntry;
  auto now = std::filesystem::file_time_type::clock::now();
  std::vector<FileEntry> before{
      {.name = "alpha.txt", .path = "/tmp/alpha.txt", .size = 1, .modifiedAt = now},
      {.name = "Beta", .path = "/tmp/Beta", .isDirectory = true, .modifiedAt = now},
      {.name = "notes.md", .path = "/tmp/notes.md", .size = 5, .modifiedAt = now},
  };
  std::vector<FileEntry> after{
      {.name = "alpha.txt", .path = "/tmp/alpha.txt", .size = 2, .modifiedAt = now + std::chrono::seconds(1)},
      {.name = "Beta", .path = "/tmp/Beta", .isDirectory = true, .modifiedAt = now},
      {.name = "new.txt", .path = "/tmp/new.txt", .size = 1, .modifiedAt = now},
  };

  auto filtered = lambda_files::filterEntries(before, "TA");
  REQUIRE(filtered.size() == 1);
  CHECK(filtered[0].name == "Beta");

  auto changes = lambda_files::diffDirectoryEntries(before, after);
  CHECK(changes.added == std::vector<std::filesystem::path>{"/tmp/new.txt"});
  CHECK(changes.removed == std::vector<std::filesystem::path>{"/tmp/notes.md"});
  CHECK(changes.modified == std::vector<std::filesystem::path>{"/tmp/alpha.txt"});

  CHECK(lambda_files::directoryListingChanged(before, "", {.entries = after}));
  CHECK_FALSE(lambda_files::directoryListingChanged(before, "", {.entries = before}));
  CHECK(lambda_files::directoryListingChanged(before, "", {.error = "permission denied"}));
  CHECK_FALSE(lambda_files::directoryListingChanged(before, "permission denied",
                                                   {.error = "permission denied"}));
}

TEST_CASE("FilesStore model owns directory state filtering errors and refresh diffs") {
  using lambda_files::FileEntry;
  using lambda_files::FileSortKey;
  auto now = std::filesystem::file_time_type::clock::now();
  auto root = tempRoot("lambda-files-model-test");
  auto first = root / "first";
  auto second = root / "second";
  std::filesystem::create_directories(first);
  std::filesystem::create_directories(second);

  lambda_files::FilesPreferences preferences;
  preferences.sortKey = FileSortKey::Size;
  preferences.sortAscending = false;
  auto model = lambda_files::makeFilesModel(first, preferences);
  CHECK(model.history.current == lambda_files::normalizeDirectoryPath(first));

  model = lambda_files::applyDirectoryListing(model, first, {
      .entries = {
          FileEntry{.name = "small.txt", .path = first / "small.txt", .size = 1, .modifiedAt = now},
          FileEntry{.name = "large.txt", .path = first / "large.txt", .size = 20, .modifiedAt = now},
      },
  });
  REQUIRE(model.visibleEntries.size() == 2);
  CHECK(model.visibleEntries[0].name == "large.txt");
  CHECK(model.operation.status == lambda_files::FileOperationStatus::Succeeded);

  model = lambda_files::setFilesModelQuery(model, "small");
  REQUIRE(model.visibleEntries.size() == 1);
  CHECK(model.visibleEntries[0].name == "small.txt");

  model = lambda_files::applyDirectoryListing(model, second, {
      .entries = {
          FileEntry{.name = "small.txt", .path = second / "small.txt", .size = 2, .modifiedAt = now},
          FileEntry{.name = "new.txt", .path = second / "new.txt", .size = 5, .modifiedAt = now},
      },
  });
  CHECK(model.history.current == lambda_files::normalizeDirectoryPath(second));
  CHECK(model.history.back == std::vector<std::string>{lambda_files::normalizeDirectoryPath(first)});
  CHECK(model.lastChanges.added == std::vector<std::filesystem::path>{second / "new.txt", second / "small.txt"});
  CHECK(model.lastChanges.removed == std::vector<std::filesystem::path>{first / "large.txt", first / "small.txt"});
  CHECK(model.visibleEntries.size() == 1);

  model = lambda_files::applyDirectoryListing(model, second, {.error = "permission denied"});
  CHECK(model.error == "permission denied");
  CHECK(model.operation.status == lambda_files::FileOperationStatus::Failed);
  REQUIRE(model.operation.errors.size() == 1);
  CHECK(model.operation.errors[0].path == second);

  std::filesystem::remove_all(root);
}

TEST_CASE("FilesStore refresh preserves selected paths that still exist") {
  using lambda_files::FileEntry;
  auto now = std::filesystem::file_time_type::clock::now();
  auto root = tempRoot("lambda-files-selection-refresh-test");
  std::filesystem::create_directories(root);

  auto model = lambda_files::makeFilesModel(root);
  model = lambda_files::applyDirectoryListing(model, root, {
      .entries = {
          FileEntry{.name = "alpha.txt", .path = root / "alpha.txt", .size = 1, .modifiedAt = now},
          FileEntry{.name = "bravo.txt", .path = root / "bravo.txt", .size = 2, .modifiedAt = now},
          FileEntry{.name = "charlie.txt", .path = root / "charlie.txt", .size = 3, .modifiedAt = now},
      },
  });
  model.selection = lambda_files::rangeSelection(lambda_files::selectOnly(model.entries, 1), model.entries, 2);
  CHECK(model.selection.selected ==
        std::vector<std::filesystem::path>{root / "bravo.txt", root / "charlie.txt"});
  CHECK(model.selection.anchorIndex == 1);

  model = lambda_files::applyDirectoryListing(model, root, {
      .entries = {
          FileEntry{.name = "bravo.txt", .path = root / "bravo.txt", .size = 4,
                    .modifiedAt = now + std::chrono::seconds(1)},
          FileEntry{.name = "delta.txt", .path = root / "delta.txt", .size = 5, .modifiedAt = now},
      },
  });
  CHECK(model.selection.selected == std::vector<std::filesystem::path>{root / "bravo.txt"});
  CHECK(model.selection.anchorIndex == 0);

  auto other = root / "other";
  model = lambda_files::applyDirectoryListing(model, other, {
      .entries = {
          FileEntry{.name = "bravo.txt", .path = other / "bravo.txt", .size = 4, .modifiedAt = now},
      },
  });
  CHECK(model.selection.selected.empty());
  CHECK(model.selection.anchorIndex == -1);

  std::filesystem::remove_all(root);
}

TEST_CASE("FilesStore operation progress supports completion failure and cancellation") {
  auto progress = lambda_files::beginFileOperation(lambda_files::FileOperationKind::Copy, 3, true, 42);
  CHECK(progress.active());
  CHECK(progress.id == 42);
  CHECK(progress.fractionComplete() == doctest::Approx(0.0));

  progress = lambda_files::advanceFileOperation(progress, "/tmp/a", 1);
  CHECK(progress.completedItems == 1);
  CHECK(progress.currentPath == "/tmp/a");
  CHECK(progress.fractionComplete() == doctest::Approx(1.0 / 3.0));

  auto failed = lambda_files::failFileOperation(progress, "/tmp/b", "copy failed");
  CHECK(failed.status == lambda_files::FileOperationStatus::Failed);
  REQUIRE(failed.errors.size() == 1);
  CHECK(failed.errors[0] == lambda_files::FileOperationError{.path = "/tmp/b", .message = "copy failed"});
  CHECK(lambda_files::completeFileOperation(failed).status == lambda_files::FileOperationStatus::Failed);

  auto cancelled = lambda_files::requestCancelFileOperation(progress);
  CHECK(cancelled.status == lambda_files::FileOperationStatus::Cancelled);
  CHECK(cancelled.cancelRequested);
  CHECK(lambda_files::completeFileOperation(cancelled).status == lambda_files::FileOperationStatus::Cancelled);

  auto done = lambda_files::advanceFileOperation(progress, "/tmp/c", 2);
  CHECK(done.status == lambda_files::FileOperationStatus::Succeeded);
  CHECK(done.fractionComplete() == doctest::Approx(1.0));
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

TEST_CASE("FilesStore keyboard selection moves extends and selects all") {
  std::vector<lambda_files::FileEntry> entries{
      {.name = "a", .path = "/tmp/a"},
      {.name = "b", .path = "/tmp/b"},
      {.name = "c", .path = "/tmp/c"},
      {.name = "d", .path = "/tmp/d"},
      {.name = "e", .path = "/tmp/e"},
  };

  auto state = lambda_files::moveSelectionByOffset({}, entries, 1, false);
  CHECK(state.selected == std::vector<std::filesystem::path>{"/tmp/a"});
  CHECK(lambda_files::focusedSelectionIndex(state, entries) == 0);

  state = lambda_files::moveSelectionByOffset(state, entries, 2, false);
  CHECK(state.selected == std::vector<std::filesystem::path>{"/tmp/c"});
  CHECK(state.anchorIndex == 2);

  state = lambda_files::moveSelectionByOffset(state, entries, 2, true);
  CHECK(state.selected == std::vector<std::filesystem::path>{"/tmp/c", "/tmp/d", "/tmp/e"});
  CHECK(state.anchorIndex == 2);

  state = lambda_files::moveSelectionToIndex(state, entries, 1, false);
  CHECK(state.selected == std::vector<std::filesystem::path>{"/tmp/b"});
  CHECK(state.anchorIndex == 1);

  state = lambda_files::selectAllEntries(entries);
  CHECK(state.selected == std::vector<std::filesystem::path>{"/tmp/a", "/tmp/b", "/tmp/c", "/tmp/d", "/tmp/e"});
  CHECK(state.anchorIndex == 0);

  state = lambda_files::moveSelectionToIndex(state, entries, 99, false);
  CHECK(state.selected == std::vector<std::filesystem::path>{"/tmp/e"});
}

TEST_CASE("FilesStore pointer selection supports activate toggle and range") {
  std::vector<lambda_files::FileEntry> entries{
      {.name = "a", .path = "/tmp/a"},
      {.name = "b", .path = "/tmp/b"},
      {.name = "c", .path = "/tmp/c"},
      {.name = "d", .path = "/tmp/d"},
  };

  auto result = lambda_files::selectionForPointerTap({}, entries, 1, lambda::Modifiers::None);
  CHECK(result.activate);
  CHECK(result.selection.selected == std::vector<std::filesystem::path>{"/tmp/b"});
  CHECK(result.selection.anchorIndex == 1);

  result = lambda_files::selectionForPointerTap(result.selection, entries, 3, lambda::Modifiers::Ctrl);
  CHECK_FALSE(result.activate);
  CHECK(result.selection.selected == std::vector<std::filesystem::path>{"/tmp/b", "/tmp/d"});
  CHECK(result.selection.anchorIndex == 3);

  result = lambda_files::selectionForPointerTap(result.selection, entries, 0, lambda::Modifiers::Shift);
  CHECK_FALSE(result.activate);
  CHECK(result.selection.selected == std::vector<std::filesystem::path>{"/tmp/a", "/tmp/b", "/tmp/c", "/tmp/d"});
  CHECK(result.selection.anchorIndex == 3);

  auto invalid = lambda_files::selectionForPointerTap(result.selection, entries, 42, lambda::Modifiers::None);
  CHECK_FALSE(invalid.activate);
  CHECK(invalid.selection == result.selection);
}

TEST_CASE("FilesStore context menu commands reflect selection and clipboard state") {
  std::vector<lambda_files::FileEntry> entries{
      {.name = "a", .path = "/tmp/a"},
      {.name = "b", .path = "/tmp/b"},
  };
  lambda_files::FileClipboardState emptyClipboard;

  auto background = lambda_files::contextMenuCommands(entries, {}, emptyClipboard, true);
  REQUIRE(background.size() == 4);
  CHECK(background[0] == lambda_files::FileContextCommand{
                             .kind = lambda_files::FileContextCommandKind::NewFolder,
                             .label = "New Folder",
                             .enabled = true,
                         });
  CHECK(background[2].kind == lambda_files::FileContextCommandKind::Paste);
  CHECK_FALSE(background[2].enabled);
  CHECK(background[3].kind == lambda_files::FileContextCommandKind::SelectAll);
  CHECK(background[3].enabled);

  auto clipboard = lambda_files::makeFileClipboard({"/tmp/a"}, lambda_files::FileClipboardIntent::Copy);
  auto pasteEnabled = lambda_files::contextMenuCommands(entries, {}, clipboard, true);
  CHECK(pasteEnabled[2].enabled);

  auto single = lambda_files::contextMenuCommands(entries, lambda_files::selectOnly(entries, 0), emptyClipboard, false);
  REQUIRE(single.size() == 6);
  CHECK(single[0].kind == lambda_files::FileContextCommandKind::Open);
  CHECK(single[0].enabled);
  CHECK(single[1].kind == lambda_files::FileContextCommandKind::Reveal);
  CHECK(single[1].enabled);
  CHECK(single[5].kind == lambda_files::FileContextCommandKind::Trash);
  CHECK(single[5].destructive);

  auto multiSelection = lambda_files::rangeSelection(lambda_files::selectOnly(entries, 0), entries, 1);
  auto selected = lambda_files::selectedEntries(entries, multiSelection);
  CHECK(selected.size() == 2);
  auto multi = lambda_files::contextMenuCommands(entries, multiSelection, emptyClipboard, false);
  CHECK_FALSE(multi[0].enabled);
  CHECK(multi[2].enabled);
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
  CHECK_FALSE(hasStagingCopyPath(root));

  auto copiedAgain = lambda_files::copyPath(sourceDir, destination);
  CHECK(copiedAgain.ok);
  CHECK(copiedAgain.path.filename() == "source 2");
  CHECK_FALSE(hasStagingCopyPath(root));

  auto duplicate = lambda_files::duplicatePath(root / "single.txt");
  CHECK(duplicate.ok);
  CHECK(duplicate.path.filename() == "single copy.txt");
  CHECK(std::filesystem::exists(duplicate.path));
  CHECK_FALSE(hasStagingCopyPath(root));

  auto moved = lambda_files::movePath(root / "single.txt", destination);
  CHECK(moved.ok);
  CHECK(std::filesystem::exists(destination / "single.txt"));
  CHECK_FALSE(std::filesystem::exists(root / "single.txt"));
  CHECK_FALSE(hasStagingCopyPath(root));

  std::filesystem::remove_all(root);
}

TEST_CASE("FilesStore copies symlinks without recursively following their targets") {
  auto root = tempRoot("lambda-files-symlink-copy-test");
  auto source = root / "source";
  auto destination = root / "destination";
  std::filesystem::create_directories(source / "real-dir");
  std::filesystem::create_directories(destination);
  {
    std::ofstream(source / "real-dir" / "nested.txt") << "nested";
  }

  std::error_code ec;
  std::filesystem::create_directory_symlink(source / "real-dir", source / "linked-dir", ec);
  REQUIRE_FALSE(ec);

  auto copied = lambda_files::copyPath(source / "linked-dir", destination);
  REQUIRE(copied.ok);
  CHECK(std::filesystem::is_symlink(copied.path));
  CHECK(std::filesystem::read_symlink(copied.path) == source / "real-dir");

  std::filesystem::remove_all(root);
}

TEST_CASE("FilesStore internal clipboard copies and cuts selected paths") {
  auto root = tempRoot("lambda-files-clipboard-operation-test");
  auto source = root / "source";
  auto destination = root / "destination";
  std::filesystem::create_directories(source);
  std::filesystem::create_directories(destination);
  {
    std::ofstream(source / "copy.txt") << "copy";
    std::ofstream(source / "cut.txt") << "cut";
  }

  auto copyClipboard = lambda_files::makeFileClipboard({source / "copy.txt"}, lambda_files::FileClipboardIntent::Copy);
  auto copied = lambda_files::pasteFileClipboard(copyClipboard, destination);
  REQUIRE(copied.size() == 1);
  CHECK(copied[0].ok);
  CHECK(std::filesystem::exists(source / "copy.txt"));
  CHECK(std::filesystem::exists(destination / "copy.txt"));

  auto cutClipboard = lambda_files::makeFileClipboard({source / "cut.txt"}, lambda_files::FileClipboardIntent::Cut);
  auto moved = lambda_files::pasteFileClipboard(cutClipboard, destination);
  REQUIRE(moved.size() == 1);
  CHECK(moved[0].ok);
  CHECK_FALSE(std::filesystem::exists(source / "cut.txt"));
  CHECK(std::filesystem::exists(destination / "cut.txt"));

  std::filesystem::remove_all(root);
}

TEST_CASE("FilesStore serializes and parses URI lists") {
  std::vector<std::filesystem::path> paths{"/tmp/alpha.txt", "/tmp/space name.txt"};
  std::string const uriList = lambda_files::serializeUriList(paths);
  CHECK(uriList.find("file:///tmp/space%20name.txt") != std::string::npos);

  auto parsed = lambda_files::parseUriList("# comment\r\nfile:///tmp/alpha.txt\r\nfile:///tmp/space%20name.txt\r\n");
  CHECK(parsed == paths);

  auto clipboard = lambda_files::makeFileClipboard(paths, lambda_files::FileClipboardIntent::Copy);
  CHECK(lambda_files::serializeFileClipboardText(clipboard) == uriList);
  auto imported = lambda_files::fileClipboardFromUriListText(uriList);
  CHECK(imported.intent == lambda_files::FileClipboardIntent::Copy);
  CHECK(imported.paths == paths);
}

TEST_CASE("FilesStore trashes and restores files with metadata") {
  ScopedEnv dataHome("XDG_DATA_HOME");
  auto root = tempRoot("lambda-files-trash-test");
  auto data = root / "data";
  auto source = root / "Documents" / "notes.txt";
  std::filesystem::create_directories(source.parent_path());
  setenv("XDG_DATA_HOME", data.c_str(), 1);
  {
    std::ofstream(source) << "notes";
  }

  auto trashed = lambda_files::trashPath(source);
  CHECK(trashed.ok);
  CHECK_FALSE(std::filesystem::exists(source));
  CHECK(std::filesystem::exists(trashed.path));
  CHECK(trashed.path.parent_path() == lambda_files::trashFilesDirectory());

  auto info = lambda_files::parseTrashInfo(lambda_files::trashInfoDirectory() / "notes.txt.trashinfo");
  REQUIRE(info);
  CHECK(info->originalPath == std::filesystem::absolute(source).lexically_normal());
  CHECK_FALSE(info->deletionDate.empty());

  auto restored = lambda_files::restoreTrashedPath(trashed.path);
  CHECK(restored.ok);
  CHECK(restored.path == std::filesystem::absolute(source).lexically_normal());
  CHECK(std::filesystem::exists(restored.path));
  CHECK_FALSE(std::filesystem::exists(lambda_files::trashInfoDirectory() / "notes.txt.trashinfo"));

  std::filesystem::remove_all(root);
}

TEST_CASE("FilesStore trash and restore avoid collisions") {
  ScopedEnv dataHome("XDG_DATA_HOME");
  auto root = tempRoot("lambda-files-trash-collision-test");
  auto data = root / "data";
  setenv("XDG_DATA_HOME", data.c_str(), 1);
  auto first = root / "one.txt";
  auto second = root / "two.txt";
  {
    std::ofstream(first) << "one";
    std::ofstream(second) << "two";
  }

  auto trashedFirst = lambda_files::trashPath(first);
  REQUIRE(trashedFirst.ok);
  auto trashedSecond = lambda_files::trashPath(second);
  REQUIRE(trashedSecond.ok);
  CHECK(trashedSecond.path.filename() == "two.txt");

  {
    std::ofstream(first) << "replacement";
  }
  auto restoredFirst = lambda_files::restoreTrashedPath(trashedFirst.path);
  CHECK(restoredFirst.ok);
  CHECK(restoredFirst.path.filename() == "one 2.txt");
  CHECK(std::filesystem::exists(root / "one.txt"));
  CHECK(std::filesystem::exists(root / "one 2.txt"));

  std::filesystem::remove_all(root);
}

TEST_CASE("FilesStore resolves conflicts and undoes safe operations") {
  auto root = tempRoot("lambda-files-undo-test");
  auto source = root / "source";
  auto destination = root / "destination";
  std::filesystem::create_directories(source);
  std::filesystem::create_directories(destination);
  {
    std::ofstream(root / "taken.txt") << "taken";
    std::ofstream(source / "move.txt") << "move";
  }

  CHECK(lambda_files::resolveConflictPath(root / "free.txt", lambda_files::FileConflictDecision::KeepBoth) ==
        root / "free.txt");
  CHECK(lambda_files::resolveConflictPath(root / "taken.txt", lambda_files::FileConflictDecision::KeepBoth) ==
        root / "taken 2.txt");
  CHECK(lambda_files::resolveConflictPath(root / "taken.txt", lambda_files::FileConflictDecision::Replace) ==
        root / "taken.txt");
  CHECK(lambda_files::resolveConflictPath(root / "taken.txt", lambda_files::FileConflictDecision::Skip).empty());
  CHECK(lambda_files::resolveConflictPath(root / "taken.txt", lambda_files::FileConflictDecision::Cancel).empty());

  auto created = lambda_files::createFile(root, "created.txt");
  REQUIRE(created.ok);
  auto undoCreate = lambda_files::undoFileOperation({
      .kind = lambda_files::FileUndoKind::Create,
      .afterPath = created.path,
  });
  CHECK(undoCreate.ok);
  CHECK_FALSE(std::filesystem::exists(created.path));

  auto renamed = lambda_files::renamePath(root / "taken.txt", "renamed.txt");
  REQUIRE(renamed.ok);
  auto undoRename = lambda_files::undoFileOperation({
      .kind = lambda_files::FileUndoKind::Rename,
      .beforePath = root / "taken.txt",
      .afterPath = renamed.path,
  });
  CHECK(undoRename.ok);
  CHECK(std::filesystem::exists(root / "taken.txt"));
  CHECK_FALSE(std::filesystem::exists(root / "renamed.txt"));

  auto moved = lambda_files::movePath(source / "move.txt", destination);
  REQUIRE(moved.ok);
  auto undoMove = lambda_files::undoFileOperation({
      .kind = lambda_files::FileUndoKind::Move,
      .beforePath = source / "move.txt",
      .afterPath = moved.path,
  });
  CHECK(undoMove.ok);
  CHECK(std::filesystem::exists(source / "move.txt"));
  CHECK_FALSE(std::filesystem::exists(destination / "move.txt"));

  auto copied = lambda_files::copyPath(source / "move.txt", destination);
  REQUIRE(copied.ok);
  auto undoCopy = lambda_files::undoFileOperation({
      .kind = lambda_files::FileUndoKind::Copy,
      .afterPath = copied.path,
      .removeCopiedItem = true,
  });
  CHECK(undoCopy.ok);
  CHECK_FALSE(std::filesystem::exists(copied.path));

  std::filesystem::remove_all(root);
}

TEST_CASE("FilesStore undo restores trashed items through metadata") {
  ScopedEnv dataHome("XDG_DATA_HOME");
  auto root = tempRoot("lambda-files-undo-trash-test");
  auto data = root / "data";
  auto source = root / "undo-trash.txt";
  setenv("XDG_DATA_HOME", data.c_str(), 1);
  {
    std::ofstream(source) << "trash";
  }

  auto trashed = lambda_files::trashPath(source);
  REQUIRE(trashed.ok);
  auto restored = lambda_files::undoFileOperation({
      .kind = lambda_files::FileUndoKind::Trash,
      .beforePath = source,
      .afterPath = trashed.path,
  });
  CHECK(restored.ok);
  CHECK(std::filesystem::exists(source));
  CHECK_FALSE(std::filesystem::exists(trashed.path));

  std::filesystem::remove_all(root);
}

TEST_CASE("FilesStore resolves open-with choices from MIME app fixtures") {
  std::vector<lambda_shell::AppRegistryEntry> apps{
      {.appId = "org.example.TextEditor",
       .name = "Text Editor",
       .icon = "text-editor",
       .command = "text-editor --open %f",
       .mimeTypes = {"text/plain"}},
      {.appId = "org.example.ImageViewer",
       .name = "Image Viewer",
       .icon = "image-viewer",
       .command = "image-viewer %f",
       .mimeTypes = {"image/png", "image/jpeg"}},
      {.appId = "lambda-files",
       .name = "Files",
       .icon = "lambda-files",
       .command = "lambda-files %f",
       .mimeTypes = {"inode/directory"}},
      {.appId = "org.example.Hidden",
       .name = "Hidden",
       .command = "hidden %f",
       .hidden = true,
       .mimeTypes = {"text/plain"}},
  };
  auto mimeApps = lambda_files::parseMimeAppsList(R"(
[Default Applications]
text/plain=org.example.TextEditor.desktop;
inode/directory=lambda-files.desktop;

[Added Associations]
text/plain=org.example.Hidden.desktop;org.example.ImageViewer.desktop;
image/png=org.example.ImageViewer.desktop;
)");

  auto textChoices = lambda_files::openWithChoices("/tmp/readme.txt", false, apps, mimeApps);
  REQUIRE(textChoices.size() == 1);
  CHECK(textChoices[0].app.appId == "org.example.TextEditor");
  CHECK(textChoices[0].isDefault);
  CHECK(lambda_files::openCommandForChoice(textChoices[0], "/tmp/readme.txt") ==
        std::vector<std::string>{"text-editor", "--open", "/tmp/readme.txt"});

  lambda_files::FileEntry textEntry{
      .name = "readme.txt",
      .path = "/tmp/readme.txt",
      .isDirectory = false,
  };
  auto textPlan = lambda_files::openEntryPlan(textEntry, apps, mimeApps);
  REQUIRE(textPlan.ok);
  CHECK(textPlan.command == std::vector<std::string>{"text-editor", "--open", "/tmp/readme.txt"});

  auto imageDefault = lambda_files::defaultOpenWithChoice("/tmp/photo.png", false, apps, mimeApps);
  REQUIRE(imageDefault);
  CHECK(imageDefault->app.appId == "org.example.ImageViewer");
  CHECK_FALSE(imageDefault->isDefault);

  auto directoryDefault = lambda_files::defaultOpenWithChoice("/tmp", true, apps, mimeApps);
  REQUIRE(directoryDefault);
  CHECK(directoryDefault->app.appId == "lambda-files");
  CHECK(directoryDefault->isDefault);

  auto missingPlan = lambda_files::openEntryPlan({.name = "archive.bin", .path = "/tmp/archive.bin"}, apps, mimeApps);
  CHECK_FALSE(missingPlan.ok);
  CHECK(missingPlan.error == "No application is registered for this file type.");
}

TEST_CASE("FilesStore prefers local Preview for supported image files") {
  std::vector<lambda_shell::AppRegistryEntry> apps{
      {.appId = "lambda-preview",
       .name = "Preview",
       .icon = "image-viewer",
       .command = "/tmp/apps/lambda-preview",
       .local = true,
       .mimeTypes = {"image/png", "image/jpeg", "image/gif", "image/webp", "image/svg+xml"}},
      {.appId = "firefox",
       .name = "Firefox",
       .icon = "firefox",
       .command = "firefox %u",
       .mimeTypes = {"image/png", "image/jpeg", "image/gif", "image/webp", "image/svg+xml"}},
      {.appId = "org.example.TextEditor",
       .name = "Text Editor",
       .icon = "text-editor",
       .command = "text-editor %f",
       .mimeTypes = {"text/plain"}},
  };
  auto mimeApps = lambda_files::parseMimeAppsList(R"(
[Default Applications]
image/png=firefox.desktop;
image/jpeg=firefox.desktop;
image/gif=firefox.desktop;
image/webp=firefox.desktop;
image/svg+xml=firefox.desktop;
text/plain=org.example.TextEditor.desktop;
)");

  for (auto const& path : {"/tmp/photo.png",
                           "/tmp/photo.jpg",
                           "/tmp/animation.gif",
                           "/tmp/photo.webp",
                           "/tmp/vector.svg"}) {
    auto choice = lambda_files::defaultOpenWithChoice(path, false, apps, mimeApps);
    REQUIRE(choice);
    CHECK(choice->app.appId == "lambda-preview");
    CHECK(choice->isDefault);
    CHECK(lambda_files::openCommandForChoice(*choice, path) ==
          std::vector<std::string>{"/tmp/apps/lambda-preview", path});
  }

  auto choices = lambda_files::openWithChoices("/tmp/photo.png", false, apps, mimeApps);
  REQUIRE(choices.size() >= 2);
  CHECK(choices[0].app.appId == "lambda-preview");
  CHECK(choices[0].isDefault);
  CHECK(choices[1].app.appId == "firefox");
  CHECK_FALSE(choices[1].isDefault);

  lambda_files::FileEntry imageEntry{
      .name = "photo.png",
      .path = "/tmp/photo.png",
      .isDirectory = false,
  };
  auto imagePlan = lambda_files::openEntryPlan(imageEntry, apps, mimeApps);
  REQUIRE(imagePlan.ok);
  CHECK(imagePlan.command == std::vector<std::string>{"/tmp/apps/lambda-preview", "/tmp/photo.png"});

  auto textChoice = lambda_files::defaultOpenWithChoice("/tmp/readme.txt", false, apps, mimeApps);
  REQUIRE(textChoice);
  CHECK(textChoice->app.appId == "org.example.TextEditor");
  CHECK(textChoice->isDefault);
}

TEST_CASE("FilesStore launches file open commands without blocking the caller") {
  std::vector<lambda_shell::AppRegistryEntry> apps{
      {.appId = "org.example.SlowViewer",
       .name = "Slow Viewer",
       .command = "/bin/sh -c \"sleep 2\"",
       .mimeTypes = {"text/plain"}},
  };
  lambda_files::FileEntry entry{
      .name = "readme.txt",
      .path = "/tmp/readme.txt",
      .isDirectory = false,
  };

  std::string error;
  auto const start = std::chrono::steady_clock::now();
  bool const opened = lambda_files::openEntryWithApps(entry, apps, {}, error);
  auto const elapsed = std::chrono::steady_clock::now() - start;

  CHECK(opened);
  CHECK(error.empty());
  CHECK(elapsed < std::chrono::milliseconds(1000));
}

TEST_CASE("FilesStore loads mimeapps lists with deterministic precedence") {
  auto root = tempRoot("lambda-files-mimeapps-load-test");
  auto user = root / "user-mimeapps.list";
  auto system = root / "system-mimeapps.list";
  {
    std::ofstream(user) << R"(
[Default Applications]
text/plain=org.example.User.desktop;
[Added Associations]
text/plain=org.example.User.desktop;
)";
    std::ofstream(system) << R"(
[Default Applications]
text/plain=org.example.System.desktop;
image/png=org.example.Image.desktop;
[Added Associations]
text/plain=org.example.System.desktop;
)";
  }

  auto mimeApps = lambda_files::loadMimeAppsList({user, system});
  CHECK(mimeApps.defaults["text/plain"] == std::vector<std::string>{"org.example.User", "org.example.System"});
  CHECK(mimeApps.defaults["image/png"] == std::vector<std::string>{"org.example.Image"});
  CHECK(mimeApps.associations["text/plain"] ==
        std::vector<std::string>{"org.example.User", "org.example.System"});

  std::filesystem::remove_all(root);
}

TEST_CASE("FilesStore looks up file icons through icon theme fallback data") {
  auto root = tempRoot("lambda-files-icon-test");
  std::filesystem::create_directories(root / "48x48" / "mimetypes");
  std::filesystem::create_directories(root / "48x48" / "places");
  {
    std::ofstream(root / "48x48" / "mimetypes" / "text-x-generic.svg") << "text";
    std::ofstream(root / "48x48" / "places" / "folder.png") << "folder";
  }

  auto text = lambda_files::lookupFileIcon(root, "/tmp/readme.txt", false, 48);
  CHECK(text.iconName == "text-x-generic");
  CHECK(text.themePath == root / "48x48" / "mimetypes" / "text-x-generic.svg");
  CHECK_FALSE(text.fallback);

  auto folder = lambda_files::lookupFileIcon(root, "/tmp", true, 48);
  CHECK(folder.iconName == "folder");
  CHECK(folder.themePath == root / "48x48" / "places" / "folder.png");
  CHECK_FALSE(folder.fallback);

  auto unknown = lambda_files::lookupFileIcon(root, "/tmp/archive.unknown", false, 48);
  CHECK(unknown.iconName == "application-octet-stream");
  CHECK(unknown.themePath.empty());
  CHECK(unknown.fallback);

  auto secondary = root / "secondary";
  std::filesystem::create_directories(secondary / "48x48" / "mimetypes");
  {
    std::ofstream(secondary / "48x48" / "mimetypes" / "image-x-generic.svg") << "image";
  }
  auto resolved = lambda_files::resolveFileIcon({root / "missing", secondary}, "/tmp/photo.png", false, 48);
  CHECK(resolved.iconName == "image-x-generic");
  CHECK(resolved.themePath == secondary / "48x48" / "mimetypes" / "image-x-generic.svg");
  CHECK_FALSE(resolved.fallback);

  std::filesystem::remove_all(root);
}

TEST_CASE("FilesStore preferences parse serialize and preserve defaults for invalid values") {
  auto preferences = lambda_files::parseFilesPreferencesToml(R"(
show_hidden = true
view_mode = "list"
sort_key = "modified_time"
sort_ascending = false
icon_size = 128
show_trash = false
)");
  CHECK(preferences.showHidden);
  CHECK(preferences.viewMode == "list");
  CHECK(preferences.sortKey == lambda_files::FileSortKey::ModifiedTime);
  CHECK_FALSE(preferences.sortAscending);
  CHECK(preferences.iconSize == 128);
  CHECK_FALSE(preferences.showTrash);

  CHECK(lambda_files::parseFilesPreferencesToml(lambda_files::writeFilesPreferencesToml(preferences)) == preferences);

  auto fallback = lambda_files::parseFilesPreferencesToml(R"(
show_hidden = maybe
view_mode = "columns"
sort_key = "random"
icon_size = 4
)");
  CHECK(fallback == lambda_files::defaultFilesPreferences());

  ScopedEnv configEnv("LAMBDA_FILES_CONFIG");
  auto root = tempRoot("lambda-files-preferences-persist-test");
  auto configPath = root / "lambda-files" / "config.toml";
  auto const configPathString = configPath.string();
  setenv("LAMBDA_FILES_CONFIG", configPathString.c_str(), 1);

  auto created = lambda_files::loadFilesPreferences();
  CHECK(created.created);
  CHECK(created.path == configPath);
  CHECK(created.preferences == lambda_files::defaultFilesPreferences());
  CHECK(std::filesystem::exists(configPath));

  preferences.showHidden = true;
  preferences.viewMode = "list";
  preferences.sortKey = lambda_files::FileSortKey::Size;
  preferences.sortAscending = false;
  auto saved = lambda_files::saveFilesPreferences(preferences);
  REQUIRE(saved.ok);
  auto loaded = lambda_files::loadFilesPreferences();
  CHECK_FALSE(loaded.created);
  CHECK(loaded.preferences == preferences);

  std::filesystem::remove_all(root);
}
