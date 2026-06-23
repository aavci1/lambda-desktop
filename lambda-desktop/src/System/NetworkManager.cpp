#include <Lambda/System/NetworkManager.hpp>

#include <algorithm>
#include <memory>
#include <string_view>
#include <utility>

namespace lambdaui::system {

namespace {

constexpr char kPropertiesInterface[] = "org.freedesktop.DBus.Properties";

std::string decodeSsid(dbus::ByteArray const& bytes) {
  std::string ssid;
  ssid.reserve(bytes.values.size());
  for (auto const byte : bytes.values) {
    if (byte == 0) {
      ssid.push_back('?');
    } else {
      ssid.push_back(static_cast<char>(byte));
    }
  }
  return ssid;
}

dbus::ByteArray encodeSsid(std::string_view ssid) {
  dbus::ByteArray bytes;
  bytes.values.reserve(ssid.size());
  for (char ch : ssid) {
    bytes.values.push_back(static_cast<std::uint8_t>(ch));
  }
  return bytes;
}

dbus::ObjectPath objectPathOrRoot(std::string path) {
  if (path.empty()) {
    path = "/";
  }
  return dbus::ObjectPath{std::move(path)};
}

std::shared_ptr<dbus::DictionaryValue>
variantDictionaryValue(std::map<std::string, dbus::BasicValue> const& values) {
  auto dictionary = std::make_shared<dbus::DictionaryValue>();
  dictionary->keySignature = "s";
  dictionary->valueSignature = "v";
  dictionary->entries.reserve(values.size());
  for (auto const& [key, value] : values) {
    dictionary->entries.push_back(dbus::DictionaryEntry{
        .key = key,
        .value = value,
    });
  }
  return dictionary;
}

std::shared_ptr<dbus::DictionaryValue>
namespacedDictionaryValue(dbus::NamespacedVariantDictionary const& settings) {
  auto dictionary = std::make_shared<dbus::DictionaryValue>();
  dictionary->keySignature = "s";
  dictionary->valueSignature = "a{sv}";
  dictionary->entries.reserve(settings.values.size());
  for (auto const& [group, values] : settings.values) {
    dictionary->entries.push_back(dbus::DictionaryEntry{
        .key = group,
        .value = variantDictionaryValue(values),
    });
  }
  return dictionary;
}

bool connectedState(NetworkManagerState state) {
  return state == NetworkManagerState::ConnectedLocal || state == NetworkManagerState::ConnectedSite ||
         state == NetworkManagerState::ConnectedGlobal;
}

bool connectingState(NetworkDeviceState state) {
  return state == NetworkDeviceState::Prepare || state == NetworkDeviceState::Config ||
         state == NetworkDeviceState::NeedAuth || state == NetworkDeviceState::IpConfig ||
         state == NetworkDeviceState::IpCheck || state == NetworkDeviceState::Secondaries;
}

std::vector<std::string> pathStrings(dbus::ObjectPathArray const& paths) {
  std::vector<std::string> output;
  output.reserve(paths.values.size());
  for (auto const& path : paths.values) {
    if (!path.value.empty() && path.value != "/") {
      output.push_back(path.value);
    }
  }
  return output;
}

bool hasPath(std::vector<NetworkAccessPointSnapshot> const& accessPoints, std::string const& path) {
  return std::any_of(accessPoints.begin(), accessPoints.end(), [&](auto const& accessPoint) {
    return accessPoint.path == path;
  });
}

std::string stringSetting(dbus::NamespacedVariantDictionary const& settings,
                          std::string const& group,
                          std::string const& key) {
  auto const groupIt = settings.values.find(group);
  if (groupIt == settings.values.end()) return {};
  auto const valueIt = groupIt->second.find(key);
  if (valueIt == groupIt->second.end()) return {};
  if (auto value = std::get_if<std::string>(&valueIt->second)) {
    return *value;
  }
  return {};
}

bool boolSetting(dbus::NamespacedVariantDictionary const& settings,
                 std::string const& group,
                 std::string const& key) {
  auto const groupIt = settings.values.find(group);
  if (groupIt == settings.values.end()) return false;
  auto const valueIt = groupIt->second.find(key);
  if (valueIt == groupIt->second.end()) return false;
  if (auto value = std::get_if<bool>(&valueIt->second)) {
    return *value;
  }
  return false;
}

bool connectedVpn(NetworkActiveConnectionSnapshot const& connection) {
  return connection.vpn && connection.state == NetworkActiveConnectionState::Activated;
}

bool connectingVpn(NetworkActiveConnectionSnapshot const& connection) {
  return connection.vpn && (connection.state == NetworkActiveConnectionState::Activating ||
                            connection.state == NetworkActiveConnectionState::Deactivating);
}

NetworkVpnState vpnStateFor(std::vector<NetworkActiveConnectionSnapshot> const& activeConnections) {
  if (std::any_of(activeConnections.begin(), activeConnections.end(), connectedVpn)) {
    return NetworkVpnState::Active;
  }
  if (std::any_of(activeConnections.begin(), activeConnections.end(), connectingVpn)) {
    return NetworkVpnState::Connecting;
  }
  return NetworkVpnState::Inactive;
}

void notify(std::shared_ptr<std::function<void()>> const& handler) {
  if (handler && *handler) {
    (*handler)();
  }
}

} // namespace

NetworkManagerClient::NetworkManagerClient(dbus::Bus bus) : bus_(std::move(bus)) {}

NetworkManagerClient NetworkManagerClient::connectSystem() {
  return NetworkManagerClient(dbus::Bus::open(dbus::BusType::System));
}

std::vector<std::string> NetworkManagerClient::devicePaths() {
  dbus::Message reply = bus_.call(dbus::MethodCall{
      .destination = serviceName,
      .path = objectPath,
      .interface = interfaceName,
      .member = "GetDevices",
      .arguments = {},
  });

  std::vector<std::string> paths;
  for (auto const& path : reply.readObjectPathArray().values) {
    if (!path.value.empty()) {
      paths.push_back(path.value);
    }
  }
  return paths;
}

NetworkManagerSnapshot NetworkManagerClient::readSnapshot() {
  NetworkManagerSnapshot snapshot;
  snapshot.state =
      static_cast<NetworkManagerState>(std::get<std::uint32_t>(getManagerProperty("State", "u")));
  try {
    snapshot.connectivity = static_cast<NetworkConnectivity>(
        std::get<std::uint32_t>(getManagerProperty("Connectivity", "u")));
  } catch (...) {
  }
  try {
    snapshot.metered =
        static_cast<NetworkMetered>(std::get<std::uint32_t>(getManagerProperty("Metered", "u")));
  } catch (...) {
  }
  snapshot.networkingEnabled = std::get<bool>(getManagerProperty("NetworkingEnabled", "b"));
  snapshot.wirelessEnabled = std::get<bool>(getManagerProperty("WirelessEnabled", "b"));
  snapshot.wirelessHardwareEnabled =
      std::get<bool>(getManagerProperty("WirelessHardwareEnabled", "b"));

  for (auto const& path : devicePaths()) {
    try {
      snapshot.devices.push_back(readDevice(path));
    } catch (...) {
    }
  }
  for (auto const& path : activeConnectionPaths()) {
    try {
      snapshot.activeConnections.push_back(readActiveConnection(path));
    } catch (...) {
    }
  }
  snapshot.vpnState = vpnStateFor(snapshot.activeConnections);
  for (auto const& path : savedConnectionPaths()) {
    try {
      snapshot.savedConnections.push_back(readSavedConnection(path));
    } catch (...) {
    }
  }

  return snapshot;
}

dbus::Slot NetworkManagerClient::watchManagerChanged(std::function<void()> handler) {
  return bus_.matchSignal(
      dbus::SignalMatch{
          .sender = serviceName,
          .path = objectPath,
          .interface = kPropertiesInterface,
          .member = "PropertiesChanged",
      },
      [handler = std::move(handler)](dbus::Message& message) mutable {
        auto const changed = dbus::readPropertiesChanged(message);
        if (changed.interface != NetworkManagerClient::interfaceName) {
          return;
        }
        if (handler) {
          handler();
        }
      });
}

NetworkManagerStatusWatch NetworkManagerClient::watchStatusChanges(std::function<void()> handler) {
  auto sharedHandler = std::make_shared<std::function<void()>>(std::move(handler));
  return NetworkManagerStatusWatch{
      .managerChanged =
          bus_.matchSignal(
              dbus::SignalMatch{
                  .sender = serviceName,
                  .path = objectPath,
                  .interface = kPropertiesInterface,
                  .member = "PropertiesChanged",
              },
              [sharedHandler](dbus::Message& message) {
                auto const changed = dbus::readPropertiesChanged(message);
                if (changed.interface != NetworkManagerClient::interfaceName) {
                  return;
                }
                notify(sharedHandler);
              }),
      .deviceOrAccessPointChanged =
          bus_.matchSignal(
              dbus::SignalMatch{
                  .sender = serviceName,
                  .path = {},
                  .interface = kPropertiesInterface,
                  .member = "PropertiesChanged",
              },
              [sharedHandler](dbus::Message& message) {
                auto const changed = dbus::readPropertiesChanged(message);
                if (changed.interface != NetworkManagerClient::deviceInterfaceName &&
                    changed.interface != NetworkManagerClient::wirelessDeviceInterfaceName &&
                    changed.interface != NetworkManagerClient::accessPointInterfaceName &&
                    changed.interface != NetworkManagerClient::activeConnectionInterfaceName &&
                    changed.interface != NetworkManagerClient::settingsConnectionInterfaceName) {
                  return;
                }
                notify(sharedHandler);
              }),
      .deviceAdded =
          bus_.matchSignal(
              dbus::SignalMatch{
                  .sender = serviceName,
                  .path = objectPath,
                  .interface = interfaceName,
                  .member = "DeviceAdded",
              },
              [sharedHandler](dbus::Message&) {
                notify(sharedHandler);
              }),
      .deviceRemoved =
          bus_.matchSignal(
              dbus::SignalMatch{
                  .sender = serviceName,
                  .path = objectPath,
                  .interface = interfaceName,
                  .member = "DeviceRemoved",
              },
              [sharedHandler](dbus::Message&) {
                notify(sharedHandler);
              }),
      .accessPointAdded =
          bus_.matchSignal(
              dbus::SignalMatch{
                  .sender = serviceName,
                  .path = {},
                  .interface = wirelessDeviceInterfaceName,
                  .member = "AccessPointAdded",
              },
              [sharedHandler](dbus::Message&) {
                notify(sharedHandler);
              }),
      .accessPointRemoved =
          bus_.matchSignal(
              dbus::SignalMatch{
                  .sender = serviceName,
                  .path = {},
                  .interface = wirelessDeviceInterfaceName,
                  .member = "AccessPointRemoved",
              },
              [sharedHandler](dbus::Message&) {
                notify(sharedHandler);
              }),
      .connectionAdded =
          bus_.matchSignal(
              dbus::SignalMatch{
                  .sender = serviceName,
                  .path = settingsObjectPath,
                  .interface = settingsInterfaceName,
                  .member = "NewConnection",
              },
              [sharedHandler](dbus::Message&) {
                notify(sharedHandler);
              }),
      .connectionRemoved =
          bus_.matchSignal(
              dbus::SignalMatch{
                  .sender = serviceName,
                  .path = settingsObjectPath,
                  .interface = settingsInterfaceName,
                  .member = "ConnectionRemoved",
              },
              [sharedHandler](dbus::Message&) {
                notify(sharedHandler);
              }),
  };
}

void NetworkManagerClient::setWirelessEnabled(bool enabled) {
  bus_.setProperty(dbus::PropertyAddress{
                       .destination = serviceName,
                       .path = objectPath,
                       .interface = interfaceName,
                       .name = "WirelessEnabled",
                   },
                   enabled);
}

std::string NetworkManagerClient::activateConnection(std::string connectionPath,
                                                     std::string devicePath,
                                                     std::string specificObjectPath) {
  dbus::Message reply = bus_.call(dbus::MethodCall{
      .destination = serviceName,
      .path = objectPath,
      .interface = interfaceName,
      .member = "ActivateConnection",
      .arguments = {objectPathOrRoot(std::move(connectionPath)),
                    objectPathOrRoot(std::move(devicePath)),
                    objectPathOrRoot(std::move(specificObjectPath))},
  });
  return reply.readObjectPath().value;
}

NetworkActivationResult NetworkManagerClient::addAndActivateWirelessConnection(std::string ssid,
                                                                               std::string devicePath,
                                                                               std::string accessPointPath,
                                                                               std::string password) {
  dbus::NamespacedVariantDictionary settings;
  settings.values["connection"]["id"] = ssid;
  settings.values["connection"]["type"] = std::string("802-11-wireless");
  settings.values["connection"]["autoconnect"] = true;
  settings.values["802-11-wireless"]["mode"] = std::string("infrastructure");
  settings.values["802-11-wireless"]["ssid"] = encodeSsid(ssid);
  if (!password.empty()) {
    settings.values["802-11-wireless-security"]["key-mgmt"] = std::string("wpa-psk");
    settings.values["802-11-wireless-security"]["psk"] = std::move(password);
  }

  dbus::Message reply = bus_.call(dbus::MethodCall{
      .destination = serviceName,
      .path = objectPath,
      .interface = interfaceName,
      .member = "AddAndActivateConnection",
      .arguments = {namespacedDictionaryValue(settings),
                    objectPathOrRoot(std::move(devicePath)),
                    objectPathOrRoot(std::move(accessPointPath))},
  });
  return NetworkActivationResult{
      .connectionPath = reply.readObjectPath().value,
      .activeConnectionPath = reply.readObjectPath().value,
  };
}

void NetworkManagerClient::deactivateConnection(std::string activeConnectionPath) {
  (void)bus_.call(dbus::MethodCall{
      .destination = serviceName,
      .path = objectPath,
      .interface = interfaceName,
      .member = "DeactivateConnection",
      .arguments = {objectPathOrRoot(std::move(activeConnectionPath))},
  });
}

dbus::BasicValue NetworkManagerClient::getManagerProperty(std::string const& name,
                                                          std::string_view signature) {
  return bus_.getProperty(dbus::PropertyAddress{
                              .destination = serviceName,
                              .path = objectPath,
                              .interface = interfaceName,
                              .name = name,
                          },
                          signature);
}

dbus::BasicValue NetworkManagerClient::getDeviceProperty(std::string const& path,
                                                         std::string const& interface,
                                                         std::string const& name,
                                                         std::string_view signature) {
  return bus_.getProperty(dbus::PropertyAddress{
                              .destination = serviceName,
                              .path = path,
                              .interface = interface,
                              .name = name,
                          },
                          signature);
}

dbus::BasicValue NetworkManagerClient::getActiveConnectionProperty(std::string const& path,
                                                                   std::string const& name,
                                                                   std::string_view signature) {
  return bus_.getProperty(dbus::PropertyAddress{
                              .destination = serviceName,
                              .path = path,
                              .interface = activeConnectionInterfaceName,
                              .name = name,
                          },
                          signature);
}

dbus::BasicValue NetworkManagerClient::getSettingsConnectionProperty(std::string const& path,
                                                                     std::string const& name,
                                                                     std::string_view signature) {
  return bus_.getProperty(dbus::PropertyAddress{
                              .destination = serviceName,
                              .path = path,
                              .interface = settingsConnectionInterfaceName,
                              .name = name,
                          },
                          signature);
}

std::vector<std::string> NetworkManagerClient::accessPointPaths(std::string const& devicePath) {
  dbus::Message reply = bus_.call(dbus::MethodCall{
      .destination = serviceName,
      .path = devicePath,
      .interface = wirelessDeviceInterfaceName,
      .member = "GetAccessPoints",
      .arguments = {},
  });
  return pathStrings(reply.readObjectPathArray());
}

std::vector<std::string> NetworkManagerClient::activeConnectionPaths() {
  try {
    return pathStrings(std::get<dbus::ObjectPathArray>(getManagerProperty("ActiveConnections", "ao")));
  } catch (...) {
    return {};
  }
}

std::vector<std::string> NetworkManagerClient::savedConnectionPaths() {
  try {
    dbus::Message reply = bus_.call(dbus::MethodCall{
        .destination = serviceName,
        .path = settingsObjectPath,
        .interface = settingsInterfaceName,
        .member = "ListConnections",
        .arguments = {},
    });
    return pathStrings(reply.readObjectPathArray());
  } catch (...) {
    return {};
  }
}

NetworkDeviceSnapshot NetworkManagerClient::readDevice(std::string const& path) {
  NetworkDeviceSnapshot device;
  device.path = path;
  device.interfaceName =
      std::get<std::string>(getDeviceProperty(path, deviceInterfaceName, "Interface", "s"));
  device.type = static_cast<NetworkDeviceType>(
      std::get<std::uint32_t>(getDeviceProperty(path, deviceInterfaceName, "DeviceType", "u")));
  device.state = static_cast<NetworkDeviceState>(
      std::get<std::uint32_t>(getDeviceProperty(path, deviceInterfaceName, "State", "u")));

  if (device.type == NetworkDeviceType::Wifi) {
    try {
      auto activeAccessPoint = std::get<dbus::ObjectPath>(
          getDeviceProperty(path, wirelessDeviceInterfaceName, "ActiveAccessPoint", "o"));
      if (!activeAccessPoint.value.empty() && activeAccessPoint.value != "/") {
        device.activeAccessPoint = readAccessPoint(activeAccessPoint.value);
        device.activeAccessPoint.active = true;
      }
    } catch (...) {
    }
    try {
      for (auto const& accessPointPath : accessPointPaths(path)) {
        try {
          auto accessPoint = readAccessPoint(accessPointPath);
          accessPoint.active = accessPoint.path == device.activeAccessPoint.path;
          device.accessPoints.push_back(std::move(accessPoint));
        } catch (...) {
        }
      }
    } catch (...) {
    }
    if (!device.activeAccessPoint.path.empty() && !hasPath(device.accessPoints, device.activeAccessPoint.path)) {
      device.accessPoints.push_back(device.activeAccessPoint);
    }
  }

  return device;
}

NetworkAccessPointSnapshot NetworkManagerClient::readAccessPoint(std::string const& path) {
  NetworkAccessPointSnapshot accessPoint;
  accessPoint.path = path;
  accessPoint.ssid = decodeSsid(
      std::get<dbus::ByteArray>(getDeviceProperty(path, accessPointInterfaceName, "Ssid", "ay")));
  accessPoint.strength =
      std::get<std::uint8_t>(getDeviceProperty(path, accessPointInterfaceName, "Strength", "y"));
  try {
    accessPoint.hardwareAddress =
        std::get<std::string>(getDeviceProperty(path, accessPointInterfaceName, "HwAddress", "s"));
  } catch (...) {
  }
  try {
    accessPoint.frequencyMhz =
        std::get<std::uint32_t>(getDeviceProperty(path, accessPointInterfaceName, "Frequency", "u"));
  } catch (...) {
  }
  try {
    accessPoint.maxBitrateKbps =
        std::get<std::uint32_t>(getDeviceProperty(path, accessPointInterfaceName, "MaxBitrate", "u"));
  } catch (...) {
  }
  try {
    accessPoint.lastSeenSeconds =
        std::get<std::int32_t>(getDeviceProperty(path, accessPointInterfaceName, "LastSeen", "i"));
  } catch (...) {
  }
  try {
    accessPoint.flags =
        std::get<std::uint32_t>(getDeviceProperty(path, accessPointInterfaceName, "Flags", "u"));
  } catch (...) {
  }
  try {
    accessPoint.wpaFlags =
        std::get<std::uint32_t>(getDeviceProperty(path, accessPointInterfaceName, "WpaFlags", "u"));
  } catch (...) {
  }
  try {
    accessPoint.rsnFlags =
        std::get<std::uint32_t>(getDeviceProperty(path, accessPointInterfaceName, "RsnFlags", "u"));
  } catch (...) {
  }
  accessPoint.requiresAuthentication =
      accessPoint.flags != 0 || accessPoint.wpaFlags != 0 || accessPoint.rsnFlags != 0;
  return accessPoint;
}

NetworkActiveConnectionSnapshot NetworkManagerClient::readActiveConnection(std::string const& path) {
  NetworkActiveConnectionSnapshot connection;
  connection.path = path;
  try {
    connection.id = std::get<std::string>(getActiveConnectionProperty(path, "Id", "s"));
  } catch (...) {
  }
  try {
    connection.uuid = std::get<std::string>(getActiveConnectionProperty(path, "Uuid", "s"));
  } catch (...) {
  }
  try {
    connection.type = std::get<std::string>(getActiveConnectionProperty(path, "Type", "s"));
  } catch (...) {
  }
  try {
    connection.connectionPath =
        std::get<dbus::ObjectPath>(getActiveConnectionProperty(path, "Connection", "o")).value;
  } catch (...) {
  }
  try {
    connection.specificObjectPath =
        std::get<dbus::ObjectPath>(getActiveConnectionProperty(path, "SpecificObject", "o")).value;
  } catch (...) {
  }
  try {
    connection.state = static_cast<NetworkActiveConnectionState>(
        std::get<std::uint32_t>(getActiveConnectionProperty(path, "State", "u")));
  } catch (...) {
  }
  try {
    connection.vpn = std::get<bool>(getActiveConnectionProperty(path, "Vpn", "b"));
  } catch (...) {
    connection.vpn = connection.type == "vpn";
  }
  try {
    connection.defaultRoute = std::get<bool>(getActiveConnectionProperty(path, "Default", "b"));
  } catch (...) {
  }
  try {
    connection.defaultRoute6 = std::get<bool>(getActiveConnectionProperty(path, "Default6", "b"));
  } catch (...) {
  }
  try {
    connection.devicePaths =
        pathStrings(std::get<dbus::ObjectPathArray>(getActiveConnectionProperty(path, "Devices", "ao")));
  } catch (...) {
  }
  return connection;
}

NetworkSavedConnectionSnapshot NetworkManagerClient::readSavedConnection(std::string const& path) {
  NetworkSavedConnectionSnapshot connection;
  connection.path = path;
  dbus::Message reply = bus_.call(dbus::MethodCall{
      .destination = serviceName,
      .path = path,
      .interface = settingsConnectionInterfaceName,
      .member = "GetSettings",
      .arguments = {},
  });
  auto settings = reply.readNamespacedVariantDictionary();
  connection.id = stringSetting(settings, "connection", "id");
  connection.uuid = stringSetting(settings, "connection", "uuid");
  connection.type = stringSetting(settings, "connection", "type");
  connection.autoconnect = boolSetting(settings, "connection", "autoconnect");
  connection.vpn = connection.type == "vpn" || settings.values.contains("vpn");
  try {
    connection.filename =
        std::get<std::string>(getSettingsConnectionProperty(path, "Filename", "s"));
  } catch (...) {
  }
  try {
    connection.unsaved = std::get<bool>(getSettingsConnectionProperty(path, "Unsaved", "b"));
  } catch (...) {
  }
  return connection;
}

std::string formatNetworkStatus(NetworkManagerSnapshot const& snapshot) {
  if (!snapshot.networkingEnabled || snapshot.state == NetworkManagerState::Disabled) {
    return "off";
  }
  if (snapshot.state == NetworkManagerState::Connecting) {
    return "connecting";
  }
  if (connectedState(snapshot.state)) {
    return "online";
  }
  if (snapshot.state == NetworkManagerState::Disconnected ||
      snapshot.state == NetworkManagerState::Disconnecting) {
    return "off";
  }
  return "unknown";
}

std::string formatWifiStatus(NetworkManagerSnapshot const& snapshot) {
  if (!snapshot.wirelessHardwareEnabled) {
    return "unavailable";
  }
  if (!snapshot.wirelessEnabled) {
    return "off";
  }

  bool hasWifi = false;
  bool hasConnectingWifi = false;
  bool hasActivatedWifi = false;
  std::string activatedFallback;

  for (auto const& device : snapshot.devices) {
    if (device.type != NetworkDeviceType::Wifi) {
      continue;
    }
    hasWifi = true;
    hasConnectingWifi = hasConnectingWifi || connectingState(device.state);
    if (device.state == NetworkDeviceState::Activated) {
      hasActivatedWifi = true;
      if (!device.activeAccessPoint.ssid.empty()) {
        return device.activeAccessPoint.ssid;
      }
      if (activatedFallback.empty()) {
        activatedFallback = device.interfaceName.empty() ? "online" : device.interfaceName;
      }
    }
  }

  if (hasActivatedWifi) {
    return activatedFallback.empty() ? "online" : activatedFallback;
  }
  if (hasConnectingWifi) {
    return "connecting";
  }
  return hasWifi ? "off" : "unavailable";
}

} // namespace lambdaui::system
