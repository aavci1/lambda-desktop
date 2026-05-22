#include "Shell/ShellIpc.hpp"

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
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

ShellIpc::ShellIpc() = default;

ShellIpc::~ShellIpc() {
  if (fd_ >= 0) {
    close(fd_);
    fd_ = -1;
  }
}

bool ShellIpc::connect() {
  if (fd_ >= 0) return true;
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) return false;
  fcntl(fd, F_SETFD, FD_CLOEXEC);
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  std::string const path = shellSocketPath();
  std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path.c_str());
  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    close(fd);
    return false;
  }
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0) {
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  }
  fd_ = fd;
  return true;
}

void ShellIpc::sendLine(std::string const& line) const {
  sendAll(fd_, line);
}

void ShellIpc::sendHello() const {
  sendLine("{\"type\":\"lambda.shell.hello\",\"protocolVersion\":1,\"shellVersion\":\"0.2.0\","
           "\"capabilities\":[\"topbar\",\"dock\",\"command-launcher\"]}");
}

void ShellIpc::claimLauncherModal() const {
  sendLine("{\"type\":\"lambda.windowManager.claimCommandLauncherModal\"}");
}

void ShellIpc::releaseLauncherModal() const {
  sendLine("{\"type\":\"lambda.windowManager.releaseCommandLauncherModal\"}");
}

void ShellIpc::dispatchReadable(LineHandler handler) {
  if (fd_ < 0 || !handler) return;
  char buffer[4096];
  for (;;) {
    ssize_t const n = read(fd_, buffer, sizeof(buffer));
    if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) break;
      close(fd_);
      fd_ = -1;
      break;
    }
    if (n == 0) {
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

} // namespace lambda_shell
