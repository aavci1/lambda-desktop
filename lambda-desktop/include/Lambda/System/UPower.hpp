#pragma once

/// \file Lambda/System/UPower.hpp
///
/// Minimal UPower client used by Shell status providers.

#include <Lambda/System/DBus.hpp>

#include <cstdint>
#include <functional>
#include <string>

namespace lambdaui::system {

enum class UPowerDeviceState : std::uint32_t {
  Unknown = 0,
  Charging = 1,
  Discharging = 2,
  Empty = 3,
  FullyCharged = 4,
  PendingCharge = 5,
  PendingDischarge = 6,
};

enum class UPowerWarningLevel : std::uint32_t {
  Unknown = 0,
  None = 1,
  Discharging = 2,
  Low = 3,
  Critical = 4,
  Action = 5,
};

struct UPowerDisplayDevice {
  bool present = false;
  bool onBattery = false;
  double percentage = 0.0;
  UPowerDeviceState state = UPowerDeviceState::Unknown;
  UPowerWarningLevel warningLevel = UPowerWarningLevel::Unknown;
  std::int64_t timeToEmptySeconds = 0;
  std::int64_t timeToFullSeconds = 0;
  std::string iconName;

  bool operator==(UPowerDisplayDevice const&) const = default;
};

struct UPowerStatusWatch {
  dbus::Slot displayDeviceChanged;
  dbus::Slot managerChanged;
  dbus::Slot deviceAdded;
  dbus::Slot deviceRemoved;
};

class UPowerClient {
public:
  static constexpr char const* serviceName = "org.freedesktop.UPower";
  static constexpr char const* objectPath = "/org/freedesktop/UPower";
  static constexpr char const* interfaceName = "org.freedesktop.UPower";
  static constexpr char const* deviceInterfaceName = "org.freedesktop.UPower.Device";

  explicit UPowerClient(dbus::Bus bus);

  [[nodiscard]] static UPowerClient connectSystem();

  [[nodiscard]] dbus::Bus& bus() noexcept { return bus_; }
  [[nodiscard]] dbus::Bus const& bus() const noexcept { return bus_; }

  [[nodiscard]] std::string displayDevicePath();
  [[nodiscard]] UPowerDisplayDevice readDisplayDevice();
  [[nodiscard]] dbus::Slot watchDisplayDeviceChanged(std::function<void()> handler);
  [[nodiscard]] UPowerStatusWatch watchStatusChanges(std::function<void()> handler);

private:
  [[nodiscard]] dbus::BasicValue getDeviceProperty(std::string const& path,
                                                   std::string const& name,
                                                   std::string_view signature);
  [[nodiscard]] dbus::BasicValue getManagerProperty(std::string const& name,
                                                    std::string_view signature);

  dbus::Bus bus_;
};

[[nodiscard]] std::string formatUPowerBatteryStatus(UPowerDisplayDevice const& device);

} // namespace lambdaui::system
