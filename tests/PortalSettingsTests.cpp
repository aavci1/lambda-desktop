#include <Lambda/System/PortalSettings.hpp>

#include "DBusTestHelpers.hpp"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <unistd.h>

namespace {

using lambda::testing::dbus::pollBus;
using lambda::testing::dbus::pumpUntil;
using lambda::testing::dbus::startPrivateBus;

} // namespace

TEST_CASE("Portal Settings support is compile-time declared") {
  CHECK((LAMBDA_HAS_DBUS == 0 || LAMBDA_HAS_DBUS == 1));
}

#if LAMBDA_HAS_DBUS

TEST_CASE("PortalSettingsService exports xdg-desktop-portal Settings backend methods") {
  auto privateBus = startPrivateBus();
  if (!privateBus) {
    MESSAGE("Skipping portal settings integration test because a private bus could not be started in this environment");
    return;
  }

  auto serviceBus = lambda::dbus::Bus::openAddress(privateBus->address);
  auto client = lambda::dbus::Bus::openAddress(privateBus->address);
  serviceBus.requestName(lambda::system::PortalSettingsService::serviceName);

  lambda::system::PortalSettingsState state{
      .colorScheme = lambda::system::PortalColorScheme::PreferDark,
      .accentColor = lambda::system::PortalAccentColor{.red = 0.25, .green = 0.5, .blue = 0.75},
      .highContrast = true,
      .reducedMotion = true,
  };
  lambda::system::PortalSettingsService service(serviceBus, state);
  auto objectSlot = service.exportObject();

  std::atomic<bool> running = true;
  std::thread serviceThread([&] {
    while (running.load()) {
      pollBus(serviceBus, 25);
    }
  });

  auto version = client.getProperty(lambda::dbus::PropertyAddress{
                                        .destination = lambda::system::PortalSettingsService::serviceName,
                                        .path = lambda::system::PortalSettingsService::objectPath,
                                        .interface = lambda::system::PortalSettingsService::interfaceName,
                                        .name = "version",
                                    },
                                    "u");
  CHECK(std::get<std::uint32_t>(version) == 1);

  auto readColorScheme = client.call(lambda::dbus::MethodCall{
      .destination = lambda::system::PortalSettingsService::serviceName,
      .path = lambda::system::PortalSettingsService::objectPath,
      .interface = lambda::system::PortalSettingsService::interfaceName,
      .member = "Read",
      .arguments = {std::string(lambda::system::PortalSettingsService::appearanceNamespace),
                    std::string("color-scheme")},
  });
  CHECK(std::get<std::uint32_t>(readColorScheme.readVariant("u")) == 1);

  auto readAccent = client.call(lambda::dbus::MethodCall{
      .destination = lambda::system::PortalSettingsService::serviceName,
      .path = lambda::system::PortalSettingsService::objectPath,
      .interface = lambda::system::PortalSettingsService::interfaceName,
      .member = "Read",
      .arguments = {std::string(lambda::system::PortalSettingsService::appearanceNamespace),
                    std::string("accent-color")},
  });
  auto accent = std::get<lambda::dbus::RgbColor>(readAccent.readVariant("(ddd)"));
  CHECK(accent.red == doctest::Approx(0.25));
  CHECK(accent.green == doctest::Approx(0.5));
  CHECK(accent.blue == doctest::Approx(0.75));

  auto readAll = client.call(lambda::dbus::MethodCall{
      .destination = lambda::system::PortalSettingsService::serviceName,
      .path = lambda::system::PortalSettingsService::objectPath,
      .interface = lambda::system::PortalSettingsService::interfaceName,
      .member = "ReadAll",
      .arguments = {lambda::dbus::StringArray{{lambda::system::PortalSettingsService::appearanceNamespace}}},
  });
  auto settings = readAll.readNamespacedVariantDictionary();
  auto appearance = settings.values.find(lambda::system::PortalSettingsService::appearanceNamespace);
  REQUIRE(appearance != settings.values.end());
  CHECK(std::get<std::uint32_t>(appearance->second.at("color-scheme")) == 1);
  CHECK(std::get<std::uint32_t>(appearance->second.at("contrast")) == 1);
  CHECK(std::get<std::uint32_t>(appearance->second.at("reduced-motion")) == 1);
  auto allAccent = std::get<lambda::dbus::RgbColor>(appearance->second.at("accent-color"));
  CHECK(allAccent.red == doctest::Approx(0.25));
  CHECK(allAccent.green == doctest::Approx(0.5));
  CHECK(allAccent.blue == doctest::Approx(0.75));

  std::uint32_t changedColorScheme = 0;
  auto signalSlot = client.matchSignal(
      lambda::dbus::SignalMatch{
          .sender = lambda::system::PortalSettingsService::serviceName,
          .path = lambda::system::PortalSettingsService::objectPath,
          .interface = lambda::system::PortalSettingsService::interfaceName,
          .member = "SettingChanged",
      },
      [&changedColorScheme](lambda::dbus::Message& message) {
        CHECK(message.readString() == lambda::system::PortalSettingsService::appearanceNamespace);
        CHECK(message.readString() == "color-scheme");
        changedColorScheme = std::get<std::uint32_t>(message.readVariant("u"));
      });

  state.colorScheme = lambda::system::PortalColorScheme::PreferLight;
  service.setState(state);
  service.emitChanged("color-scheme");
  serviceBus.flush();
  CHECK(pumpUntil(client,
                  [&] { return changedColorScheme == 2; },
                  std::chrono::milliseconds(500)));

  running = false;
  serviceThread.join();
}

#endif
