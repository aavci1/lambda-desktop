#include "Shell/ShellAppRegistry.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace {

std::filesystem::path tempRoot(char const* name) {
  auto path = std::filesystem::temp_directory_path() /
              (std::string(name) + "-" + std::to_string(static_cast<unsigned long long>(getpid())));
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path);
  return path;
}

} // namespace

TEST_CASE("Shell app registry parses desktop entry fields") {
  auto entry = lambda_shell::parseDesktopEntry(R"(
[Desktop Entry]
Name=Flux Files
GenericName=File Manager
Comment=Browse\sthe\nfilesystem
Icon=lambda-files
Exec=lambda-files --open %u
TryExec=lambda-files
NoDisplay=false
Hidden=false
Categories=Utility;FileManager;
Keywords=folder;files;
MimeType=inode/directory;text/plain;
StartupWMClass=lambda-files
)",
                                               "lambda-files.desktop");
  REQUIRE(entry);
  CHECK(entry->id == "lambda-files.desktop");
  CHECK(entry->name == "Flux Files");
  CHECK(entry->genericName == "File Manager");
  CHECK(entry->comment == "Browse the\nfilesystem");
  CHECK(entry->icon == "lambda-files");
  CHECK(entry->exec == "lambda-files --open %u");
  CHECK(entry->tryExec == "lambda-files");
  CHECK(entry->categories == std::vector<std::string>{"Utility", "FileManager"});
  CHECK(entry->keywords == std::vector<std::string>{"folder", "files"});
  CHECK(entry->mimeTypes == std::vector<std::string>{"inode/directory", "text/plain"});
  CHECK(entry->startupWmClass == "lambda-files");
  CHECK(lambda_shell::desktopEntryVisible(*entry, [](std::string const& exe) { return exe == "lambda-files"; }));

  auto app = lambda_shell::appEntryFromDesktopEntry(*entry);
  CHECK(app.appId == "lambda-files");
  CHECK(app.name == "Flux Files");
  CHECK(app.command == "lambda-files --open %u");
}

TEST_CASE("Shell app registry filters hidden no-display and failed TryExec entries") {
  auto hidden = lambda_shell::parseDesktopEntry("[Desktop Entry]\nName=Hidden\nExec=hidden\nHidden=true\n");
  REQUIRE(hidden);
  CHECK_FALSE(lambda_shell::desktopEntryVisible(*hidden));

  auto noDisplay = lambda_shell::parseDesktopEntry("[Desktop Entry]\nName=NoDisplay\nExec=nope\nNoDisplay=true\n");
  REQUIRE(noDisplay);
  CHECK_FALSE(lambda_shell::desktopEntryVisible(*noDisplay));

  auto tryExec = lambda_shell::parseDesktopEntry("[Desktop Entry]\nName=Try\nExec=try\nTryExec=missing\n");
  REQUIRE(tryExec);
  CHECK_FALSE(lambda_shell::desktopEntryVisible(*tryExec, [](std::string const&) { return false; }));
}

TEST_CASE("Shell app registry parses Exec field codes and quoting") {
  auto args = lambda_shell::parseDesktopExec(R"(app --name "two words" --file %f --literal %% %i %c)",
                                            std::filesystem::path{"/tmp/file name.txt"});
  CHECK(args == std::vector<std::string>{"app",
                                         "--name",
                                         "two words",
                                         "--file",
                                         "/tmp/file name.txt",
                                         "--literal",
                                         "%"});

  auto listArgs = lambda_shell::parseDesktopExec("app %F --done", std::filesystem::path{"/tmp/a.txt"});
  CHECK(listArgs == std::vector<std::string>{"app", "/tmp/a.txt", "--done"});
}

TEST_CASE("Shell app registry matches app aliases") {
  CHECK(lambda_shell::shellAppIdMatches("terminal", "lambda-terminal"));
  CHECK(lambda_shell::shellAppIdMatches("terminal", "foot"));
  CHECK(lambda_shell::shellAppIdMatches("files", "org.gnome.Nautilus"));
  CHECK(lambda_shell::shellAppIdMatches("settings", "lambda-settings"));
  CHECK_FALSE(lambda_shell::shellAppIdMatches("settings", "lambda-files"));
}

TEST_CASE("Shell app registry prefers local example executables") {
  auto root = tempRoot("lambda-shell-local-apps-test");
  {
    std::ofstream(root / "lambda-files") << "#!/bin/sh\n";
    std::ofstream(root / "lambda-terminal") << "#!/bin/sh\n";
  }

  auto local = lambda_shell::discoverLocalExampleApps(root, {"lambda-files", "lambda-terminal", "lambda-settings"});
  REQUIRE(local.size() == 2);
  CHECK(local[0].appId == "lambda-files");
  CHECK(local[0].command == (root / "lambda-files").string());
  CHECK(local[0].local);

  std::vector<lambda_shell::AppRegistryEntry> installed{
      {.appId = "files", .name = "Installed Files", .command = "nautilus"},
      {.appId = "org.example.Editor", .name = "Editor", .command = "editor"},
  };
  auto merged = lambda_shell::mergeAppRegistryEntries(installed, local);
  CHECK(merged.size() == 3);
  CHECK(merged[0].appId == "lambda-files");
  CHECK(merged[1].appId == "lambda-terminal");
  CHECK(merged[2].appId == "org.example.Editor");

  std::filesystem::remove_all(root);
}

TEST_CASE("Shell app registry discovers installed desktop files from fixture dirs") {
  auto root = tempRoot("lambda-shell-installed-apps-test");
  auto first = root / "first";
  auto second = root / "second";
  std::filesystem::create_directories(first / "nested");
  std::filesystem::create_directories(second);
  {
    std::ofstream(first / "org.example.Editor.desktop") << R"(
[Desktop Entry]
Name=Editor
Icon=editor
Exec=editor %f
TryExec=editor-bin
MimeType=text/plain;
)";
    std::ofstream(first / "nested" / "org.example.Viewer.desktop") << R"(
[Desktop Entry]
Name=Viewer
Icon=viewer
Exec=viewer %f
TryExec=viewer-bin
MimeType=image/png;
)";
    std::ofstream(first / "org.example.Hidden.desktop") << R"(
[Desktop Entry]
Name=Hidden
Exec=hidden
Hidden=true
)";
    std::ofstream(second / "org.example.Editor.desktop") << R"(
[Desktop Entry]
Name=Duplicate Editor
Exec=duplicate
)";
    std::ofstream(second / "org.example.Missing.desktop") << R"(
[Desktop Entry]
Name=Missing
Exec=missing
TryExec=missing-bin
)";
  }

  auto apps = lambda_shell::discoverInstalledDesktopApps({first, second}, [](std::string const& executable) {
    return executable == "editor-bin" || executable == "viewer-bin";
  });
  REQUIRE(apps.size() == 2);
  CHECK(apps[0].appId == "org.example.Editor");
  CHECK(apps[0].name == "Editor");
  CHECK(apps[0].mimeTypes == std::vector<std::string>{"text/plain"});
  CHECK(apps[1].appId == "nested-org.example.Viewer");
  CHECK(apps[1].name == "Viewer");

  std::filesystem::remove_all(root);
}

TEST_CASE("Shell app registry finds icon theme paths with fallback") {
  auto root = tempRoot("lambda-shell-icon-test");
  std::filesystem::create_directories(root / "48x48" / "apps");
  std::filesystem::create_directories(root / "48x48" / "mimetypes");
  std::filesystem::create_directories(root / "scalable" / "apps");
  {
    std::ofstream(root / "48x48" / "apps" / "files.png") << "png";
    std::ofstream(root / "48x48" / "mimetypes" / "text-x-generic.svg") << "svg";
    std::ofstream(root / "scalable" / "apps" / "settings.svg") << "svg";
  }

  CHECK(lambda_shell::lookupIconThemePath(root, "files", 48) == root / "48x48" / "apps" / "files.png");
  CHECK(lambda_shell::lookupIconThemePath(root, "text-x-generic", 48) ==
        root / "48x48" / "mimetypes" / "text-x-generic.svg");
  CHECK(lambda_shell::lookupIconThemePath(root, "settings", 48) == root / "scalable" / "apps" / "settings.svg");
  CHECK(lambda_shell::lookupIconThemePath(root, "missing", 48).empty());

  std::filesystem::remove_all(root);
}
