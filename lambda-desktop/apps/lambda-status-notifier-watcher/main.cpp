#include <Lambda/System/DBus.hpp>
#include <Lambda/System/StatusNotifierWatcher.hpp>

#include <atomic>
#include <csignal>
#include <exception>
#include <iostream>

namespace {

std::atomic<bool> gRunning = true;

void handleSignal(int) {
  gRunning = false;
}

} // namespace

int main() {
#if LAMBDA_HAS_DBUS
  std::signal(SIGINT, handleSignal);
  std::signal(SIGTERM, handleSignal);

  try {
    auto bus = lambda::dbus::Bus::open(lambda::dbus::BusType::Session);
    bus.requestName(lambda::system::StatusNotifierWatcherService::serviceName);

    lambda::system::StatusNotifierWatcherService watcher(bus);
    auto objectSlot = watcher.exportObject();
    auto ownerSlot = watcher.watchNameOwners();

    std::cerr << "lambda-status-notifier-watcher: exported "
              << lambda::system::StatusNotifierWatcherService::interfaceName
              << " on " << lambda::system::StatusNotifierWatcherService::objectPath << "\n";

    while (gRunning.load()) {
      (void)bus.waitAndProcess(1000);
    }
    bus.flush();
    return 0;
  } catch (std::exception const& error) {
    std::cerr << "lambda-status-notifier-watcher: " << error.what() << "\n";
    return 1;
  }
#else
  std::cerr << "lambda-status-notifier-watcher: D-Bus support is not available in this build\n";
  return 1;
#endif
}
