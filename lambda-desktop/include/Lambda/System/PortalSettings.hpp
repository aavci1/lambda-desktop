#pragma once

/// \file Lambda/System/PortalSettings.hpp
///
/// Minimal xdg-desktop-portal Settings backend support.

#include <Lambda/System/DBus.hpp>

#include <filesystem>
#include <optional>

namespace lambdaui::system {

enum class PortalColorScheme : std::uint32_t {
  NoPreference = 0,
  PreferDark = 1,
  PreferLight = 2,
};

struct PortalAccentColor {
  double red = 0.0;
  double green = 0.4784313725;
  double blue = 1.0;

  [[nodiscard]] bool valid() const noexcept;
};

struct PortalSettingsState {
  PortalColorScheme colorScheme = PortalColorScheme::NoPreference;
  std::optional<PortalAccentColor> accentColor = PortalAccentColor{};
  bool highContrast = false;
  bool reducedMotion = false;
};

class PortalSettingsService {
public:
  static constexpr char const* serviceName = "org.freedesktop.impl.portal.desktop.lambda";
  static constexpr char const* objectPath = "/org/freedesktop/portal/desktop";
  static constexpr char const* interfaceName = "org.freedesktop.impl.portal.Settings";
  static constexpr char const* appearanceNamespace = "org.freedesktop.appearance";

  PortalSettingsService(dbus::Bus& bus, PortalSettingsState state = {});

  [[nodiscard]] dbus::ObjectDefinition objectDefinition();
  [[nodiscard]] dbus::Slot exportObject();
  [[nodiscard]] dbus::BasicValue read(std::string const& nameSpace, std::string const& key) const;
  [[nodiscard]] dbus::NamespacedVariantDictionary readAll(dbus::StringArray const& namespaces) const;

  void setState(PortalSettingsState state);
  [[nodiscard]] PortalSettingsState const& state() const noexcept { return state_; }

  void emitChanged(std::string const& key) const;

  [[nodiscard]] static PortalSettingsState stateFromEnvironment();
  [[nodiscard]] static PortalSettingsState stateFromShellConfig(std::filesystem::path path = {});
  [[nodiscard]] static std::filesystem::path shellConfigPathFromEnvironment();

private:
  [[nodiscard]] std::map<std::string, dbus::BasicValue> appearanceSettings() const;
  [[nodiscard]] bool namespaceMatches(std::string const& requested, std::string const& available) const;

  dbus::Bus* bus_ = nullptr;
  PortalSettingsState state_;
};

} // namespace lambdaui::system
