#include <Lambda/System/DBus.hpp>
#include <Lambda/System/Secrets.hpp>

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
    bus.requestName(lambda::system::SecretsService::serviceName);

    lambda::system::SecretsService secrets(bus);
    auto exports = secrets.exportObjects();

    std::cerr << "lambda-secrets: exported "
              << lambda::system::SecretsService::serviceInterfaceName
              << " on " << lambda::system::SecretsService::objectPath << "\n";

    while (gRunning.load()) {
      (void)bus.waitAndProcess(1000);
    }
    bus.flush();
    return 0;
  } catch (std::exception const& error) {
    std::cerr << "lambda-secrets: " << error.what() << "\n";
    return 1;
  }
#else
  std::cerr << "lambda-secrets: D-Bus support is not available in this build\n";
  return 1;
#endif
}
