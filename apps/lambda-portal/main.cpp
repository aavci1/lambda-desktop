#include <Lambda/System/DBus.hpp>
#include <Lambda/System/PortalSettings.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <exception>
#include <iostream>
#include <poll.h>
#include <thread>

namespace {

std::atomic<bool> gRunning = true;

void handleSignal(int) {
  gRunning = false;
}

void waitForBus(lambda::dbus::Bus& bus, int timeoutMs) {
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

} // namespace

int main() {
#if LAMBDA_HAS_DBUS
  std::signal(SIGINT, handleSignal);
  std::signal(SIGTERM, handleSignal);

  try {
    auto bus = lambda::dbus::Bus::open(lambda::dbus::BusType::Session);
    bus.requestName(lambda::system::PortalSettingsService::serviceName);

    lambda::system::PortalSettingsService settings(
        bus,
        lambda::system::PortalSettingsService::stateFromEnvironment());
    auto settingsSlot = settings.exportObject();

    std::cerr << "lambda-portal: exported " << lambda::system::PortalSettingsService::interfaceName
              << " on " << lambda::system::PortalSettingsService::objectPath << "\n";

    while (gRunning.load()) {
      waitForBus(bus, 1000);
    }
    bus.flush();
    return 0;
  } catch (std::exception const& error) {
    std::cerr << "lambda-portal: " << error.what() << "\n";
    return 1;
  }
#else
  std::cerr << "lambda-portal: D-Bus support is not available in this build\n";
  return 1;
#endif
}
