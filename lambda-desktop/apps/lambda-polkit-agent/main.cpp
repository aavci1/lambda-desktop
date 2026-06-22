#include <Lambda/System/DBus.hpp>
#include <Lambda/System/PolkitAgent.hpp>

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
    auto bus = lambda::dbus::Bus::open(lambda::dbus::BusType::System);

    lambda::system::PolkitAuthenticationAgentService agent(bus);
    auto agentSlot = agent.exportObject();
    agent.registerAgent();

    std::cerr << "lambda-polkit-agent: registered "
              << lambda::system::PolkitAuthenticationAgentService::agentInterfaceName
              << " on " << agent.objectPath() << "\n";

    while (gRunning.load()) {
      (void)bus.waitAndProcess(1000);
    }

    try {
      agent.unregisterAgent();
    } catch (std::exception const& error) {
      std::cerr << "lambda-polkit-agent: unregister failed: " << error.what() << "\n";
    }
    bus.flush();
    return 0;
  } catch (std::exception const& error) {
    std::cerr << "lambda-polkit-agent: " << error.what() << "\n";
    return 1;
  }
#else
  std::cerr << "lambda-polkit-agent: D-Bus support is not available in this build\n";
  return 1;
#endif
}
