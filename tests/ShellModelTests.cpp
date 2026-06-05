#include "Shell/ShellModel.hpp"
#include "Shell/ShellModels.hpp"

#include "Shell/ShellIpc.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <unistd.h>

namespace {

std::filesystem::path tempRoot(char const* name) {
  auto path = std::filesystem::temp_directory_path() /
              (std::string(name) + "-" + std::to_string(static_cast<unsigned long long>(getpid())));
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path);
  return path;
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

TEST_CASE("Shell model launcher keyboard state is deterministic") {
  lambda_shell::ShellModel model;
  model.resetDockItems();

  model.openLauncher();
  CHECK(model.launcherOpen());
  CHECK(model.query().empty());
  CHECK(model.queryCursor() == 0);
  CHECK(model.highlighted() == 0);

  model.appendQueryText("ter");
  CHECK(model.query() == "ter");
  CHECK(model.queryCursor() == 3);
  REQUIRE_FALSE(model.launcherResults().empty());
  CHECK(model.launcherResults()[0].appId == "lambda-terminal");

  model.moveHighlight(100);
  CHECK(model.highlighted() == static_cast<int>(model.launcherResults().size()) - 1);
  model.moveHighlight(-100);
  CHECK(model.highlighted() == 0);

  model.backspaceQuery();
  CHECK(model.query() == "te");
  CHECK(model.queryCursor() == 2);
  model.closeLauncher();
  CHECK_FALSE(model.launcherOpen());
  CHECK(model.query().empty());
  CHECK(model.queryCursor() == 0);
  CHECK(model.highlighted() == 0);
}

TEST_CASE("Shell model launcher query editing is cursor aware") {
  lambda_shell::ShellModel model;
  model.resetDockItems();
  model.openLauncher();

  model.appendQueryText("abcd");
  model.moveQueryCursor(-2);
  CHECK(model.queryCursor() == 2);
  model.appendQueryText("X");
  CHECK(model.query() == "abXcd");
  CHECK(model.queryCursor() == 3);

  model.backspaceQuery();
  CHECK(model.query() == "abcd");
  CHECK(model.queryCursor() == 2);

  model.deleteQueryForward();
  CHECK(model.query() == "abd");
  CHECK(model.queryCursor() == 2);

  model.moveQueryCursorToStart();
  model.appendQueryText(">");
  CHECK(model.query() == ">abd");
  CHECK(model.queryCursor() == 1);

  model.moveQueryCursorToEnd();
  model.appendQueryText("<paste>");
  CHECK(model.query() == ">abd<paste>");
  CHECK(model.queryCursor() == 11);
}

TEST_CASE("Shell model applies snapshots to dock and title while status remains shell owned") {
  lambda_shell::ShellModel model;
  model.resetDockItems();

  auto changes = model.applySnapshot(R"({
    "type":"lambda.windowManager.snapshot",
    "windows":[
      {"id":1,"appId":"lambda-files","title":"Files Home","state":"normal","focused":true},
      {"id":2,"appId":"lambda-terminal","title":"Terminal","state":"minimized","focused":false},
      {"id":3,"appId":"org.example.Editor","title":"Notes","state":"normal","focused":false}
    ],
    "system":{"network":"online","wifi":"Lambda","bluetooth":"off","volume":"55%","battery":"88%"}
  })");
  CHECK(changes.dockItems);
  CHECK(changes.activeTitle);
  CHECK(model.activeTitle() == "Files Home");
  CHECK(model.systemStatus().network == "unknown");

  CHECK(model.setSystemStatus(lambda_shell::SystemStatus{
      .network = "online",
      .wifi = "Lambda",
      .bluetooth = "off",
      .volume = "55%",
      .battery = "88%",
  }));
  CHECK(model.systemStatus().network == "online");
  CHECK(model.systemStatus().wifi == "Lambda");

  bool filesFocused = false;
  bool terminalRunning = false;
  bool editorUnpinned = false;
  std::vector<std::string> dockOrder;
  for (auto const& item : model.dockItems()) {
    dockOrder.push_back(item.id);
    if (item.appId == "lambda-files") filesFocused = item.running && item.focused;
    if (item.appId == "lambda-terminal") terminalRunning = item.running && !item.focused;
    if (item.appId == "org.example.Editor") editorUnpinned = item.running && !item.pinned;
  }
  CHECK(filesFocused);
  CHECK(terminalRunning);
  CHECK(editorUnpinned);
  auto const editor = std::find(dockOrder.begin(), dockOrder.end(), "org.example.Editor");
  REQUIRE(editor != dockOrder.end());
  CHECK(std::find(dockOrder.begin(), dockOrder.end(), "trash") == dockOrder.end());

  std::vector<std::string> sent;
  for (auto const& item : model.dockItems()) {
    if (item.appId == "lambda-terminal") {
      model.activateItem(item, [&](std::string const& line) { sent.push_back(line); }, 42);
    }
  }
  REQUIRE(sent.size() == 1);
  auto message = lambda::shell::parseLine(sent[0]);
  REQUIRE(message);
  CHECK(message->kind == lambda::shell::ShellMessageKind::WindowManagerFocusApp);
  CHECK(message->requestId == 42);
  CHECK(message->focusApp.appId == "lambda-terminal");

  changes = model.applySnapshot(R"({
    "type":"lambda.windowManager.snapshot",
    "windows":[
      {"id":1,"appId":"lambda-files","title":"Files Home","state":"normal","focused":true}
    ],
    "system":{"network":"online","wifi":"Lambda","bluetooth":"off","volume":"55%","battery":"88%"}
  })");
  CHECK(changes.dockItems);
  bool terminalStillRunning = false;
  for (auto const& item : model.dockItems()) {
    CHECK(item.appId != "org.example.Editor");
    if (item.appId == "lambda-terminal") terminalStillRunning = item.running;
  }
  CHECK_FALSE(terminalStillRunning);
}

TEST_CASE("Shell model dock items come from config pins and app registry") {
  auto iconRoot = tempRoot("lambda-shell-model-icon-test");
  auto terminalIcon = iconRoot / "lambda-terminal.png";
  auto browserIcon = iconRoot / "browser.png";
  std::ofstream(terminalIcon) << "png";
  std::ofstream(browserIcon) << "png";

  lambda_shell::ShellModel model;
  lambda_shell::ShellConfig config = lambda_shell::defaultShellConfig();
  config.dockPinned = {"lambda-terminal", "missing-app", "firefox", "lambda-files"};
  std::vector<lambda_shell::AppRegistryEntry> apps{
      {.appId = "lambda-files", .name = "Files", .icon = "lambda-files", .command = "lambda-files"},
      {.appId = "lambda-terminal", .name = "Terminal", .icon = terminalIcon.string(), .command = "lambda-terminal"},
      {.appId = "browser", .name = "Browser", .icon = browserIcon.string(), .command = "firefox"},
      {.appId = "org.example.Hidden", .name = "Hidden", .command = "hidden", .hidden = true},
  };

  model.setDockItems(apps, config);
  std::vector<std::string> appIds;
  for (auto const& item : model.dockItems()) {
    if (item.kind == "app") appIds.push_back(item.appId);
  }
  CHECK(appIds == std::vector<std::string>{"lambda-terminal", "browser", "lambda-files"});
  CHECK(model.dockItems()[2].icon == terminalIcon.string());
  CHECK(model.dockItems()[2].iconPath == terminalIcon.string());
  CHECK(model.launcherResults()[0].iconPath == terminalIcon.string());
  CHECK(model.dockItems().back().kind == "app");
  CHECK(std::none_of(model.dockItems().begin(), model.dockItems().end(), [](auto const& item) {
    return item.kind == "trash";
  }));

  std::vector<std::string> sent;
  model.activateItem(model.dockItems()[2], [&](std::string const& line) { sent.push_back(line); }, 7);
  REQUIRE(sent.size() == 1);
  auto launch = lambda::shell::parseLine(sent[0]);
  REQUIRE(launch);
  CHECK(launch->kind == lambda::shell::ShellMessageKind::WindowManagerLaunchApp);
  CHECK(launch->requestId == 7);
  CHECK(launch->launchApp.appId == "lambda-terminal");

  model.resetDockItems();
  for (auto const& item : model.dockItems()) {
    CHECK(item.appId != "calendar");
    CHECK(item.appId != "mail");
    CHECK(item.appId != "music");
  }

  config.showRunningUnpinned = false;
  model.setDockItems(apps, config);
  auto hiddenUnpinned = model.applySnapshot(R"({
    "type":"lambda.windowManager.snapshot",
    "windows":[{"id":9,"appId":"org.example.Editor","title":"Notes","state":"normal","focused":true}],
    "system":{}
  })");
  CHECK_FALSE(hiddenUnpinned.dockItems);
  for (auto const& item : model.dockItems()) {
    CHECK(item.appId != "org.example.Editor");
  }

  std::filesystem::remove_all(iconRoot);
}

TEST_CASE("Shell model ignores placeholder window identities in dock snapshots") {
  lambda_shell::ShellModel model;
  model.resetDockItems();

  auto changes = model.applySnapshot(R"({
    "type":"lambda.windowManager.snapshot",
    "windows":[
      {"id":21,"appId":"unknown","title":"unknown","state":"normal","focused":false},
      {"id":22,"appId":"toggle-demo","title":"Lambda - Toggle demo","state":"normal","focused":true}
    ],
    "system":{}
  })");
  CHECK(changes.dockItems);

  int toggleCount = 0;
  for (auto const& item : model.dockItems()) {
    CHECK(item.appId != "unknown");
    if (item.appId == "toggle-demo") {
      ++toggleCount;
      CHECK(item.running);
      CHECK(item.focused);
    }
  }
  CHECK(toggleCount == 1);
}

TEST_CASE("Shell model merges alias app ids into pinned dock items") {
  lambda_shell::ShellModel model;
  lambda_shell::ShellConfig config = lambda_shell::defaultShellConfig();
  config.dockPinned = {"lambda-files", "lambda-settings"};
  std::vector<lambda_shell::AppRegistryEntry> apps{
      {.appId = "lambda-files", .name = "Files", .icon = "system-file-manager", .command = "lambda-files"},
      {.appId = "lambda-settings", .name = "Settings", .icon = "preferences-system", .command = "lambda-settings"},
  };

  model.setDockItems(apps, config);
  auto changes = model.applySnapshot(R"({
    "type":"lambda.windowManager.snapshot",
    "apps":[
      {"id":"lambda-files","name":"Files","icon":"system-file-manager","command":"lambda-files"},
      {"id":"lambda-settings","name":"Settings","icon":"preferences-system","command":"lambda-settings"}
    ],
    "windows":[
      {"id":1,"appId":"files","title":"Files","state":"normal","focused":true},
      {"id":2,"appId":"settings","title":"Settings","state":"normal","focused":false}
    ],
    "system":{}
  })");
  CHECK(changes.dockItems);

  int filesCount = 0;
  int settingsCount = 0;
  bool filesRunning = false;
  bool settingsRunning = false;
  for (auto const& item : model.dockItems()) {
    if (item.kind != "app") continue;
    if (item.appId == "lambda-files") {
      ++filesCount;
      filesRunning = item.running && item.focused;
    }
    if (item.appId == "lambda-settings") {
      ++settingsCount;
      settingsRunning = item.running && !item.focused;
    }
    CHECK(item.appId != "files");
    CHECK(item.appId != "settings");
  }
  CHECK(filesCount == 1);
  CHECK(settingsCount == 1);
  CHECK(filesRunning);
  CHECK(settingsRunning);
}

TEST_CASE("Shell model updates dock icon physical size from DPI scale") {
  lambda_shell::ShellModel model;
  lambda_shell::ShellConfig config = lambda_shell::defaultShellConfig();
  config.dockItemSize = 40;
  config.dockPinned = {"lambda-terminal"};
  std::vector<lambda_shell::AppRegistryEntry> apps{
      {.appId = "lambda-terminal", .name = "Terminal", .icon = "/tmp/lambda-terminal-test-icon.png",
       .command = "lambda-terminal"},
  };

  model.setDockItems(apps, config);
  bool sawIcon = false;
  for (auto const& item : model.dockItems()) {
    if (item.kind == "app") {
      CHECK(item.iconPixelSize == 40);
      sawIcon = true;
    }
  }
  CHECK(sawIcon);
  CHECK(model.dockItemSize() == 40);

  CHECK(model.setDockDpiScale(1.5f));
  for (auto const& item : model.dockItems()) {
    if (item.kind == "app") {
      CHECK(item.iconPixelSize == 60);
    }
  }
  CHECK_FALSE(model.setDockDpiScale(1.5f));
}

TEST_CASE("Shell model refreshes dock icon paths when icon theme changes") {
  ScopedEnv homeEnv("HOME");
  ScopedEnv dataHomeEnv("XDG_DATA_HOME");
  ScopedEnv dataDirsEnv("XDG_DATA_DIRS");
  ScopedEnv shellConfigEnv("LAMBDA_SHELL_CONFIG");
  ScopedEnv iconThemeEnv("LAMBDA_ICON_THEME");

  auto root = tempRoot("lambda-shell-model-theme-reload-test");
  auto dataHome = root / "data-home";
  auto alphaIcon = dataHome / "icons" / "Alpha" / "48x48" / "apps" / "lambda-terminal.png";
  auto betaIcon = dataHome / "icons" / "Beta" / "48x48" / "apps" / "lambda-terminal.png";
  std::filesystem::create_directories(alphaIcon.parent_path());
  std::filesystem::create_directories(betaIcon.parent_path());
  std::ofstream(alphaIcon) << "png";
  std::ofstream(betaIcon) << "png";

  auto const dataHomeString = dataHome.string();
  setenv("XDG_DATA_HOME", dataHomeString.c_str(), 1);
  unsetenv("XDG_DATA_DIRS");
  unsetenv("LAMBDA_SHELL_CONFIG");
  unsetenv("LAMBDA_ICON_THEME");
  unsetenv("HOME");

  lambda_shell::ShellModel model;
  lambda_shell::ShellConfig config = lambda_shell::defaultShellConfig();
  config.dockPinned = {"lambda-terminal"};
  config.iconTheme = "Alpha";
  std::vector<lambda_shell::AppRegistryEntry> apps{
      {.appId = "lambda-terminal", .name = "Terminal", .icon = "lambda-terminal", .command = "lambda-terminal"},
  };

  model.setDockItems(apps, config);
  auto terminalItem = std::find_if(model.dockItems().begin(), model.dockItems().end(), [](auto const& item) {
    return item.appId == "lambda-terminal";
  });
  REQUIRE(terminalItem != model.dockItems().end());
  CHECK(terminalItem->iconPath == alphaIcon.string());

  config.iconTheme = "Beta";
  model.setDockItems(apps, config);
  terminalItem = std::find_if(model.dockItems().begin(), model.dockItems().end(), [](auto const& item) {
    return item.appId == "lambda-terminal";
  });
  REQUIRE(terminalItem != model.dockItems().end());
  CHECK(terminalItem->iconPath == betaIcon.string());

  std::filesystem::remove_all(root);
}

TEST_CASE("Shell model resolves dock-specific themed icon fallbacks") {
  ScopedEnv homeEnv("HOME");
  ScopedEnv dataHomeEnv("XDG_DATA_HOME");
  ScopedEnv dataDirsEnv("XDG_DATA_DIRS");
  ScopedEnv shellConfigEnv("LAMBDA_SHELL_CONFIG");
  ScopedEnv iconThemeEnv("LAMBDA_ICON_THEME");

  auto root = tempRoot("lambda-shell-model-icon-fallback-test");
  auto dataHome = root / "data-home";
  auto settingsSmallIcon = dataHome / "icons" / "Lambda" / "32x32" / "apps" / "preferences-system.png";
  auto settingsLargeIcon = dataHome / "icons" / "Lambda" / "48x48" / "apps" / "systemsettings.png";
  std::filesystem::create_directories(settingsSmallIcon.parent_path());
  std::filesystem::create_directories(settingsLargeIcon.parent_path());
  std::ofstream(settingsSmallIcon) << "png";
  std::ofstream(settingsLargeIcon) << "png";

  auto const dataHomeString = dataHome.string();
  setenv("XDG_DATA_HOME", dataHomeString.c_str(), 1);
  unsetenv("XDG_DATA_DIRS");
  unsetenv("LAMBDA_SHELL_CONFIG");
  unsetenv("LAMBDA_ICON_THEME");
  unsetenv("HOME");

  lambda_shell::ShellModel model;
  lambda_shell::ShellConfig config = lambda_shell::defaultShellConfig();
  config.iconTheme = "Lambda";
  config.dockPinned = {"lambda-settings"};
  std::vector<lambda_shell::AppRegistryEntry> apps{
      {.appId = "lambda-settings", .name = "Settings", .icon = "preferences-system", .command = "lambda-settings"},
  };

  model.setDockItems(apps, config);
  auto settingsItem = std::find_if(model.dockItems().begin(), model.dockItems().end(), [](auto const& item) {
    return item.appId == "lambda-settings";
  });
  REQUIRE(settingsItem != model.dockItems().end());
  CHECK(settingsItem->iconPath == settingsLargeIcon.string());

  CHECK(std::none_of(model.dockItems().begin(), model.dockItems().end(), [](auto const& item) {
    return item.kind == "trash";
  }));

  std::filesystem::remove_all(root);
}
