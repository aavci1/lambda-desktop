#pragma once

/// \file Lambda/System/PortalFileChooser.hpp
///
/// Minimal xdg-desktop-portal FileChooser backend support.

#include <Lambda/System/DBus.hpp>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace lambda::system {

enum class PortalFileChooserKind {
  OpenFile,
  SaveFile,
  SaveFiles,
};

struct PortalFileChooserRequest {
  dbus::ObjectPath handle;
  std::string appId;
  std::string parentWindow;
  std::string title;
  PortalFileChooserKind kind = PortalFileChooserKind::OpenFile;
  dbus::VariantDictionary options;
  std::vector<std::string> uris;
  bool closed = false;
};

class PortalFileChooserService {
public:
  static constexpr char const* serviceName = "org.freedesktop.impl.portal.desktop.lambda";
  static constexpr char const* objectPath = "/org/freedesktop/portal/desktop";
  static constexpr char const* interfaceName = "org.freedesktop.impl.portal.FileChooser";
  static constexpr char const* requestInterfaceName = "org.freedesktop.impl.portal.Request";

  explicit PortalFileChooserService(dbus::Bus& bus);

  [[nodiscard]] dbus::ObjectDefinition objectDefinition();
  [[nodiscard]] dbus::Slot exportObject();

  [[nodiscard]] std::optional<PortalFileChooserRequest> request(std::string const& handle) const;

private:
  [[nodiscard]] dbus::MethodReply openFile(dbus::Message& message);
  [[nodiscard]] dbus::MethodReply saveFile(dbus::Message& message);
  [[nodiscard]] dbus::MethodReply saveFiles(dbus::Message& message);
  [[nodiscard]] dbus::MethodReply choose(dbus::Message& message, PortalFileChooserKind kind);
  [[nodiscard]] dbus::MethodReply closeRequest(std::string const& handle);

  void exportRequestObject(std::string const& handle);

  dbus::Bus* bus_ = nullptr;
  std::map<std::string, PortalFileChooserRequest> requests_;
  std::map<std::string, dbus::Slot> requestSlots_;
};

} // namespace lambda::system
