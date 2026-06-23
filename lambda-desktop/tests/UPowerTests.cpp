#include <Lambda/System/UPower.hpp>

#include "DBusTestHelpers.hpp"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <utility>

namespace {

using lambdaui::testing::dbus::pollBus;
using lambdaui::testing::dbus::pumpUntil;
using lambdaui::testing::dbus::startPrivateBus;

constexpr char kDisplayDevicePath[] = "/org/freedesktop/UPower/devices/DisplayDevice";

template <typename Callback>
class ScopeExit {
public:
  explicit ScopeExit(Callback callback) : callback_(std::move(callback)) {}
  ~ScopeExit() { callback_(); }

private:
  Callback callback_;
};

} // namespace

TEST_CASE("UPower support is compile-time declared") {
  CHECK((LAMBDA_HAS_DBUS == 0 || LAMBDA_HAS_DBUS == 1));
}

#if LAMBDA_HAS_DBUS

TEST_CASE("UPowerClient reads display device state and watches changes") {
  auto privateBus = startPrivateBus();
  if (!privateBus) {
    MESSAGE("Skipping UPower integration test because a private bus could not be started");
    return;
  }

  auto service = lambdaui::dbus::Bus::openAddress(privateBus->address);
  lambdaui::system::UPowerClient client(lambdaui::dbus::Bus::openAddress(privateBus->address));
  service.requestName(lambdaui::system::UPowerClient::serviceName);

  bool onBattery = true;
  bool present = true;
  double percentage = 87.6;
  std::uint32_t state = static_cast<std::uint32_t>(lambdaui::system::UPowerDeviceState::Discharging);
  std::uint32_t warningLevel = static_cast<std::uint32_t>(lambdaui::system::UPowerWarningLevel::Low);
  std::int64_t timeToEmpty = 7200;
  std::int64_t timeToFull = 0;
  std::string iconName = "battery-good-symbolic";

  auto managerSlot = service.exportObject(
      lambdaui::system::UPowerClient::objectPath,
      lambdaui::dbus::ObjectDefinition{
          .methods = {
              lambdaui::dbus::ExportedMethod{
                  .interface = lambdaui::system::UPowerClient::interfaceName,
                  .member = "GetDisplayDevice",
                  .handler = [](lambdaui::dbus::Message&) {
                    return lambdaui::dbus::MethodReply{
                        .values = {lambdaui::dbus::ObjectPath{kDisplayDevicePath}},
                    };
                  },
              },
          },
          .properties = {
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::UPowerClient::interfaceName,
                  .name = "OnBattery",
                  .value = true,
                  .writable = false,
                  .getter = [&] { return lambdaui::dbus::BasicValue(onBattery); },
                  .setter = nullptr,
              },
          },
      });

  auto displaySlot = service.exportObject(
      kDisplayDevicePath,
      lambdaui::dbus::ObjectDefinition{
          .methods = {},
          .properties = {
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::UPowerClient::deviceInterfaceName,
                  .name = "IsPresent",
                  .value = true,
                  .writable = false,
                  .getter = [&] { return lambdaui::dbus::BasicValue(present); },
                  .setter = nullptr,
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::UPowerClient::deviceInterfaceName,
                  .name = "Percentage",
                  .value = 0.0,
                  .writable = false,
                  .getter = [&] { return lambdaui::dbus::BasicValue(percentage); },
                  .setter = nullptr,
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::UPowerClient::deviceInterfaceName,
                  .name = "State",
                  .value = std::uint32_t(0),
                  .writable = false,
                  .getter = [&] { return lambdaui::dbus::BasicValue(state); },
                  .setter = nullptr,
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::UPowerClient::deviceInterfaceName,
                  .name = "TimeToEmpty",
                  .value = std::int64_t(0),
                  .writable = false,
                  .getter = [&] { return lambdaui::dbus::BasicValue(timeToEmpty); },
                  .setter = nullptr,
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::UPowerClient::deviceInterfaceName,
                  .name = "WarningLevel",
                  .value = std::uint32_t(0),
                  .writable = false,
                  .getter = [&] { return lambdaui::dbus::BasicValue(warningLevel); },
                  .setter = nullptr,
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::UPowerClient::deviceInterfaceName,
                  .name = "TimeToFull",
                  .value = std::int64_t(0),
                  .writable = false,
                  .getter = [&] { return lambdaui::dbus::BasicValue(timeToFull); },
                  .setter = nullptr,
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::UPowerClient::deviceInterfaceName,
                  .name = "IconName",
                  .value = std::string{},
                  .writable = false,
                  .getter = [&] { return lambdaui::dbus::BasicValue(iconName); },
                  .setter = nullptr,
              },
          },
      });

  std::atomic<bool> running = true;
  std::thread serviceThread([&] {
    while (running.load()) {
      pollBus(service, 25);
    }
  });
  ScopeExit stopServiceThread([&] {
    running = false;
    if (serviceThread.joinable()) {
      serviceThread.join();
    }
  });

  CHECK(client.displayDevicePath() == kDisplayDevicePath);
  auto display = client.readDisplayDevice();
  CHECK(display.present);
  CHECK(display.onBattery);
  CHECK(display.percentage == doctest::Approx(87.6));
  CHECK(display.state == lambdaui::system::UPowerDeviceState::Discharging);
  CHECK(display.warningLevel == lambdaui::system::UPowerWarningLevel::Low);
  CHECK(display.timeToEmptySeconds == 7200);
  CHECK(display.timeToFullSeconds == 0);
  CHECK(display.iconName == "battery-good-symbolic");
  CHECK(lambdaui::system::formatUPowerBatteryStatus(display) == "88%");

  int displayChanges = 0;
  auto displayChangedSlot = client.watchDisplayDeviceChanged([&] {
    ++displayChanges;
  });

  int statusChanges = 0;
  auto statusWatch = client.watchStatusChanges([&] {
    ++statusChanges;
  });

  onBattery = false;
  service.emitPropertiesChanged(
      lambdaui::system::UPowerClient::objectPath,
      lambdaui::system::UPowerClient::interfaceName,
      lambdaui::dbus::VariantDictionary{
          .values = {{"OnBattery", lambdaui::dbus::BasicValue(onBattery)}},
      });
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return statusChanges == 1; },
                  std::chrono::milliseconds(500)));
  CHECK(displayChanges == 0);

  service.emitSignal(lambdaui::system::UPowerClient::objectPath,
                     lambdaui::system::UPowerClient::interfaceName,
                     "DeviceAdded",
                     {lambdaui::dbus::ObjectPath{"/org/freedesktop/UPower/devices/battery_BAT0"}});
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return statusChanges == 2; },
                  std::chrono::milliseconds(500)));

  service.emitSignal(lambdaui::system::UPowerClient::objectPath,
                     lambdaui::system::UPowerClient::interfaceName,
                     "DeviceRemoved",
                     {lambdaui::dbus::ObjectPath{"/org/freedesktop/UPower/devices/battery_BAT0"}});
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return statusChanges == 3; },
                  std::chrono::milliseconds(500)));

  onBattery = true;
  service.emitPropertiesChanged(
      lambdaui::system::UPowerClient::objectPath,
      lambdaui::system::UPowerClient::interfaceName,
      lambdaui::dbus::VariantDictionary{
          .values = {{"OnBattery", lambdaui::dbus::BasicValue(onBattery)}},
      });
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return statusChanges == 4; },
                  std::chrono::milliseconds(500)));

  CHECK(displayChanges == 0);

  percentage = 64.2;
  service.emitPropertiesChanged(
      kDisplayDevicePath,
      lambdaui::system::UPowerClient::deviceInterfaceName,
      lambdaui::dbus::VariantDictionary{
          .values = {{"Percentage", lambdaui::dbus::BasicValue(percentage)}},
      });
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return displayChanges == 1 && statusChanges == 5; },
                  std::chrono::milliseconds(500)));

  display = client.readDisplayDevice();
  CHECK(lambdaui::system::formatUPowerBatteryStatus(display) == "64%");
}

#endif
