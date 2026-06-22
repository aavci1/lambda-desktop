#pragma once

/// \file Lambda/System/PortalInhibit.hpp
///
/// Minimal xdg-desktop-portal Inhibit backend support.

#include <Lambda/System/DBus.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace lambda::system {

struct PortalInhibitRequest {
  dbus::ObjectPath handle;
  std::string appId;
  std::string window;
  std::uint32_t flags = 0;
  std::string reason;
  bool closed = false;
};

struct PortalInhibitMonitor {
  dbus::ObjectPath handle;
  dbus::ObjectPath sessionHandle;
  std::string appId;
  std::string window;
  bool queryEndAcknowledged = false;
};

class PortalInhibitService {
public:
  static constexpr char const* serviceName = "org.freedesktop.impl.portal.desktop.lambda";
  static constexpr char const* objectPath = "/org/freedesktop/portal/desktop";
  static constexpr char const* interfaceName = "org.freedesktop.impl.portal.Inhibit";
  static constexpr char const* requestInterfaceName = "org.freedesktop.impl.portal.Request";

  explicit PortalInhibitService(dbus::Bus& bus);

  [[nodiscard]] dbus::ObjectDefinition objectDefinition();
  [[nodiscard]] dbus::Slot exportObject();

  [[nodiscard]] std::optional<PortalInhibitRequest> request(std::string const& handle) const;
  [[nodiscard]] std::optional<PortalInhibitMonitor> monitor(std::string const& sessionHandle) const;

private:
  [[nodiscard]] dbus::MethodReply inhibit(dbus::Message& message);
  [[nodiscard]] dbus::MethodReply createMonitor(dbus::Message& message);
  [[nodiscard]] dbus::MethodReply queryEndResponse(dbus::Message& message);
  [[nodiscard]] dbus::MethodReply closeRequest(std::string const& handle);

  void exportRequestObject(std::string const& handle);

  dbus::Bus* bus_ = nullptr;
  std::map<std::string, PortalInhibitRequest> requests_;
  std::map<std::string, PortalInhibitMonitor> monitors_;
  std::map<std::string, dbus::Slot> requestSlots_;
};

} // namespace lambda::system
