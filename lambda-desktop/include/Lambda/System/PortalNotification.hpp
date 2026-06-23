#pragma once

/// \file Lambda/System/PortalNotification.hpp
///
/// Minimal xdg-desktop-portal Notification backend support.

#include <Lambda/System/DBus.hpp>

#include <cstdint>
#include <map>
#include <string>

namespace lambdaui::system {

struct PortalNotificationKey {
  std::string appId;
  std::string id;

  bool operator<(PortalNotificationKey const& other) const noexcept {
    if (appId != other.appId) return appId < other.appId;
    return id < other.id;
  }

  bool operator==(PortalNotificationKey const&) const = default;
};

class PortalNotificationService {
public:
  static constexpr char const* serviceName = "org.freedesktop.impl.portal.desktop.lambda";
  static constexpr char const* objectPath = "/org/freedesktop/portal/desktop";
  static constexpr char const* interfaceName = "org.freedesktop.impl.portal.Notification";

  explicit PortalNotificationService(dbus::Bus& bus);

  [[nodiscard]] dbus::ObjectDefinition objectDefinition();
  [[nodiscard]] dbus::Slot exportObject();
  [[nodiscard]] dbus::Slot watchNotificationActions();

  [[nodiscard]] std::map<PortalNotificationKey, std::uint32_t> const& daemonIds() const noexcept {
    return daemonIds_;
  }

private:
  [[nodiscard]] dbus::MethodReply addNotification(dbus::Message& message);
  [[nodiscard]] dbus::MethodReply removeNotification(dbus::Message& message);
  void emitActionInvoked(std::uint32_t daemonId, std::string action);

  dbus::Bus* bus_ = nullptr;
  std::map<PortalNotificationKey, std::uint32_t> daemonIds_;
  std::map<std::uint32_t, PortalNotificationKey> portalKeys_;
};

} // namespace lambdaui::system
