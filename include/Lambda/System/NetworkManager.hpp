#pragma once

/// \file Lambda/System/NetworkManager.hpp
///
/// Minimal NetworkManager client used by Shell network status providers.

#include <Lambda/System/DBus.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lambda::system {

enum class NetworkManagerState : std::uint32_t {
  Unknown = 0,
  Disabled = 10,
  Disconnected = 20,
  Disconnecting = 30,
  Connecting = 40,
  ConnectedLocal = 50,
  ConnectedSite = 60,
  ConnectedGlobal = 70,
};

enum class NetworkDeviceType : std::uint32_t {
  Unknown = 0,
  Ethernet = 1,
  Wifi = 2,
};

enum class NetworkDeviceState : std::uint32_t {
  Unknown = 0,
  Unmanaged = 10,
  Unavailable = 20,
  Disconnected = 30,
  Prepare = 40,
  Config = 50,
  NeedAuth = 60,
  IpConfig = 70,
  IpCheck = 80,
  Secondaries = 90,
  Activated = 100,
  Deactivating = 110,
  Failed = 120,
};

struct NetworkAccessPointSnapshot {
  std::string path;
  std::string ssid;
  std::uint8_t strength = 0;

  bool operator==(NetworkAccessPointSnapshot const&) const = default;
};

struct NetworkDeviceSnapshot {
  std::string path;
  std::string interfaceName;
  NetworkDeviceType type = NetworkDeviceType::Unknown;
  NetworkDeviceState state = NetworkDeviceState::Unknown;
  NetworkAccessPointSnapshot activeAccessPoint;

  bool operator==(NetworkDeviceSnapshot const&) const = default;
};

struct NetworkManagerSnapshot {
  NetworkManagerState state = NetworkManagerState::Unknown;
  bool networkingEnabled = false;
  bool wirelessEnabled = false;
  bool wirelessHardwareEnabled = false;
  std::vector<NetworkDeviceSnapshot> devices;

  bool operator==(NetworkManagerSnapshot const&) const = default;
};

class NetworkManagerClient {
public:
  static constexpr char const* serviceName = "org.freedesktop.NetworkManager";
  static constexpr char const* objectPath = "/org/freedesktop/NetworkManager";
  static constexpr char const* interfaceName = "org.freedesktop.NetworkManager";
  static constexpr char const* deviceInterfaceName = "org.freedesktop.NetworkManager.Device";
  static constexpr char const* wirelessDeviceInterfaceName =
      "org.freedesktop.NetworkManager.Device.Wireless";
  static constexpr char const* accessPointInterfaceName =
      "org.freedesktop.NetworkManager.AccessPoint";

  explicit NetworkManagerClient(dbus::Bus bus);

  [[nodiscard]] static NetworkManagerClient connectSystem();

  [[nodiscard]] dbus::Bus& bus() noexcept { return bus_; }
  [[nodiscard]] dbus::Bus const& bus() const noexcept { return bus_; }

  [[nodiscard]] std::vector<std::string> devicePaths();
  [[nodiscard]] NetworkManagerSnapshot readSnapshot();
  void setWirelessEnabled(bool enabled);

private:
  [[nodiscard]] dbus::BasicValue getManagerProperty(std::string const& name,
                                                    std::string_view signature);
  [[nodiscard]] dbus::BasicValue getDeviceProperty(std::string const& path,
                                                   std::string const& interface,
                                                   std::string const& name,
                                                   std::string_view signature);
  [[nodiscard]] NetworkDeviceSnapshot readDevice(std::string const& path);
  [[nodiscard]] NetworkAccessPointSnapshot readAccessPoint(std::string const& path);

  dbus::Bus bus_;
};

[[nodiscard]] std::string formatNetworkStatus(NetworkManagerSnapshot const& snapshot);
[[nodiscard]] std::string formatWifiStatus(NetworkManagerSnapshot const& snapshot);

} // namespace lambda::system
