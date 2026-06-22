#include "Shell/ShellConnection.hpp"

#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <unistd.h>

namespace {

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

TEST_CASE("shell connection reports missing socket as clean connection failure") {
  ScopedEnv socketEnv("LAMBDA_SHELL_SOCKET");
  auto const path = std::filesystem::temp_directory_path() / "lambda-shell-missing-ipc-test.sock";
  auto const pathString = path.string();
  std::filesystem::remove(path);
  setenv("LAMBDA_SHELL_SOCKET", pathString.c_str(), 1);

  lambda_shell::ShellConnection connection;
  CHECK_FALSE(connection.connect());
  CHECK_FALSE(connection.connected());
  CHECK(connection.lastErrorNumber() != 0);
  CHECK(connection.lastErrorMessage().find(pathString) != std::string::npos);
}

TEST_CASE("shell connection marks peer close as disconnected without delivering partial messages") {
  int fds[2]{-1, -1};
  REQUIRE(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);

  lambda_shell::ShellConnection connection;
  connection.adoptFdForTesting(fds[0]);
  fds[0] = -1;

  std::string delivered;
  std::string const complete = "one\n";
  REQUIRE(write(fds[1], complete.data(), complete.size()) == static_cast<ssize_t>(complete.size()));
  std::string const partial = "two";
  REQUIRE(write(fds[1], partial.data(), partial.size()) == static_cast<ssize_t>(partial.size()));
  close(fds[1]);
  fds[1] = -1;

  connection.dispatchReadable([&](std::string_view line) { delivered.append(line); });

  CHECK(delivered == "one");
  CHECK_FALSE(connection.connected());
  CHECK(connection.lastErrorNumber() == 0);
  CHECK(connection.lastErrorMessage().find("disconnected") != std::string::npos);
}
