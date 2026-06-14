#pragma once

#include <Lambda/System/DBus.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <optional>
#include <poll.h>
#include <signal.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <utility>

namespace lambda::testing::dbus {

inline std::string trimLine(std::string line) {
  while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
    line.pop_back();
  }
  return line;
}

struct PrivateBus {
  std::string address;
  pid_t pid = -1;

  PrivateBus() = default;
  PrivateBus(PrivateBus const&) = delete;
  PrivateBus& operator=(PrivateBus const&) = delete;

  PrivateBus(PrivateBus&& other) noexcept
      : address(std::move(other.address)),
        pid(std::exchange(other.pid, -1)) {}

  PrivateBus& operator=(PrivateBus&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    if (pid > 0) {
      kill(pid, SIGTERM);
    }
    address = std::move(other.address);
    pid = std::exchange(other.pid, -1);
    return *this;
  }

  ~PrivateBus() {
    if (pid > 0) {
      kill(pid, SIGTERM);
    }
  }
};

inline std::optional<PrivateBus> startPrivateBus() {
#if LAMBDA_HAS_DBUS
  if (access("/usr/bin/dbus-daemon", X_OK) != 0) {
    return std::nullopt;
  }
  FILE* pipe =
      popen("/usr/bin/dbus-daemon --session --fork --print-address=1 --print-pid=1 2>/dev/null", "r");
  if (!pipe) {
    return std::nullopt;
  }

  char addressBuffer[1024] = {};
  char pidBuffer[64] = {};
  bool const readAddress = std::fgets(addressBuffer, sizeof(addressBuffer), pipe) != nullptr;
  bool const readPid = std::fgets(pidBuffer, sizeof(pidBuffer), pipe) != nullptr;
  int const closeStatus = pclose(pipe);
  if (!readAddress || !readPid || closeStatus != 0) {
    return std::nullopt;
  }

  PrivateBus bus;
  bus.address = trimLine(addressBuffer);
  bus.pid = static_cast<pid_t>(std::strtol(pidBuffer, nullptr, 10));
  if (bus.address.empty() || bus.pid <= 0) {
    return std::nullopt;
  }
  return bus;
#else
  return std::nullopt;
#endif
}

inline void pollBus(lambda::dbus::Bus& bus, int timeoutMs) {
  pollfd fd{
      .fd = bus.eventFileDescriptor(),
      .events = static_cast<short>(bus.eventMask()),
      .revents = 0,
  };
  if (fd.fd >= 0) {
    (void)poll(&fd, 1, timeoutMs);
  } else {
    std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
  }
  (void)bus.processPending();
}

inline bool pumpUntil(lambda::dbus::Bus& bus,
                      std::function<bool()> done,
                      std::chrono::milliseconds timeout) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (!done() && std::chrono::steady_clock::now() < deadline) {
    pollBus(bus, 10);
  }
  return done();
}

} // namespace lambda::testing::dbus
