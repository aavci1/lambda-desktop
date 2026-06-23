#include "Shell/ShellIpc.hpp"

#include <doctest/doctest.h>

TEST_CASE("shell IPC escape and roundtrip launchApp") {
  std::string const appId = R"(app"with\slashes)";
  std::string const line = lambdaui::shell::serializeLaunchApp(appId, 77);
  auto message = lambdaui::shell::parseLine(line);
  REQUIRE(message.has_value());
  REQUIRE(message->kind == lambdaui::shell::ShellMessageKind::WindowManagerLaunchApp);
  REQUIRE(message->requestId == 77);
  REQUIRE(message->launchApp.appId == appId);
  REQUIRE(lambdaui::shell::serialize(*message) == line);
}

TEST_CASE("shell IPC hello roundtrip") {
  std::string const line =
      lambdaui::shell::serializeShellHello(1, "0.2.0", {"dock", "command-launcher"});
  auto message = lambdaui::shell::parseLine(line);
  REQUIRE(message.has_value());
  REQUIRE(message->kind == lambdaui::shell::ShellMessageKind::ShellHello);
  REQUIRE(message->requestId == 0);
  REQUIRE(message->hello.protocolVersion == 1);
  REQUIRE(message->hello.shellVersion == "0.2.0");
  REQUIRE(message->hello.capabilities.size() == 2);
}

TEST_CASE("shell IPC malformed lines do not crash") {
  REQUIRE_FALSE(lambdaui::shell::parseLine("").has_value());
  REQUIRE_FALSE(lambdaui::shell::parseLine("{not json").has_value());
  REQUIRE_FALSE(lambdaui::shell::parseLine(R"({"type":"unknown.message"})").has_value());

  auto partial = lambdaui::shell::parseLine(R"({"type":"lambda.windowManager.launchApp"})");
  REQUIRE(partial.has_value());
  REQUIRE(partial->kind == lambdaui::shell::ShellMessageKind::WindowManagerLaunchApp);
  REQUIRE(partial->launchApp.appId.empty());
}

TEST_CASE("shell IPC focusWindow parses numeric id") {
  auto message = lambdaui::shell::parseLine(R"({"type":"lambda.windowManager.focusWindow","windowId":42,"requestId":9})");
  REQUIRE(message.has_value());
  REQUIRE(message->kind == lambdaui::shell::ShellMessageKind::WindowManagerFocusWindow);
  REQUIRE(message->requestId == 9);
  REQUIRE(message->focusWindow.windowId == 42u);
}

TEST_CASE("shell IPC quitApp roundtrip") {
  std::string const line = lambdaui::shell::serializeQuitApp("lambda-files", 91);
  auto message = lambdaui::shell::parseLine(line);
  REQUIRE(message.has_value());
  REQUIRE(message->kind == lambdaui::shell::ShellMessageKind::WindowManagerQuitApp);
  REQUIRE(message->requestId == 91);
  REQUIRE(message->quitApp.appId == "lambda-files");
  REQUIRE(lambdaui::shell::serialize(*message) == line);
}

TEST_CASE("shell IPC serialize helpers match parseLine") {
  std::string const claim = lambdaui::shell::serializeClaimCommandLauncherModal();
  auto claimMessage = lambdaui::shell::parseLine(claim);
  REQUIRE(claimMessage.has_value());
  REQUIRE(claimMessage->kind == lambdaui::shell::ShellMessageKind::WindowManagerClaimCommandLauncherModal);

  std::string const error = lambdaui::shell::serializeWindowManagerError("not-found", "missing app");
  auto errorMessage = lambdaui::shell::parseLine(error);
  REQUIRE(errorMessage.has_value());
  REQUIRE(errorMessage->kind == lambdaui::shell::ShellMessageKind::WindowManagerError);
  REQUIRE(errorMessage->error.code == "not-found");
  REQUIRE(errorMessage->error.message == "missing app");
}

TEST_CASE("shell IPC request ids roundtrip for command and error messages") {
  auto refresh = lambdaui::shell::parseLine(lambdaui::shell::serializeRefreshState(101));
  REQUIRE(refresh);
  CHECK(refresh->requestId == 101);
  CHECK(lambdaui::shell::serialize(*refresh) == lambdaui::shell::serializeRefreshState(101));

  auto open = lambdaui::shell::parseLine(lambdaui::shell::serializeOpenCommandLauncher(102));
  REQUIRE(open);
  CHECK(open->requestId == 102);

  auto claim = lambdaui::shell::parseLine(lambdaui::shell::serializeClaimCommandLauncherModal(103));
  REQUIRE(claim);
  CHECK(claim->requestId == 103);

  auto release = lambdaui::shell::parseLine(lambdaui::shell::serializeReleaseCommandLauncherModal(104));
  REQUIRE(release);
  CHECK(release->requestId == 104);

  auto error = lambdaui::shell::parseLine(lambdaui::shell::serializeWindowManagerError("bad-request", "Malformed", 105));
  REQUIRE(error);
  CHECK(error->requestId == 105);
  CHECK(error->error.code == "bad-request");
}
