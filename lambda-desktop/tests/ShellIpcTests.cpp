#include "Shell/ShellIpc.hpp"

#include <doctest/doctest.h>

TEST_CASE("shell IPC escape and roundtrip launchApp") {
  std::string const appId = R"(app"with\slashes)";
  std::string const line = lambda::shell::serializeLaunchApp(appId, 77);
  auto message = lambda::shell::parseLine(line);
  REQUIRE(message.has_value());
  REQUIRE(message->kind == lambda::shell::ShellMessageKind::WindowManagerLaunchApp);
  REQUIRE(message->requestId == 77);
  REQUIRE(message->launchApp.appId == appId);
  REQUIRE(lambda::shell::serialize(*message) == line);
}

TEST_CASE("shell IPC hello roundtrip") {
  std::string const line =
      lambda::shell::serializeShellHello(1, "0.2.0", {"dock", "command-launcher"});
  auto message = lambda::shell::parseLine(line);
  REQUIRE(message.has_value());
  REQUIRE(message->kind == lambda::shell::ShellMessageKind::ShellHello);
  REQUIRE(message->requestId == 0);
  REQUIRE(message->hello.protocolVersion == 1);
  REQUIRE(message->hello.shellVersion == "0.2.0");
  REQUIRE(message->hello.capabilities.size() == 2);
}

TEST_CASE("shell IPC malformed lines do not crash") {
  REQUIRE_FALSE(lambda::shell::parseLine("").has_value());
  REQUIRE_FALSE(lambda::shell::parseLine("{not json").has_value());
  REQUIRE_FALSE(lambda::shell::parseLine(R"({"type":"unknown.message"})").has_value());

  auto partial = lambda::shell::parseLine(R"({"type":"lambda.windowManager.launchApp"})");
  REQUIRE(partial.has_value());
  REQUIRE(partial->kind == lambda::shell::ShellMessageKind::WindowManagerLaunchApp);
  REQUIRE(partial->launchApp.appId.empty());
}

TEST_CASE("shell IPC focusWindow parses numeric id") {
  auto message = lambda::shell::parseLine(R"({"type":"lambda.windowManager.focusWindow","windowId":42,"requestId":9})");
  REQUIRE(message.has_value());
  REQUIRE(message->kind == lambda::shell::ShellMessageKind::WindowManagerFocusWindow);
  REQUIRE(message->requestId == 9);
  REQUIRE(message->focusWindow.windowId == 42u);
}

TEST_CASE("shell IPC quitApp roundtrip") {
  std::string const line = lambda::shell::serializeQuitApp("lambda-files", 91);
  auto message = lambda::shell::parseLine(line);
  REQUIRE(message.has_value());
  REQUIRE(message->kind == lambda::shell::ShellMessageKind::WindowManagerQuitApp);
  REQUIRE(message->requestId == 91);
  REQUIRE(message->quitApp.appId == "lambda-files");
  REQUIRE(lambda::shell::serialize(*message) == line);
}

TEST_CASE("shell IPC serialize helpers match parseLine") {
  std::string const claim = lambda::shell::serializeClaimCommandLauncherModal();
  auto claimMessage = lambda::shell::parseLine(claim);
  REQUIRE(claimMessage.has_value());
  REQUIRE(claimMessage->kind == lambda::shell::ShellMessageKind::WindowManagerClaimCommandLauncherModal);

  std::string const error = lambda::shell::serializeWindowManagerError("not-found", "missing app");
  auto errorMessage = lambda::shell::parseLine(error);
  REQUIRE(errorMessage.has_value());
  REQUIRE(errorMessage->kind == lambda::shell::ShellMessageKind::WindowManagerError);
  REQUIRE(errorMessage->error.code == "not-found");
  REQUIRE(errorMessage->error.message == "missing app");
}

TEST_CASE("shell IPC request ids roundtrip for command and error messages") {
  auto refresh = lambda::shell::parseLine(lambda::shell::serializeRefreshState(101));
  REQUIRE(refresh);
  CHECK(refresh->requestId == 101);
  CHECK(lambda::shell::serialize(*refresh) == lambda::shell::serializeRefreshState(101));

  auto open = lambda::shell::parseLine(lambda::shell::serializeOpenCommandLauncher(102));
  REQUIRE(open);
  CHECK(open->requestId == 102);

  auto claim = lambda::shell::parseLine(lambda::shell::serializeClaimCommandLauncherModal(103));
  REQUIRE(claim);
  CHECK(claim->requestId == 103);

  auto release = lambda::shell::parseLine(lambda::shell::serializeReleaseCommandLauncherModal(104));
  REQUIRE(release);
  CHECK(release->requestId == 104);

  auto error = lambda::shell::parseLine(lambda::shell::serializeWindowManagerError("bad-request", "Malformed", 105));
  REQUIRE(error);
  CHECK(error->requestId == 105);
  CHECK(error->error.code == "bad-request");
}
