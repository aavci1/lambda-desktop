#include <Lambda/System/DBus.hpp>
#include <Lambda/System/PortalAccount.hpp>
#include <Lambda/System/PortalAppChooser.hpp>
#include <Lambda/System/PortalFileChooser.hpp>
#include <Lambda/System/PortalInhibit.hpp>
#include <Lambda/System/PortalNotification.hpp>
#include <Lambda/System/PortalSettings.hpp>

#include <atomic>
#include <csignal>
#include <exception>
#include <iostream>
#include <iterator>
#include <utility>

namespace {

std::atomic<bool> gRunning = true;

void handleSignal(int) {
  gRunning = false;
}

lambda::dbus::ObjectDefinition mergeDefinitions(lambda::dbus::ObjectDefinition first,
                                                lambda::dbus::ObjectDefinition second) {
  first.methods.insert(first.methods.end(),
                       std::make_move_iterator(second.methods.begin()),
                       std::make_move_iterator(second.methods.end()));
  first.properties.insert(first.properties.end(),
                          std::make_move_iterator(second.properties.begin()),
                          std::make_move_iterator(second.properties.end()));
  return first;
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
        lambda::system::PortalSettingsService::stateFromShellConfig());
    lambda::system::PortalAccountService account(bus);
    lambda::system::PortalAppChooserService appChooser(bus);
    lambda::system::PortalFileChooserService fileChooser(bus);
    lambda::system::PortalInhibitService inhibit(bus);
    lambda::system::PortalNotificationService notifications(bus);

    auto portalDefinition = mergeDefinitions(settings.objectDefinition(), account.objectDefinition());
    portalDefinition = mergeDefinitions(std::move(portalDefinition), appChooser.objectDefinition());
    portalDefinition = mergeDefinitions(std::move(portalDefinition), fileChooser.objectDefinition());
    portalDefinition = mergeDefinitions(std::move(portalDefinition), inhibit.objectDefinition());
    portalDefinition =
        mergeDefinitions(std::move(portalDefinition), notifications.objectDefinition());
    auto portalSlot = bus.exportObject(lambda::system::PortalSettingsService::objectPath,
                                       std::move(portalDefinition));
    auto notificationActionSlot = notifications.watchNotificationActions();

    std::cerr << "lambda-portal: exported portal backends"
              << " on " << lambda::system::PortalSettingsService::objectPath << "\n";

    while (gRunning.load()) {
      (void)bus.waitAndProcess(1000);
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
