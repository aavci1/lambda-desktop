#include "Shell/ShellAppRegistry.hpp"

#include <Lambda/Graphics/Image.hpp>

#include <doctest/doctest.h>

#include <cstdlib>
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

void makeExecutable(std::filesystem::path const& path) {
  std::filesystem::permissions(path,
                               std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec |
                                   std::filesystem::perms::others_exec,
                               std::filesystem::perm_options::add);
}

struct ScopedEnv {
  explicit ScopedEnv(char const* name) : name(name) {
    if (char const* value = std::getenv(name)) {
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

} // namespace

TEST_CASE("Shell app registry parses desktop entry fields") {
  auto entry = lambda_shell::parseDesktopEntry(R"(
[Desktop Entry]
Name=Lambda Files
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
  CHECK(entry->name == "Lambda Files");
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
  CHECK(app.name == "Lambda Files");
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
  CHECK(lambda_shell::shellAppIdMatches("editor", "lambda-editor"));
  CHECK(lambda_shell::shellAppIdMatches("terminal", "lambda-terminal"));
  CHECK(lambda_shell::shellAppIdMatches("terminal", "foot"));
  CHECK(lambda_shell::shellAppIdMatches("files", "org.gnome.Nautilus"));
  CHECK(lambda_shell::shellAppIdMatches("preview", "lambda-preview"));
  CHECK(lambda_shell::shellAppIdMatches("settings", "lambda-settings"));
  CHECK_FALSE(lambda_shell::shellAppIdMatches("settings", "lambda-files"));
}

TEST_CASE("Shell app registry prefers local lambda app executables") {
  auto root = tempRoot("lambda-shell-local-apps-test");
  auto appsDir = root / "apps";
  std::filesystem::create_directories(appsDir / "lambda-files");
  std::filesystem::create_directories(appsDir / "lambda-preview");
  std::filesystem::create_directories(appsDir / "lambda-terminal");
  std::filesystem::create_directories(appsDir / "lambda-settings");
  {
    std::ofstream(appsDir / "lambda-files" / "lambda-files") << "#!/bin/sh\n";
    std::ofstream(appsDir / "lambda-preview" / "lambda-preview") << "#!/bin/sh\n";
    std::ofstream(appsDir / "lambda-terminal" / "lambda-terminal") << "#!/bin/sh\n";
    std::ofstream(appsDir / "lambda-settings" / "lambda-settings") << "#!/bin/sh\n";
  }
  makeExecutable(appsDir / "lambda-files" / "lambda-files");
  makeExecutable(appsDir / "lambda-preview" / "lambda-preview");
  makeExecutable(appsDir / "lambda-terminal" / "lambda-terminal");

  auto local = lambda_shell::discoverLocalLambdaApps(
      {appsDir},
      {"lambda-files", "lambda-preview", "lambda-terminal", "lambda-settings"});
  REQUIRE(local.size() == 3);
  CHECK(local[0].appId == "lambda-files");
  CHECK(local[0].name == "Files");
  CHECK(local[0].icon == "system-file-manager");
  CHECK(local[0].command == (appsDir / "lambda-files" / "lambda-files").string());
  CHECK(local[0].local);
  CHECK(local[1].appId == "lambda-preview");
  CHECK(local[1].icon == "image-viewer");
  CHECK(local[1].mimeTypes == std::vector<std::string>{"image/png",
                                                       "image/jpeg",
                                                       "image/gif",
                                                       "image/webp",
                                                       "image/svg+xml"});
  CHECK(local[2].appId == "lambda-terminal");
  CHECK(local[2].icon == "utilities-terminal");

  std::vector<lambda_shell::AppRegistryEntry> installed{
      {.appId = "files", .name = "Installed Files", .command = "nautilus"},
      {.appId = "org.example.Editor", .name = "Editor", .command = "editor"},
  };
  auto merged = lambda_shell::mergeAppRegistryEntries(installed, local);
  CHECK(merged.size() == 4);
  CHECK(merged[0].appId == "lambda-files");
  CHECK(merged[1].appId == "lambda-preview");
  CHECK(merged[2].appId == "lambda-terminal");
  CHECK(merged[3].appId == "org.example.Editor");

  std::filesystem::remove_all(root);
}

TEST_CASE("Shell app registry resolves launch commands from shared registry") {
  auto root = tempRoot("lambda-shell-launch-registry-test");
  auto appsDir = root / "apps";
  std::filesystem::create_directories(appsDir / "lambda-terminal");
  {
    std::ofstream(appsDir / "lambda-terminal" / "lambda-terminal") << "#!/bin/sh\n";
  }
  makeExecutable(appsDir / "lambda-terminal" / "lambda-terminal");

  auto registry = lambda_shell::buildDefaultAppRegistry({appsDir}, {}, {});
  auto terminal = lambda_shell::resolveAppLaunchCommand("terminal", registry);
  REQUIRE(terminal);
  CHECK(*terminal == "'" + (appsDir / "lambda-terminal" / "lambda-terminal").string() + "'");

  auto browser = lambda_shell::resolveAppLaunchCommand("browser", registry);
  REQUIRE(browser);
  CHECK(*browser == "'firefox'");

  CHECK_FALSE(lambda_shell::resolveAppLaunchCommand("calendar", registry));

  std::vector<lambda_shell::AppRegistryEntry> apps{
      {.appId = "org.example.Editor", .name = "Editor", .command = R"(editor --new-window "two words" %f %i %c)"},
  };
  auto editor = lambda_shell::resolveAppLaunchCommand("org.example.Editor", apps);
  REQUIRE(editor);
  CHECK(*editor == "'editor' '--new-window' 'two words'");

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
  std::filesystem::create_directories(root / "apps" / "48");
  std::filesystem::create_directories(root / "48x48" / "categories");
  std::filesystem::create_directories(root / "48x48" / "legacy");
  std::filesystem::create_directories(root / "48x48" / "mimetypes");
  std::filesystem::create_directories(root / "scalable" / "apps");
  std::filesystem::create_directories(root / "places" / "scalable");
  {
    std::ofstream(root / "48x48" / "apps" / "files.png") << "png";
    std::ofstream(root / "apps" / "48" / "system-file-manager.svg") << "svg";
    std::ofstream(root / "48x48" / "categories" / "preferences-system.png") << "png";
    std::ofstream(root / "48x48" / "legacy" / "utilities-terminal.png") << "png";
    std::ofstream(root / "48x48" / "mimetypes" / "text-x-generic.svg") << "svg";
    std::ofstream(root / "scalable" / "apps" / "settings.svg") << "svg";
    std::ofstream(root / "places" / "scalable" / "user-trash.svg") << "svg";
  }

  CHECK(lambda_shell::lookupIconThemePath(root, "files", 48) == root / "48x48" / "apps" / "files.png");
  CHECK(lambda_shell::lookupIconThemePath(root, "system-file-manager", 48) ==
        root / "apps" / "48" / "system-file-manager.svg");
  CHECK(lambda_shell::lookupIconThemePath(root, "preferences-system", 48) ==
        root / "48x48" / "categories" / "preferences-system.png");
  CHECK(lambda_shell::lookupIconThemePath(root, "utilities-terminal", 48) ==
        root / "48x48" / "legacy" / "utilities-terminal.png");
  CHECK(lambda_shell::lookupIconThemePath(root, "text-x-generic", 48) ==
        root / "48x48" / "mimetypes" / "text-x-generic.svg");
  CHECK(lambda_shell::lookupIconThemePath(root, "settings", 48) == root / "scalable" / "apps" / "settings.svg");
  CHECK(lambda_shell::lookupIconThemePath(root, "user-trash", 48) == root / "places" / "scalable" / "user-trash.svg");
  CHECK(lambda_shell::lookupIconThemePath(root, "missing", 48).empty());

  std::filesystem::remove_all(root);
}

#if LAMBDAUI_VULKAN
TEST_CASE("Image loader decodes SVG theme icons on Linux") {
  auto root = tempRoot("lambda-shell-svg-icon-test");
  auto icon = root / "icon.svg";
  std::ofstream(icon) << R"(<svg xmlns="http://www.w3.org/2000/svg" width="4" height="4" viewBox="0 0 4 4">
<rect width="4" height="4" fill="#336699"/>
</svg>)";

  auto decoded = lambdaui::decodeImageRgbaFromFile(icon.string());
  REQUIRE(decoded);
  CHECK(decoded->width == 4);
  CHECK(decoded->height == 4);
  REQUIRE(decoded->pixels.size() == 64);
  CHECK(decoded->pixels[3] == 255);

  auto scaled = lambdaui::decodeImageRgbaFromFile(icon.string(), 24);
  REQUIRE(scaled);
  CHECK(scaled->width == 24);
  CHECK(scaled->height == 24);
  CHECK(scaled->pixels.size() == 24u * 24u * 4u);

  std::filesystem::remove_all(root);
}
#endif

TEST_CASE("Shell app registry resolves configured icon themes through XDG roots") {
  ScopedEnv homeEnv("HOME");
  ScopedEnv dataHomeEnv("XDG_DATA_HOME");
  ScopedEnv dataDirsEnv("XDG_DATA_DIRS");
  ScopedEnv shellConfigEnv("LAMBDA_SHELL_CONFIG");
  ScopedEnv iconThemeEnv("LAMBDA_ICON_THEME");

  auto root = tempRoot("lambda-shell-icon-theme-roots-test");
  auto dataHome = root / "data-home";
  auto dataDir = root / "data-dir";
  auto shellConfig = root / "lambda-shell.toml";
  auto themedIcon = dataHome / "icons" / "Lambda" / "48x48" / "apps" / "lambda-terminal.png";
  auto inheritedIcon = dataDir / "icons" / "Lambda-Base" / "48x48" / "apps" / "lambda-files.png";
  auto fallbackIcon = dataDir / "icons" / "hicolor" / "48x48" / "apps" / "lambda-settings.png";
  std::filesystem::create_directories(themedIcon.parent_path());
  std::filesystem::create_directories(inheritedIcon.parent_path());
  std::filesystem::create_directories(fallbackIcon.parent_path());
  std::ofstream(dataHome / "icons" / "Lambda" / "index.theme") << "[Icon Theme]\nInherits=Lambda-Base,hicolor\n";
  std::ofstream(themedIcon) << "png";
  std::ofstream(inheritedIcon) << "png";
  std::ofstream(fallbackIcon) << "png";

  auto const dataHomeString = dataHome.string();
  auto const dataDirString = dataDir.string();
  setenv("XDG_DATA_HOME", dataHomeString.c_str(), 1);
  setenv("XDG_DATA_DIRS", dataDirString.c_str(), 1);
  auto const shellConfigString = shellConfig.string();
  setenv("LAMBDA_SHELL_CONFIG", shellConfigString.c_str(), 1);
  unsetenv("LAMBDA_ICON_THEME");
  unsetenv("HOME");
  std::ofstream(shellConfig) << "[appearance]\nicon_theme = \"Lambda\"\n";

  CHECK(lambda_shell::configuredIconThemeName() == "Lambda");
  std::ofstream(shellConfig) << "[appearance]\nicon_theme = 'Lambda'\n";
  CHECK(lambda_shell::configuredIconThemeName() == "Lambda");
  CHECK(lambda_shell::resolveIconThemePath("lambda-terminal", "Lambda", 48) == themedIcon);
  CHECK(lambda_shell::resolveIconThemePath("lambda-files", "Lambda", 48) == inheritedIcon);
  CHECK(lambda_shell::resolveIconThemePath("lambda-settings", "Lambda", 48) == fallbackIcon);
  CHECK(lambda_shell::resolveIconThemePath("lambda-terminal", "", 48) == themedIcon);
  CHECK(lambda_shell::resolveIconThemePath("lambda-files", "", 48) == inheritedIcon);
  CHECK(lambda_shell::resolveIconThemePath("lambda-settings", "", 48) == fallbackIcon);
  CHECK(lambda_shell::resolveIconThemePath("missing", "Lambda", 48).empty());

  std::filesystem::remove_all(root);
}

TEST_CASE("Shell app registry honors freedesktop scaled icon directories") {
  ScopedEnv homeEnv("HOME");
  ScopedEnv dataHomeEnv("XDG_DATA_HOME");
  ScopedEnv dataDirsEnv("XDG_DATA_DIRS");
  ScopedEnv shellConfigEnv("LAMBDA_SHELL_CONFIG");
  ScopedEnv iconThemeEnv("LAMBDA_ICON_THEME");

  auto root = tempRoot("lambda-shell-scaled-icon-theme-test");
  auto dataHome = root / "data-home";
  auto themeRoot = dataHome / "icons" / "Lambda";
  auto smallIcon = themeRoot / "apps" / "22" / "lambda-terminal.png";
  auto scaledIcon = themeRoot / "apps@2x" / "32" / "lambda-terminal.png";
  std::filesystem::create_directories(smallIcon.parent_path());
  std::filesystem::create_directories(scaledIcon.parent_path());
  std::ofstream(themeRoot / "index.theme") << R"(
[Icon Theme]
Name=Lambda
Directories=apps/22
ScaledDirectories=apps@2x/32

[apps/22]
Size=22
Type=Fixed

[apps@2x/32]
Size=32
Scale=2
Type=Fixed
)";
  std::ofstream(smallIcon) << "png";
  std::ofstream(scaledIcon) << "png";

  auto const dataHomeString = dataHome.string();
  setenv("XDG_DATA_HOME", dataHomeString.c_str(), 1);
  unsetenv("XDG_DATA_DIRS");
  unsetenv("LAMBDA_SHELL_CONFIG");
  unsetenv("LAMBDA_ICON_THEME");
  unsetenv("HOME");

  CHECK(lambda_shell::resolveIconThemePath("lambda-terminal", "Lambda", 64) == scaledIcon);

  std::filesystem::remove_all(root);
}

TEST_CASE("Shell app registry prefers downsampling larger fixed icons over upscaling smaller icons") {
  ScopedEnv homeEnv("HOME");
  ScopedEnv dataHomeEnv("XDG_DATA_HOME");
  ScopedEnv dataDirsEnv("XDG_DATA_DIRS");
  ScopedEnv shellConfigEnv("LAMBDA_SHELL_CONFIG");
  ScopedEnv iconThemeEnv("LAMBDA_ICON_THEME");

  auto root = tempRoot("lambda-shell-larger-icon-theme-test");
  auto dataHome = root / "data-home";
  auto themeRoot = dataHome / "icons" / "Lambda";
  auto smallIcon = themeRoot / "apps" / "64" / "lambda-terminal.png";
  auto largeIcon = themeRoot / "apps" / "96" / "lambda-terminal.png";
  std::filesystem::create_directories(smallIcon.parent_path());
  std::filesystem::create_directories(largeIcon.parent_path());
  std::ofstream(themeRoot / "index.theme") << R"(
[Icon Theme]
Name=Lambda
Directories=apps/64

[apps/64]
Size=64
Type=Fixed
)";
  std::ofstream(smallIcon) << "png";
  std::ofstream(largeIcon) << "png";

  auto const dataHomeString = dataHome.string();
  setenv("XDG_DATA_HOME", dataHomeString.c_str(), 1);
  unsetenv("XDG_DATA_DIRS");
  unsetenv("LAMBDA_SHELL_CONFIG");
  unsetenv("LAMBDA_ICON_THEME");
  unsetenv("HOME");

  CHECK(lambda_shell::resolveIconThemePath("lambda-terminal", "Lambda", 72) == largeIcon);

  std::filesystem::remove_all(root);
}
