#include "Shell/ShellModel.hpp"
#include "Shell/ShellModels.hpp"

#include <Flux/Shell/ShellIpc.hpp>

#include <doctest/doctest.h>

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

TEST_CASE("Shell model applies structured snapshots to dock status title and system status") {
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
  CHECK(changes.systemStatus);
  CHECK(model.activeTitle() == "Files Home");
  CHECK(model.systemStatus().network == "online");
  CHECK(model.systemStatus().wifi == "Lambda");

  bool filesFocused = false;
  bool terminalRunning = false;
  bool editorUnpinned = false;
  for (auto const& item : model.dockItems()) {
    if (item.appId == "lambda-files") filesFocused = item.running && item.focused;
    if (item.appId == "lambda-terminal") terminalRunning = item.running && !item.focused;
    if (item.appId == "org.example.Editor") editorUnpinned = item.running && !item.pinned;
  }
  CHECK(filesFocused);
  CHECK(terminalRunning);
  CHECK(editorUnpinned);

  std::vector<std::string> sent;
  for (auto const& item : model.dockItems()) {
    if (item.appId == "lambda-terminal") {
      model.activateItem(item, [&](std::string const& line) { sent.push_back(line); }, 42);
    }
  }
  REQUIRE(sent.size() == 1);
  auto message = flux::shell::parseLine(sent[0]);
  REQUIRE(message);
  CHECK(message->kind == flux::shell::ShellMessageKind::WindowManagerFocusApp);
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
  for (auto const& item : model.dockItems()) {
    CHECK(item.appId != "org.example.Editor");
  }
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
  CHECK(model.dockItems().back().kind == "trash");
  CHECK(model.dockItems().back().disabled);

  std::vector<std::string> sent;
  model.activateItem(model.dockItems()[2], [&](std::string const& line) { sent.push_back(line); }, 7);
  REQUIRE(sent.size() == 1);
  auto launch = flux::shell::parseLine(sent[0]);
  REQUIRE(launch);
  CHECK(launch->kind == flux::shell::ShellMessageKind::WindowManagerLaunchApp);
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
