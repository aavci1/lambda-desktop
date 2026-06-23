#pragma once

/// \file Lambda/System/PortalAccount.hpp
///
/// Minimal xdg-desktop-portal Account backend support.

#include <Lambda/System/DBus.hpp>

#include <optional>
#include <string>

namespace lambdaui::system {

struct PortalAccountState {
  std::string id;
  std::string name;
  std::string imageUri;
};

struct PortalAccountRequest {
  dbus::ObjectPath handle;
  std::string appId;
  std::string window;
  std::string reason;
};

class PortalAccountService {
public:
  static constexpr char const* serviceName = "org.freedesktop.impl.portal.desktop.lambda";
  static constexpr char const* objectPath = "/org/freedesktop/portal/desktop";
  static constexpr char const* interfaceName = "org.freedesktop.impl.portal.Account";

  explicit PortalAccountService(dbus::Bus& bus, PortalAccountState state = stateFromSystem());

  [[nodiscard]] static PortalAccountState stateFromSystem();

  [[nodiscard]] dbus::ObjectDefinition objectDefinition();
  [[nodiscard]] dbus::Slot exportObject();

  [[nodiscard]] PortalAccountState const& state() const noexcept { return state_; }
  [[nodiscard]] std::optional<PortalAccountRequest> const& lastRequest() const noexcept {
    return lastRequest_;
  }

private:
  [[nodiscard]] dbus::MethodReply getUserInformation(dbus::Message& message);

  dbus::Bus* bus_ = nullptr;
  PortalAccountState state_;
  std::optional<PortalAccountRequest> lastRequest_;
};

} // namespace lambdaui::system
