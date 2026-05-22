#pragma once

#include "Shell/ShellModel.hpp"

#include <functional>
#include <string>

namespace lambda_shell {

class ShellIpc {
public:
  using LineHandler = std::function<void(std::string_view line)>;

  ShellIpc();
  ~ShellIpc();

  ShellIpc(ShellIpc const&) = delete;
  ShellIpc& operator=(ShellIpc const&) = delete;

  bool connect();
  int fd() const noexcept { return fd_; }
  bool connected() const noexcept { return fd_ >= 0; }

  void sendLine(std::string const& line) const;
  void sendHello() const;
  void claimLauncherModal() const;
  void releaseLauncherModal() const;

  /// Drains readable bytes and invokes `handler` for each newline-delimited message.
  void dispatchReadable(LineHandler handler);

private:
  int fd_ = -1;
  std::string readBuffer_;
};

std::string shellSocketPath();
std::string compositorDisplayName();

} // namespace lambda_shell
