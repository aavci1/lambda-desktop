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
constexpr char kSecondAccessPointPath[] = "/org/freedesktop/NetworkManager/AccessPoint/10";
constexpr char kActiveConnectionPath[] = "/org/freedesktop/NetworkManager/ActiveConnection/1";
constexpr char kVpnActiveConnectionPath[] = "/org/freedesktop/NetworkManager/ActiveConnection/2";
constexpr char kSavedConnectionPath[] = "/org/freedesktop/NetworkManager/Settings/1";
constexpr char kVpnSavedConnectionPath[] = "/org/freedesktop/NetworkManager/Settings/2";

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

lambda::dbus::ObjectPathArray objectPaths(std::initializer_list<std::string_view> values) {
  lambda::dbus::ObjectPathArray array;
  array.values.reserve(values.size());
  for (auto value : values) {
    array.values.push_back(lambda::dbus::ObjectPath{std::string(value)});
  }
  return array;
}

lambda::dbus::NamespacedVariantDictionary connectionSettings(std::string id,
                                                             std::string uuid,
                                                             std::string type,
                                                             bool autoconnect) {
  lambda::dbus::NamespacedVariantDictionary settings;
  settings.values["connection"]["id"] = std::move(id);
  settings.values["connection"]["uuid"] = std::move(uuid);
  settings.values["connection"]["type"] = std::move(type);
  settings.values["connection"]["autoconnect"] = autoconnect;
  return settings;
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
  std::uint32_t connectivity =
      static_cast<std::uint32_t>(lambda::system::NetworkConnectivity::Full);
  std::uint32_t metered =
      static_cast<std::uint32_t>(lambda::system::NetworkMetered::GuessNo);
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
                        .values = {objectPaths({kEthernetDevicePath, kWifiDevicePath})},
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
                  .name = "Connectivity",
                  .value = std::uint32_t(0),
                  .writable = false,
                  .getter = [&] { return lambda::dbus::BasicValue(connectivity); },
                  .setter = nullptr,
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::interfaceName,
                  .name = "Metered",
                  .value = std::uint32_t(0),
                  .writable = false,
                  .getter = [&] { return lambda::dbus::BasicValue(metered); },
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
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::interfaceName,
                  .name = "ActiveConnections",
                  .value = lambda::dbus::ObjectPathArray{},
                  .writable = false,
                  .getter = [&] {
                    return lambda::dbus::BasicValue(
                        objectPaths({kActiveConnectionPath, kVpnActiveConnectionPath}));
                  },
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
          .methods = {
              lambda::dbus::ExportedMethod{
                  .interface = lambda::system::NetworkManagerClient::wirelessDeviceInterfaceName,
                  .member = "GetAccessPoints",
                  .handler = [](lambda::dbus::Message&) {
                    return lambda::dbus::MethodReply{
                        .values = {objectPaths({kAccessPointPath, kSecondAccessPointPath})},
                    };
                  },
              },
          },
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
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::accessPointInterfaceName,
                  .name = "HwAddress",
                  .value = std::string("00:11:22:33:44:55"),
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::accessPointInterfaceName,
                  .name = "Frequency",
                  .value = std::uint32_t(2412),
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::accessPointInterfaceName,
                  .name = "MaxBitrate",
                  .value = std::uint32_t(54000),
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::accessPointInterfaceName,
                  .name = "LastSeen",
                  .value = std::int32_t(12),
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::accessPointInterfaceName,
                  .name = "Flags",
                  .value = std::uint32_t(1),
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::accessPointInterfaceName,
                  .name = "WpaFlags",
                  .value = std::uint32_t(0x100),
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::accessPointInterfaceName,
                  .name = "RsnFlags",
                  .value = std::uint32_t(0x100),
              },
          },
      });

  auto secondAccessPointSlot = service.exportObject(
      kSecondAccessPointPath,
      lambda::dbus::ObjectDefinition{
          .methods = {},
          .properties = {
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::accessPointInterfaceName,
                  .name = "Ssid",
                  .value = ssidBytes("CoffeeGuest"),
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::accessPointInterfaceName,
                  .name = "Strength",
                  .value = std::uint8_t(44),
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::accessPointInterfaceName,
                  .name = "Flags",
                  .value = std::uint32_t(0),
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::accessPointInterfaceName,
                  .name = "WpaFlags",
                  .value = std::uint32_t(0),
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::accessPointInterfaceName,
                  .name = "RsnFlags",
                  .value = std::uint32_t(0),
              },
          },
      });

  auto activeConnectionSlot = service.exportObject(
      kActiveConnectionPath,
      lambda::dbus::ObjectDefinition{
          .methods = {},
          .properties = {
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::activeConnectionInterfaceName,
                  .name = "Id",
                  .value = std::string("LambdaNet"),
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::activeConnectionInterfaceName,
                  .name = "Uuid",
                  .value = std::string("11111111-1111-1111-1111-111111111111"),
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::activeConnectionInterfaceName,
                  .name = "Type",
                  .value = std::string("802-11-wireless"),
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::activeConnectionInterfaceName,
                  .name = "Connection",
                  .value = lambda::dbus::ObjectPath{kSavedConnectionPath},
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::activeConnectionInterfaceName,
                  .name = "SpecificObject",
                  .value = lambda::dbus::ObjectPath{kAccessPointPath},
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::activeConnectionInterfaceName,
                  .name = "State",
                  .value = static_cast<std::uint32_t>(
                      lambda::system::NetworkActiveConnectionState::Activated),
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::activeConnectionInterfaceName,
                  .name = "Vpn",
                  .value = false,
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::activeConnectionInterfaceName,
                  .name = "Default",
                  .value = true,
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::activeConnectionInterfaceName,
                  .name = "Default6",
                  .value = true,
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::activeConnectionInterfaceName,
                  .name = "Devices",
                  .value = objectPaths({kWifiDevicePath}),
              },
          },
      });

  auto vpnActiveConnectionSlot = service.exportObject(
      kVpnActiveConnectionPath,
      lambda::dbus::ObjectDefinition{
          .methods = {},
          .properties = {
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::activeConnectionInterfaceName,
                  .name = "Id",
                  .value = std::string("Lambda VPN"),
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::activeConnectionInterfaceName,
                  .name = "Uuid",
                  .value = std::string("22222222-2222-2222-2222-222222222222"),
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::activeConnectionInterfaceName,
                  .name = "Type",
                  .value = std::string("vpn"),
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::activeConnectionInterfaceName,
                  .name = "Connection",
                  .value = lambda::dbus::ObjectPath{kVpnSavedConnectionPath},
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::activeConnectionInterfaceName,
                  .name = "SpecificObject",
                  .value = lambda::dbus::ObjectPath{"/"},
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::activeConnectionInterfaceName,
                  .name = "State",
                  .value = static_cast<std::uint32_t>(
                      lambda::system::NetworkActiveConnectionState::Activated),
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::activeConnectionInterfaceName,
                  .name = "Vpn",
                  .value = true,
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::activeConnectionInterfaceName,
                  .name = "Default",
                  .value = false,
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::activeConnectionInterfaceName,
                  .name = "Default6",
                  .value = false,
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::activeConnectionInterfaceName,
                  .name = "Devices",
                  .value = objectPaths({}),
              },
          },
      });

  auto settingsSlot = service.exportObject(
      lambda::system::NetworkManagerClient::settingsObjectPath,
      lambda::dbus::ObjectDefinition{
          .methods = {
              lambda::dbus::ExportedMethod{
                  .interface = lambda::system::NetworkManagerClient::settingsInterfaceName,
                  .member = "ListConnections",
                  .handler = [](lambda::dbus::Message&) {
                    return lambda::dbus::MethodReply{
                        .values = {objectPaths({kSavedConnectionPath, kVpnSavedConnectionPath})},
                    };
                  },
              },
          },
          .properties = {},
      });

  auto savedConnectionSlot = service.exportObject(
      kSavedConnectionPath,
      lambda::dbus::ObjectDefinition{
          .methods = {
              lambda::dbus::ExportedMethod{
                  .interface = lambda::system::NetworkManagerClient::settingsConnectionInterfaceName,
                  .member = "GetSettings",
                  .handler = [](lambda::dbus::Message&) {
                    return lambda::dbus::MethodReply{
                        .values = {connectionSettings("LambdaNet",
                                                      "11111111-1111-1111-1111-111111111111",
                                                      "802-11-wireless",
                                                      true)},
                    };
                  },
              },
          },
          .properties = {
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::settingsConnectionInterfaceName,
                  .name = "Filename",
                  .value = std::string("/etc/NetworkManager/system-connections/LambdaNet.nmconnection"),
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::settingsConnectionInterfaceName,
                  .name = "Unsaved",
                  .value = false,
              },
          },
      });

  auto vpnSavedConnectionSlot = service.exportObject(
      kVpnSavedConnectionPath,
      lambda::dbus::ObjectDefinition{
          .methods = {
              lambda::dbus::ExportedMethod{
                  .interface = lambda::system::NetworkManagerClient::settingsConnectionInterfaceName,
                  .member = "GetSettings",
                  .handler = [](lambda::dbus::Message&) {
                    return lambda::dbus::MethodReply{
                        .values = {connectionSettings("Lambda VPN",
                                                      "22222222-2222-2222-2222-222222222222",
                                                      "vpn",
                                                      false)},
                    };
                  },
              },
          },
          .properties = {
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::settingsConnectionInterfaceName,
                  .name = "Filename",
                  .value = std::string("/etc/NetworkManager/system-connections/LambdaVPN.nmconnection"),
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::NetworkManagerClient::settingsConnectionInterfaceName,
                  .name = "Unsaved",
                  .value = false,
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
  CHECK(snapshot.connectivity == lambda::system::NetworkConnectivity::Full);
  CHECK(snapshot.metered == lambda::system::NetworkMetered::GuessNo);
  CHECK(snapshot.networkingEnabled);
  CHECK(snapshot.wirelessEnabled);
  CHECK(snapshot.wirelessHardwareEnabled);
  CHECK(snapshot.vpnState == lambda::system::NetworkVpnState::Active);
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
  CHECK(wifi->activeAccessPoint.active);
  CHECK(wifi->activeAccessPoint.requiresAuthentication);
  CHECK(wifi->activeAccessPoint.hardwareAddress == "00:11:22:33:44:55");
  CHECK(wifi->activeAccessPoint.frequencyMhz == 2412);
  CHECK(wifi->activeAccessPoint.maxBitrateKbps == 54000);
  CHECK(wifi->activeAccessPoint.lastSeenSeconds == 12);
  CHECK(wifi->activeAccessPoint.flags == 1);
  CHECK(wifi->activeAccessPoint.wpaFlags == 0x100);
  CHECK(wifi->activeAccessPoint.rsnFlags == 0x100);
  REQUIRE(wifi->accessPoints.size() == 2);
  CHECK(wifi->accessPoints[0].path == kAccessPointPath);
  CHECK(wifi->accessPoints[0].active);
  CHECK(wifi->accessPoints[0].ssid == "LambdaNet");
  CHECK(wifi->accessPoints[1].path == kSecondAccessPointPath);
  CHECK_FALSE(wifi->accessPoints[1].active);
  CHECK(wifi->accessPoints[1].ssid == "CoffeeGuest");
  CHECK(wifi->accessPoints[1].strength == 44);
  CHECK_FALSE(wifi->accessPoints[1].requiresAuthentication);
  REQUIRE(snapshot.activeConnections.size() == 2);
  CHECK(snapshot.activeConnections[0].path == kActiveConnectionPath);
  CHECK(snapshot.activeConnections[0].id == "LambdaNet");
  CHECK(snapshot.activeConnections[0].type == "802-11-wireless");
  CHECK(snapshot.activeConnections[0].connectionPath == kSavedConnectionPath);
  CHECK(snapshot.activeConnections[0].specificObjectPath == kAccessPointPath);
  CHECK(snapshot.activeConnections[0].state == lambda::system::NetworkActiveConnectionState::Activated);
  CHECK_FALSE(snapshot.activeConnections[0].vpn);
  CHECK(snapshot.activeConnections[0].defaultRoute);
  CHECK(snapshot.activeConnections[0].defaultRoute6);
  CHECK(snapshot.activeConnections[0].devicePaths == std::vector<std::string>{kWifiDevicePath});
  CHECK(snapshot.activeConnections[1].path == kVpnActiveConnectionPath);
  CHECK(snapshot.activeConnections[1].id == "Lambda VPN");
  CHECK(snapshot.activeConnections[1].type == "vpn");
  CHECK(snapshot.activeConnections[1].vpn);
  REQUIRE(snapshot.savedConnections.size() == 2);
  CHECK(snapshot.savedConnections[0].path == kSavedConnectionPath);
  CHECK(snapshot.savedConnections[0].id == "LambdaNet");
  CHECK(snapshot.savedConnections[0].uuid == "11111111-1111-1111-1111-111111111111");
  CHECK(snapshot.savedConnections[0].type == "802-11-wireless");
  CHECK(snapshot.savedConnections[0].autoconnect);
  CHECK_FALSE(snapshot.savedConnections[0].unsaved);
  CHECK_FALSE(snapshot.savedConnections[0].vpn);
  CHECK(snapshot.savedConnections[0].filename.ends_with("LambdaNet.nmconnection"));
  CHECK(snapshot.savedConnections[1].path == kVpnSavedConnectionPath);
  CHECK(snapshot.savedConnections[1].id == "Lambda VPN");
  CHECK(snapshot.savedConnections[1].type == "vpn");
  CHECK(snapshot.savedConnections[1].vpn);
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
  int statusChanges = 0;
  auto statusWatch = client.watchStatusChanges([&] {
    ++statusChanges;
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
  CHECK(statusChanges == 1);

  wifiState = static_cast<std::uint32_t>(lambda::system::NetworkDeviceState::Activated);
  service.emitPropertiesChanged(
      kWifiDevicePath,
      lambda::system::NetworkManagerClient::deviceInterfaceName,
      lambda::dbus::VariantDictionary{
          .values = {{"State", lambda::dbus::BasicValue(wifiState)}},
      });
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return statusChanges == 2; },
                  std::chrono::milliseconds(500)));

  service.emitPropertiesChanged(
      kWifiDevicePath,
      lambda::system::NetworkManagerClient::wirelessDeviceInterfaceName,
      lambda::dbus::VariantDictionary{
          .values = {
              {"ActiveAccessPoint", lambda::dbus::BasicValue(lambda::dbus::ObjectPath{kAccessPointPath})},
          },
      });
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return statusChanges == 3; },
                  std::chrono::milliseconds(500)));

  ssid = "UpdatedNet";
  service.emitPropertiesChanged(
      kAccessPointPath,
      lambda::system::NetworkManagerClient::accessPointInterfaceName,
      lambda::dbus::VariantDictionary{
          .values = {{"Ssid", lambda::dbus::BasicValue(ssidBytes(ssid))}},
      });
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return statusChanges == 4; },
                  std::chrono::milliseconds(500)));

  service.emitSignal(lambda::system::NetworkManagerClient::objectPath,
                     lambda::system::NetworkManagerClient::interfaceName,
                     "DeviceAdded",
                     {lambda::dbus::ObjectPath{"/org/freedesktop/NetworkManager/Devices/3"}});
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return statusChanges == 5; },
                  std::chrono::milliseconds(500)));

  service.emitSignal(lambda::system::NetworkManagerClient::objectPath,
                     lambda::system::NetworkManagerClient::interfaceName,
                     "DeviceRemoved",
                     {lambda::dbus::ObjectPath{"/org/freedesktop/NetworkManager/Devices/3"}});
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return statusChanges == 6; },
                  std::chrono::milliseconds(500)));

  service.emitPropertiesChanged(
      kVpnActiveConnectionPath,
      lambda::system::NetworkManagerClient::activeConnectionInterfaceName,
      lambda::dbus::VariantDictionary{
          .values = {{"State",
                      lambda::dbus::BasicValue(static_cast<std::uint32_t>(
                          lambda::system::NetworkActiveConnectionState::Activated))}},
      });
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return statusChanges == 7; },
                  std::chrono::milliseconds(500)));

  service.emitSignal(kWifiDevicePath,
                     lambda::system::NetworkManagerClient::wirelessDeviceInterfaceName,
                     "AccessPointAdded",
                     {lambda::dbus::ObjectPath{kSecondAccessPointPath}});
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return statusChanges == 8; },
                  std::chrono::milliseconds(500)));

  service.emitSignal(kWifiDevicePath,
                     lambda::system::NetworkManagerClient::wirelessDeviceInterfaceName,
                     "AccessPointRemoved",
                     {lambda::dbus::ObjectPath{kSecondAccessPointPath}});
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return statusChanges == 9; },
                  std::chrono::milliseconds(500)));

  service.emitSignal(lambda::system::NetworkManagerClient::settingsObjectPath,
                     lambda::system::NetworkManagerClient::settingsInterfaceName,
                     "NewConnection",
                     {lambda::dbus::ObjectPath{kSavedConnectionPath}});
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return statusChanges == 10; },
                  std::chrono::milliseconds(500)));

  service.emitSignal(lambda::system::NetworkManagerClient::settingsObjectPath,
                     lambda::system::NetworkManagerClient::settingsInterfaceName,
                     "ConnectionRemoved",
                     {lambda::dbus::ObjectPath{kSavedConnectionPath}});
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return statusChanges == 11; },
                  std::chrono::milliseconds(500)));

  managerState = static_cast<std::uint32_t>(lambda::system::NetworkManagerState::ConnectedGlobal);
  snapshot = client.readSnapshot();
  CHECK(lambda::system::formatWifiStatus(snapshot) == "UpdatedNet");
}

#endif
