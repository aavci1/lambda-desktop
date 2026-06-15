#include <Lambda/System/Secrets.hpp>

#include "DBusTestHelpers.hpp"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using lambda::testing::dbus::pollBus;
using lambda::testing::dbus::startPrivateBus;

template <typename Callback>
class ScopeExit {
public:
  explicit ScopeExit(Callback callback) : callback_(std::move(callback)) {}
  ~ScopeExit() { callback_(); }

private:
  Callback callback_;
};

std::shared_ptr<lambda::dbus::VariantValue> variantString(std::string value) {
  return std::make_shared<lambda::dbus::VariantValue>(
      lambda::dbus::VariantValue{std::move(value)});
}

std::shared_ptr<lambda::dbus::DictionaryValue>
attributes(std::map<std::string, std::string> values) {
  std::vector<lambda::dbus::DictionaryEntry> entries;
  entries.reserve(values.size());
  for (auto& [key, value] : values) {
    entries.push_back(lambda::dbus::DictionaryEntry{.key = std::move(key),
                                                   .value = std::move(value)});
  }
  return std::make_shared<lambda::dbus::DictionaryValue>(
      lambda::dbus::DictionaryValue{.keySignature = "s",
                                    .valueSignature = "s",
                                    .entries = std::move(entries)});
}

std::map<std::string, std::string> attributesFromValue(lambda::dbus::BasicValue const& value) {
  std::map<std::string, std::string> result;
  auto dictionary = std::get<std::shared_ptr<lambda::dbus::DictionaryValue>>(value);
  if (!dictionary) {
    return result;
  }
  for (auto const& entry : dictionary->entries) {
    result[std::get<std::string>(entry.key)] = std::get<std::string>(entry.value);
  }
  return result;
}

std::shared_ptr<lambda::dbus::VariantDictionary>
itemProperties(std::string label, std::map<std::string, std::string> itemAttributes) {
  auto properties = std::make_shared<lambda::dbus::VariantDictionary>();
  properties->values["org.freedesktop.Secret.Item.Label"] = std::move(label);
  properties->values["org.freedesktop.Secret.Item.Attributes"] =
      attributes(std::move(itemAttributes));
  return properties;
}

lambda::dbus::ObjectPathArray paths(std::vector<std::string> values) {
  lambda::dbus::ObjectPathArray array;
  array.values.reserve(values.size());
  for (auto& value : values) {
    array.values.push_back(lambda::dbus::ObjectPath{std::move(value)});
  }
  return array;
}

lambda::system::SecretValue secret(std::string sessionPath, std::string value) {
  return lambda::system::SecretValue{
      .sessionPath = std::move(sessionPath),
      .parameters = {},
      .value = std::vector<std::uint8_t>(value.begin(), value.end()),
      .contentType = "text/plain; charset=utf8",
  };
}

} // namespace

TEST_CASE("Secret Service support is compile-time declared") {
  CHECK((LAMBDA_HAS_DBUS == 0 || LAMBDA_HAS_DBUS == 1));
}

#if LAMBDA_HAS_DBUS

TEST_CASE("SecretsService stores searches retrieves replaces and deletes items") {
  auto privateBus = startPrivateBus();
  if (!privateBus) {
    MESSAGE("Skipping Secret Service integration test because a private bus could not be started");
    return;
  }

  auto serviceBus = lambda::dbus::Bus::openAddress(privateBus->address);
  auto client = lambda::dbus::Bus::openAddress(privateBus->address);
  serviceBus.requestName(lambda::system::SecretsService::serviceName);

  lambda::system::SecretsService secrets(serviceBus);
  auto exports = secrets.exportObjects();

  std::atomic<bool> running = true;
  std::thread serviceThread([&] {
    while (running.load()) {
      pollBus(serviceBus, 25);
    }
  });
  ScopeExit stopServiceThread([&] {
    running = false;
    if (serviceThread.joinable()) {
      serviceThread.join();
    }
  });

  auto openReply = client.call(lambda::dbus::MethodCall{
      .destination = lambda::system::SecretsService::serviceName,
      .path = lambda::system::SecretsService::objectPath,
      .interface = lambda::system::SecretsService::serviceInterfaceName,
      .member = "OpenSession",
      .arguments = {std::string("plain"), variantString("")},
  });
  CHECK(std::get<std::string>(openReply.readVariant("s")).empty());
  std::string const sessionPath = openReply.readObjectPath().value;
  CHECK(sessionPath.find("/org/freedesktop/secrets/session/") == 0);

  auto collections = std::get<lambda::dbus::ObjectPathArray>(
      client.getProperty(lambda::dbus::PropertyAddress{
                             .destination = lambda::system::SecretsService::serviceName,
                             .path = lambda::system::SecretsService::objectPath,
                             .interface = lambda::system::SecretsService::serviceInterfaceName,
                             .name = "Collections",
                         },
                         "ao"));
  REQUIRE(collections.values.size() == 1);
  CHECK(collections.values[0].value == lambda::system::SecretsService::defaultCollectionPath);

  auto aliasReply = client.call(lambda::dbus::MethodCall{
      .destination = lambda::system::SecretsService::serviceName,
      .path = lambda::system::SecretsService::objectPath,
      .interface = lambda::system::SecretsService::serviceInterfaceName,
      .member = "ReadAlias",
      .arguments = {std::string("default")},
  });
  CHECK(aliasReply.readObjectPath().value == lambda::system::SecretsService::defaultCollectionPath);

  auto createReply = client.call(lambda::dbus::MethodCall{
      .destination = lambda::system::SecretsService::serviceName,
      .path = lambda::system::SecretsService::defaultAliasPath,
      .interface = lambda::system::SecretsService::collectionInterfaceName,
      .member = "CreateItem",
      .arguments = {itemProperties("Wi-Fi password",
                                   {{"application", "lambda-tests"},
                                    {"account", "test-network"}}),
                    lambda::system::secretValueToDBus(secret(sessionPath, "first-secret")),
                    true},
  });
  std::string const itemPath = createReply.readObjectPath().value;
  CHECK(createReply.readObjectPath().value == lambda::system::SecretsService::promptNonePath);
  CHECK(itemPath.find(lambda::system::SecretsService::defaultCollectionPath) == 0);

  auto searchReply = client.call(lambda::dbus::MethodCall{
      .destination = lambda::system::SecretsService::serviceName,
      .path = lambda::system::SecretsService::objectPath,
      .interface = lambda::system::SecretsService::serviceInterfaceName,
      .member = "SearchItems",
      .arguments = {attributes({{"application", "lambda-tests"}})},
  });
  auto unlocked = searchReply.readObjectPathArray();
  auto locked = searchReply.readObjectPathArray();
  REQUIRE(unlocked.values.size() == 1);
  CHECK(unlocked.values[0].value == itemPath);
  CHECK(locked.values.empty());

  auto getReply = client.call(lambda::dbus::MethodCall{
      .destination = lambda::system::SecretsService::serviceName,
      .path = lambda::system::SecretsService::objectPath,
      .interface = lambda::system::SecretsService::serviceInterfaceName,
      .member = "GetSecrets",
      .arguments = {paths({itemPath}), lambda::dbus::ObjectPath{sessionPath}},
  });
  auto secretMap = std::get<std::shared_ptr<lambda::dbus::DictionaryValue>>(
      getReply.readBasic("a{o(oayays)}"));
  REQUIRE(secretMap);
  REQUIRE(secretMap->entries.size() == 1);
  CHECK(std::get<lambda::dbus::ObjectPath>(secretMap->entries[0].key).value == itemPath);
  auto retrieved = lambda::system::secretValueFromDBus(secretMap->entries[0].value);
  CHECK(retrieved.sessionPath == sessionPath);
  CHECK(retrieved.value == std::vector<std::uint8_t>({'f', 'i', 'r', 's', 't', '-', 's', 'e', 'c', 'r', 'e', 't'}));

  auto label = std::get<std::string>(
      client.getProperty(lambda::dbus::PropertyAddress{
                             .destination = lambda::system::SecretsService::serviceName,
                             .path = itemPath,
                             .interface = lambda::system::SecretsService::itemInterfaceName,
                             .name = "Label",
                         },
                         "s"));
  CHECK(label == "Wi-Fi password");
  auto itemAttributes = attributesFromValue(
      client.getProperty(lambda::dbus::PropertyAddress{
                             .destination = lambda::system::SecretsService::serviceName,
                             .path = itemPath,
                             .interface = lambda::system::SecretsService::itemInterfaceName,
                             .name = "Attributes",
                         },
                         "a{ss}"));
  CHECK(itemAttributes.at("account") == "test-network");

  auto replaceReply = client.call(lambda::dbus::MethodCall{
      .destination = lambda::system::SecretsService::serviceName,
      .path = lambda::system::SecretsService::defaultCollectionPath,
      .interface = lambda::system::SecretsService::collectionInterfaceName,
      .member = "CreateItem",
      .arguments = {itemProperties("Wi-Fi password updated",
                                   {{"application", "lambda-tests"},
                                    {"account", "test-network"}}),
                    lambda::system::secretValueToDBus(secret(sessionPath, "second-secret")),
                    true},
  });
  CHECK(replaceReply.readObjectPath().value == itemPath);
  CHECK(replaceReply.readObjectPath().value == lambda::system::SecretsService::promptNonePath);

  auto itemSecretReply = client.call(lambda::dbus::MethodCall{
      .destination = lambda::system::SecretsService::serviceName,
      .path = itemPath,
      .interface = lambda::system::SecretsService::itemInterfaceName,
      .member = "GetSecret",
      .arguments = {lambda::dbus::ObjectPath{sessionPath}},
  });
  auto replaced = lambda::system::secretValueFromDBus(itemSecretReply.readBasic("(oayays)"));
  CHECK(replaced.value == std::vector<std::uint8_t>({'s', 'e', 'c', 'o', 'n', 'd', '-', 's', 'e', 'c', 'r', 'e', 't'}));

  auto deleteReply = client.call(lambda::dbus::MethodCall{
      .destination = lambda::system::SecretsService::serviceName,
      .path = itemPath,
      .interface = lambda::system::SecretsService::itemInterfaceName,
      .member = "Delete",
  });
  CHECK(deleteReply.readObjectPath().value == lambda::system::SecretsService::promptNonePath);

  auto emptySearch = client.call(lambda::dbus::MethodCall{
      .destination = lambda::system::SecretsService::serviceName,
      .path = lambda::system::SecretsService::objectPath,
      .interface = lambda::system::SecretsService::serviceInterfaceName,
      .member = "SearchItems",
      .arguments = {attributes({{"application", "lambda-tests"}})},
  });
  CHECK(emptySearch.readObjectPathArray().values.empty());
  CHECK(emptySearch.readObjectPathArray().values.empty());
}

#endif
