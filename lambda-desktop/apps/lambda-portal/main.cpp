#include <Lambda/System/DBus.hpp>
#include <Lambda/System/PortalAccount.hpp>
#include <Lambda/System/PortalAppChooser.hpp>
#include <Lambda/System/PortalFileChooser.hpp>
#include <Lambda/System/PortalInhibit.hpp>
#include <Lambda/System/PortalNotification.hpp>
#include <Lambda/System/PortalScreenCast.hpp>
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

lambdaui::dbus::ObjectDefinition mergeDefinitions(lambdaui::dbus::ObjectDefinition first,
                                                lambdaui::dbus::ObjectDefinition second) {
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
    auto bus = lambdaui::dbus::Bus::open(lambdaui::dbus::BusType::Session);
    bus.requestName(lambdaui::system::PortalSettingsService::serviceName);

    lambdaui::system::PortalSettingsService settings(
        bus,
        lambdaui::system::PortalSettingsService::stateFromShellConfig());
    lambdaui::system::PortalAccountService account(bus);
    lambdaui::system::PortalAppChooserService appChooser(bus);
    lambdaui::system::PortalFileChooserService fileChooser(bus);
    lambdaui::system::PortalInhibitService inhibit(bus);
    lambdaui::system::PortalNotificationService notifications(bus);
    lambdaui::system::PortalScreenCastService screenCast(bus);

    auto portalDefinition = mergeDefinitions(settings.objectDefinition(), account.objectDefinition());
    portalDefinition = mergeDefinitions(std::move(portalDefinition), appChooser.objectDefinition());
    portalDefinition = mergeDefinitions(std::move(portalDefinition), fileChooser.objectDefinition());
    portalDefinition = mergeDefinitions(std::move(portalDefinition), inhibit.objectDefinition());
    portalDefinition =
        mergeDefinitions(std::move(portalDefinition), notifications.objectDefinition());
    portalDefinition = mergeDefinitions(std::move(portalDefinition), screenCast.objectDefinition());
    auto portalSlot = bus.exportObject(lambdaui::system::PortalSettingsService::objectPath,
                                       std::move(portalDefinition));
    auto notificationActionSlot = notifications.watchNotificationActions();

    std::cerr << "lambda-portal: exported portal backends"
              << " on " << lambdaui::system::PortalSettingsService::objectPath << "\n";

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
