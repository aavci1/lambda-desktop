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

using lambdaui::testing::dbus::pollBus;
using lambdaui::testing::dbus::pumpUntil;
using lambdaui::testing::dbus::startPrivateBus;

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

lambdaui::dbus::ByteArray ssidBytes(std::string_view ssid) {
  lambdaui::dbus::ByteArray bytes;
  bytes.values.reserve(ssid.size());
  for (char ch : ssid) {
    bytes.values.push_back(static_cast<std::uint8_t>(ch));
  }
  return bytes;
}

lambdaui::dbus::ObjectPathArray objectPaths(std::initializer_list<std::string_view> values) {
  lambdaui::dbus::ObjectPathArray array;
  array.values.reserve(values.size());
  for (auto value : values) {
    array.values.push_back(lambdaui::dbus::ObjectPath{std::string(value)});
  }
  return array;
}

lambdaui::dbus::NamespacedVariantDictionary connectionSettings(std::string id,
                                                             std::string uuid,
                                                             std::string type,
                                                             bool autoconnect) {
  lambdaui::dbus::NamespacedVariantDictionary settings;
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

  auto service = lambdaui::dbus::Bus::openAddress(privateBus->address);
  lambdaui::system::NetworkManagerClient client(lambdaui::dbus::Bus::openAddress(privateBus->address));
  service.requestName(lambdaui::system::NetworkManagerClient::serviceName);

  bool networkingEnabled = true;
  bool wirelessEnabled = true;
  bool wirelessHardwareEnabled = true;
  std::uint32_t managerState =
      static_cast<std::uint32_t>(lambdaui::system::NetworkManagerState::ConnectedGlobal);
  std::uint32_t connectivity =
      static_cast<std::uint32_t>(lambdaui::system::NetworkConnectivity::Full);
  std::uint32_t metered =
      static_cast<std::uint32_t>(lambdaui::system::NetworkMetered::GuessNo);
  std::uint32_t ethernetState =
      static_cast<std::uint32_t>(lambdaui::system::NetworkDeviceState::Activated);
  std::uint32_t wifiState =
      static_cast<std::uint32_t>(lambdaui::system::NetworkDeviceState::Activated);
  std::uint8_t strength = 82;
  std::string ssid = "LambdaNet";
  std::string activatedConnection;
  std::string activatedDevice;
  std::string activatedSpecificObject;
  lambdaui::dbus::NamespacedVariantDictionary addedConnectionSettings;
  std::string addActivateDevice;
  std::string addActivateSpecificObject;
  int addActivateCalls = 0;
  std::string deactivatedConnection;

  auto managerSlot = service.exportObject(
      lambdaui::system::NetworkManagerClient::objectPath,
      lambdaui::dbus::ObjectDefinition{
          .methods = {
              lambdaui::dbus::ExportedMethod{
                  .interface = lambdaui::system::NetworkManagerClient::interfaceName,
                  .member = "GetDevices",
                  .handler = [](lambdaui::dbus::Message&) {
                    return lambdaui::dbus::MethodReply{
                        .values = {objectPaths({kEthernetDevicePath, kWifiDevicePath})},
                    };
                  },
              },
              lambdaui::dbus::ExportedMethod{
                  .interface = lambdaui::system::NetworkManagerClient::interfaceName,
                  .member = "ActivateConnection",
                  .handler = [&](lambdaui::dbus::Message& message) {
                    activatedConnection = message.readObjectPath().value;
                    activatedDevice = message.readObjectPath().value;
                    activatedSpecificObject = message.readObjectPath().value;
                    return lambdaui::dbus::MethodReply{
                        .values = {lambdaui::dbus::ObjectPath{kActiveConnectionPath}},
                    };
                  },
              },
              lambdaui::dbus::ExportedMethod{
                  .interface = lambdaui::system::NetworkManagerClient::interfaceName,
                  .member = "AddAndActivateConnection",
                  .handler = [&](lambdaui::dbus::Message& message) {
                    addedConnectionSettings = message.readNamespacedVariantDictionary();
                    addActivateDevice = message.readObjectPath().value;
                    addActivateSpecificObject = message.readObjectPath().value;
                    ++addActivateCalls;
                    return lambdaui::dbus::MethodReply{
                        .values = {lambdaui::dbus::ObjectPath{kSavedConnectionPath},
                                   lambdaui::dbus::ObjectPath{kActiveConnectionPath}},
                    };
                  },
              },
              lambdaui::dbus::ExportedMethod{
                  .interface = lambdaui::system::NetworkManagerClient::interfaceName,
                  .member = "DeactivateConnection",
                  .handler = [&](lambdaui::dbus::Message& message) {
                    deactivatedConnection = message.readObjectPath().value;
                    return lambdaui::dbus::MethodReply{};
                  },
              },
          },
          .properties = {
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::interfaceName,
                  .name = "State",
                  .value = std::uint32_t(0),
                  .writable = false,
                  .getter = [&] { return lambdaui::dbus::BasicValue(managerState); },
                  .setter = nullptr,
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::interfaceName,
                  .name = "Connectivity",
                  .value = std::uint32_t(0),
                  .writable = false,
                  .getter = [&] { return lambdaui::dbus::BasicValue(connectivity); },
                  .setter = nullptr,
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::interfaceName,
                  .name = "Metered",
                  .value = std::uint32_t(0),
                  .writable = false,
                  .getter = [&] { return lambdaui::dbus::BasicValue(metered); },
                  .setter = nullptr,
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::interfaceName,
                  .name = "NetworkingEnabled",
                  .value = false,
                  .writable = false,
                  .getter = [&] { return lambdaui::dbus::BasicValue(networkingEnabled); },
                  .setter = nullptr,
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::interfaceName,
                  .name = "WirelessEnabled",
                  .value = true,
                  .writable = true,
                  .getter = [&] { return lambdaui::dbus::BasicValue(wirelessEnabled); },
                  .setter = [&](lambdaui::dbus::BasicValue const& value) {
                    wirelessEnabled = std::get<bool>(value);
                  },
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::interfaceName,
                  .name = "WirelessHardwareEnabled",
                  .value = true,
                  .writable = false,
                  .getter = [&] { return lambdaui::dbus::BasicValue(wirelessHardwareEnabled); },
                  .setter = nullptr,
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::interfaceName,
                  .name = "ActiveConnections",
                  .value = lambdaui::dbus::ObjectPathArray{},
                  .writable = false,
                  .getter = [&] {
                    return lambdaui::dbus::BasicValue(
                        objectPaths({kActiveConnectionPath, kVpnActiveConnectionPath}));
                  },
                  .setter = nullptr,
              },
          },
      });

  auto ethernetSlot = service.exportObject(
      kEthernetDevicePath,
      lambdaui::dbus::ObjectDefinition{
          .methods = {},
          .properties = {
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::deviceInterfaceName,
                  .name = "Interface",
                  .value = std::string("enp0s1"),
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::deviceInterfaceName,
                  .name = "DeviceType",
                  .value = static_cast<std::uint32_t>(lambdaui::system::NetworkDeviceType::Ethernet),
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::deviceInterfaceName,
                  .name = "State",
                  .value = std::uint32_t(0),
                  .getter = [&] { return lambdaui::dbus::BasicValue(ethernetState); },
              },
          },
      });

  auto wifiSlot = service.exportObject(
      kWifiDevicePath,
      lambdaui::dbus::ObjectDefinition{
          .methods = {
              lambdaui::dbus::ExportedMethod{
                  .interface = lambdaui::system::NetworkManagerClient::wirelessDeviceInterfaceName,
                  .member = "GetAccessPoints",
                  .handler = [](lambdaui::dbus::Message&) {
                    return lambdaui::dbus::MethodReply{
                        .values = {objectPaths({kAccessPointPath, kSecondAccessPointPath})},
                    };
                  },
              },
          },
          .properties = {
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::deviceInterfaceName,
                  .name = "Interface",
                  .value = std::string("wlan0"),
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::deviceInterfaceName,
                  .name = "DeviceType",
                  .value = static_cast<std::uint32_t>(lambdaui::system::NetworkDeviceType::Wifi),
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::deviceInterfaceName,
                  .name = "State",
                  .value = std::uint32_t(0),
                  .getter = [&] { return lambdaui::dbus::BasicValue(wifiState); },
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::wirelessDeviceInterfaceName,
                  .name = "ActiveAccessPoint",
                  .value = lambdaui::dbus::ObjectPath{kAccessPointPath},
              },
          },
      });

  auto accessPointSlot = service.exportObject(
      kAccessPointPath,
      lambdaui::dbus::ObjectDefinition{
          .methods = {},
          .properties = {
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::accessPointInterfaceName,
                  .name = "Ssid",
                  .value = lambdaui::dbus::ByteArray{},
                  .getter = [&] { return lambdaui::dbus::BasicValue(ssidBytes(ssid)); },
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::accessPointInterfaceName,
                  .name = "Strength",
                  .value = std::uint8_t(0),
                  .getter = [&] { return lambdaui::dbus::BasicValue(strength); },
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::accessPointInterfaceName,
                  .name = "HwAddress",
                  .value = std::string("00:11:22:33:44:55"),
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::accessPointInterfaceName,
                  .name = "Frequency",
                  .value = std::uint32_t(2412),
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::accessPointInterfaceName,
                  .name = "MaxBitrate",
                  .value = std::uint32_t(54000),
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::accessPointInterfaceName,
                  .name = "LastSeen",
                  .value = std::int32_t(12),
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::accessPointInterfaceName,
                  .name = "Flags",
                  .value = std::uint32_t(1),
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::accessPointInterfaceName,
                  .name = "WpaFlags",
                  .value = std::uint32_t(0x100),
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::accessPointInterfaceName,
                  .name = "RsnFlags",
                  .value = std::uint32_t(0x100),
              },
          },
      });

  auto secondAccessPointSlot = service.exportObject(
      kSecondAccessPointPath,
      lambdaui::dbus::ObjectDefinition{
          .methods = {},
          .properties = {
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::accessPointInterfaceName,
                  .name = "Ssid",
                  .value = ssidBytes("CoffeeGuest"),
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::accessPointInterfaceName,
                  .name = "Strength",
                  .value = std::uint8_t(44),
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::accessPointInterfaceName,
                  .name = "Flags",
                  .value = std::uint32_t(0),
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::accessPointInterfaceName,
                  .name = "WpaFlags",
                  .value = std::uint32_t(0),
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::accessPointInterfaceName,
                  .name = "RsnFlags",
                  .value = std::uint32_t(0),
              },
          },
      });

  auto activeConnectionSlot = service.exportObject(
      kActiveConnectionPath,
      lambdaui::dbus::ObjectDefinition{
          .methods = {},
          .properties = {
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::activeConnectionInterfaceName,
                  .name = "Id",
                  .value = std::string("LambdaNet"),
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::activeConnectionInterfaceName,
                  .name = "Uuid",
                  .value = std::string("11111111-1111-1111-1111-111111111111"),
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::activeConnectionInterfaceName,
                  .name = "Type",
                  .value = std::string("802-11-wireless"),
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::activeConnectionInterfaceName,
                  .name = "Connection",
                  .value = lambdaui::dbus::ObjectPath{kSavedConnectionPath},
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::activeConnectionInterfaceName,
                  .name = "SpecificObject",
                  .value = lambdaui::dbus::ObjectPath{kAccessPointPath},
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::activeConnectionInterfaceName,
                  .name = "State",
                  .value = static_cast<std::uint32_t>(
                      lambdaui::system::NetworkActiveConnectionState::Activated),
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::activeConnectionInterfaceName,
                  .name = "Vpn",
                  .value = false,
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::activeConnectionInterfaceName,
                  .name = "Default",
                  .value = true,
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::activeConnectionInterfaceName,
                  .name = "Default6",
                  .value = true,
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::activeConnectionInterfaceName,
                  .name = "Devices",
                  .value = objectPaths({kWifiDevicePath}),
              },
          },
      });

  auto vpnActiveConnectionSlot = service.exportObject(
      kVpnActiveConnectionPath,
      lambdaui::dbus::ObjectDefinition{
          .methods = {},
          .properties = {
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::activeConnectionInterfaceName,
                  .name = "Id",
                  .value = std::string("Lambda VPN"),
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::activeConnectionInterfaceName,
                  .name = "Uuid",
                  .value = std::string("22222222-2222-2222-2222-222222222222"),
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::activeConnectionInterfaceName,
                  .name = "Type",
                  .value = std::string("vpn"),
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::activeConnectionInterfaceName,
                  .name = "Connection",
                  .value = lambdaui::dbus::ObjectPath{kVpnSavedConnectionPath},
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::activeConnectionInterfaceName,
                  .name = "SpecificObject",
                  .value = lambdaui::dbus::ObjectPath{"/"},
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::activeConnectionInterfaceName,
                  .name = "State",
                  .value = static_cast<std::uint32_t>(
                      lambdaui::system::NetworkActiveConnectionState::Activated),
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::activeConnectionInterfaceName,
                  .name = "Vpn",
                  .value = true,
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::activeConnectionInterfaceName,
                  .name = "Default",
                  .value = false,
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::activeConnectionInterfaceName,
                  .name = "Default6",
                  .value = false,
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::activeConnectionInterfaceName,
                  .name = "Devices",
                  .value = objectPaths({}),
              },
          },
      });

  auto settingsSlot = service.exportObject(
      lambdaui::system::NetworkManagerClient::settingsObjectPath,
      lambdaui::dbus::ObjectDefinition{
          .methods = {
              lambdaui::dbus::ExportedMethod{
                  .interface = lambdaui::system::NetworkManagerClient::settingsInterfaceName,
                  .member = "ListConnections",
                  .handler = [](lambdaui::dbus::Message&) {
                    return lambdaui::dbus::MethodReply{
                        .values = {objectPaths({kSavedConnectionPath, kVpnSavedConnectionPath})},
                    };
                  },
              },
          },
          .properties = {},
      });

  auto savedConnectionSlot = service.exportObject(
      kSavedConnectionPath,
      lambdaui::dbus::ObjectDefinition{
          .methods = {
              lambdaui::dbus::ExportedMethod{
                  .interface = lambdaui::system::NetworkManagerClient::settingsConnectionInterfaceName,
                  .member = "GetSettings",
                  .handler = [](lambdaui::dbus::Message&) {
                    return lambdaui::dbus::MethodReply{
                        .values = {connectionSettings("LambdaNet",
                                                      "11111111-1111-1111-1111-111111111111",
                                                      "802-11-wireless",
                                                      true)},
                    };
                  },
              },
          },
          .properties = {
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::settingsConnectionInterfaceName,
                  .name = "Filename",
                  .value = std::string("/etc/NetworkManager/system-connections/LambdaNet.nmconnection"),
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::settingsConnectionInterfaceName,
                  .name = "Unsaved",
                  .value = false,
              },
          },
      });

  auto vpnSavedConnectionSlot = service.exportObject(
      kVpnSavedConnectionPath,
      lambdaui::dbus::ObjectDefinition{
          .methods = {
              lambdaui::dbus::ExportedMethod{
                  .interface = lambdaui::system::NetworkManagerClient::settingsConnectionInterfaceName,
                  .member = "GetSettings",
                  .handler = [](lambdaui::dbus::Message&) {
                    return lambdaui::dbus::MethodReply{
                        .values = {connectionSettings("Lambda VPN",
                                                      "22222222-2222-2222-2222-222222222222",
                                                      "vpn",
                                                      false)},
                    };
                  },
              },
          },
          .properties = {
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::settingsConnectionInterfaceName,
                  .name = "Filename",
                  .value = std::string("/etc/NetworkManager/system-connections/LambdaVPN.nmconnection"),
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::NetworkManagerClient::settingsConnectionInterfaceName,
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
  CHECK(snapshot.state == lambdaui::system::NetworkManagerState::ConnectedGlobal);
  CHECK(snapshot.connectivity == lambdaui::system::NetworkConnectivity::Full);
  CHECK(snapshot.metered == lambdaui::system::NetworkMetered::GuessNo);
  CHECK(snapshot.networkingEnabled);
  CHECK(snapshot.wirelessEnabled);
  CHECK(snapshot.wirelessHardwareEnabled);
  CHECK(snapshot.vpnState == lambdaui::system::NetworkVpnState::Active);
  REQUIRE(snapshot.devices.size() == 2);

  auto const wifi = std::find_if(snapshot.devices.begin(), snapshot.devices.end(), [](auto const& device) {
    return device.type == lambdaui::system::NetworkDeviceType::Wifi;
  });
  REQUIRE(wifi != snapshot.devices.end());
  CHECK(wifi->interfaceName == "wlan0");
  CHECK(wifi->state == lambdaui::system::NetworkDeviceState::Activated);
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
  CHECK(snapshot.activeConnections[0].state == lambdaui::system::NetworkActiveConnectionState::Activated);
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
  CHECK(lambdaui::system::formatNetworkStatus(snapshot) == "online");
  CHECK(lambdaui::system::formatWifiStatus(snapshot) == "LambdaNet");

  client.setWirelessEnabled(false);
  CHECK(!wirelessEnabled);
  snapshot = client.readSnapshot();
  CHECK(lambdaui::system::formatWifiStatus(snapshot) == "off");

  auto activatedPath = client.activateConnection(kSavedConnectionPath, kWifiDevicePath, kAccessPointPath);
  CHECK(activatedPath == kActiveConnectionPath);
  CHECK(activatedConnection == kSavedConnectionPath);
  CHECK(activatedDevice == kWifiDevicePath);
  CHECK(activatedSpecificObject == kAccessPointPath);

  auto activationResult =
      client.addAndActivateWirelessConnection("CoffeeGuest", kWifiDevicePath, kSecondAccessPointPath);
  CHECK(activationResult.connectionPath == kSavedConnectionPath);
  CHECK(activationResult.activeConnectionPath == kActiveConnectionPath);
  CHECK(addActivateCalls == 1);
  CHECK(addActivateDevice == kWifiDevicePath);
  CHECK(addActivateSpecificObject == kSecondAccessPointPath);
  CHECK(std::get<std::string>(addedConnectionSettings.values["connection"]["id"]) == "CoffeeGuest");
  CHECK(std::get<std::string>(addedConnectionSettings.values["connection"]["type"]) == "802-11-wireless");
  CHECK(std::get<bool>(addedConnectionSettings.values["connection"]["autoconnect"]));
  CHECK(std::get<std::string>(addedConnectionSettings.values["802-11-wireless"]["mode"]) == "infrastructure");
  CHECK(std::get<lambdaui::dbus::ByteArray>(
            addedConnectionSettings.values["802-11-wireless"]["ssid"]).values == ssidBytes("CoffeeGuest").values);
  CHECK_FALSE(addedConnectionSettings.values.contains("802-11-wireless-security"));

  activationResult =
      client.addAndActivateWirelessConnection("SecureNet", kWifiDevicePath, kSecondAccessPointPath, "correct horse");
  CHECK(activationResult.activeConnectionPath == kActiveConnectionPath);
  CHECK(addActivateCalls == 2);
  CHECK(std::get<std::string>(addedConnectionSettings.values["connection"]["id"]) == "SecureNet");
  CHECK(std::get<std::string>(
            addedConnectionSettings.values["802-11-wireless-security"]["key-mgmt"]) == "wpa-psk");
  CHECK(std::get<std::string>(
            addedConnectionSettings.values["802-11-wireless-security"]["psk"]) == "correct horse");

  client.deactivateConnection(kActiveConnectionPath);
  CHECK(deactivatedConnection == kActiveConnectionPath);

  managerState = static_cast<std::uint32_t>(lambdaui::system::NetworkManagerState::Connecting);
  wifiState = static_cast<std::uint32_t>(lambdaui::system::NetworkDeviceState::Config);
  wirelessEnabled = true;
  snapshot = client.readSnapshot();
  CHECK(lambdaui::system::formatNetworkStatus(snapshot) == "connecting");
  CHECK(lambdaui::system::formatWifiStatus(snapshot) == "connecting");

  int managerChanges = 0;
  auto managerChangedSlot = client.watchManagerChanged([&] {
    ++managerChanges;
  });
  int statusChanges = 0;
  auto statusWatch = client.watchStatusChanges([&] {
    ++statusChanges;
  });
  managerState = static_cast<std::uint32_t>(lambdaui::system::NetworkManagerState::Disconnected);
  service.emitPropertiesChanged(
      lambdaui::system::NetworkManagerClient::objectPath,
      lambdaui::system::NetworkManagerClient::interfaceName,
      lambdaui::dbus::VariantDictionary{
          .values = {{"State", lambdaui::dbus::BasicValue(managerState)}},
      });
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return managerChanges == 1; },
                  std::chrono::milliseconds(500)));
  CHECK(statusChanges == 1);

  wifiState = static_cast<std::uint32_t>(lambdaui::system::NetworkDeviceState::Activated);
  service.emitPropertiesChanged(
      kWifiDevicePath,
      lambdaui::system::NetworkManagerClient::deviceInterfaceName,
      lambdaui::dbus::VariantDictionary{
          .values = {{"State", lambdaui::dbus::BasicValue(wifiState)}},
      });
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return statusChanges == 2; },
                  std::chrono::milliseconds(500)));

  service.emitPropertiesChanged(
      kWifiDevicePath,
      lambdaui::system::NetworkManagerClient::wirelessDeviceInterfaceName,
      lambdaui::dbus::VariantDictionary{
          .values = {
              {"ActiveAccessPoint", lambdaui::dbus::BasicValue(lambdaui::dbus::ObjectPath{kAccessPointPath})},
          },
      });
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return statusChanges == 3; },
                  std::chrono::milliseconds(500)));

  ssid = "UpdatedNet";
  service.emitPropertiesChanged(
      kAccessPointPath,
      lambdaui::system::NetworkManagerClient::accessPointInterfaceName,
      lambdaui::dbus::VariantDictionary{
          .values = {{"Ssid", lambdaui::dbus::BasicValue(ssidBytes(ssid))}},
      });
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return statusChanges == 4; },
                  std::chrono::milliseconds(500)));

  service.emitSignal(lambdaui::system::NetworkManagerClient::objectPath,
                     lambdaui::system::NetworkManagerClient::interfaceName,
                     "DeviceAdded",
                     {lambdaui::dbus::ObjectPath{"/org/freedesktop/NetworkManager/Devices/3"}});
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return statusChanges == 5; },
                  std::chrono::milliseconds(500)));

  service.emitSignal(lambdaui::system::NetworkManagerClient::objectPath,
                     lambdaui::system::NetworkManagerClient::interfaceName,
                     "DeviceRemoved",
                     {lambdaui::dbus::ObjectPath{"/org/freedesktop/NetworkManager/Devices/3"}});
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return statusChanges == 6; },
                  std::chrono::milliseconds(500)));

  service.emitPropertiesChanged(
      kVpnActiveConnectionPath,
      lambdaui::system::NetworkManagerClient::activeConnectionInterfaceName,
      lambdaui::dbus::VariantDictionary{
          .values = {{"State",
                      lambdaui::dbus::BasicValue(static_cast<std::uint32_t>(
                          lambdaui::system::NetworkActiveConnectionState::Activated))}},
      });
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return statusChanges == 7; },
                  std::chrono::milliseconds(500)));

  service.emitSignal(kWifiDevicePath,
                     lambdaui::system::NetworkManagerClient::wirelessDeviceInterfaceName,
                     "AccessPointAdded",
                     {lambdaui::dbus::ObjectPath{kSecondAccessPointPath}});
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return statusChanges == 8; },
                  std::chrono::milliseconds(500)));

  service.emitSignal(kWifiDevicePath,
                     lambdaui::system::NetworkManagerClient::wirelessDeviceInterfaceName,
                     "AccessPointRemoved",
                     {lambdaui::dbus::ObjectPath{kSecondAccessPointPath}});
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return statusChanges == 9; },
                  std::chrono::milliseconds(500)));

  service.emitSignal(lambdaui::system::NetworkManagerClient::settingsObjectPath,
                     lambdaui::system::NetworkManagerClient::settingsInterfaceName,
                     "NewConnection",
                     {lambdaui::dbus::ObjectPath{kSavedConnectionPath}});
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return statusChanges == 10; },
                  std::chrono::milliseconds(500)));

  service.emitSignal(lambdaui::system::NetworkManagerClient::settingsObjectPath,
                     lambdaui::system::NetworkManagerClient::settingsInterfaceName,
                     "ConnectionRemoved",
                     {lambdaui::dbus::ObjectPath{kSavedConnectionPath}});
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return statusChanges == 11; },
                  std::chrono::milliseconds(500)));

  managerState = static_cast<std::uint32_t>(lambdaui::system::NetworkManagerState::ConnectedGlobal);
  snapshot = client.readSnapshot();
  CHECK(lambdaui::system::formatWifiStatus(snapshot) == "UpdatedNet");
}

#endif
