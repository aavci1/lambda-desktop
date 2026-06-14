#include <Lambda/System/NetworkManager.hpp>

#include <algorithm>
#include <utility>

namespace lambda::system {

namespace {

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

bool connectedState(NetworkManagerState state) {
  return state == NetworkManagerState::ConnectedLocal || state == NetworkManagerState::ConnectedSite ||
         state == NetworkManagerState::ConnectedGlobal;
}

bool connectingState(NetworkDeviceState state) {
  return state == NetworkDeviceState::Prepare || state == NetworkDeviceState::Config ||
         state == NetworkDeviceState::NeedAuth || state == NetworkDeviceState::IpConfig ||
         state == NetworkDeviceState::IpCheck || state == NetworkDeviceState::Secondaries;
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

  return snapshot;
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
      }
    } catch (...) {
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
  return accessPoint;
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

} // namespace lambda::system
