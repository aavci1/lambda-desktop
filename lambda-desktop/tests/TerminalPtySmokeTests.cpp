#include "TerminalCore.hpp"

#include <doctest/doctest.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif

namespace {

std::string readAvailable(int fd) {
  std::string output;
  char buffer[256]{};
  auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (std::chrono::steady_clock::now() < deadline) {
    ssize_t const n = read(fd, buffer, sizeof(buffer));
    if (n > 0) {
      output.append(buffer, static_cast<std::size_t>(n));
      if (output.find("lambda-pty-ready") != std::string::npos) break;
      continue;
    }
    if (n == 0) break;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }
    break;
  }
  return output;
}

void pushPtyOutput(lambda_terminal::TerminalTextBuffer& buffer, std::string const& output) {
  std::string line;
  for (char ch : output) {
    if (ch == '\r') continue;
    if (ch == '\n') {
      buffer.pushLine(line);
      line.clear();
      continue;
    }
    line.push_back(ch);
  }
  if (!line.empty()) buffer.pushLine(line);
}

} // namespace

TEST_CASE("terminal pty smoke feeds controlled child output into model") {
  int master = -1;
  pid_t const child = forkpty(&master, nullptr, nullptr, nullptr);
  REQUIRE(child >= 0);
  if (child == 0) {
    execl("/bin/sh", "sh", "-c", "printf 'lambda-pty-ready\\n'", static_cast<char*>(nullptr));
    _exit(127);
  }

  int flags = fcntl(master, F_GETFL, 0);
  if (flags >= 0) {
    fcntl(master, F_SETFL, flags | O_NONBLOCK);
  }

  std::string const output = readAvailable(master);
  int status = 0;
  waitpid(child, &status, 0);
  close(master);

  REQUIRE(output.find("lambda-pty-ready") != std::string::npos);
  lambda_terminal::TerminalTextBuffer buffer{4, 20};
  pushPtyOutput(buffer, output);
  CHECK(buffer.viewportLines() == std::vector<std::string>{"lambda-pty-ready"});
  CHECK(WIFEXITED(status));
  CHECK(WEXITSTATUS(status) == 0);
}

TEST_CASE("terminal child cleanup reaps exited child") {
  pid_t const child = fork();
  REQUIRE(child >= 0);
  if (child == 0) {
    _exit(7);
  }

  std::this_thread::sleep_for(std::chrono::milliseconds{20});
  auto result = lambda_terminal::cleanupTerminalChildProcess(child, std::chrono::milliseconds{20});
  CHECK(result.reaped);
  CHECK(result.exited);
  CHECK(result.exitStatus == 7);
  CHECK_FALSE(result.sentKill);
}

TEST_CASE("terminal child cleanup escalates when hangup is ignored") {
  int readyPipe[2]{-1, -1};
  REQUIRE(pipe(readyPipe) == 0);
  pid_t const child = fork();
  REQUIRE(child >= 0);
  if (child == 0) {
    close(readyPipe[0]);
    std::signal(SIGHUP, SIG_IGN);
    char const ready = '1';
    (void)write(readyPipe[1], &ready, 1);
    close(readyPipe[1]);
    for (;;) {
      pause();
    }
  }
  close(readyPipe[1]);
  char ready = 0;
  REQUIRE(read(readyPipe[0], &ready, 1) == 1);
  close(readyPipe[0]);

  auto result = lambda_terminal::cleanupTerminalChildProcess(child, std::chrono::milliseconds{20});
  CHECK(result.reaped);
  CHECK(result.sentHangup);
  CHECK(result.sentKill);
  CHECK(result.signaled);
  CHECK(result.termSignal == SIGKILL);
}
