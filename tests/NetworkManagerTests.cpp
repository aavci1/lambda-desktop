#include <Lambda/System/NetworkManager.hpp>

#include "DBusTestHelpers.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <string_view>
#include <thread>
#include <utility>

namespace {

using lambda::testing::dbus::pollBus;
using lambda::testing::dbus::pumpUntil;
using lambda::testing::dbus::startPrivateBus;

constexpr char kEthernetDevicePath[] = "/org/freedesktop/NetworkManager/Devices/1";
constexpr char kWifiDevicePath[] = "/org/freedesktop/NetworkManager/Devices/2";
constexpr char kAccessPointPath[] = "/org/freedesktop/NetworkManager/AccessPoint/9";

template <typename Callback>
class ScopeExit {
public:
  explicit ScopeExit(Callback callback) : callback_(std::move(callback)) {}
  ~ScopeExit() { callback_(); }

private:
  Callback callback_;
};

lambda::dbus::ByteArray ssidBytes(std::string_view ssid) {
  lambda::dbus::ByteArray bytes;
  bytes.values.reserve(ssid.size());
  for (char ch : ssid) {
    bytes.values.push_back(static_cast<std::uint8_t>(ch));
  }
  return bytes;
}

} // namespace

TEST_CASE("NetworkManager support is compile-time declared") {
  CHECK((LAMBDA_HAS_DBUS == 0 || LAMBDA_HAS_DBUS == 1));
}

#if LAMBDA_HAS_DBUS

TEST_CASE("NetworkManagerClient reads device and Wi-Fi status") {
  auto privateBus = startPrivateBus();
  if (!privateBus) {
    MESSAGE("Skipping NetworkManager integration test because a private bus could not be started");
    return;
  }

  auto service = lambda::dbus::Bus::openAddress(privateBus->address);
  lambda::system::NetworkManagerClient client(lambda::dbus::Bus::openAddress(privateBus->address));
  service.requestName(lambda::system::NetworkManagerClient::serviceName);

  bool networkingEnabled = true;
  bool wirelessEnabled = true;
  bool wirelessHardwareEnabled = true;
  std::uint32_t managerState =
      static_cast<std::uint32_t>(lambda::system::NetworkManagerState::ConnectedGlobal);
  std::uint32_t ethernetState =
      static_cast<std::uint32_t>(lambda::system::NetworkDeviceState::Activated);
  std::uint32_t wifiState =
      static_cast<std::uint32_t>(lambda::system::NetworkDeviceState::Activated);
  std::uint8_t strength = 82;
  std::string ssid = "LambdaNet";

  auto managerSlot = service.exportObject(
      lambda::system::NetworkManagerClient::objectPath,
      lambda::dbus::ObjectDefinition{
          .methods = {
              lambda::dbus::ExportedMethod{
                  .interface = lambda::system::NetworkManagerClient::interfaceName,
                  .member = "GetDevices",
                  .handler = [](lambda::dbus::Message&) {
                    return lambda::dbus::MethodReply{
                        .values = {lambda::dbus::ObjectPathArray{
                            .values = {
                                lambda::dbus::ObjectPath{kEthernetDevicePath},
                                lambda::dbus::ObjectPath{kWifiDevicePath},
                            },
                        }},
                    };
                  },
              },
          },
          .properties = {
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::interfaceName,
                  .name = "State",
                  .value = std::uint32_t(0),
                  .writable = false,
                  .getter = [&] { return lambda::dbus::BasicValue(managerState); },
                  .setter = nullptr,
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::interfaceName,
                  .name = "NetworkingEnabled",
                  .value = false,
                  .writable = false,
                  .getter = [&] { return lambda::dbus::BasicValue(networkingEnabled); },
                  .setter = nullptr,
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::interfaceName,
                  .name = "WirelessEnabled",
                  .value = true,
                  .writable = true,
                  .getter = [&] { return lambda::dbus::BasicValue(wirelessEnabled); },
                  .setter = [&](lambda::dbus::BasicValue const& value) {
                    wirelessEnabled = std::get<bool>(value);
                  },
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::interfaceName,
                  .name = "WirelessHardwareEnabled",
                  .value = true,
                  .writable = false,
                  .getter = [&] { return lambda::dbus::BasicValue(wirelessHardwareEnabled); },
                  .setter = nullptr,
              },
          },
      });

  auto ethernetSlot = service.exportObject(
      kEthernetDevicePath,
      lambda::dbus::ObjectDefinition{
          .methods = {},
          .properties = {
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::deviceInterfaceName,
                  .name = "Interface",
                  .value = std::string("enp0s1"),
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::deviceInterfaceName,
                  .name = "DeviceType",
                  .value = static_cast<std::uint32_t>(lambda::system::NetworkDeviceType::Ethernet),
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::deviceInterfaceName,
                  .name = "State",
                  .value = std::uint32_t(0),
                  .getter = [&] { return lambda::dbus::BasicValue(ethernetState); },
              },
          },
      });

  auto wifiSlot = service.exportObject(
      kWifiDevicePath,
      lambda::dbus::ObjectDefinition{
          .methods = {},
          .properties = {
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::deviceInterfaceName,
                  .name = "Interface",
                  .value = std::string("wlan0"),
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::deviceInterfaceName,
                  .name = "DeviceType",
                  .value = static_cast<std::uint32_t>(lambda::system::NetworkDeviceType::Wifi),
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::deviceInterfaceName,
                  .name = "State",
                  .value = std::uint32_t(0),
                  .getter = [&] { return lambda::dbus::BasicValue(wifiState); },
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::wirelessDeviceInterfaceName,
                  .name = "ActiveAccessPoint",
                  .value = lambda::dbus::ObjectPath{kAccessPointPath},
              },
          },
      });

  auto accessPointSlot = service.exportObject(
      kAccessPointPath,
      lambda::dbus::ObjectDefinition{
          .methods = {},
          .properties = {
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::accessPointInterfaceName,
                  .name = "Ssid",
                  .value = lambda::dbus::ByteArray{},
                  .getter = [&] { return lambda::dbus::BasicValue(ssidBytes(ssid)); },
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::accessPointInterfaceName,
                  .name = "Strength",
                  .value = std::uint8_t(0),
                  .getter = [&] { return lambda::dbus::BasicValue(strength); },
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

  auto paths = client.devicePaths();
  CHECK(paths == std::vector<std::string>{kEthernetDevicePath, kWifiDevicePath});

  auto snapshot = client.readSnapshot();
  CHECK(snapshot.state == lambda::system::NetworkManagerState::ConnectedGlobal);
  CHECK(snapshot.networkingEnabled);
  CHECK(snapshot.wirelessEnabled);
  CHECK(snapshot.wirelessHardwareEnabled);
  REQUIRE(snapshot.devices.size() == 2);

  auto const wifi = std::find_if(snapshot.devices.begin(), snapshot.devices.end(), [](auto const& device) {
    return device.type == lambda::system::NetworkDeviceType::Wifi;
  });
  REQUIRE(wifi != snapshot.devices.end());
  CHECK(wifi->interfaceName == "wlan0");
  CHECK(wifi->state == lambda::system::NetworkDeviceState::Activated);
  CHECK(wifi->activeAccessPoint.path == kAccessPointPath);
  CHECK(wifi->activeAccessPoint.ssid == "LambdaNet");
  CHECK(wifi->activeAccessPoint.strength == 82);
  CHECK(lambda::system::formatNetworkStatus(snapshot) == "online");
  CHECK(lambda::system::formatWifiStatus(snapshot) == "LambdaNet");

  client.setWirelessEnabled(false);
  CHECK(!wirelessEnabled);
  snapshot = client.readSnapshot();
  CHECK(lambda::system::formatWifiStatus(snapshot) == "off");

  managerState = static_cast<std::uint32_t>(lambda::system::NetworkManagerState::Connecting);
  wifiState = static_cast<std::uint32_t>(lambda::system::NetworkDeviceState::Config);
  wirelessEnabled = true;
  snapshot = client.readSnapshot();
  CHECK(lambda::system::formatNetworkStatus(snapshot) == "connecting");
  CHECK(lambda::system::formatWifiStatus(snapshot) == "connecting");

  int managerChanges = 0;
  auto managerChangedSlot = client.watchManagerChanged([&] {
    ++managerChanges;
  });
  managerState = static_cast<std::uint32_t>(lambda::system::NetworkManagerState::Disconnected);
  service.emitPropertiesChanged(
      lambda::system::NetworkManagerClient::objectPath,
      lambda::system::NetworkManagerClient::interfaceName,
      lambda::dbus::VariantDictionary{
          .values = {{"State", lambda::dbus::BasicValue(managerState)}},
      });
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return managerChanges == 1; },
                  std::chrono::milliseconds(500)));
}

#endif
