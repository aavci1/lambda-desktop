#include <Lambda/System/BlueZ.hpp>

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace lambdaui::system {

namespace {

using InterfaceProperties = std::map<std::string, dbus::BasicValue>;

constexpr char kPropertiesInterface[] = "org.freedesktop.DBus.Properties";

void notify(std::shared_ptr<std::function<void()>> const& handler) {
  if (handler && *handler) {
    (*handler)();
  }
}

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

std::uint8_t byteProperty(InterfaceProperties const& properties, std::string const& name) {
  auto const it = properties.find(name);
  if (it == properties.end()) {
    return 0;
  }
  if (auto value = std::get_if<std::uint8_t>(&it->second)) {
    return *value;
  }
  return 0;
}

std::uint16_t uint16Property(InterfaceProperties const& properties, std::string const& name) {
  auto const it = properties.find(name);
  if (it == properties.end()) {
    return 0;
  }
  if (auto value = std::get_if<std::uint16_t>(&it->second)) {
    return *value;
  }
  return 0;
}

std::uint32_t uint32Property(InterfaceProperties const& properties, std::string const& name) {
  auto const it = properties.find(name);
  if (it == properties.end()) {
    return 0;
  }
  if (auto value = std::get_if<std::uint32_t>(&it->second)) {
    return *value;
  }
  return 0;
}

std::optional<std::int16_t> int16Property(InterfaceProperties const& properties,
                                          std::string const& name) {
  auto const it = properties.find(name);
  if (it == properties.end()) {
    return std::nullopt;
  }
  if (auto value = std::get_if<std::int16_t>(&it->second)) {
    return *value;
  }
  return std::nullopt;
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

std::vector<std::string> stringArrayProperty(InterfaceProperties const& properties,
                                             std::string const& name) {
  auto const it = properties.find(name);
  if (it == properties.end()) {
    return {};
  }
  if (auto value = std::get_if<dbus::StringArray>(&it->second)) {
    return value->values;
  }
  return {};
}

BlueZAdapterSnapshot adapterSnapshot(std::string const& path, InterfaceProperties const& properties) {
  return BlueZAdapterSnapshot{
      .path = path,
      .address = stringProperty(properties, "Address"),
      .addressType = stringProperty(properties, "AddressType"),
      .name = stringProperty(properties, "Name"),
      .alias = stringProperty(properties, "Alias"),
      .modalias = stringProperty(properties, "Modalias"),
      .powerState = stringProperty(properties, "PowerState"),
      .deviceClass = uint32Property(properties, "Class"),
      .powered = boolProperty(properties, "Powered"),
      .discoverable = boolProperty(properties, "Discoverable"),
      .pairable = boolProperty(properties, "Pairable"),
      .connectable = boolProperty(properties, "Connectable"),
      .discovering = boolProperty(properties, "Discovering"),
      .uuids = stringArrayProperty(properties, "UUIDs"),
      .roles = stringArrayProperty(properties, "Roles"),
  };
}

BlueZDeviceSnapshot deviceSnapshot(std::string const& path,
                                   InterfaceProperties const& properties,
                                   InterfaceProperties const* batteryProperties) {
  BlueZDeviceSnapshot device{
      .path = path,
      .adapterPath = objectPathProperty(properties, "Adapter"),
      .address = stringProperty(properties, "Address"),
      .addressType = stringProperty(properties, "AddressType"),
      .alias = stringProperty(properties, "Alias"),
      .name = stringProperty(properties, "Name"),
      .iconName = stringProperty(properties, "Icon"),
      .modalias = stringProperty(properties, "Modalias"),
      .deviceClass = uint32Property(properties, "Class"),
      .appearance = uint16Property(properties, "Appearance"),
      .paired = boolProperty(properties, "Paired"),
      .connected = boolProperty(properties, "Connected"),
      .trusted = boolProperty(properties, "Trusted"),
      .blocked = boolProperty(properties, "Blocked"),
      .legacyPairing = boolProperty(properties, "LegacyPairing"),
      .servicesResolved = boolProperty(properties, "ServicesResolved"),
      .wakeAllowed = boolProperty(properties, "WakeAllowed"),
      .hasRssi = false,
      .rssi = 0,
      .hasTxPower = false,
      .txPower = 0,
      .batteryPercentage = -1,
      .batterySource = {},
      .uuids = stringArrayProperty(properties, "UUIDs"),
  };
  if (auto rssi = int16Property(properties, "RSSI")) {
    device.hasRssi = true;
    device.rssi = *rssi;
  }
  if (auto txPower = int16Property(properties, "TxPower")) {
    device.hasTxPower = true;
    device.txPower = *txPower;
  }
  if (batteryProperties) {
    auto const percentage = byteProperty(*batteryProperties, "Percentage");
    device.batteryPercentage = static_cast<int>(std::min<std::uint8_t>(percentage, 100));
    device.batterySource = stringProperty(*batteryProperties, "Source");
  }
  return device;
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
      auto const battery = interfaces.find(batteryInterfaceName);
      InterfaceProperties const* batteryProperties =
          battery == interfaces.end() ? nullptr : &battery->second;
      snapshot.devices.push_back(deviceSnapshot(path, device->second, batteryProperties));
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

void BlueZClient::startDiscovery(std::string const& adapterPath) {
  (void)bus_.call(dbus::MethodCall{
      .destination = serviceName,
      .path = adapterPath,
      .interface = adapterInterfaceName,
      .member = "StartDiscovery",
      .arguments = {},
  });
}

void BlueZClient::stopDiscovery(std::string const& adapterPath) {
  (void)bus_.call(dbus::MethodCall{
      .destination = serviceName,
      .path = adapterPath,
      .interface = adapterInterfaceName,
      .member = "StopDiscovery",
      .arguments = {},
  });
}

void BlueZClient::removeDevice(std::string const& adapterPath, std::string const& devicePath) {
  (void)bus_.call(dbus::MethodCall{
      .destination = serviceName,
      .path = adapterPath,
      .interface = adapterInterfaceName,
      .member = "RemoveDevice",
      .arguments = {dbus::ObjectPath{devicePath}},
  });
}

void BlueZClient::pairDevice(std::string const& devicePath) {
  (void)bus_.call(dbus::MethodCall{
      .destination = serviceName,
      .path = devicePath,
      .interface = deviceInterfaceName,
      .member = "Pair",
      .arguments = {},
  });
}

void BlueZClient::cancelDevicePairing(std::string const& devicePath) {
  (void)bus_.call(dbus::MethodCall{
      .destination = serviceName,
      .path = devicePath,
      .interface = deviceInterfaceName,
      .member = "CancelPairing",
      .arguments = {},
  });
}

void BlueZClient::connectDevice(std::string const& devicePath) {
  (void)bus_.call(dbus::MethodCall{
      .destination = serviceName,
      .path = devicePath,
      .interface = deviceInterfaceName,
      .member = "Connect",
      .arguments = {},
  });
}

void BlueZClient::disconnectDevice(std::string const& devicePath) {
  (void)bus_.call(dbus::MethodCall{
      .destination = serviceName,
      .path = devicePath,
      .interface = deviceInterfaceName,
      .member = "Disconnect",
      .arguments = {},
  });
}

void BlueZClient::setDeviceTrusted(std::string const& devicePath, bool trusted) {
  bus_.setProperty(dbus::PropertyAddress{
                       .destination = serviceName,
                       .path = devicePath,
                       .interface = deviceInterfaceName,
                       .name = "Trusted",
                   },
                   trusted);
}

void BlueZClient::setDeviceBlocked(std::string const& devicePath, bool blocked) {
  bus_.setProperty(dbus::PropertyAddress{
                       .destination = serviceName,
                       .path = devicePath,
                       .interface = deviceInterfaceName,
                       .name = "Blocked",
                   },
                   blocked);
}

dbus::Slot BlueZClient::watchAdapterOrDeviceChanged(std::function<void()> handler) {
  return bus_.matchSignal(
      dbus::SignalMatch{
          .sender = serviceName,
          .path = {},
          .interface = kPropertiesInterface,
          .member = "PropertiesChanged",
      },
      [handler = std::move(handler)](dbus::Message& message) mutable {
        auto const changed = dbus::readPropertiesChanged(message);
        if (changed.interface != BlueZClient::adapterInterfaceName &&
            changed.interface != BlueZClient::deviceInterfaceName &&
            changed.interface != BlueZClient::batteryInterfaceName) {
          return;
        }
        if (handler) {
          handler();
        }
      });
}

dbus::Slot BlueZClient::watchDeviceDisconnected(
    std::function<void(BlueZDeviceDisconnectedEvent)> handler) {
  return bus_.matchSignal(
      dbus::SignalMatch{
          .sender = serviceName,
          .path = {},
          .interface = deviceInterfaceName,
          .member = "Disconnected",
      },
      [handler = std::move(handler)](dbus::Message& message) mutable {
        if (!handler) {
          return;
        }
        handler(BlueZDeviceDisconnectedEvent{
            .path = message.path(),
            .reason = message.readString(),
            .message = message.readString(),
        });
      });
}

BlueZStatusWatch BlueZClient::watchStatusChanges(std::function<void()> handler) {
  auto sharedHandler = std::make_shared<std::function<void()>>(std::move(handler));
  return BlueZStatusWatch{
      .adapterOrDeviceChanged =
          bus_.matchSignal(
              dbus::SignalMatch{
                  .sender = serviceName,
                  .path = {},
                  .interface = kPropertiesInterface,
                  .member = "PropertiesChanged",
              },
              [sharedHandler](dbus::Message& message) {
                auto const changed = dbus::readPropertiesChanged(message);
                if (changed.interface != BlueZClient::adapterInterfaceName &&
                    changed.interface != BlueZClient::deviceInterfaceName &&
                    changed.interface != BlueZClient::batteryInterfaceName) {
                  return;
                }
                notify(sharedHandler);
              }),
      .interfacesAdded =
          bus_.matchSignal(
              dbus::SignalMatch{
                  .sender = serviceName,
                  .path = objectManagerPath,
                  .interface = objectManagerInterfaceName,
                  .member = "InterfacesAdded",
              },
              [sharedHandler](dbus::Message&) {
                notify(sharedHandler);
              }),
      .interfacesRemoved =
          bus_.matchSignal(
              dbus::SignalMatch{
                  .sender = serviceName,
                  .path = objectManagerPath,
                  .interface = objectManagerInterfaceName,
                  .member = "InterfacesRemoved",
              },
              [sharedHandler](dbus::Message&) {
                notify(sharedHandler);
              }),
      .deviceDisconnected =
          bus_.matchSignal(
              dbus::SignalMatch{
                  .sender = serviceName,
                  .path = {},
                  .interface = deviceInterfaceName,
                  .member = "Disconnected",
              },
              [sharedHandler](dbus::Message&) {
                notify(sharedHandler);
              }),
  };
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

} // namespace lambdaui::system
