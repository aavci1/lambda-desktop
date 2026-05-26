#include "Shell/ShellModel.hpp"

#include <Flux/Shell/ShellIpc.hpp>

#include <doctest/doctest.h>

#include <string>
#include <vector>

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
  CHECK(model.launcherResults()[0].appId == "terminal");

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
      {"id":2,"appId":"lambda-terminal","title":"Terminal","state":"minimized","focused":false}
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
  for (auto const& item : model.dockItems()) {
    if (item.appId == "files") filesFocused = item.running && item.focused;
    if (item.appId == "terminal") terminalRunning = item.running && !item.focused;
  }
  CHECK(filesFocused);
  CHECK(terminalRunning);

  std::vector<std::string> sent;
  for (auto const& item : model.dockItems()) {
    if (item.appId == "terminal") {
      model.activateItem(item, [&](std::string const& line) { sent.push_back(line); });
    }
  }
  REQUIRE(sent.size() == 1);
  auto message = flux::shell::parseLine(sent[0]);
  REQUIRE(message);
  CHECK(message->kind == flux::shell::ShellMessageKind::WindowManagerFocusApp);
  CHECK(message->focusApp.appId == "terminal");
}
