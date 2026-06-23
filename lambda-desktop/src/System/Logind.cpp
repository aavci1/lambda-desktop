#include <Lambda/System/Logind.hpp>

#include <unistd.h>

#include <utility>

namespace lambdaui::system {

namespace {

constexpr char kLogin1Service[] = "org.freedesktop.login1";
constexpr char kManagerPath[] = "/org/freedesktop/login1";
constexpr char kManagerInterface[] = "org.freedesktop.login1.Manager";
constexpr char kSessionInterface[] = "org.freedesktop.login1.Session";

} // namespace

InhibitorLock::InhibitorLock(dbus::UnixFd fd) : fd_(std::move(fd)) {}

LogindClient::LogindClient(dbus::Bus bus) : bus_(std::move(bus)) {}

LogindClient LogindClient::connectSystem() {
  return LogindClient(dbus::Bus::open(dbus::BusType::System));
}

void LogindClient::suspend(bool interactive) {
  callManagerPowerMethod("Suspend", interactive);
}

void LogindClient::hibernate(bool interactive) {
  callManagerPowerMethod("Hibernate", interactive);
}

void LogindClient::powerOff(bool interactive) {
  callManagerPowerMethod("PowerOff", interactive);
}

void LogindClient::reboot(bool interactive) {
  callManagerPowerMethod("Reboot", interactive);
}

void LogindClient::lockCurrentSession() {
  callCurrentSessionMethod("Lock");
}

void LogindClient::terminateCurrentSession() {
  callCurrentSessionMethod("Terminate");
}

InhibitorLock LogindClient::inhibit(std::string what, std::string who, std::string why, std::string mode) {
  dbus::Message reply = bus_.call(dbus::MethodCall{
      .destination = kLogin1Service,
      .path = kManagerPath,
      .interface = kManagerInterface,
      .member = "Inhibit",
      .arguments = {std::move(what), std::move(who), std::move(why), std::move(mode)},
  });
  return InhibitorLock(reply.readUnixFd());
}

std::string LogindClient::sessionPathForPid(std::uint32_t pid) {
  dbus::Message reply = bus_.call(dbus::MethodCall{
      .destination = kLogin1Service,
      .path = kManagerPath,
      .interface = kManagerInterface,
      .member = "GetSessionByPID",
      .arguments = {pid},
  });
  return reply.readObjectPath().value;
}

std::string LogindClient::currentSessionPath() {
  return sessionPathForPid(static_cast<std::uint32_t>(getpid()));
}

dbus::Slot LogindClient::watchPrepareForSleep(std::function<void(bool)> handler) {
  return bus_.matchSignal(
      dbus::SignalMatch{
          .sender = kLogin1Service,
          .path = kManagerPath,
          .interface = kManagerInterface,
          .member = "PrepareForSleep",
      },
      [handler = std::move(handler)](dbus::Message& message) mutable {
        if (handler) {
          handler(message.readBool());
        }
      });
}

dbus::Slot LogindClient::watchCurrentSessionLock(std::function<void()> handler) {
  return watchSessionLock(currentSessionPath(), std::move(handler));
}

dbus::Slot LogindClient::watchCurrentSessionUnlock(std::function<void()> handler) {
  return watchSessionUnlock(currentSessionPath(), std::move(handler));
}

dbus::Slot LogindClient::watchSessionLock(std::string const& sessionPath,
                                          std::function<void()> handler) {
  return watchSessionSignal(sessionPath, "Lock", std::move(handler));
}

dbus::Slot LogindClient::watchSessionUnlock(std::string const& sessionPath,
                                            std::function<void()> handler) {
  return watchSessionSignal(sessionPath, "Unlock", std::move(handler));
}

void LogindClient::callManagerPowerMethod(std::string const& member, bool interactive) {
  (void)bus_.call(dbus::MethodCall{
      .destination = kLogin1Service,
      .path = kManagerPath,
      .interface = kManagerInterface,
      .member = member,
      .arguments = {interactive},
  });
}

void LogindClient::callCurrentSessionMethod(std::string const& member) {
  (void)bus_.call(dbus::MethodCall{
      .destination = kLogin1Service,
      .path = currentSessionPath(),
      .interface = kSessionInterface,
      .member = member,
      .arguments = {},
  });
}

dbus::Slot LogindClient::watchSessionSignal(std::string const& sessionPath,
                                            std::string const& member,
                                            std::function<void()> handler) {
  return bus_.matchSignal(
      dbus::SignalMatch{
          .sender = kLogin1Service,
          .path = sessionPath,
          .interface = kSessionInterface,
          .member = member,
      },
      [handler = std::move(handler)](dbus::Message&) mutable {
        if (handler) {
          handler();
        }
      });
}

} // namespace lambdaui::system
