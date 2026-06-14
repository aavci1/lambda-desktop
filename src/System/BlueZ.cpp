#include <Lambda/System/BlueZ.hpp>

#include <algorithm>
#include <map>
#include <utility>
#include <vector>

namespace lambda::system {

namespace {

using InterfaceProperties = std::map<std::string, dbus::BasicValue>;

std::string stringProperty(InterfaceProperties const& properties, std::string const& name) {
  auto const it = properties.find(name);
  if (it == properties.end()) {
    return {};
  }
  if (auto value = std::get_if<std::string>(&it->second)) {
    return *value;
  }
  return {};
}

bool boolProperty(InterfaceProperties const& properties, std::string const& name) {
  auto const it = properties.find(name);
  if (it == properties.end()) {
    return false;
  }
  if (auto value = std::get_if<bool>(&it->second)) {
    return *value;
  }
  return false;
}

std::string objectPathProperty(InterfaceProperties const& properties, std::string const& name) {
  auto const it = properties.find(name);
  if (it == properties.end()) {
    return {};
  }
  if (auto value = std::get_if<dbus::ObjectPath>(&it->second)) {
    return value->value;
  }
  return {};
}

BlueZAdapterSnapshot adapterSnapshot(std::string const& path, InterfaceProperties const& properties) {
  return BlueZAdapterSnapshot{
      .path = path,
      .address = stringProperty(properties, "Address"),
      .alias = stringProperty(properties, "Alias"),
      .powered = boolProperty(properties, "Powered"),
      .discovering = boolProperty(properties, "Discovering"),
  };
}

BlueZDeviceSnapshot deviceSnapshot(std::string const& path, InterfaceProperties const& properties) {
  return BlueZDeviceSnapshot{
      .path = path,
      .adapterPath = objectPathProperty(properties, "Adapter"),
      .address = stringProperty(properties, "Address"),
      .alias = stringProperty(properties, "Alias"),
      .name = stringProperty(properties, "Name"),
      .paired = boolProperty(properties, "Paired"),
      .connected = boolProperty(properties, "Connected"),
  };
}

std::string displayName(BlueZDeviceSnapshot const& device) {
  if (!device.alias.empty()) {
    return device.alias;
  }
  if (!device.name.empty()) {
    return device.name;
  }
  return device.address;
}

bool adapterPowered(BlueZSnapshot const& snapshot, std::string const& adapterPath) {
  return std::any_of(snapshot.adapters.begin(), snapshot.adapters.end(), [&](auto const& adapter) {
    return adapter.path == adapterPath && adapter.powered;
  });
}

} // namespace

BlueZClient::BlueZClient(dbus::Bus bus) : bus_(std::move(bus)) {}

BlueZClient BlueZClient::connectSystem() {
  return BlueZClient(dbus::Bus::open(dbus::BusType::System));
}

BlueZSnapshot BlueZClient::readSnapshot() {
  BlueZSnapshot snapshot;
  auto objects = managedObjects();
  for (auto const& [path, interfaces] : objects.values) {
    if (auto adapter = interfaces.find(adapterInterfaceName); adapter != interfaces.end()) {
      snapshot.adapters.push_back(adapterSnapshot(path, adapter->second));
    }
    if (auto device = interfaces.find(deviceInterfaceName); device != interfaces.end()) {
      snapshot.devices.push_back(deviceSnapshot(path, device->second));
    }
  }
  return snapshot;
}

void BlueZClient::setAdapterPowered(std::string const& adapterPath, bool powered) {
  bus_.setProperty(dbus::PropertyAddress{
                       .destination = serviceName,
                       .path = adapterPath,
                       .interface = adapterInterfaceName,
                       .name = "Powered",
                   },
                   powered);
}

dbus::Slot BlueZClient::watchAdapterOrDeviceChanged(std::function<void()> handler) {
  return bus_.matchSignal(
      dbus::SignalMatch{
          .sender = serviceName,
          .path = {},
          .interface = "org.freedesktop.DBus.Properties",
          .member = "PropertiesChanged",
      },
      [handler = std::move(handler)](dbus::Message& message) mutable {
        auto const changed = dbus::readPropertiesChanged(message);
        if (changed.interface != BlueZClient::adapterInterfaceName &&
            changed.interface != BlueZClient::deviceInterfaceName) {
          return;
        }
        if (handler) {
          handler();
        }
      });
}

dbus::ManagedObjectDictionary BlueZClient::managedObjects() {
  dbus::Message reply = bus_.call(dbus::MethodCall{
      .destination = serviceName,
      .path = objectManagerPath,
      .interface = objectManagerInterfaceName,
      .member = "GetManagedObjects",
      .arguments = {},
  });
  return reply.readManagedObjectDictionary();
}

std::string formatBluetoothStatus(BlueZSnapshot const& snapshot) {
  if (snapshot.adapters.empty()) {
    return "unavailable";
  }

  bool hasPoweredAdapter = false;
  std::vector<std::string> connectedNames;
  for (auto const& adapter : snapshot.adapters) {
    hasPoweredAdapter = hasPoweredAdapter || adapter.powered;
  }
  if (!hasPoweredAdapter) {
    return "off";
  }

  for (auto const& device : snapshot.devices) {
    if (!device.connected || !adapterPowered(snapshot, device.adapterPath)) {
      continue;
    }
    std::string name = displayName(device);
    connectedNames.push_back(name.empty() ? "connected" : name);
  }

  if (connectedNames.empty()) {
    return "on";
  }
  if (connectedNames.size() == 1) {
    return connectedNames.front();
  }
  return std::to_string(connectedNames.size()) + " devices";
}

} // namespace lambda::system
