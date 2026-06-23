#pragma once

/// \file Lambda/System/PolkitAgent.hpp
///
/// Minimal polkit authentication-agent registration support.

#include <Lambda/System/DBus.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace lambdaui::system {

struct PolkitSubject {
  std::string kind;
  dbus::VariantDictionary details;
};

struct PolkitAuthenticationRequest {
  std::string actionId;
  std::string message;
  std::string iconName;
  std::map<std::string, std::string> details;
  std::string cookie;
  std::size_t identityCount = 0;
};

[[nodiscard]] PolkitSubject polkitUnixSessionSubject(std::string sessionId);
[[nodiscard]] PolkitSubject polkitUnixProcessSubject(std::uint32_t pid,
                                                     std::int32_t uid,
                                                     std::uint64_t startTime);
[[nodiscard]] PolkitSubject currentPolkitSubject();
[[nodiscard]] dbus::BasicValue polkitSubjectValue(PolkitSubject const& subject);
[[nodiscard]] std::string polkitLocaleFromEnvironment();

class PolkitAuthenticationAgentService {
public:
  static constexpr char const* authorityServiceName = "org.freedesktop.PolicyKit1";
  static constexpr char const* authorityObjectPath = "/org/freedesktop/PolicyKit1/Authority";
  static constexpr char const* authorityInterfaceName = "org.freedesktop.PolicyKit1.Authority";
  static constexpr char const* agentInterfaceName = "org.freedesktop.PolicyKit1.AuthenticationAgent";
  static constexpr char const* defaultObjectPath = "/org/lambda/PolicyKit1/AuthenticationAgent";

  explicit PolkitAuthenticationAgentService(dbus::Bus& bus);
  PolkitAuthenticationAgentService(dbus::Bus& bus,
                                   PolkitSubject subject,
                                   std::string locale = polkitLocaleFromEnvironment(),
                                   std::string objectPath = defaultObjectPath);

  [[nodiscard]] dbus::Slot exportObject();

  void registerAgent();
  void unregisterAgent();

  [[nodiscard]] PolkitSubject const& subject() const noexcept { return subject_; }
  [[nodiscard]] std::string const& locale() const noexcept { return locale_; }
  [[nodiscard]] std::string const& objectPath() const noexcept { return objectPath_; }
  [[nodiscard]] std::optional<PolkitAuthenticationRequest> const& lastAuthentication() const noexcept {
    return lastAuthentication_;
  }
  [[nodiscard]] std::vector<std::string> const& cancelledCookies() const noexcept {
    return cancelledCookies_;
  }

private:
  [[nodiscard]] dbus::MethodReply beginAuthentication(dbus::Message& message);
  [[nodiscard]] dbus::MethodReply cancelAuthentication(dbus::Message& message);

  dbus::Bus* bus_ = nullptr;
  PolkitSubject subject_;
  std::string locale_;
  std::string objectPath_;
  std::optional<PolkitAuthenticationRequest> lastAuthentication_;
  std::vector<std::string> cancelledCookies_;
};

} // namespace lambdaui::system
