#include "Compositor/Wayland/WaylandServerImpl.hpp"

#include "Shell/ShellAppRegistry.hpp"

#include "Shell/ShellIpc.hpp"

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

namespace lambdaui::compositor {
namespace {

std::string runtimePath(char const* name) {
  if (char const* runtimeDir = std::getenv("XDG_RUNTIME_DIR"); runtimeDir && *runtimeDir) {
    return std::string(runtimeDir) + "/" + name;
  }
  return std::string("/tmp/") + name;
}

void sendLine(int fd, std::string const& line) {
  if (fd < 0) return;
  std::string payload = line;
  payload.push_back('\n');
  char const* data = payload.data();
  std::size_t remaining = payload.size();
  while (remaining > 0) {
    ssize_t const written = write(fd, data, remaining);
    if (written < 0) {
      if (errno == EINTR) continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK) return;
      return;
    }
    data += written;
    remaining -= static_cast<std::size_t>(written);
  }
}

std::string appRegistryJson() {
  auto const apps = lambda_shell::buildDefaultAppRegistry(
      lambda_shell::defaultLocalLambdaAppDirs(),
      lambda_shell::defaultXdgApplicationDirs(),
      lambda_shell::executableInPath);
  std::string json = "[";
  bool first = true;
  for (auto const& app : apps) {
    if (app.hidden || app.noDisplay || app.command.empty()) continue;
    if (!first) json.push_back(',');
    first = false;
    json += "{\"id\":\"";
    json += lambdaui::shell::escapeJson(app.appId);
    json += "\",\"name\":\"";
    json += lambdaui::shell::escapeJson(app.name.empty() ? app.appId : app.name);
    json += "\",\"icon\":\"";
    json += lambdaui::shell::escapeJson(app.icon);
    json += "\",\"command\":\"";
    json += lambdaui::shell::escapeJson(app.command);
    json += "\"}";
  }
  json += "]";
  return json;
}

std::string windowStateJson(WaylandServer::Impl const* server, WaylandServer::Impl::Surface const* surface) {
  auto* toplevel = toplevelForSurface(const_cast<WaylandServer::Impl*>(server),
                                      const_cast<WaylandServer::Impl::Surface*>(surface));
  std::string state = "normal";
  if (surface->minimized) state = "minimized";
  else if (surface->fullscreen) state = "fullscreen";
  else if (surface->maximized) state = "maximized";
  std::string const appId = toplevel && !toplevel->appId.empty() ? toplevel->appId : "unknown";
  std::string const title = toplevel && !toplevel->title.empty() ? toplevel->title : appId;
  return "{\"id\":" + std::to_string(surface->id) +
         ",\"appId\":\"" + lambdaui::shell::escapeJson(appId) +
         "\",\"title\":\"" + lambdaui::shell::escapeJson(title) +
         "\",\"outputId\":\"" + lambdaui::shell::escapeJson(server->output_.name) +
         "\",\"state\":\"" + state +
         "\",\"focused\":" + (server->keyboardFocus_ == surface ? "true" : "false") +
         ",\"attention\":false}";
}

std::string desktopSnapshotJson(WaylandServer::Impl const* server) {
  std::string json = "{\"type\":\"lambda.windowManager.snapshot\",\"outputs\":[{\"id\":\"" +
                     lambdaui::shell::escapeJson(server->output_.name) + "\",\"name\":\"" +
                     lambdaui::shell::escapeJson(server->output_.name) + "\",\"width\":" +
                     std::to_string(server->logicalOutputWidth()) + ",\"height\":" +
                     std::to_string(server->logicalOutputHeight()) + ",\"scale\":" +
                     std::to_string(server->preferredScale()) + "}],\"apps\":" +
                     appRegistryJson() + ",\"windows\":[";
  bool first = true;
  for (auto const& surface : server->surfaces_) {
    if (!surface || !surfaceIsXdgToplevel(surface.get())) continue;
    if (!first) json.push_back(',');
    first = false;
    json += windowStateJson(server, surface.get());
  }
  json += "],\"activeWindowId\":";
  json += server->keyboardFocus_ && surfaceIsXdgToplevel(server->keyboardFocus_)
              ? std::to_string(server->keyboardFocus_->id)
              : "null";
  json += "}";
  return json;
}

void closeShellClient(WaylandServer::Impl* server) {
  if (server->shellClientFd_ >= 0) close(server->shellClientFd_);
  server->shellClientFd_ = -1;
  server->shellReadBuffer_.clear();
  server->shellHelloReceived_ = false;
  server->shellSnapshotDirty_ = false;
}

void sendWelcome(WaylandServer::Impl* server) {
  sendLine(server->shellClientFd_,
           "{\"type\":\"lambda.windowManager.welcome\",\"protocolVersion\":1,"
           "\"sessionId\":\"lambda-session\",\"outputs\":[],"
           "\"theme\":{\"mode\":\"system\",\"accent\":\"#2a7fff\"}}");
  sendLine(server->shellClientFd_, desktopSnapshotJson(server));
}

void handleShellLine(WaylandServer::Impl* server, std::string_view line) {
  auto message = lambdaui::shell::parseLine(line);
  if (!message) return;

  switch (message->kind) {
  case lambdaui::shell::ShellMessageKind::ShellHello:
    server->shellHelloReceived_ = true;
    sendWelcome(server);
    return;
  case lambdaui::shell::ShellMessageKind::WindowManagerLaunchApp:
    if (!server->launchShellApp(message->launchApp.appId)) {
      sendLine(server->shellClientFd_,
               lambdaui::shell::serializeWindowManagerError("not-found", "app is not launchable", message->requestId));
    }
    return;
  case lambdaui::shell::ShellMessageKind::WindowManagerFocusApp:
    if (!server->focusShellApp(message->focusApp.appId, 0)) {
      sendLine(server->shellClientFd_,
               lambdaui::shell::serializeWindowManagerError(
                   "not-found", "app has no running windows", message->requestId));
    }
    return;
  case lambdaui::shell::ShellMessageKind::WindowManagerFocusWindow:
    if (!server->focusShellWindow(message->focusWindow.windowId, 0)) {
      sendLine(server->shellClientFd_,
               lambdaui::shell::serializeWindowManagerError("not-found", "window not found", message->requestId));
    }
    return;
  case lambdaui::shell::ShellMessageKind::WindowManagerQuitApp:
    if (!server->quitShellApp(message->quitApp.appId)) {
      sendLine(server->shellClientFd_,
               lambdaui::shell::serializeWindowManagerError(
                   "not-found", "app has no running windows", message->requestId));
    }
    return;
  case lambdaui::shell::ShellMessageKind::WindowManagerClaimCommandLauncherModal:
    server->claimCommandLauncherModal(0);
    return;
  case lambdaui::shell::ShellMessageKind::WindowManagerReleaseCommandLauncherModal:
    server->releaseCommandLauncherModal(0);
    return;
  case lambdaui::shell::ShellMessageKind::ShellRefreshState:
    sendLine(server->shellClientFd_, desktopSnapshotJson(server));
    return;
  default:
    return;
  }
}

} // namespace

int WaylandServer::Impl::shellIpcFd() const noexcept {
  return shellClientFd_ >= 0 ? shellClientFd_ : shellListenFd_;
}

void WaylandServer::Impl::initializeShellIpc() {
  shellSocketPath_ = runtimePath("lambda-window-manager-shell.sock");
  shellListenFd_ = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (shellListenFd_ < 0) {
    std::fprintf(stderr, "lambda-window-manager: shell IPC socket failed: %s\n", std::strerror(errno));
    return;
  }
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", shellSocketPath_.c_str());
  unlink(shellSocketPath_.c_str());
  if (bind(shellListenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
      listen(shellListenFd_, 1) != 0) {
    std::fprintf(stderr, "lambda-window-manager: shell IPC bind failed: %s\n", std::strerror(errno));
    close(shellListenFd_);
    shellListenFd_ = -1;
    return;
  }
  chmod(shellSocketPath_.c_str(), 0600);
  setenv("LAMBDA_SHELL_SOCKET", shellSocketPath_.c_str(), 1);
  std::fprintf(stderr, "lambda-window-manager: shell IPC %s\n", shellSocketPath_.c_str());
}

void WaylandServer::Impl::shutdownShellIpc() {
  closeShellClient(this);
  if (shellListenFd_ >= 0) close(shellListenFd_);
  shellListenFd_ = -1;
  if (!shellSocketPath_.empty()) unlink(shellSocketPath_.c_str());
}

void WaylandServer::Impl::dispatchShellIpc() {
  if (shellListenFd_ >= 0 && shellClientFd_ < 0) {
    int fd = accept4(shellListenFd_, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (fd >= 0) {
      shellClientFd_ = fd;
      shellReadBuffer_.clear();
      shellHelloReceived_ = false;
      std::fprintf(stderr, "lambda-window-manager: lambda-shell connected\n");
    }
  }
  if (shellClientFd_ < 0) return;

  char buffer[4096];
  for (;;) {
    ssize_t const readBytes = read(shellClientFd_, buffer, sizeof(buffer));
    if (readBytes < 0) {
      if (errno == EINTR) continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK) break;
      closeShellClient(this);
      return;
    }
    if (readBytes == 0) {
      closeShellClient(this);
      return;
    }
    shellReadBuffer_.append(buffer, static_cast<std::size_t>(readBytes));
    for (;;) {
      std::size_t const newline = shellReadBuffer_.find('\n');
      if (newline == std::string_view::npos) break;
      std::string line = shellReadBuffer_.substr(0, newline);
      shellReadBuffer_.erase(0, newline + 1u);
      handleShellLine(this, line);
    }
  }
  if (shellHelloReceived_ && shellSnapshotDirty_) {
    shellSnapshotDirty_ = false;
    sendLine(shellClientFd_, desktopSnapshotJson(this));
  }
}

void WaylandServer::Impl::requestShellOpenCommandLauncher() {
  if (shellClientFd_ < 0 || !shellHelloReceived_) {
    std::fprintf(stderr, "lambda-window-manager: lambda-shell is not connected; cannot open command launcher\n");
    return;
  }
  sendLine(shellClientFd_, lambdaui::shell::serializeOpenCommandLauncher());
}

void WaylandServer::Impl::notifyShellStateChanged() {
  shellSnapshotDirty_ = true;
}

} // namespace lambdaui::compositor
