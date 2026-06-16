#pragma once

/// \file Lambda/System/PortalScreenCast.hpp
///
/// Minimal xdg-desktop-portal ScreenCast backend support.

#include <Lambda/System/DBus.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace lambda::system {

enum class PortalScreenCastRequestKind {
  CreateSession,
  SelectSources,
  Start,
};

struct PortalScreenCastStream {
  std::uint32_t nodeId = 0;
  std::uint64_t pipeWireSerial = 0;
  std::uint32_t sourceType = 1;
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t width = 0;
  std::int32_t height = 0;
  std::string mappingId;
};

struct PortalScreenCastRequest {
  dbus::ObjectPath handle;
  dbus::ObjectPath sessionHandle;
  std::string appId;
  std::string parentWindow;
  PortalScreenCastRequestKind kind = PortalScreenCastRequestKind::CreateSession;
  dbus::VariantDictionary options;
  std::uint32_t response = 0;
  bool closed = false;
};

struct PortalScreenCastSession {
  dbus::ObjectPath handle;
  std::string sessionId;
  std::string appId;
  dbus::VariantDictionary createOptions;
  dbus::VariantDictionary selectOptions;
  std::vector<PortalScreenCastStream> streams;
  bool sourcesSelected = false;
  bool started = false;
  bool closed = false;
};

class PortalScreenCastService {
public:
  static constexpr char const* serviceName = "org.freedesktop.impl.portal.desktop.lambda";
  static constexpr char const* objectPath = "/org/freedesktop/portal/desktop";
  static constexpr char const* interfaceName = "org.freedesktop.impl.portal.ScreenCast";
  static constexpr char const* requestInterfaceName = "org.freedesktop.impl.portal.Request";
  static constexpr char const* sessionInterfaceName = "org.freedesktop.impl.portal.Session";

  explicit PortalScreenCastService(dbus::Bus& bus);

  [[nodiscard]] dbus::ObjectDefinition objectDefinition();
  [[nodiscard]] dbus::Slot exportObject();

  [[nodiscard]] std::optional<PortalScreenCastRequest> request(std::string const& handle) const;
  [[nodiscard]] std::optional<PortalScreenCastSession> session(std::string const& handle) const;

private:
  [[nodiscard]] dbus::MethodReply createSession(dbus::Message& message);
  [[nodiscard]] dbus::MethodReply selectSources(dbus::Message& message);
  [[nodiscard]] dbus::MethodReply start(dbus::Message& message);
  [[nodiscard]] dbus::MethodReply closeRequest(std::string const& handle);
  [[nodiscard]] dbus::MethodReply closeSession(std::string const& handle);

  void exportRequestObject(std::string const& handle);
  void exportSessionObject(std::string const& handle);

  dbus::Bus* bus_ = nullptr;
  std::map<std::string, PortalScreenCastRequest> requests_;
  std::map<std::string, PortalScreenCastSession> sessions_;
  std::map<std::string, dbus::Slot> requestSlots_;
  std::map<std::string, dbus::Slot> sessionSlots_;
};

} // namespace lambda::system
