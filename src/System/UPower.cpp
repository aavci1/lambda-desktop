#include <Lambda/System/UPower.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace lambda::system {

namespace {

constexpr char kPropertiesInterface[] = "org.freedesktop.DBus.Properties";

void notify(std::shared_ptr<std::function<void()>> const& handler) {
  if (handler && *handler) {
    (*handler)();
  }
}

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
        auto const changed = dbus::readPropertiesChanged(message);
        if (changed.interface != UPowerClient::deviceInterfaceName) {
          return;
        }
        if (handler) {
          handler();
        }
      });
}

UPowerStatusWatch UPowerClient::watchStatusChanges(std::function<void()> handler) {
  std::string const displayPath = displayDevicePath();
  auto sharedHandler = std::make_shared<std::function<void()>>(std::move(handler));
  return UPowerStatusWatch{
      .displayDeviceChanged =
          bus_.matchSignal(
              dbus::SignalMatch{
                  .sender = serviceName,
                  .path = displayPath,
                  .interface = kPropertiesInterface,
                  .member = "PropertiesChanged",
              },
              [sharedHandler](dbus::Message& message) {
                auto const changed = dbus::readPropertiesChanged(message);
                if (changed.interface != UPowerClient::deviceInterfaceName) {
                  return;
                }
                notify(sharedHandler);
              }),
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
                if (changed.interface != UPowerClient::interfaceName) {
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
  };
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
