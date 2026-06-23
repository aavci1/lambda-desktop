#include <Lambda/System/BlueZ.hpp>

#include "DBusTestHelpers.hpp"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <optional>
#include <thread>
#include <utility>

namespace {

using lambdaui::testing::dbus::pollBus;
using lambdaui::testing::dbus::pumpUntil;
using lambdaui::testing::dbus::startPrivateBus;

constexpr char kAdapterPath[] = "/org/bluez/hci0";
constexpr char kDevicePath[] = "/org/bluez/hci0/dev_11_22_33_44_55_66";

template <typename Callback>
class ScopeExit {
public:
  explicit ScopeExit(Callback callback) : callback_(std::move(callback)) {}
  ~ScopeExit() { callback_(); }

private:
  Callback callback_;
};

lambdaui::dbus::ManagedObjectDictionary managedObjects(bool powered, bool discovering, bool connected) {
  lambdaui::dbus::ManagedObjectDictionary objects;
  auto& adapter = objects.values[kAdapterPath][lambdaui::system::BlueZClient::adapterInterfaceName];
  adapter["Address"] = std::string("00:11:22:33:44:55");
  adapter["AddressType"] = std::string("public");
  adapter["Name"] = std::string("lambda-host");
  adapter["Alias"] = std::string("lambda-host");
  adapter["Modalias"] = std::string("usb:v1D6Bp0246d0542");
  adapter["PowerState"] = powered ? std::string("on") : std::string("off");
  adapter["Class"] = std::uint32_t(0x001f00);
  adapter["Powered"] = powered;
  adapter["Discoverable"] = true;
  adapter["Pairable"] = true;
  adapter["Connectable"] = true;
  adapter["Discovering"] = discovering;
  adapter["UUIDs"] = lambdaui::dbus::StringArray{
      .values = {"0000110a-0000-1000-8000-00805f9b34fb"},
  };
  adapter["Roles"] = lambdaui::dbus::StringArray{
      .values = {"central", "peripheral"},
  };

  auto& device = objects.values[kDevicePath][lambdaui::system::BlueZClient::deviceInterfaceName];
  device["Address"] = std::string("11:22:33:44:55:66");
  device["AddressType"] = std::string("public");
  device["Alias"] = std::string("Keyboard");
  device["Name"] = std::string("Keyboard");
  device["Icon"] = std::string("input-keyboard");
  device["Class"] = std::uint32_t(0x000540);
  device["Appearance"] = std::uint16_t(0x03c1);
  device["UUIDs"] = lambdaui::dbus::StringArray{
      .values = {"00001124-0000-1000-8000-00805f9b34fb"},
  };
  device["Adapter"] = lambdaui::dbus::ObjectPath{kAdapterPath};
  device["Paired"] = true;
  device["Connected"] = connected;
  device["Trusted"] = true;
  device["Blocked"] = false;
  device["LegacyPairing"] = false;
  device["Modalias"] = std::string("bluetooth:v05ACp024Fd011B");
  device["ServicesResolved"] = connected;
  device["WakeAllowed"] = true;
  device["RSSI"] = std::int16_t(-48);
  device["TxPower"] = std::int16_t(6);
  device["ManufacturerData"] = lambdaui::dbus::EmptyVariantDictionary{};

  auto& battery = objects.values[kDevicePath][lambdaui::system::BlueZClient::batteryInterfaceName];
  battery["Percentage"] = std::uint8_t(73);
  battery["Source"] = std::string("HID");
  return objects;
}

} // namespace

TEST_CASE("BlueZ support is compile-time declared") {
  CHECK((LAMBDA_HAS_DBUS == 0 || LAMBDA_HAS_DBUS == 1));
}

#if LAMBDA_HAS_DBUS

TEST_CASE("BlueZClient reads adapter and connected device status") {
  auto privateBus = startPrivateBus();
  if (!privateBus) {
    MESSAGE("Skipping BlueZ integration test because a private bus could not be started");
    return;
  }

  auto service = lambdaui::dbus::Bus::openAddress(privateBus->address);
  lambdaui::system::BlueZClient client(lambdaui::dbus::Bus::openAddress(privateBus->address));
  service.requestName(lambdaui::system::BlueZClient::serviceName);

  bool powered = true;
  bool discovering = false;
  bool connected = true;
  bool trusted = true;
  bool blocked = false;
  int startDiscoveryCalls = 0;
  int stopDiscoveryCalls = 0;
  int pairCalls = 0;
  int cancelPairingCalls = 0;
  int connectCalls = 0;
  int disconnectCalls = 0;
  std::string removedDevicePath;

  auto managerSlot = service.exportObject(
      lambdaui::system::BlueZClient::objectManagerPath,
      lambdaui::dbus::ObjectDefinition{
          .methods = {
              lambdaui::dbus::ExportedMethod{
                  .interface = lambdaui::system::BlueZClient::objectManagerInterfaceName,
                  .member = "GetManagedObjects",
                  .handler = [&](lambdaui::dbus::Message&) {
                    return lambdaui::dbus::MethodReply{
                        .values = {managedObjects(powered, discovering, connected)},
                    };
                  },
              },
          },
          .properties = {},
      });

  auto adapterSlot = service.exportObject(
      kAdapterPath,
      lambdaui::dbus::ObjectDefinition{
          .methods = {
              lambdaui::dbus::ExportedMethod{
                  .interface = lambdaui::system::BlueZClient::adapterInterfaceName,
                  .member = "StartDiscovery",
                  .handler = [&](lambdaui::dbus::Message&) {
                    discovering = true;
                    ++startDiscoveryCalls;
                    return lambdaui::dbus::MethodReply{};
                  },
              },
              lambdaui::dbus::ExportedMethod{
                  .interface = lambdaui::system::BlueZClient::adapterInterfaceName,
                  .member = "StopDiscovery",
                  .handler = [&](lambdaui::dbus::Message&) {
                    discovering = false;
                    ++stopDiscoveryCalls;
                    return lambdaui::dbus::MethodReply{};
                  },
              },
              lambdaui::dbus::ExportedMethod{
                  .interface = lambdaui::system::BlueZClient::adapterInterfaceName,
                  .member = "RemoveDevice",
                  .handler = [&](lambdaui::dbus::Message& message) {
                    removedDevicePath = message.readObjectPath().value;
                    return lambdaui::dbus::MethodReply{};
                  },
              },
          },
          .properties = {
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::BlueZClient::adapterInterfaceName,
                  .name = "Powered",
                  .value = true,
                  .writable = true,
                  .getter = [&] { return lambdaui::dbus::BasicValue(powered); },
                  .setter = [&](lambdaui::dbus::BasicValue const& value) {
                    powered = std::get<bool>(value);
                  },
              },
          },
      });

  auto deviceSlot = service.exportObject(
      kDevicePath,
      lambdaui::dbus::ObjectDefinition{
          .methods = {
              lambdaui::dbus::ExportedMethod{
                  .interface = lambdaui::system::BlueZClient::deviceInterfaceName,
                  .member = "Pair",
                  .handler = [&](lambdaui::dbus::Message&) {
                    ++pairCalls;
                    return lambdaui::dbus::MethodReply{};
                  },
              },
              lambdaui::dbus::ExportedMethod{
                  .interface = lambdaui::system::BlueZClient::deviceInterfaceName,
                  .member = "CancelPairing",
                  .handler = [&](lambdaui::dbus::Message&) {
                    ++cancelPairingCalls;
                    return lambdaui::dbus::MethodReply{};
                  },
              },
              lambdaui::dbus::ExportedMethod{
                  .interface = lambdaui::system::BlueZClient::deviceInterfaceName,
                  .member = "Connect",
                  .handler = [&](lambdaui::dbus::Message&) {
                    connected = true;
                    ++connectCalls;
                    return lambdaui::dbus::MethodReply{};
                  },
              },
              lambdaui::dbus::ExportedMethod{
                  .interface = lambdaui::system::BlueZClient::deviceInterfaceName,
                  .member = "Disconnect",
                  .handler = [&](lambdaui::dbus::Message&) {
                    connected = false;
                    ++disconnectCalls;
                    return lambdaui::dbus::MethodReply{};
                  },
              },
          },
          .properties = {
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::BlueZClient::deviceInterfaceName,
                  .name = "Trusted",
                  .value = true,
                  .writable = true,
                  .getter = [&] { return lambdaui::dbus::BasicValue(trusted); },
                  .setter = [&](lambdaui::dbus::BasicValue const& value) {
                    trusted = std::get<bool>(value);
                  },
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::BlueZClient::deviceInterfaceName,
                  .name = "Blocked",
                  .value = false,
                  .writable = true,
                  .getter = [&] { return lambdaui::dbus::BasicValue(blocked); },
                  .setter = [&](lambdaui::dbus::BasicValue const& value) {
                    blocked = std::get<bool>(value);
                  },
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

  auto snapshot = client.readSnapshot();
  REQUIRE(snapshot.adapters.size() == 1);
  CHECK(snapshot.adapters.front().path == kAdapterPath);
  CHECK(snapshot.adapters.front().address == "00:11:22:33:44:55");
  CHECK(snapshot.adapters.front().addressType == "public");
  CHECK(snapshot.adapters.front().name == "lambda-host");
  CHECK(snapshot.adapters.front().alias == "lambda-host");
  CHECK(snapshot.adapters.front().modalias == "usb:v1D6Bp0246d0542");
  CHECK(snapshot.adapters.front().powerState == "on");
  CHECK(snapshot.adapters.front().deviceClass == 0x001f00);
  CHECK(snapshot.adapters.front().powered);
  CHECK(snapshot.adapters.front().discoverable);
  CHECK(snapshot.adapters.front().pairable);
  CHECK(snapshot.adapters.front().connectable);
  CHECK_FALSE(snapshot.adapters.front().discovering);
  CHECK(snapshot.adapters.front().uuids == std::vector<std::string>{"0000110a-0000-1000-8000-00805f9b34fb"});
  CHECK(snapshot.adapters.front().roles == std::vector<std::string>{"central", "peripheral"});
  REQUIRE(snapshot.devices.size() == 1);
  CHECK(snapshot.devices.front().adapterPath == kAdapterPath);
  CHECK(snapshot.devices.front().address == "11:22:33:44:55:66");
  CHECK(snapshot.devices.front().addressType == "public");
  CHECK(snapshot.devices.front().alias == "Keyboard");
  CHECK(snapshot.devices.front().name == "Keyboard");
  CHECK(snapshot.devices.front().iconName == "input-keyboard");
  CHECK(snapshot.devices.front().deviceClass == 0x000540);
  CHECK(snapshot.devices.front().appearance == 0x03c1);
  CHECK(snapshot.devices.front().uuids == std::vector<std::string>{"00001124-0000-1000-8000-00805f9b34fb"});
  CHECK(snapshot.devices.front().paired);
  CHECK(snapshot.devices.front().connected);
  CHECK(snapshot.devices.front().trusted);
  CHECK_FALSE(snapshot.devices.front().blocked);
  CHECK_FALSE(snapshot.devices.front().legacyPairing);
  CHECK(snapshot.devices.front().modalias == "bluetooth:v05ACp024Fd011B");
  CHECK(snapshot.devices.front().servicesResolved);
  CHECK(snapshot.devices.front().wakeAllowed);
  CHECK(snapshot.devices.front().hasRssi);
  CHECK(snapshot.devices.front().rssi == -48);
  CHECK(snapshot.devices.front().hasTxPower);
  CHECK(snapshot.devices.front().txPower == 6);
  CHECK(snapshot.devices.front().batteryPercentage == 73);
  CHECK(snapshot.devices.front().batterySource == "HID");
  CHECK(lambdaui::system::formatBluetoothStatus(snapshot) == "Keyboard");

  connected = false;
  snapshot = client.readSnapshot();
  CHECK(lambdaui::system::formatBluetoothStatus(snapshot) == "on");

  client.setAdapterPowered(kAdapterPath, false);
  CHECK(!powered);
  snapshot = client.readSnapshot();
  CHECK(lambdaui::system::formatBluetoothStatus(snapshot) == "off");

  client.startDiscovery(kAdapterPath);
  CHECK(discovering);
  CHECK(startDiscoveryCalls == 1);
  client.stopDiscovery(kAdapterPath);
  CHECK_FALSE(discovering);
  CHECK(stopDiscoveryCalls == 1);
  client.pairDevice(kDevicePath);
  CHECK(pairCalls == 1);
  client.cancelDevicePairing(kDevicePath);
  CHECK(cancelPairingCalls == 1);
  client.connectDevice(kDevicePath);
  CHECK(connected);
  CHECK(connectCalls == 1);
  client.disconnectDevice(kDevicePath);
  CHECK_FALSE(connected);
  CHECK(disconnectCalls == 1);
  client.setDeviceTrusted(kDevicePath, false);
  CHECK_FALSE(trusted);
  client.setDeviceBlocked(kDevicePath, true);
  CHECK(blocked);
  client.removeDevice(kAdapterPath, kDevicePath);
  CHECK(removedDevicePath == kDevicePath);

  int adapterChanges = 0;
  auto adapterChangedSlot = client.watchAdapterOrDeviceChanged([&] {
    ++adapterChanges;
  });
  int statusChanges = 0;
  auto statusWatch = client.watchStatusChanges([&] {
    ++statusChanges;
  });
  std::optional<lambdaui::system::BlueZDeviceDisconnectedEvent> disconnectedEvent;
  auto disconnectedSlot = client.watchDeviceDisconnected(
      [&](lambdaui::system::BlueZDeviceDisconnectedEvent event) {
        disconnectedEvent = std::move(event);
      });
  powered = true;
  service.emitPropertiesChanged(
      kAdapterPath,
      lambdaui::system::BlueZClient::adapterInterfaceName,
      lambdaui::dbus::VariantDictionary{
          .values = {{"Powered", lambdaui::dbus::BasicValue(powered)}},
      });
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return adapterChanges == 1; },
                  std::chrono::milliseconds(500)));
  CHECK(statusChanges == 1);

  lambdaui::dbus::NamespacedVariantDictionary addedInterfaces;
  addedInterfaces.values[lambdaui::system::BlueZClient::deviceInterfaceName] = {
      {"Connected", lambdaui::dbus::BasicValue(false)},
  };
  service.emitSignal(lambdaui::system::BlueZClient::objectManagerPath,
                     lambdaui::system::BlueZClient::objectManagerInterfaceName,
                     "InterfacesAdded",
                     {lambdaui::dbus::ObjectPath{"/org/bluez/hci0/dev_11_22_33_44_55_66"},
                      addedInterfaces});
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return statusChanges == 2; },
                  std::chrono::milliseconds(500)));

  service.emitSignal(lambdaui::system::BlueZClient::objectManagerPath,
                     lambdaui::system::BlueZClient::objectManagerInterfaceName,
                     "InterfacesRemoved",
                     {lambdaui::dbus::ObjectPath{"/org/bluez/hci0/dev_11_22_33_44_55_66"},
                      lambdaui::dbus::StringArray{
                          .values = {lambdaui::system::BlueZClient::deviceInterfaceName},
                      }});
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return statusChanges == 3; },
                  std::chrono::milliseconds(500)));

  service.emitPropertiesChanged(
      kDevicePath,
      lambdaui::system::BlueZClient::batteryInterfaceName,
      lambdaui::dbus::VariantDictionary{
          .values = {{"Percentage", lambdaui::dbus::BasicValue(std::uint8_t(71))}},
      });
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return statusChanges == 4; },
                  std::chrono::milliseconds(500)));

  service.emitSignal(kDevicePath,
                     lambdaui::system::BlueZClient::deviceInterfaceName,
                     "Disconnected",
                     {std::string("org.bluez.Reason.Timeout"),
                      std::string("Connection timed out")});
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return disconnectedEvent.has_value() && statusChanges == 5; },
                  std::chrono::milliseconds(500)));
  REQUIRE(disconnectedEvent.has_value());
  CHECK(disconnectedEvent->path == kDevicePath);
  CHECK(disconnectedEvent->reason == "org.bluez.Reason.Timeout");
  CHECK(disconnectedEvent->message == "Connection timed out");

  CHECK(lambdaui::system::formatBluetoothStatus(lambdaui::system::BlueZSnapshot{}) == "unavailable");
}

#endif
