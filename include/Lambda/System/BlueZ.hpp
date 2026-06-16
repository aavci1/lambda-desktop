#pragma once

/// \file Lambda/System/BlueZ.hpp
///
/// Minimal BlueZ client used by Shell Bluetooth status providers.

#include <Lambda/System/DBus.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace lambda::system {

struct BlueZAdapterSnapshot {
  std::string path;
  std::string address;
  std::string addressType;
  std::string name;
  std::string alias;
  std::string modalias;
  std::string powerState;
  std::uint32_t deviceClass = 0;
  bool powered = false;
  bool discoverable = false;
  bool pairable = false;
  bool connectable = false;
  bool discovering = false;
  std::vector<std::string> uuids;
  std::vector<std::string> roles;

  bool operator==(BlueZAdapterSnapshot const&) const = default;
};

struct BlueZDeviceSnapshot {
  std::string path;
  std::string adapterPath;
  std::string address;
  std::string addressType;
  std::string alias;
  std::string name;
  std::string iconName;
  std::string modalias;
  std::uint32_t deviceClass = 0;
  std::uint16_t appearance = 0;
  bool paired = false;
  bool connected = false;
  bool trusted = false;
  bool blocked = false;
  bool legacyPairing = false;
  bool servicesResolved = false;
  bool wakeAllowed = false;
  bool hasRssi = false;
  std::int16_t rssi = 0;
  bool hasTxPower = false;
  std::int16_t txPower = 0;
  int batteryPercentage = -1;
  std::string batterySource;
  std::vector<std::string> uuids;

  bool operator==(BlueZDeviceSnapshot const&) const = default;
};

struct BlueZDeviceDisconnectedEvent {
  std::string path;
  std::string reason;
  std::string message;

  bool operator==(BlueZDeviceDisconnectedEvent const&) const = default;
};

struct BlueZSnapshot {
  std::vector<BlueZAdapterSnapshot> adapters;
  std::vector<BlueZDeviceSnapshot> devices;

  bool operator==(BlueZSnapshot const&) const = default;
};

struct BlueZStatusWatch {
  dbus::Slot adapterOrDeviceChanged;
  dbus::Slot interfacesAdded;
  dbus::Slot interfacesRemoved;
  dbus::Slot deviceDisconnected;
};

class BlueZClient {
public:
  static constexpr char const* serviceName = "org.bluez";
  static constexpr char const* objectManagerPath = "/";
  static constexpr char const* objectManagerInterfaceName = "org.freedesktop.DBus.ObjectManager";
  static constexpr char const* adapterInterfaceName = "org.bluez.Adapter1";
  static constexpr char const* deviceInterfaceName = "org.bluez.Device1";
  static constexpr char const* batteryInterfaceName = "org.bluez.Battery1";

  explicit BlueZClient(dbus::Bus bus);

  [[nodiscard]] static BlueZClient connectSystem();

  [[nodiscard]] dbus::Bus& bus() noexcept { return bus_; }
  [[nodiscard]] dbus::Bus const& bus() const noexcept { return bus_; }

  [[nodiscard]] BlueZSnapshot readSnapshot();
  [[nodiscard]] dbus::Slot watchAdapterOrDeviceChanged(std::function<void()> handler);
  [[nodiscard]] dbus::Slot watchDeviceDisconnected(std::function<void(BlueZDeviceDisconnectedEvent)> handler);
  [[nodiscard]] BlueZStatusWatch watchStatusChanges(std::function<void()> handler);
  void setAdapterPowered(std::string const& adapterPath, bool powered);

private:
  [[nodiscard]] dbus::ManagedObjectDictionary managedObjects();

  dbus::Bus bus_;
};

[[nodiscard]] std::string formatBluetoothStatus(BlueZSnapshot const& snapshot);

} // namespace lambda::system
