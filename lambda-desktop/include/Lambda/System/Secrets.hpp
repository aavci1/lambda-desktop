#pragma once

/// \file Lambda/System/Secrets.hpp
///
/// Minimal Freedesktop Secret Service-compatible in-memory store.

#include <Lambda/System/DBus.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace lambdaui::system {

struct SecretValue {
  std::string sessionPath;
  std::vector<std::uint8_t> parameters;
  std::vector<std::uint8_t> value;
  std::string contentType = "text/plain; charset=utf8";

  bool operator==(SecretValue const&) const = default;
};

struct SecretItemRecord {
  std::string path;
  std::string label;
  std::map<std::string, std::string> attributes;
  SecretValue secret;
  std::uint64_t created = 0;
  std::uint64_t modified = 0;
  bool deleted = false;

  bool operator==(SecretItemRecord const&) const = default;
};

struct SecretServiceExports {
  dbus::Slot service;
  dbus::Slot collection;
  dbus::Slot defaultAlias;
};

class SecretsService {
public:
  static constexpr char const* serviceName = "org.freedesktop.secrets";
  static constexpr char const* objectPath = "/org/freedesktop/secrets";
  static constexpr char const* serviceInterfaceName = "org.freedesktop.Secret.Service";
  static constexpr char const* collectionInterfaceName = "org.freedesktop.Secret.Collection";
  static constexpr char const* itemInterfaceName = "org.freedesktop.Secret.Item";
  static constexpr char const* sessionInterfaceName = "org.freedesktop.Secret.Session";
  static constexpr char const* defaultCollectionPath = "/org/freedesktop/secrets/collection/lambda";
  static constexpr char const* defaultAliasPath = "/org/freedesktop/secrets/aliases/default";
  static constexpr char const* promptNonePath = "/";

  explicit SecretsService(dbus::Bus& bus);

  [[nodiscard]] SecretServiceExports exportObjects();

  [[nodiscard]] std::string createItem(std::string label,
                                       std::map<std::string, std::string> attributes,
                                       SecretValue secret,
                                       bool replace);
  [[nodiscard]] std::vector<std::string> searchItems(std::map<std::string, std::string> const& attributes) const;
  [[nodiscard]] SecretItemRecord const* item(std::string const& path) const;
  [[nodiscard]] std::vector<SecretItemRecord> items() const;
  [[nodiscard]] std::string const& collectionLabel() const noexcept { return collectionLabel_; }

private:
  [[nodiscard]] dbus::ObjectDefinition serviceDefinition();
  [[nodiscard]] dbus::ObjectDefinition collectionDefinition();
  [[nodiscard]] dbus::ObjectDefinition itemDefinition(std::string path);
  [[nodiscard]] dbus::ObjectDefinition sessionDefinition(std::string path);

  [[nodiscard]] std::string createSession(std::string sender);
  void exportItem(std::string const& path);
  [[nodiscard]] SecretItemRecord* item(std::string const& path);

  dbus::Bus* bus_ = nullptr;
  std::string collectionLabel_ = "Lambda Keyring";
  std::uint64_t collectionCreated_ = 0;
  std::uint64_t collectionModified_ = 0;
  std::uint32_t nextItemId_ = 1;
  std::uint32_t nextSessionId_ = 1;
  std::map<std::string, SecretItemRecord> items_;
  std::map<std::string, dbus::Slot> itemSlots_;
  std::map<std::string, dbus::Slot> sessionSlots_;
};

[[nodiscard]] dbus::BasicValue secretValueToDBus(SecretValue const& secret);
[[nodiscard]] SecretValue secretValueFromDBus(dbus::BasicValue const& value);

} // namespace lambdaui::system
