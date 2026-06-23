#pragma once

/// \file Lambda/System/PortalAppChooser.hpp
///
/// Minimal xdg-desktop-portal AppChooser backend support.

#include <Lambda/System/DBus.hpp>

#include <map>
#include <optional>
#include <string>

namespace lambdaui::system {

struct PortalAppChooserRequest {
  dbus::ObjectPath handle;
  std::string appId;
  std::string parentWindow;
  dbus::StringArray choices;
  dbus::VariantDictionary options;
};

class PortalAppChooserService {
public:
  static constexpr char const* serviceName = "org.freedesktop.impl.portal.desktop.lambda";
  static constexpr char const* objectPath = "/org/freedesktop/portal/desktop";
  static constexpr char const* interfaceName = "org.freedesktop.impl.portal.AppChooser";

  explicit PortalAppChooserService(dbus::Bus& bus);

  [[nodiscard]] dbus::ObjectDefinition objectDefinition();
  [[nodiscard]] dbus::Slot exportObject();

  [[nodiscard]] std::optional<PortalAppChooserRequest> const& lastRequest() const noexcept {
    return lastRequest_;
  }

  [[nodiscard]] std::optional<dbus::StringArray> updatedChoices(std::string const& handle) const;

private:
  [[nodiscard]] dbus::MethodReply chooseApplication(dbus::Message& message);
  [[nodiscard]] dbus::MethodReply updateChoices(dbus::Message& message);
  [[nodiscard]] std::optional<std::string> choose(dbus::StringArray const& choices,
                                                  dbus::VariantDictionary const& options) const;

  dbus::Bus* bus_ = nullptr;
  std::optional<PortalAppChooserRequest> lastRequest_;
  std::map<std::string, dbus::StringArray> updatedChoices_;
};

} // namespace lambdaui::system
