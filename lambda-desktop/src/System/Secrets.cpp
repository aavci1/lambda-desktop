#include <Lambda/System/Secrets.hpp>

#include <algorithm>
#include <chrono>
#include <memory>
#include <utility>

namespace lambda::system {

namespace {

constexpr char kCollectionLabelProperty[] = "org.freedesktop.Secret.Collection.Label";
constexpr char kItemLabelProperty[] = "org.freedesktop.Secret.Item.Label";
constexpr char kItemAttributesProperty[] = "org.freedesktop.Secret.Item.Attributes";
constexpr char kErrorNoSuchObject[] = "org.freedesktop.Secret.Error.NoSuchObject";
constexpr char kErrorNoSession[] = "org.freedesktop.Secret.Error.NoSession";

std::uint64_t nowSeconds() {
  using namespace std::chrono;
  return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

dbus::ObjectPath dbusObjectPath(std::string value) {
  return dbus::ObjectPath{std::move(value)};
}

dbus::ObjectPathArray objectPathArray(std::vector<std::string> const& paths) {
  dbus::ObjectPathArray array;
  array.values.reserve(paths.size());
  for (auto const& path : paths) {
    array.values.push_back(dbusObjectPath(path));
  }
  return array;
}

std::shared_ptr<dbus::DictionaryValue>
attributesDictionary(std::map<std::string, std::string> const& attributes) {
  std::vector<dbus::DictionaryEntry> entries;
  entries.reserve(attributes.size());
  for (auto const& [key, value] : attributes) {
    entries.push_back(dbus::DictionaryEntry{.key = key, .value = value});
  }
  return std::make_shared<dbus::DictionaryValue>(
      dbus::DictionaryValue{.keySignature = "s",
                            .valueSignature = "s",
                            .entries = std::move(entries)});
}

std::map<std::string, std::string> attributesFromDictionary(dbus::DictionaryValue const& dictionary) {
  std::map<std::string, std::string> attributes;
  for (auto const& entry : dictionary.entries) {
    attributes[std::get<std::string>(entry.key)] = std::get<std::string>(entry.value);
  }
  return attributes;
}

std::map<std::string, std::string> readAttributes(dbus::Message& message) {
  auto value = message.readBasic("a{ss}");
  auto dictionary = std::get<std::shared_ptr<dbus::DictionaryValue>>(value);
  return dictionary ? attributesFromDictionary(*dictionary) : std::map<std::string, std::string>{};
}

std::optional<std::string> stringProperty(dbus::VariantDictionary const& properties,
                                          std::string const& name) {
  auto it = properties.values.find(name);
  if (it == properties.values.end()) {
    return std::nullopt;
  }
  if (auto value = std::get_if<std::string>(&it->second)) {
    return *value;
  }
  return std::nullopt;
}

std::map<std::string, std::string> attributesProperty(dbus::VariantDictionary const& properties) {
  auto it = properties.values.find(kItemAttributesProperty);
  if (it == properties.values.end()) {
    return {};
  }
  if (auto dictionary = std::get_if<std::shared_ptr<dbus::DictionaryValue>>(&it->second)) {
    return *dictionary ? attributesFromDictionary(**dictionary) : std::map<std::string, std::string>{};
  }
  return {};
}

bool matchesAttributes(std::map<std::string, std::string> const& itemAttributes,
                       std::map<std::string, std::string> const& query) {
  return std::all_of(query.begin(), query.end(), [&](auto const& expected) {
    auto it = itemAttributes.find(expected.first);
    return it != itemAttributes.end() && it->second == expected.second;
  });
}

std::shared_ptr<dbus::DictionaryValue>
secretMap(std::vector<SecretItemRecord const*> const& items, std::string const& sessionPath) {
  std::vector<dbus::DictionaryEntry> entries;
  entries.reserve(items.size());
  for (auto const* item : items) {
    if (!item || item->deleted) {
      continue;
    }
    SecretValue secret = item->secret;
    secret.sessionPath = sessionPath;
    entries.push_back(dbus::DictionaryEntry{.key = dbusObjectPath(item->path),
                                            .value = secretValueToDBus(secret)});
  }
  return std::make_shared<dbus::DictionaryValue>(
      dbus::DictionaryValue{.keySignature = "o",
                            .valueSignature = "(oayays)",
                            .entries = std::move(entries)});
}

dbus::MethodReply methodReply(std::vector<dbus::ReplyValue> values) {
  dbus::MethodReply reply;
  reply.values = std::move(values);
  return reply;
}

} // namespace

dbus::BasicValue secretValueToDBus(SecretValue const& secret) {
  return std::make_shared<dbus::StructValue>(
      dbus::StructValue{.signature = "oayays",
                        .fields = {dbusObjectPath(secret.sessionPath),
                                   dbus::ByteArray{secret.parameters},
                                   dbus::ByteArray{secret.value},
                                   secret.contentType}});
}

SecretValue secretValueFromDBus(dbus::BasicValue const& value) {
  auto structure = std::get<std::shared_ptr<dbus::StructValue>>(value);
  if (!structure || structure->fields.size() != 4) {
    throw dbus::Error(-EINVAL, "read Secret Service secret", "secret struct is invalid");
  }
  return SecretValue{
      .sessionPath = std::get<dbus::ObjectPath>(structure->fields[0]).value,
      .parameters = std::get<dbus::ByteArray>(structure->fields[1]).values,
      .value = std::get<dbus::ByteArray>(structure->fields[2]).values,
      .contentType = std::get<std::string>(structure->fields[3]),
  };
}

SecretsService::SecretsService(dbus::Bus& bus)
    : bus_(&bus),
      collectionCreated_(nowSeconds()),
      collectionModified_(collectionCreated_) {}

SecretServiceExports SecretsService::exportObjects() {
  return SecretServiceExports{
      .service = bus_->exportObject(objectPath, serviceDefinition()),
      .collection = bus_->exportObject(defaultCollectionPath, collectionDefinition()),
      .defaultAlias = bus_->exportObject(defaultAliasPath, collectionDefinition()),
  };
}

std::string SecretsService::createItem(std::string label,
                                       std::map<std::string, std::string> attributes,
                                       SecretValue secret,
                                       bool replace) {
  if (replace) {
    for (auto& [path, item] : items_) {
      if (!item.deleted && item.attributes == attributes) {
        item.label = std::move(label);
        item.secret = std::move(secret);
        item.modified = nowSeconds();
        collectionModified_ = item.modified;
        bus_->emitSignal(defaultCollectionPath, collectionInterfaceName, "ItemChanged", {dbusObjectPath(path)});
        return path;
      }
    }
  }

  std::string path = std::string(defaultCollectionPath) + "/i" + std::to_string(nextItemId_++);
  auto timestamp = nowSeconds();
  auto [it, inserted] = items_.emplace(path,
                                       SecretItemRecord{
                                           .path = path,
                                           .label = std::move(label),
                                           .attributes = std::move(attributes),
                                           .secret = std::move(secret),
                                           .created = timestamp,
                                           .modified = timestamp,
                                       });
  if (inserted) {
    exportItem(path);
    collectionModified_ = timestamp;
    bus_->emitSignal(defaultCollectionPath, collectionInterfaceName, "ItemCreated", {dbusObjectPath(path)});
  }
  return it->first;
}

std::vector<std::string> SecretsService::searchItems(std::map<std::string, std::string> const& attributes) const {
  std::vector<std::string> paths;
  for (auto const& [path, item] : items_) {
    if (!item.deleted && matchesAttributes(item.attributes, attributes)) {
      paths.push_back(path);
    }
  }
  return paths;
}

SecretItemRecord const* SecretsService::item(std::string const& path) const {
  auto it = items_.find(path);
  return it == items_.end() || it->second.deleted ? nullptr : &it->second;
}

SecretItemRecord* SecretsService::item(std::string const& path) {
  auto it = items_.find(path);
  return it == items_.end() || it->second.deleted ? nullptr : &it->second;
}

std::vector<SecretItemRecord> SecretsService::items() const {
  std::vector<SecretItemRecord> result;
  for (auto const& [path, item] : items_) {
    (void)path;
    if (!item.deleted) {
      result.push_back(item);
    }
  }
  return result;
}

dbus::ObjectDefinition SecretsService::serviceDefinition() {
  return dbus::ObjectDefinition{
      .methods = {
          dbus::ExportedMethod{
              .interface = serviceInterfaceName,
              .member = "OpenSession",
              .handler = [this](dbus::Message& message) {
                auto algorithm = message.readString();
                (void)message.readVariant("s");
                if (algorithm != "plain") {
                  return dbus::MethodReply::error("org.freedesktop.DBus.Error.NotSupported",
                                                  "Only the Secret Service plain session algorithm is supported");
                }
                std::string sessionPath = createSession(message.sender());
                return methodReply({dbus::VariantValue{std::string()},
                                    dbus::BasicValue(dbusObjectPath(std::move(sessionPath)))});
              },
          },
          dbus::ExportedMethod{
              .interface = serviceInterfaceName,
              .member = "CreateCollection",
              .handler = [this](dbus::Message& message) {
                auto properties = message.readVariantDictionary();
                auto alias = message.readString();
                if (auto label = stringProperty(properties, kCollectionLabelProperty)) {
                  collectionLabel_ = *label;
                  collectionModified_ = nowSeconds();
                }
                (void)alias;
                return methodReply({dbus::BasicValue(dbusObjectPath(defaultCollectionPath)),
                                    dbus::BasicValue(dbusObjectPath(promptNonePath))});
              },
          },
          dbus::ExportedMethod{
              .interface = serviceInterfaceName,
              .member = "SearchItems",
              .handler = [this](dbus::Message& message) {
                auto paths = searchItems(readAttributes(message));
                return methodReply({dbus::BasicValue(objectPathArray(paths)),
                                    dbus::BasicValue(dbus::ObjectPathArray{})});
              },
          },
          dbus::ExportedMethod{
              .interface = serviceInterfaceName,
              .member = "Unlock",
              .handler = [](dbus::Message& message) {
                auto objects = message.readObjectPathArray();
                return methodReply({dbus::BasicValue(objects),
                                    dbus::BasicValue(dbusObjectPath(promptNonePath))});
              },
          },
          dbus::ExportedMethod{
              .interface = serviceInterfaceName,
              .member = "Lock",
              .handler = [](dbus::Message& message) {
                (void)message.readObjectPathArray();
                return methodReply({dbus::BasicValue(dbus::ObjectPathArray{}),
                                    dbus::BasicValue(dbusObjectPath(promptNonePath))});
              },
          },
          dbus::ExportedMethod{
              .interface = serviceInterfaceName,
              .member = "GetSecrets",
              .handler = [this](dbus::Message& message) {
                auto paths = message.readObjectPathArray();
                auto session = message.readObjectPath();
                if (sessionSlots_.find(session.value) == sessionSlots_.end()) {
                  return dbus::MethodReply::error(kErrorNoSession, "Secret Service session does not exist");
                }
                std::vector<SecretItemRecord const*> records;
                for (auto const& path : paths.values) {
                  if (auto const* record = item(path.value)) {
                    records.push_back(record);
                  }
                }
                return methodReply({dbus::BasicValue(secretMap(records, session.value))});
              },
          },
          dbus::ExportedMethod{
              .interface = serviceInterfaceName,
              .member = "ReadAlias",
              .handler = [](dbus::Message& message) {
                auto alias = message.readString();
                return methodReply({dbus::BasicValue(dbusObjectPath(alias == "default" ? defaultCollectionPath
                                                                                       : promptNonePath))});
              },
          },
          dbus::ExportedMethod{
              .interface = serviceInterfaceName,
              .member = "SetAlias",
              .handler = [](dbus::Message& message) {
                (void)message.readString();
                (void)message.readObjectPath();
                return dbus::MethodReply{};
              },
          },
      },
      .properties = {
          dbus::ExportedProperty{
              .interface = serviceInterfaceName,
              .name = "Collections",
              .value = dbus::ObjectPathArray{},
              .writable = false,
              .getter = [] {
                return dbus::BasicValue(objectPathArray({defaultCollectionPath}));
              },
              .setter = nullptr,
          },
      },
  };
}

dbus::ObjectDefinition SecretsService::collectionDefinition() {
  return dbus::ObjectDefinition{
      .methods = {
          dbus::ExportedMethod{
              .interface = collectionInterfaceName,
              .member = "Delete",
              .handler = [](dbus::Message&) {
                return methodReply({dbus::BasicValue(dbusObjectPath(promptNonePath))});
              },
          },
          dbus::ExportedMethod{
              .interface = collectionInterfaceName,
              .member = "SearchItems",
              .handler = [this](dbus::Message& message) {
                return methodReply({dbus::BasicValue(objectPathArray(searchItems(readAttributes(message))))});
              },
          },
          dbus::ExportedMethod{
              .interface = collectionInterfaceName,
              .member = "CreateItem",
              .handler = [this](dbus::Message& message) {
                auto properties = message.readVariantDictionary();
                auto secret = secretValueFromDBus(message.readBasic("(oayays)"));
                bool const replace = message.readBool();
                std::string path = createItem(stringProperty(properties, kItemLabelProperty).value_or("Secret"),
                                              attributesProperty(properties),
                                              std::move(secret),
                                              replace);
                return methodReply({dbus::BasicValue(dbusObjectPath(std::move(path))),
                                    dbus::BasicValue(dbusObjectPath(promptNonePath))});
              },
          },
      },
      .properties = {
          dbus::ExportedProperty{
              .interface = collectionInterfaceName,
              .name = "Items",
              .value = dbus::ObjectPathArray{},
              .writable = false,
              .getter = [this] {
                return dbus::BasicValue(objectPathArray(searchItems({})));
              },
              .setter = nullptr,
          },
          dbus::ExportedProperty{
              .interface = collectionInterfaceName,
              .name = "Label",
              .value = std::string("Lambda Keyring"),
              .writable = true,
              .getter = [this] {
                return dbus::BasicValue(collectionLabel_);
              },
              .setter = [this](dbus::BasicValue const& value) {
                collectionLabel_ = std::get<std::string>(value);
                collectionModified_ = nowSeconds();
              },
          },
          dbus::ExportedProperty{
              .interface = collectionInterfaceName,
              .name = "Locked",
              .value = false,
              .writable = false,
              .getter = nullptr,
              .setter = nullptr,
          },
          dbus::ExportedProperty{
              .interface = collectionInterfaceName,
              .name = "Created",
              .value = std::uint64_t(0),
              .writable = false,
              .getter = [this] {
                return dbus::BasicValue(collectionCreated_);
              },
              .setter = nullptr,
          },
          dbus::ExportedProperty{
              .interface = collectionInterfaceName,
              .name = "Modified",
              .value = std::uint64_t(0),
              .writable = false,
              .getter = [this] {
                return dbus::BasicValue(collectionModified_);
              },
              .setter = nullptr,
          },
      },
  };
}

dbus::ObjectDefinition SecretsService::itemDefinition(std::string path) {
  return dbus::ObjectDefinition{
      .methods = {
          dbus::ExportedMethod{
              .interface = itemInterfaceName,
              .member = "Delete",
              .handler = [this, path](dbus::Message&) {
                auto* record = item(path);
                if (!record) {
                  return dbus::MethodReply::error(kErrorNoSuchObject, "Secret item does not exist");
                }
                record->deleted = true;
                record->modified = nowSeconds();
                collectionModified_ = record->modified;
                bus_->emitSignal(defaultCollectionPath, collectionInterfaceName, "ItemDeleted", {dbusObjectPath(path)});
                return methodReply({dbus::BasicValue(dbusObjectPath(promptNonePath))});
              },
          },
          dbus::ExportedMethod{
              .interface = itemInterfaceName,
              .member = "GetSecret",
              .handler = [this, path](dbus::Message& message) {
                auto session = message.readObjectPath();
                if (sessionSlots_.find(session.value) == sessionSlots_.end()) {
                  return dbus::MethodReply::error(kErrorNoSession, "Secret Service session does not exist");
                }
                auto const* record = item(path);
                if (!record) {
                  return dbus::MethodReply::error(kErrorNoSuchObject, "Secret item does not exist");
                }
                SecretValue secret = record->secret;
                secret.sessionPath = session.value;
                return methodReply({secretValueToDBus(secret)});
              },
          },
          dbus::ExportedMethod{
              .interface = itemInterfaceName,
              .member = "SetSecret",
              .handler = [this, path](dbus::Message& message) {
                auto* record = item(path);
                if (!record) {
                  return dbus::MethodReply::error(kErrorNoSuchObject, "Secret item does not exist");
                }
                record->secret = secretValueFromDBus(message.readBasic("(oayays)"));
                record->modified = nowSeconds();
                collectionModified_ = record->modified;
                bus_->emitSignal(defaultCollectionPath, collectionInterfaceName, "ItemChanged", {dbusObjectPath(path)});
                return dbus::MethodReply{};
              },
          },
      },
      .properties = {
          dbus::ExportedProperty{
              .interface = itemInterfaceName,
              .name = "Locked",
              .value = false,
              .writable = false,
              .getter = nullptr,
              .setter = nullptr,
          },
          dbus::ExportedProperty{
              .interface = itemInterfaceName,
              .name = "Attributes",
              .value = attributesDictionary({}),
              .writable = true,
              .getter = [this, path] {
                auto const* record = item(path);
                return dbus::BasicValue(attributesDictionary(record ? record->attributes
                                                                    : std::map<std::string, std::string>{}));
              },
              .setter = [this, path](dbus::BasicValue const& value) {
                if (auto* record = item(path)) {
                  auto dictionary = std::get<std::shared_ptr<dbus::DictionaryValue>>(value);
                  record->attributes = dictionary ? attributesFromDictionary(*dictionary)
                                                  : std::map<std::string, std::string>{};
                  record->modified = nowSeconds();
                }
              },
          },
          dbus::ExportedProperty{
              .interface = itemInterfaceName,
              .name = "Label",
              .value = std::string(),
              .writable = true,
              .getter = [this, path] {
                auto const* record = item(path);
                return dbus::BasicValue(record ? record->label : std::string());
              },
              .setter = [this, path](dbus::BasicValue const& value) {
                if (auto* record = item(path)) {
                  record->label = std::get<std::string>(value);
                  record->modified = nowSeconds();
                }
              },
          },
          dbus::ExportedProperty{
              .interface = itemInterfaceName,
              .name = "Created",
              .value = std::uint64_t(0),
              .writable = false,
              .getter = [this, path] {
                auto const* record = item(path);
                return dbus::BasicValue(record ? record->created : std::uint64_t(0));
              },
              .setter = nullptr,
          },
          dbus::ExportedProperty{
              .interface = itemInterfaceName,
              .name = "Modified",
              .value = std::uint64_t(0),
              .writable = false,
              .getter = [this, path] {
                auto const* record = item(path);
                return dbus::BasicValue(record ? record->modified : std::uint64_t(0));
              },
              .setter = nullptr,
          },
      },
  };
}

dbus::ObjectDefinition SecretsService::sessionDefinition(std::string path) {
  (void)path;
  return dbus::ObjectDefinition{
      .methods = {
          dbus::ExportedMethod{
              .interface = sessionInterfaceName,
              .member = "Close",
              .handler = [](dbus::Message&) {
                return dbus::MethodReply{};
              },
          },
      },
      .properties = {},
  };
}

std::string SecretsService::createSession(std::string sender) {
  (void)sender;
  std::string path = std::string(SecretsService::objectPath) + "/session/s" + std::to_string(nextSessionId_++);
  sessionSlots_[path] = bus_->exportObject(path, sessionDefinition(path));
  return path;
}

void SecretsService::exportItem(std::string const& path) {
  itemSlots_[path] = bus_->exportObject(path, itemDefinition(path));
}

} // namespace lambda::system
