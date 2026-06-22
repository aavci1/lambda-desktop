#include "Shell/ShellConnection.hpp"

#include "Shell/ShellIpc.hpp"

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string_view>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace lambda_shell {

namespace {

std::string runtimePath(char const* name) {
  if (char const* runtimeDir = std::getenv("XDG_RUNTIME_DIR"); runtimeDir && *runtimeDir) {
    return std::string(runtimeDir) + "/" + name;
  }
  return std::string("/tmp/") + name;
}

void sendAll(int fd, std::string const& line) {
  if (fd < 0) return;
  std::string payload = line;
  payload.push_back('\n');
  char const* data = payload.data();
  std::size_t remaining = payload.size();
  while (remaining > 0) {
    ssize_t const written = write(fd, data, remaining);
    if (written < 0) {
      if (errno == EINTR) continue;
      return;
    }
    data += written;
    remaining -= static_cast<std::size_t>(written);
  }
}

std::string errnoMessage(std::string_view action, std::string_view target, int error) {
  std::string message(action);
  if (!target.empty()) {
    message.push_back(' ');
    message.append(target);
  }
  message.append(": ");
  message.append(std::strerror(error));
  return message;
}

} // namespace

std::string shellSocketPath() {
  if (char const* env = std::getenv("LAMBDA_SHELL_SOCKET"); env && *env) {
    return env;
  }
  return runtimePath("lambda-window-manager-shell.sock");
}

std::string compositorDisplayName() {
  if (char const* display = std::getenv("WAYLAND_DISPLAY"); display && *display) {
    return display;
  }
  FILE* file = std::fopen(runtimePath("lambda-window-manager-display").c_str(), "r");
  if (!file) return {};
  char buffer[128]{};
  std::fgets(buffer, sizeof(buffer), file);
  std::fclose(file);
  std::string name(buffer);
  while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) {
    name.pop_back();
  }
  return name;
}

ShellConnection::ShellConnection() = default;

ShellConnection::~ShellConnection() {
  if (fd_ >= 0) {
    close(fd_);
    fd_ = -1;
  }
}

bool ShellConnection::connect() {
  if (fd_ >= 0) return true;
  lastErrorNumber_ = 0;
  lastErrorMessage_.clear();
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    lastErrorNumber_ = errno;
    lastErrorMessage_ = errnoMessage("socket", {}, lastErrorNumber_);
    return false;
  }
  fcntl(fd, F_SETFD, FD_CLOEXEC);
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  std::string const path = shellSocketPath();
  if (path.size() >= sizeof(addr.sun_path)) {
    close(fd);
    lastErrorNumber_ = ENAMETOOLONG;
    lastErrorMessage_ = errnoMessage("connect", path, lastErrorNumber_);
    return false;
  }
  std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path.c_str());
  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    lastErrorNumber_ = errno;
    lastErrorMessage_ = errnoMessage("connect", path, lastErrorNumber_);
    close(fd);
    return false;
  }
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0) {
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  }
  fd_ = fd;
  lastErrorNumber_ = 0;
  lastErrorMessage_.clear();
  return true;
}

void ShellConnection::sendLine(std::string const& line) const {
  sendAll(fd_, line);
}

void ShellConnection::sendHello(std::uint64_t requestId) const {
  sendLine(lambda::shell::serializeShellHello(1, "0.2.0", {"dock", "command-launcher"}, requestId));
}

void ShellConnection::claimLauncherModal(std::uint64_t requestId) const {
  sendLine(lambda::shell::serializeClaimCommandLauncherModal(requestId));
}

void ShellConnection::releaseLauncherModal(std::uint64_t requestId) const {
  sendLine(lambda::shell::serializeReleaseCommandLauncherModal(requestId));
}

void ShellConnection::dispatchReadable(LineHandler handler) {
  if (fd_ < 0 || !handler) return;
  char buffer[4096];
  for (;;) {
    ssize_t const n = read(fd_, buffer, sizeof(buffer));
    if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) break;
      lastErrorNumber_ = errno;
      lastErrorMessage_ = errnoMessage("read", "lambda-window-manager shell IPC", lastErrorNumber_);
      close(fd_);
      fd_ = -1;
      break;
    }
    if (n == 0) {
      lastErrorNumber_ = 0;
      lastErrorMessage_ = "lambda-window-manager shell IPC disconnected";
      close(fd_);
      fd_ = -1;
      break;
    }
    readBuffer_.append(buffer, static_cast<std::size_t>(n));
    for (;;) {
      std::size_t const newline = readBuffer_.find('\n');
      if (newline == std::string::npos) break;
      std::string line = readBuffer_.substr(0, newline);
      readBuffer_.erase(0, newline + 1u);
      handler(line);
    }
  }
}

#ifdef LAMBDA_TESTING
void ShellConnection::adoptFdForTesting(int fd) {
  if (fd_ >= 0) {
    close(fd_);
  }
  fd_ = fd;
  lastErrorNumber_ = 0;
  lastErrorMessage_.clear();
  readBuffer_.clear();
}
#endif

} // namespace lambda_shell
