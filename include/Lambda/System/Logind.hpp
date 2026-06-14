#pragma once

/// \file Lambda/System/Logind.hpp
///
/// Minimal logind client used by Lambda session services.

#include <Lambda/System/DBus.hpp>

#include <cstdint>
#include <functional>
#include <string>

namespace lambda::system {

class InhibitorLock {
public:
  InhibitorLock() = default;
  explicit InhibitorLock(dbus::UnixFd fd);

  InhibitorLock(InhibitorLock const&) = delete;
  InhibitorLock& operator=(InhibitorLock const&) = delete;
  InhibitorLock(InhibitorLock&&) noexcept = default;
  InhibitorLock& operator=(InhibitorLock&&) noexcept = default;

  [[nodiscard]] bool valid() const noexcept { return fd_.valid(); }
  [[nodiscard]] int fd() const noexcept { return fd_.get(); }
  [[nodiscard]] int release() noexcept { return fd_.release(); }

private:
  dbus::UnixFd fd_;
};

class LogindClient {
public:
  explicit LogindClient(dbus::Bus bus);

  [[nodiscard]] static LogindClient connectSystem();

  [[nodiscard]] dbus::Bus& bus() noexcept { return bus_; }
  [[nodiscard]] dbus::Bus const& bus() const noexcept { return bus_; }

  void suspend(bool interactive = true);
  void hibernate(bool interactive = true);
  void powerOff(bool interactive = true);
  void reboot(bool interactive = true);

  [[nodiscard]] InhibitorLock inhibit(std::string what,
                                      std::string who,
                                      std::string why,
                                      std::string mode);

  [[nodiscard]] std::string sessionPathForPid(std::uint32_t pid);
  [[nodiscard]] std::string currentSessionPath();
  [[nodiscard]] dbus::Slot watchPrepareForSleep(std::function<void(bool)> handler);
  [[nodiscard]] dbus::Slot watchCurrentSessionLock(std::function<void()> handler);
  [[nodiscard]] dbus::Slot watchCurrentSessionUnlock(std::function<void()> handler);
  [[nodiscard]] dbus::Slot watchSessionLock(std::string const& sessionPath,
                                            std::function<void()> handler);
  [[nodiscard]] dbus::Slot watchSessionUnlock(std::string const& sessionPath,
                                              std::function<void()> handler);

private:
  void callManagerPowerMethod(std::string const& member, bool interactive);
  [[nodiscard]] dbus::Slot watchSessionSignal(std::string const& sessionPath,
                                              std::string const& member,
                                              std::function<void()> handler);

  dbus::Bus bus_;
};

} // namespace lambda::system
