#include <Lambda/System/BlueZ.hpp>

#include "DBusTestHelpers.hpp"

#include <doctest/doctest.h>

#include <atomic>
#include <thread>
#include <utility>

namespace {

using lambda::testing::dbus::pollBus;
using lambda::testing::dbus::startPrivateBus;

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

lambda::dbus::ManagedObjectDictionary managedObjects(bool powered, bool discovering, bool connected) {
  lambda::dbus::ManagedObjectDictionary objects;
  auto& adapter = objects.values[kAdapterPath][lambda::system::BlueZClient::adapterInterfaceName];
  adapter["Address"] = std::string("00:11:22:33:44:55");
  adapter["Alias"] = std::string("lambda-host");
  adapter["Powered"] = powered;
  adapter["Discovering"] = discovering;

  auto& device = objects.values[kDevicePath][lambda::system::BlueZClient::deviceInterfaceName];
  device["Address"] = std::string("11:22:33:44:55:66");
  device["Alias"] = std::string("Keyboard");
  device["Name"] = std::string("Keyboard");
  device["Adapter"] = lambda::dbus::ObjectPath{kAdapterPath};
  device["Paired"] = true;
  device["Connected"] = connected;
  device["ManufacturerData"] = lambda::dbus::EmptyVariantDictionary{};
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

  auto service = lambda::dbus::Bus::openAddress(privateBus->address);
  lambda::system::BlueZClient client(lambda::dbus::Bus::openAddress(privateBus->address));
  service.requestName(lambda::system::BlueZClient::serviceName);

  bool powered = true;
  bool discovering = false;
  bool connected = true;

  auto managerSlot = service.exportObject(
      lambda::system::BlueZClient::objectManagerPath,
      lambda::dbus::ObjectDefinition{
          .methods = {
              lambda::dbus::ExportedMethod{
                  .interface = lambda::system::BlueZClient::objectManagerInterfaceName,
                  .member = "GetManagedObjects",
                  .handler = [&](lambda::dbus::Message&) {
                    return lambda::dbus::MethodReply{
                        .values = {managedObjects(powered, discovering, connected)},
                    };
                  },
              },
          },
          .properties = {},
      });

  auto adapterSlot = service.exportObject(
      kAdapterPath,
      lambda::dbus::ObjectDefinition{
          .methods = {},
          .properties = {
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::BlueZClient::adapterInterfaceName,
                  .name = "Powered",
                  .value = true,
                  .writable = true,
                  .getter = [&] { return lambda::dbus::BasicValue(powered); },
                  .setter = [&](lambda::dbus::BasicValue const& value) {
                    powered = std::get<bool>(value);
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
  CHECK(snapshot.adapters.front().powered);
  REQUIRE(snapshot.devices.size() == 1);
  CHECK(snapshot.devices.front().adapterPath == kAdapterPath);
  CHECK(snapshot.devices.front().connected);
  CHECK(lambda::system::formatBluetoothStatus(snapshot) == "Keyboard");

  connected = false;
  snapshot = client.readSnapshot();
  CHECK(lambda::system::formatBluetoothStatus(snapshot) == "on");

  client.setAdapterPowered(kAdapterPath, false);
  CHECK(!powered);
  snapshot = client.readSnapshot();
  CHECK(lambda::system::formatBluetoothStatus(snapshot) == "off");

  CHECK(lambda::system::formatBluetoothStatus(lambda::system::BlueZSnapshot{}) == "unavailable");
}

#endif
