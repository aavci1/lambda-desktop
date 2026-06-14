#include <Lambda/System/UPower.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace lambda::system {

namespace {

constexpr char kPropertiesInterface[] = "org.freedesktop.DBus.Properties";

} // namespace

UPowerClient::UPowerClient(dbus::Bus bus) : bus_(std::move(bus)) {}

UPowerClient UPowerClient::connectSystem() {
  return UPowerClient(dbus::Bus::open(dbus::BusType::System));
}

std::string UPowerClient::displayDevicePath() {
  dbus::Message reply = bus_.call(dbus::MethodCall{
      .destination = serviceName,
      .path = objectPath,
      .interface = interfaceName,
      .member = "GetDisplayDevice",
      .arguments = {},
  });
  return reply.readObjectPath().value;
}

UPowerDisplayDevice UPowerClient::readDisplayDevice() {
  std::string const path = displayDevicePath();
  UPowerDisplayDevice device;
  device.present = std::get<bool>(getDeviceProperty(path, "IsPresent", "b"));
  device.onBattery = std::get<bool>(getManagerProperty("OnBattery", "b"));
  device.percentage = std::get<double>(getDeviceProperty(path, "Percentage", "d"));
  device.state =
      static_cast<UPowerDeviceState>(std::get<std::uint32_t>(getDeviceProperty(path, "State", "u")));
  device.timeToEmptySeconds = std::get<std::int64_t>(getDeviceProperty(path, "TimeToEmpty", "x"));
  device.timeToFullSeconds = std::get<std::int64_t>(getDeviceProperty(path, "TimeToFull", "x"));
  device.iconName = std::get<std::string>(getDeviceProperty(path, "IconName", "s"));
  return device;
}

dbus::Slot UPowerClient::watchDisplayDeviceChanged(std::function<void()> handler) {
  std::string const path = displayDevicePath();
  return bus_.matchSignal(
      dbus::SignalMatch{
          .sender = serviceName,
          .path = path,
          .interface = kPropertiesInterface,
          .member = "PropertiesChanged",
      },
      [handler = std::move(handler)](dbus::Message& message) mutable {
        if (message.readString() != UPowerClient::deviceInterfaceName) {
          return;
        }
        message.skip("a{sv}");
        message.skip("as");
        if (handler) {
          handler();
        }
      });
}

dbus::BasicValue UPowerClient::getDeviceProperty(std::string const& path,
                                                 std::string const& name,
                                                 std::string_view signature) {
  return bus_.getProperty(dbus::PropertyAddress{
                              .destination = serviceName,
                              .path = path,
                              .interface = deviceInterfaceName,
                              .name = name,
                          },
                          signature);
}

dbus::BasicValue UPowerClient::getManagerProperty(std::string const& name,
                                                  std::string_view signature) {
  return bus_.getProperty(dbus::PropertyAddress{
                              .destination = serviceName,
                              .path = objectPath,
                              .interface = interfaceName,
                              .name = name,
                          },
                          signature);
}

std::string formatUPowerBatteryStatus(UPowerDisplayDevice const& device) {
  if (!device.present) {
    return "unavailable";
  }
  int const rounded = std::clamp(static_cast<int>(std::lround(device.percentage)), 0, 100);
  return std::to_string(rounded) + "%";
}

} // namespace lambda::system
