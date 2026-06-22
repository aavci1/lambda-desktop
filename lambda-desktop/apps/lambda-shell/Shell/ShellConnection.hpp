#pragma once

#include "Shell/ShellModel.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace lambda_shell {

class ShellConnection {
public:
  using LineHandler = std::function<void(std::string_view line)>;

  ShellConnection();
  ~ShellConnection();

  ShellConnection(ShellConnection const&) = delete;
  ShellConnection& operator=(ShellConnection const&) = delete;

  bool connect();
  int fd() const noexcept { return fd_; }
  bool connected() const noexcept { return fd_ >= 0; }
  int lastErrorNumber() const noexcept { return lastErrorNumber_; }
  std::string const& lastErrorMessage() const noexcept { return lastErrorMessage_; }

  void sendLine(std::string const& line) const;
  void sendHello(std::uint64_t requestId = 0) const;
  void claimLauncherModal(std::uint64_t requestId = 0) const;
  void releaseLauncherModal(std::uint64_t requestId = 0) const;

  /// Drains readable bytes and invokes `handler` for each newline-delimited message.
  void dispatchReadable(LineHandler handler);

#ifdef LAMBDA_TESTING
  void adoptFdForTesting(int fd);
#endif

private:
  int fd_ = -1;
  int lastErrorNumber_ = 0;
  std::string lastErrorMessage_;
  std::string readBuffer_;
};

std::string shellSocketPath();
std::string compositorDisplayName();

} // namespace lambda_shell
