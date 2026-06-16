#pragma once

/// \file Lambda/System/NetworkManager.hpp
///
/// Minimal NetworkManager client used by Shell network status providers.

#include <Lambda/System/DBus.hpp>

#include <cstdint>
#include <functional>
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

enum class NetworkConnectivity : std::uint32_t {
  Unknown = 0,
  None = 1,
  Portal = 2,
  Limited = 3,
  Full = 4,
};

enum class NetworkMetered : std::uint32_t {
  Unknown = 0,
  Yes = 1,
  No = 2,
  GuessYes = 3,
  GuessNo = 4,
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

enum class NetworkActiveConnectionState : std::uint32_t {
  Unknown = 0,
  Activating = 1,
  Activated = 2,
  Deactivating = 3,
  Deactivated = 4,
};

enum class NetworkVpnState : std::uint8_t {
  Unknown,
  Inactive,
  Connecting,
  Active,
};

struct NetworkAccessPointSnapshot {
  std::string path;
  std::string ssid;
  std::uint8_t strength = 0;
  std::string hardwareAddress;
  std::uint32_t frequencyMhz = 0;
  std::uint32_t maxBitrateKbps = 0;
  std::int32_t lastSeenSeconds = -1;
  std::uint32_t flags = 0;
  std::uint32_t wpaFlags = 0;
  std::uint32_t rsnFlags = 0;
  bool active = false;
  bool requiresAuthentication = false;

  bool operator==(NetworkAccessPointSnapshot const&) const = default;
};

struct NetworkDeviceSnapshot {
  std::string path;
  std::string interfaceName;
  NetworkDeviceType type = NetworkDeviceType::Unknown;
  NetworkDeviceState state = NetworkDeviceState::Unknown;
  NetworkAccessPointSnapshot activeAccessPoint;
  std::vector<NetworkAccessPointSnapshot> accessPoints;

  bool operator==(NetworkDeviceSnapshot const&) const = default;
};

struct NetworkActiveConnectionSnapshot {
  std::string path;
  std::string id;
  std::string uuid;
  std::string type;
  std::string connectionPath;
  std::string specificObjectPath;
  NetworkActiveConnectionState state = NetworkActiveConnectionState::Unknown;
  bool vpn = false;
  bool defaultRoute = false;
  bool defaultRoute6 = false;
  std::vector<std::string> devicePaths;

  bool operator==(NetworkActiveConnectionSnapshot const&) const = default;
};

struct NetworkSavedConnectionSnapshot {
  std::string path;
  std::string id;
  std::string uuid;
  std::string type;
  std::string filename;
  bool autoconnect = false;
  bool unsaved = false;
  bool vpn = false;

  bool operator==(NetworkSavedConnectionSnapshot const&) const = default;
};

struct NetworkManagerSnapshot {
  NetworkManagerState state = NetworkManagerState::Unknown;
  NetworkConnectivity connectivity = NetworkConnectivity::Unknown;
  NetworkMetered metered = NetworkMetered::Unknown;
  NetworkVpnState vpnState = NetworkVpnState::Unknown;
  bool networkingEnabled = false;
  bool wirelessEnabled = false;
  bool wirelessHardwareEnabled = false;
  std::vector<NetworkDeviceSnapshot> devices;
  std::vector<NetworkActiveConnectionSnapshot> activeConnections;
  std::vector<NetworkSavedConnectionSnapshot> savedConnections;

  bool operator==(NetworkManagerSnapshot const&) const = default;
};

struct NetworkManagerStatusWatch {
  dbus::Slot managerChanged;
  dbus::Slot deviceOrAccessPointChanged;
  dbus::Slot deviceAdded;
  dbus::Slot deviceRemoved;
  dbus::Slot accessPointAdded;
  dbus::Slot accessPointRemoved;
  dbus::Slot connectionAdded;
  dbus::Slot connectionRemoved;
};

class NetworkManagerClient {
public:
  static constexpr char const* serviceName = "org.freedesktop.NetworkManager";
  static constexpr char const* objectPath = "/org/freedesktop/NetworkManager";
  static constexpr char const* interfaceName = "org.freedesktop.NetworkManager";
  static constexpr char const* settingsObjectPath = "/org/freedesktop/NetworkManager/Settings";
  static constexpr char const* settingsInterfaceName = "org.freedesktop.NetworkManager.Settings";
  static constexpr char const* settingsConnectionInterfaceName =
      "org.freedesktop.NetworkManager.Settings.Connection";
  static constexpr char const* activeConnectionInterfaceName =
      "org.freedesktop.NetworkManager.Connection.Active";
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
  [[nodiscard]] dbus::Slot watchManagerChanged(std::function<void()> handler);
  [[nodiscard]] NetworkManagerStatusWatch watchStatusChanges(std::function<void()> handler);
  void setWirelessEnabled(bool enabled);

private:
  [[nodiscard]] dbus::BasicValue getManagerProperty(std::string const& name,
                                                    std::string_view signature);
  [[nodiscard]] dbus::BasicValue getDeviceProperty(std::string const& path,
                                                   std::string const& interface,
                                                   std::string const& name,
                                                   std::string_view signature);
  [[nodiscard]] dbus::BasicValue getActiveConnectionProperty(std::string const& path,
                                                             std::string const& name,
                                                             std::string_view signature);
  [[nodiscard]] dbus::BasicValue getSettingsConnectionProperty(std::string const& path,
                                                               std::string const& name,
                                                               std::string_view signature);
  [[nodiscard]] std::vector<std::string> accessPointPaths(std::string const& devicePath);
  [[nodiscard]] std::vector<std::string> activeConnectionPaths();
  [[nodiscard]] std::vector<std::string> savedConnectionPaths();
  [[nodiscard]] NetworkDeviceSnapshot readDevice(std::string const& path);
  [[nodiscard]] NetworkAccessPointSnapshot readAccessPoint(std::string const& path);
  [[nodiscard]] NetworkActiveConnectionSnapshot readActiveConnection(std::string const& path);
  [[nodiscard]] NetworkSavedConnectionSnapshot readSavedConnection(std::string const& path);

  dbus::Bus bus_;
};

[[nodiscard]] std::string formatNetworkStatus(NetworkManagerSnapshot const& snapshot);
[[nodiscard]] std::string formatWifiStatus(NetworkManagerSnapshot const& snapshot);

} // namespace lambda::system
