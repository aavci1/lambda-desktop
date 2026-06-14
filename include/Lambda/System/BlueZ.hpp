#pragma once

/// \file Lambda/System/BlueZ.hpp
///
/// Minimal BlueZ client used by Shell Bluetooth status providers.

#include <Lambda/System/DBus.hpp>

#include <string>
#include <vector>

namespace lambda::system {

struct BlueZAdapterSnapshot {
  std::string path;
  std::string address;
  std::string alias;
  bool powered = false;
  bool discovering = false;

  bool operator==(BlueZAdapterSnapshot const&) const = default;
};

struct BlueZDeviceSnapshot {
  std::string path;
  std::string adapterPath;
  std::string address;
  std::string alias;
  std::string name;
  bool paired = false;
  bool connected = false;

  bool operator==(BlueZDeviceSnapshot const&) const = default;
};

struct BlueZSnapshot {
  std::vector<BlueZAdapterSnapshot> adapters;
  std::vector<BlueZDeviceSnapshot> devices;

  bool operator==(BlueZSnapshot const&) const = default;
};

class BlueZClient {
public:
  static constexpr char const* serviceName = "org.bluez";
  static constexpr char const* objectManagerPath = "/";
  static constexpr char const* objectManagerInterfaceName = "org.freedesktop.DBus.ObjectManager";
  static constexpr char const* adapterInterfaceName = "org.bluez.Adapter1";
  static constexpr char const* deviceInterfaceName = "org.bluez.Device1";

  explicit BlueZClient(dbus::Bus bus);

  [[nodiscard]] static BlueZClient connectSystem();

  [[nodiscard]] dbus::Bus& bus() noexcept { return bus_; }
  [[nodiscard]] dbus::Bus const& bus() const noexcept { return bus_; }

  [[nodiscard]] BlueZSnapshot readSnapshot();
  void setAdapterPowered(std::string const& adapterPath, bool powered);

private:
  [[nodiscard]] dbus::ManagedObjectDictionary managedObjects();

  dbus::Bus bus_;
};

[[nodiscard]] std::string formatBluetoothStatus(BlueZSnapshot const& snapshot);

} // namespace lambda::system
