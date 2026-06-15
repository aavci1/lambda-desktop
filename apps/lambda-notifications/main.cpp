#include <Lambda/System/DBus.hpp>
#include <Lambda/System/Notifications.hpp>

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>

namespace {

std::atomic<bool> gRunning = true;

void handleSignal(int) {
  gRunning = false;
}

std::size_t historyLimitFromEnvironment() {
  char const* raw = std::getenv("LAMBDA_NOTIFICATIONS_HISTORY_LIMIT");
  if (!raw || !*raw) {
    return 100;
  }
  char* end = nullptr;
  long const value = std::strtol(raw, &end, 10);
  if (end == raw || *end != '\0' || value <= 0) {
    return 100;
  }
  return static_cast<std::size_t>(value);
}

bool dndFromEnvironment() {
  char const* raw = std::getenv("LAMBDA_NOTIFICATIONS_DND");
  if (!raw || !*raw) {
    return false;
  }
  return raw[0] == '1' || raw[0] == 't' || raw[0] == 'T' || raw[0] == 'y' || raw[0] == 'Y';
}

} // namespace

int main() {
#if LAMBDA_HAS_DBUS
  std::signal(SIGINT, handleSignal);
  std::signal(SIGTERM, handleSignal);

  try {
    auto bus = lambda::dbus::Bus::open(lambda::dbus::BusType::Session);
    bus.requestName(lambda::system::NotificationsService::serviceName);

    lambda::system::NotificationsService notifications(bus, historyLimitFromEnvironment());
    notifications.setDoNotDisturb(dndFromEnvironment());
    auto notificationSlot = notifications.exportObject();

    std::cerr << "lambda-notifications: exported "
              << lambda::system::NotificationsService::interfaceName
              << " on " << lambda::system::NotificationsService::objectPath << "\n";

    while (gRunning.load()) {
      (void)bus.waitAndProcess(1000);
    }
    bus.flush();
    return 0;
  } catch (std::exception const& error) {
    std::cerr << "lambda-notifications: " << error.what() << "\n";
    return 1;
  }
#else
  std::cerr << "lambda-notifications: D-Bus support is not available in this build\n";
  return 1;
#endif
}
